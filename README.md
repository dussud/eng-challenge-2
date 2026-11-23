# Arduino Uno Code

This repo is for the Arduino Uno (Kona328) code, i.e. the main controller, connected to each subsystem and linked to the ESP32.
It sends subsystem readings to the Nano ESP32 over UART (SoftwareSerial, 9600 baud).

## Setup

The folder name must match the `.ino` name. Open the `.ino` in the Arduino IDE; the other files appear as tabs and compile automatically.

## Wiring

| Kona328 | Nano ESP32 | Notes |
|---|---|---|
| D3 (TX) | D0 (RX) | Through a 1k / 2k voltage divider (5V to 3.3V) |
| D2 (RX) | D1 (TX) | Direct |
| GND | GND | Required |

```
Kona D3 ──[ 1kΩ ]──┬── ESP32 D0
                   │
              [ 1.8kΩ ]
                   │
                  GND
```

## Sending a reading

```cpp
sendReading("TEMP", temperature);
```

This sends `TEMP:23.50` to the ESP32 and copies it to the Serial Monitor as `[sent] TEMP:23.50`. It accepts `int`, `long`, `unsigned long` and `float` values.

## Rules

1. Choose the appropriate subsystem name: `TEMP` / `PH` / `STIR`
2. Send one reading per `sendReading()` call.
3. Don't use pins **D0 to D3**; they're used by USB and the link.
4. Don't create your own SoftwareSerial for the link.
5. Avoid long `delay()` calls, since they hold up everyone's readings. Use `millis()` timing (see the example).
6. Use `Serial.print()` for your own debugging; it only goes to the laptop.

## Adding a subsystem (example)

Put your code in its own pair of files (e.g. `Example.h` / `Example.cpp`) in the sketch folder, or directly in the `.ino` for quick tests.

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

## Reading a setpoint

Setpoints from the dashboard arrive automatically (the ESP32 forwards them, and `UARTLinkCheckIncoming()` stores them). Read them in your subsystem:

```cpp
float target = getSetpoint("TEMP");   // or "PH" / "STIR"
if (!isnan(target)) {
  // control towards target
}
```

It returns `NAN` until the first setpoint has been received.

## Link protocol

One message per line, 9600 baud:

| Direction | Message | Meaning |
|---|---|---|
| Uno → ESP32 | `TEMP:23.50` | Reading (`TEMP` / `PH` / `STIR`) |
| Uno → ESP32 | `READY` | Sent at boot; the ESP32 replies with every known setpoint |
| ESP32 → Uno | `SET:TEMP:30.00` | New setpoint |
| Uno → ESP32 | `ACK:TEMP:30.00` | Setpoint applied; the ESP32 resends every 3 s until it gets this |

The ESP32 maps the names to MQTT: `TEMP` ↔ `temperature`, `PH` ↔ `ph`, `STIR` ↔ `rpm`, and publishes readings to `bioreactor/team13/telemetry/<param>` every 2 s (readings older than 10 s are not published).
