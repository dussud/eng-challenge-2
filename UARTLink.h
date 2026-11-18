#ifndef UART_LINK_H
#define UART_LINK_H

#include <Arduino.h>

// Call once in setup(), after Serial.begin().
void UARTLinkBegin();

// Call every loop() to print anything the ESP32 sends back.
void UARTLinkCheckIncoming();

// Send one reading to the ESP32 as "NAME:value\n".
void sendReading(const char *name, int value);
void sendReading(const char *name, long value);
void sendReading(const char *name, unsigned long value);
void sendReading(const char *name, float value);

#endif
