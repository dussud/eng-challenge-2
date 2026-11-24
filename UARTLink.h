#ifndef UART_LINK_H
#define UART_LINK_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Uno <-> Nano ESP32 link protocol (one message per line, 9600 baud)
//
//   Uno  -> Nano   NAME:value          reading, e.g. "TEMP:23.50"
//   Uno  -> Nano   READY               sent at boot, asks for all setpoints
//   Uno  -> Nano   ACK:NAME:value      confirms a setpoint was applied
//   Nano -> Uno    SET:NAME:value      new setpoint, e.g. "SET:TEMP:30.00"
//
// NAME is one of TEMP / PH / STIR.
// ---------------------------------------------------------------------------

// Call once in setup(), after Serial.begin().
void UARTLinkBegin();

// Call every loop(): reads setpoints sent by the ESP32.
void UARTLinkCheckIncoming();

// Send one reading to the ESP32 as "NAME:value\n".
void sendReading(const char *name, int value);
void sendReading(const char *name, long value);
void sendReading(const char *name, unsigned long value);
void sendReading(const char *name, float value);

// Latest setpoint received for NAME ("TEMP", "PH" or "STIR").
// Returns NAN until the first setpoint arrives; check with isnan().
float getSetpoint(const char *name);

#endif
