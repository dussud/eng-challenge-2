#include "UARTLink.h"
#include "Stirring.h"

unsigned long lastStirSendMs = 0;

void setup() {
  Serial.begin(9600);
  UARTLinkBegin();

  // Subsystem setup calls go here
  stirringSetup();
}

void loop() {
  UARTLinkCheckIncoming();

  // Subsystem update calls go here.
  // Read targets with getSetpoint("TEMP") / getSetpoint("PH") / getSetpoint("STIR")
  // (NAN until the dashboard has sent one), and send readings with
  // sendReading("TEMP", temperature) etc.
  stirringUpdate(getSetpoint("STIR"));
  if (millis() - lastStirSendMs >= 1000) {
    lastStirSendMs = millis();
    sendReading("STIR", (int)round(stirringSpeed()));
  }
}
