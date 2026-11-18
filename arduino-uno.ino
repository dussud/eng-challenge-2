#include "KonaLink.h"

void setup() {
  Serial.begin(9600);
  konaLinkBegin();

  // Subsystem setup calls go here
}

void loop() {
  konaLinkCheckIncoming();

  // Subsystem update calls go here
}
