#include "UARTLink.h"
#include "Stirring.h"

void setup() {
  Serial.begin(9600);
  UARTLinkBegin();

  // Subsystem setup calls go here
  stirringSetup();
}

void loop() {
  UARTLinkCheckIncoming();

  // Subsystem update calls go here
  stirringUpdate();
}
