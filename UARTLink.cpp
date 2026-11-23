#include "UARTLink.h"
#include <SoftwareSerial.h>

static const uint8_t LINK_RX_PIN = 2;  // to ESP32 D1 (TX)
static const uint8_t LINK_TX_PIN = 3;  // to ESP32 D0 (RX) via divider
static const long LINK_BAUD = 9600;

static SoftwareSerial link(LINK_RX_PIN, LINK_TX_PIN);

struct Setpoint {
  const char *name;
  float value;
};

static Setpoint setpoints[] = {
  {"TEMP", NAN},
  {"PH",   NAN},
  {"STIR", NAN},
};
static const uint8_t SETPOINT_COUNT = sizeof(setpoints) / sizeof(setpoints[0]);

static Setpoint *findSetpoint(const char *name) {
  for (uint8_t i = 0; i < SETPOINT_COUNT; i++) {
    if (strcmp(setpoints[i].name, name) == 0) return &setpoints[i];
  }
  return NULL;
}

void UARTLinkBegin() {
  link.begin(LINK_BAUD);
  // Ask the ESP32 for the current setpoints (in case it booted first)
  link.println("READY");
  Serial.println("[sent] READY");
}

float getSetpoint(const char *name) {
  Setpoint *sp = findSetpoint(name);
  return sp ? sp->value : NAN;
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

// Handles "SET:NAME:value". Anything else is just printed.
static void handleLine(char *line) {
  Serial.print("[ESP32] ");
  Serial.println(line);

  if (strncmp(line, "SET:", 4) != 0) return;

  char *name = line + 4;
  char *sep = strchr(name, ':');
  if (!sep) return;
  *sep = '\0';
  char *valueStr = sep + 1;

  char *end;
  float value = (float)strtod(valueStr, &end);
  if (end == valueStr) {
    Serial.println("  -> bad setpoint value, ignored");
    return;
  }

  Setpoint *sp = findSetpoint(name);
  if (!sp) {
    Serial.println("  -> unknown setpoint name, ignored");
    return;
  }
  sp->value = value;

  // Confirm so the ESP32 stops resending it
  link.print("ACK:");
  link.print(name);
  link.print(':');
  link.println(value);
  Serial.print("[sent] ACK:");
  Serial.print(name);
  Serial.print(':');
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
      if (len > 0) handleLine(buffer);
      len = 0;
    } else if (len < sizeof(buffer) - 1) {
      buffer[len++] = c;
    }
  }
}
