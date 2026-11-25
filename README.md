# ENGF0001 Challenge 2: Bioreactor (Team 13)

All the code for the bioreactor lives here:

```
eng-challenge-2/
├── arduino-uno/        Main controller (OrangePip Kona328, Uno clone): runs the subsystems
├── arduino-nano/       Arduino Nano ESP32: bridges the Uno to the MQTT broker over Wi-Fi
├── nodered/            Node-RED dashboard flow (setpoints + live readings)
└── tests/              Standalone test sketches (e.g. stirring PI controller on its own)
```

```
 subsystems ──► Uno (Kona328) ──UART──► Nano ESP32 ──Wi-Fi/MQTT──► broker ──► Node-RED dashboard
                     ▲                      │                                       │
                     └──── setpoints ───────┴──────────────── setpoints ◄───────────┘
```

The Uno does all the control. The ESP32 only translates between the UART link and MQTT.

## Getting started

Each sketch folder must keep the same name as its `.ino` (Arduino IDE).

| Sketch | Board (Arduino IDE) | Libraries |
|---|---|---|
| `arduino-uno/arduino-uno.ino` | Arduino Uno | none |
| `arduino-nano/arduino-nano.ino` | Arduino Nano ESP32 (esp32 core) | PicoMQTT |

### Secrets (Nano only)

Wi-Fi and MQTT credentials go in `arduino-nano/arduino_secrets.h`. Copy the `arduino-nano/arduino_secrets.example.h` file, fill in your credentials and rename it. (don't commit it pls)

### Node-RED

Import `nodered/bioreactor-flow.json` (Menu → Import), set the MQTT broker node to `mqtt.dussud.org`, and deploy. It needs `@flowfuse/node-red-dashboard` (Dashboard 2.0). The topics are under `bioreactor/team13/` and must match `TOPIC_BASE` in `arduino-nano.ino`.

## Wiring: Uno ↔ Nano ESP32

| Kona328 | Nano ESP32 | Notes |
|---|---|---|
| D3 (TX) | D0 (RX) | Through a 1k / 1.8k voltage divider (5 V → 3.3 V) |
| D7 (RX) | D1 (TX) | Direct (3.3 V is read as HIGH by the Uno) |
| GND | GND | Required |

```
Kona D3 ──[ 1kΩ ]──┬── ESP32 D0
                   │
              [ 1.8kΩ ]
                   │
                  GND
```

## UART protocol

- **Physical:** Uno SoftwareSerial (RX D7, TX D3) ↔ Nano `Serial0` (RX D0, TX D1), 9600 baud, 8N1.
- **Framing:** plain ASCII, one message per line, ending in `\n` (`\r` is ignored). Lines longer than 63 characters are truncated.
- **Values:** decimal numbers, sent with 2 decimal places (`23.50`). Integers are also accepted (`STIR:500`).

### Messages

| Direction | Format | Example | Meaning |
|---|---|---|---|
| Uno → ESP32 | `NAME:value` | `TEMP:23.50` | A reading from a subsystem |
| Uno → ESP32 | `READY` | `READY` | Uno has (re)booted; the ESP32 resends every setpoint it knows |
| ESP32 → Uno | `SET:NAME:value` | `SET:TEMP:30.00` | New setpoint from the dashboard |
| Uno → ESP32 | `ACK:NAME:value` | `ACK:TEMP:30.00` | Uno stored the setpoint (echoes the value) |

### Names

| Link name | MQTT name | Units |
|---|---|---|
| `TEMP` | `temperature` | °C |
| `PH` | `ph` | pH |
| `STIR` | `rpm` | RPM |

### Behaviour

- **Setpoint delivery:** the ESP32 resends a setpoint every 3 s until it receives an `ACK` with the same value (within 0.005). 
- **Uno reboot:** the Uno sends `READY` from `UARTLinkBegin()`. The ESP32 replies with `SET` for every known setpoint, so the Uno gets its targets back.
- **ESP32 reboot:** setpoints are retained on the broker, so the ESP32 gets them again when it subscribes and forwards them to the Uno.
- **Readings:** the Uno can send readings as often as it likes. The ESP32 keeps the latest value per name and publishes it every 2 s. Readings older than 10 s are not published, so a dead subsystem disappears from the dashboard instead of showing a stale value.
- **Unknown names** are ignored by both sides (and logged on the ESP32's USB serial).

### MQTT topics (ESP32 ↔ broker)

| Topic | Direction | Payload |
|---|---|---|
| `bioreactor/team13/telemetry/<param>` | ESP32 → dashboard | `{"value":23.50}` |
| `bioreactor/team13/setpoint/<param>` | dashboard → ESP32 | `30.0` (retained) |
| `bioreactor/team13/status` | ESP32 → dashboard | `online` / `offline` (retained, `offline` set as last will) |

`<param>` is the MQTT name from the table above.

## Writing Uno subsystem code

### The subsystem interface

Every subsystem is a pair of files in `arduino-uno/` (e.g. `Heating.h` / `Heating.cpp`) exposing exactly two functions:

```cpp
void heatingSetup();                   // called once from setup(): pins, timers, interrupts
float heatingUpdate(float setpoint);   // called on every loop(): controls, returns latest reading
```

- `xxxSetup()` takes no arguments and runs once.
- `xxxUpdate(setpoint)` receives the current setpoint each time and returns the latest reading (°C, pH or RPM). `setpoint` is `NAN` until the dashboard has sent one, so the subsystem must handle that (e.g. stay off).
- Subsystems **don't talk to the link**. They never call `sendReading()` or `getSetpoint()` and don't include `UARTLink.h`. The main controller (`arduino-uno.ino`) handles communication.

### Timing: call fast, report slowly

There are two separate rates: 

| What | Rate | Who handles it |
|---|---|---|
| Control (reading sensors, PI/PID, driving outputs) | As fast as the subsystem needs, e.g. every 10 ms for stirring | Inside `xxxUpdate()`, with its own `millis()`/`micros()` timer |
| Reporting readings to the ESP32 | Every 2 s | The main loop, with one `millis()` timer |

So `xxxUpdate()` is called on **every** `loop()` pass. Inside `xxxUpdate()`, check whether your control period has elapsed; if it hasn't, just return the last reading.

The main loop keeps the value each update returns and sends the latest one every 2 s. Readings are therefore never sent from inside a subsystem.

### Rules

1. Use the subsystem names `TEMP` / `PH` / `STIR` in the main loop.
2. Pins **D0, D1, D3 and D7** are taken by USB (D0/D1) and the link (TX D3, RX D7). D2 is the stirring Hall sensor (hardware interrupt INT0).
3. Don't create your own SoftwareSerial for the link.
4. `xxxUpdate()` must return quickly: no `delay()` and no waiting loops, since they hold up every other subsystem. Use `millis()` timing.
5. Use `Serial.print()` for your own debugging; it only goes to the laptop. Keep it light (e.g. every 100 ms or more), since printing is slow.

### Example subsystem

```cpp
// Example.h
#ifndef EXAMPLE_H
#define EXAMPLE_H
void exampleSetup();
float exampleUpdate(float setpoint);
#endif
```

```cpp
// Example.cpp
#include <Arduino.h>
#include "Example.h"

static const uint8_t SENSOR_PIN = A0;
static const uint8_t OUTPUT_PIN = 5;
static const unsigned long CONTROL_PERIOD_MS = 100;

static unsigned long lastControlMs = 0;
static float reading = NAN;

void exampleSetup() {
  pinMode(SENSOR_PIN, INPUT);
  pinMode(OUTPUT_PIN, OUTPUT);
}

float exampleUpdate(float setpoint) {
  if (millis() - lastControlMs < CONTROL_PERIOD_MS) return reading;  // not time yet
  lastControlMs = millis();

  reading = analogRead(SENSOR_PIN) * (5.0 / 1023);   // convert to real units

  if (isnan(setpoint)) {
    digitalWrite(OUTPUT_PIN, LOW);                   // no setpoint yet: stay off
  } else {
    digitalWrite(OUTPUT_PIN, reading < setpoint);    // simple on/off control
  }
  return reading;
}
```

### Main controller (`arduino-uno.ino`)

```cpp
#include "UARTLink.h"
#include "Stirring.h"
#include "Example.h"

const unsigned long REPORT_INTERVAL_MS = 2000;
unsigned long lastReportMs = 0;

float stirReading = NAN;
float exampleReading = NAN;

void setup() {
  Serial.begin(9600);
  UARTLinkBegin();

  stirringSetup();
  exampleSetup();
}

void loop() {
  UARTLinkCheckIncoming();   // receive setpoints from the ESP32

  // Control: every pass
  stirReading    = stirringUpdate(getSetpoint("STIR"));
  exampleReading = exampleUpdate(getSetpoint("TEMP"));

  // Reporting: every 2 s
  if (millis() - lastReportMs >= REPORT_INTERVAL_MS) {
    lastReportMs = millis();
    if (!isnan(stirReading))    sendReading("STIR", stirReading);
    if (!isnan(exampleReading)) sendReading("TEMP", exampleReading);
  }
}
```

### Link functions (main controller only)

- `sendReading("TEMP", value)` sends `TEMP:23.50` to the ESP32 and echoes `[sent] TEMP:23.50` on the Serial Monitor. It accepts `int`, `long`, `unsigned long` and `float`.
- `getSetpoint("TEMP")` returns the latest setpoint from the dashboard, or `NAN` until one has arrived. `UARTLinkCheckIncoming()` fills these in, so call it at the top of every `loop()`.

## Subsystems

| Subsystem | Files | Pins | Status |
|---|---|---|---|
| Stirring (PI speed control) | `arduino-uno/Stirring.*` | Hall sensor D2, MOSFET gate D10 | Integrated; PI runs every 10 ms, setpoint clamped to 300–1000 RPM (0 = stop) |
| Heating | | | To do |
| pH | | | To do |

The sketches in `tests/` run a subsystem on its own without the link.