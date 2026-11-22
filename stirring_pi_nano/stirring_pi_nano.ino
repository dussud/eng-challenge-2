// PI motor speed controller - stirring subsystem
// Based on the lecture code (Nano ESP32 version, slides 32-33)
// Board: Arduino Nano ESP32
// Wiring: Hall sensor signal -> D2, MOSFET gate -> D10 (3.3V logic MOSFET)
// Motor and sensor powered from VBUS (5V)
// Needs a 470 uF capacitor across VBUS/GND and a 1N4001 diode across the motor

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
const byte encoderpin = D2, motorpin = D10;

// Variables
float measspeed, meanmeasspeed, error, KIinterror, deltat, setspeed;
long currtime, prevtime, T1;
// long Tsend;             // LINK
int Vmotor;

// Shared with the interrupt, so volatile
volatile long Tsens = 1e6, pulseTime, pulseT[8];
volatile int count;
volatile bool blinkk;

void IRAM_ATTR Tsense();

void setup() {
  pinMode(encoderpin, INPUT_PULLUP);
  pinMode(LED_RED, OUTPUT);  // on-board LED, blinks once per revolution
  attachInterrupt(digitalPinToInterrupt(encoderpin), Tsense, RISING);
  Serial.begin(115200);
  // UARTLinkBegin();     // LINK

  // 20 kHz, 10-bit PWM (works on both old and new ESP32 board packages)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(motorpin, 20000, 10);
#else
  ledcSetup(0, 20000, 10);
  ledcAttachPin(motorpin, 0);
#endif

  setspeed = constrain(SETPOINT_RPM, SET_MIN, SET_MAX);
}

void loop() {
  currtime = micros();
  deltat = (currtime - prevtime) * 1e-6;  // time since last update, for integration

  // UARTLinkCheckIncoming();   // LINK

  if (currtime - T1 > 0) {
    prevtime = currtime;
    T1 = T1 + 10000;  // 10 ms update period

    // Copy interrupt values (32-bit reads are safe on the ESP32)
    long period = Tsens;
    long lastPulse = pulseTime;

    measspeed = 7 * freqtoRPM * 1e6 / float(period);  // speed from time of 7 pulses (1 motor rev)
    if (currtime - lastPulse > 100000) { measspeed = 0; }  // no pulses for 0.1 s -> stopped
    error = setspeed - measspeed;

    // PI controller
    KIinterror = KIinterror + KI * error * deltat;
    KIinterror = constrain(KIinterror, 0, 5);          // stop integral runaway
    Vmotor = round(204 * (Kp * error + KIinterror));   // volts -> 0-1023 PWM (5 V supply)
    Vmotor = constrain(Vmotor, 0, 1023);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(motorpin, Vmotor);
#else
    ledcWrite(0, Vmotor);
#endif

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

// Interrupt: measures the time of the last 7 sensor pulses
// Integers only - floats here can crash the Nano ESP32 while Serial is used
void IRAM_ATTR Tsense() {
  pulseTime = micros();
  if (abs(pulseTime - pulseT[0]) > Tmin) {  // ignore pulses that are too fast (noise)
    for (int i = 7; i > 0; i--) { pulseT[i] = pulseT[i - 1]; }  // shift previous pulse times
    pulseT[0] = pulseTime;
    Tsens = pulseT[0] - pulseT[7];  // time of the last 7 pulses
    count = count + 2;
    if (count > int(Npulses)) { count = count - int(Npulses); digitalWrite(LED_RED, blinkk); blinkk = !blinkk; }
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
