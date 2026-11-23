#include "UARTLink.h"

void setup() {
  Serial.begin(9600);
  UARTLinkBegin();

  // Subsystem setup calls go here
}

void loop() {
  UARTLinkCheckIncoming();

  // Subsystem update calls go here.
  // Read targets with getSetpoint("TEMP") / getSetpoint("PH") / getSetpoint("STIR")
  // (NAN until the dashboard has sent one), and send readings with
  // sendReading("TEMP", temperature) etc.
}
