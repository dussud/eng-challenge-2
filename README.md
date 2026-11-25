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

Each sketch folder must keep the same name as its `.ino` (an Arduino IDE rule). Open the `.ino` and the other files in that folder appear as tabs and compile with it. Subfolders are ignored, so the sketches don't interfere with each other.

| Sketch | Board (Arduino IDE) | Libraries |
|---|---|---|
| `arduino-uno/arduino-uno.ino` | Arduino Uno | none (SoftwareSerial is built in) |
| `arduino-nano/arduino-nano.ino` | Arduino Nano ESP32 (esp32 core) | PicoMQTT |

### Secrets (Nano only)

Wi-Fi and MQTT credentials go in `arduino-nano/arduino_secrets.h`, which is gitignored:

```bash
cp arduino-nano/arduino_secrets.example.h arduino-nano/arduino_secrets.h
```

Then fill in your values. For eduroam, set `SECRET_WIFI_USERNAME` to your full login; leave it as `""` for a normal WPA2 network. **Never commit `arduino_secrets.h`.**

### Node-RED

Import `nodered/bioreactor-flow.json` (Menu → Import), set the MQTT broker node to your broker, and deploy. It needs `@flowfuse/node-red-dashboard` (Dashboard 2.0). The topics are under `bioreactor/team13/` and must match `TOPIC_BASE` in `arduino-nano.ino`.

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

- **Setpoint delivery:** the ESP32 resends a setpoint every 3 s until it receives an `ACK` with the same value (within 0.005). A lost line on the link therefore only delays a setpoint; it is never dropped.
- **Uno reboot:** the Uno sends `READY` from `UARTLinkBegin()`. The ESP32 replies with `SET` for every known setpoint, so the Uno gets its targets back without the dashboard doing anything.
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

### Rules

1. Use the subsystem names `TEMP` / `PH` / `STIR`.
2. Send one reading per `sendReading()` call.
3. Pins **D0, D1, D3 and D7** are taken by USB (D0/D1) and the link (TX D3, RX D7). D2 is the stirring Hall sensor (hardware interrupt INT0).
4. Don't create your own SoftwareSerial for the link.
5. Avoid long `delay()` calls, since they hold up everyone's readings. Use `millis()` timing (see the example).
6. Use `Serial.print()` for your own debugging; it only goes to the laptop.

### Sending a reading

```cpp
sendReading("TEMP", temperature);
```

This sends `TEMP:23.50` to the ESP32 and copies it to the Serial Monitor as `[sent] TEMP:23.50`. It accepts `int`, `long`, `unsigned long` and `float` values.

### Reading a setpoint

Setpoints from the dashboard arrive automatically: the ESP32 forwards them, and `UARTLinkCheckIncoming()` stores them. Read them in your subsystem:

```cpp
float target = getSetpoint("TEMP");   // or "PH" / "STIR"
if (!isnan(target)) {
  // control towards target
}
```

It returns `NAN` until the first setpoint has been received.

### Adding a subsystem (example)

Put your code in its own pair of files in `arduino-uno/` (see `Stirring.h` / `Stirring.cpp`).

```cpp
// Example.h
#ifndef EXAMPLE_H
#define EXAMPLE_H
void exampleSetup();
void exampleUpdate();
#endif
```

```cpp
// Example.cpp
#include <Arduino.h>
#include "UARTLink.h"
#include "Example.h"

static const uint8_t EXAMPLE_PIN = A0;
static const unsigned long EXAMPLE_INTERVAL_MS = 1000;
static unsigned long lastMs = 0;

void exampleSetup() {
  pinMode(EXAMPLE_PIN, INPUT);
}

void exampleUpdate() {
  if (millis() - lastMs >= EXAMPLE_INTERVAL_MS) {
    lastMs = millis();
    sendReading("EXAMPLE", analogRead(EXAMPLE_PIN));
  }
}
```

Then register it in `arduino-uno.ino`:

```cpp
#include "UARTLink.h"
#include "Example.h"

void setup() {
  Serial.begin(9600);
  UARTLinkBegin();
  exampleSetup();
}

void loop() {
  UARTLinkCheckIncoming();
  exampleUpdate();
}
```

## Subsystems

| Subsystem | Files | Pins | Status |
|---|---|---|---|
| Stirring (PI speed control) | `arduino-uno/Stirring.*` | Hall sensor D2, MOSFET gate D10 | Integrated; setpoint from `getSetpoint("STIR")`, clamped to 300–1000 RPM (0 = stop) |
| Heating | | | To do |
| pH | | | To do |

The sketches in `tests/` run a subsystem on its own, without the link. They're for tuning on the bench.
