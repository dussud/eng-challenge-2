#include "UARTLink.h"
#include <SoftwareSerial.h>

static const uint8_t LINK_RX_PIN = 2;  // to ESP32 D1 (TX)
static const uint8_t LINK_TX_PIN = 3;  // to ESP32 D0 (RX) via divider
static const long LINK_BAUD = 9600;

static SoftwareSerial link(LINK_RX_PIN, LINK_TX_PIN);

void UARTLinkBegin() {
  link.begin(LINK_BAUD);
}

static void printName(const char *name) {
  link.print(name);
  link.print(':');
  Serial.print("[sent] ");
  Serial.print(name);
  Serial.print(':');
}

void sendReading(const char *name, int value) {
  sendReading(name, (long)value);
}

void sendReading(const char *name, long value) {
  printName(name);
  link.println(value);
  Serial.println(value);
}

void sendReading(const char *name, unsigned long value) {
  printName(name);
  link.println(value);
  Serial.println(value);
}

void sendReading(const char *name, float value) {
  printName(name);
  link.println(value);
  Serial.println(value);
}

void UARTLinkCheckIncoming() {
  static char buffer[64];
  static uint8_t len = 0;

  while (link.available()) {
    char c = link.read();
    if (c == '\r') continue;

    if (c == '\n') {
      buffer[len] = '\0';
      if (len > 0) {
        Serial.print("[ESP32] ");
        Serial.println(buffer);
      }
      len = 0;
    } else if (len < sizeof(buffer) - 1) {
      buffer[len++] = c;
    }
  }
}
