#include "UARTLink.h"
#include "Stirring.h"

const unsigned long REPORT_INTERVAL_MS = 2000;
unsigned long lastReportMs = 0;

float stirReading = NAN;

void setup() {
  Serial.begin(9600);
  UARTLinkBegin();

  // Subsystem setup calls go here
  stirringSetup();
}

void loop() {
  UARTLinkCheckIncoming();

  stirReading = stirringUpdate(getSetpoint("STIR"));

  if (millis() - lastReportMs >= REPORT_INTERVAL_MS) {
    lastReportMs = millis();
    if (!isnan(stirReading)) sendReading("STIR", (int)round(stirReading));
  }
}
