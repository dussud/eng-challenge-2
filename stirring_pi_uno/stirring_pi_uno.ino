// PI motor speed controller - stirring subsystem
// Based on the lecture appendix code (Uno/Nano version)
// Board: OrangePip Kona328 (Uno clone)
// Wiring: Hall sensor signal -> D2, MOSFET gate -> D10
// Needs a 470 uF capacitor across 5V/GND and a 1N4001 diode across the motor

// #include "UARTLink.h"   // LINK

// Motor (plant) and sensor constants
const float Kv = 250;      // motor speed constant in RPM per Volt
const float T = 0.15;      // motor and load time constant (s)
const float Npulses = 70;  // pulses per revolution
const int RPMmax = 1500;   // ignore pulses faster than this (noise)
const int Tmin = 6e7 / RPMmax / Npulses;

// Desired control parameters ('fast' design)
const float wn = 1 / T;
const float zeta = 1;

// PI coefficients, calculated from the above
const float wo = 1 / T;
const float Kp = (2 * zeta * wn / wo - 1) / Kv;
const float KI = wn * wn / Kv / wo;
const float freqtoRPM = 60 / Npulses;

// Setpoint - set here for now, later it comes from the dashboard
const float SETPOINT_RPM = 500;
const float SET_MIN = 300, SET_MAX = 1000;

// Pins
const byte encoderpin = 2, motorpin = 10;

// Variables
float measspeed, meanmeasspeed, error, KIinterror, deltaT, setspeed;
long currtime, prevtime, T1;
// long Tsend;             // LINK
int Vmotor;

// Shared with the interrupt, so volatile
volatile float freq;
volatile long pulseT, prevpulseT;
volatile int trig;

void setup() {
  pinMode(encoderpin, INPUT_PULLUP);
  pinMode(13, OUTPUT);  // on-board LED, blinks once per revolution
  attachInterrupt(digitalPinToInterrupt(encoderpin), freqcount, RISING);
  Serial.begin(115200);
  // UARTLinkBegin();     // LINK

  TCCR1A = 0b00000011;  // 10-bit PWM on D9/D10
  TCCR1B = 0b00000001;  // 7.8 kHz

  setspeed = constrain(SETPOINT_RPM, SET_MIN, SET_MAX);
}

void loop() {
  currtime = micros();
  deltaT = (currtime - prevtime) * 1e-6;  // time since last update, for integration

  // UARTLinkCheckIncoming();   // LINK

  if (currtime - T1 > 0) {
    prevtime = currtime;
    T1 = T1 + 10000;  // 10 ms update period

    // Copy interrupt values safely
    noInterrupts();
    if (currtime - pulseT > 100000) { freq = 0; }  // no pulses for 0.1 s -> stopped
    float f = freq;
    interrupts();

    measspeed = f * freqtoRPM;
    error = setspeed - measspeed;

    // PI controller
    KIinterror = KIinterror + KI * error * deltaT;
    KIinterror = constrain(KIinterror, 0, 5);          // stop integral runaway
    Vmotor = round(204 * (Kp * error + KIinterror));   // volts -> 0-1023 PWM (5 V supply)
    Vmotor = constrain(Vmotor, 0, 1023);
    if (Vmotor == 255) { Vmotor = 256; }              // Uno 10-bit PWM bug workaround
    analogWrite(motorpin, Vmotor);

    // Smoothed speed for display
    meanmeasspeed = 0.1 * measspeed + 0.9 * meanmeasspeed;
    Serial.print("Min:0,Max:1100,Set:");
    Serial.print(setspeed, 0);
    Serial.print(",Speed:");
    Serial.println(meanmeasspeed, 0);

    // LINK
    // if (currtime - Tsend > 0) {
    //   Tsend = Tsend + 1000000;
    //   sendReading("STIR", (int)round(meanmeasspeed));
    // }
  }
}

// Interrupt: measures the time between sensor pulses
void freqcount() {
  pulseT = micros();
  if (abs(pulseT - prevpulseT) > Tmin) {  // ignore pulses that are too fast (noise)
    freq = 0.75 * freq + 2.5e5 / float(pulseT - prevpulseT);  // smoothed pulse frequency (Hz)
    prevpulseT = pulseT;
    trig++;
    if (2 * trig >= Npulses) { trig = 0; digitalWrite(13, !digitalRead(13)); }
  }
}

// LINK
// void handleLinkLine(const char *line) {
//   if (strncmp(line, "STIR_SET:", 9) == 0) {
//     float newSet = atof(line + 9);
//     if (newSet == 0) { setspeed = 0; }                       // 0 = stop
//     else { setspeed = constrain(newSet, SET_MIN, SET_MAX); }
//   }
// }
