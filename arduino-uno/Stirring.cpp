// PI motor speed controller - stirring subsystem
// Based on the lecture appendix code (Uno/Nano version)
// Board: OrangePip Kona328 (Uno clone)
// Wiring: Hall sensor signal -> D2, MOSFET gate -> D10
// Needs a 470 uF capacitor across 5V/GND and a 1N4001 diode across the motor

#define DEBUG 0

#include <Arduino.h>
#include "Stirring.h"

// Motor (plant) and sensor constants
static const float Kv = 250;      // motor speed constant in RPM per Volt
static const float T = 0.15;      // motor and load time constant (s)
static const float Npulses = 70;  // pulses per revolution
static const int RPMmax = 1500;   // ignore pulses faster than this (noise)
static const int Tmin = 6e7 / RPMmax / Npulses;

// Desired control parameters ('fast' design)
static const float wn = 1 / T;
static const float zeta = 1;

// PI coefficients, calculated from the above
static const float wo = 1 / T;
static const float Kp = (2 * zeta * wn / wo - 1) / Kv;
static const float KI = wn * wn / Kv / wo;
static const float freqtoRPM = 60 / Npulses;

// Setpoint limits
static const float SET_MIN = 300, SET_MAX = 1000;

// Pins
static const byte encoderpin = 2, motorpin = 10;

// How often to print to the Serial Plotter
static const long PRINT_INTERVAL = 100000;  // 100 ms

// Variables
static float measspeed, meanmeasspeed, error, KIinterror, deltaT, setspeed;
static long currtime, prevtime, T1, Tprint;
static int Vmotor;

// Shared with the interrupt, so volatile
static volatile float freq;
static volatile long pulseT, prevpulseT;
static volatile int trig;

static void freqcount();

void stirringSetup() {
  pinMode(encoderpin, INPUT_PULLUP);
  pinMode(13, OUTPUT);  // on-board LED, blinks once per revolution
  attachInterrupt(digitalPinToInterrupt(encoderpin), freqcount, RISING);

  TCCR1A = 0b00000011;  // 10-bit PWM on D9/D10
  TCCR1B = 0b00000001;  // 7.8 kHz
}

// Runs the PI controller every 10 ms and returns the measured (smoothed) speed in RPM.
// Call on every loop(); between control periods it just returns the last value.
float stirringUpdate(float setpoint) {
  if (isnan(setpoint) || setpoint == 0) { setspeed = 0; }  // no setpoint yet, or 0 = stop
  else { setspeed = constrain(setpoint, SET_MIN, SET_MAX); }

  currtime = micros();
  deltaT = (currtime - prevtime) * 1e-6;  // time since last update, for integration

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
  }

  #if DEBUG
  if (currtime - Tprint > 0) {
    Tprint = Tprint + PRINT_INTERVAL;
    Serial.print("Min:0,Max:1100,Set:");
    Serial.print(setspeed, 0);
    Serial.print(",Speed:");
    Serial.println(meanmeasspeed, 0);
  }
  #endif

  return meanmeasspeed;
}

// Interrupt: measures the time between sensor pulses
static void freqcount() {
  pulseT = micros();
  if (abs(pulseT - prevpulseT) > Tmin) {  // ignore pulses that are too fast (noise)
    freq = 0.75 * freq + 2.5e5 / float(pulseT - prevpulseT);  // smoothed pulse frequency (Hz)
    prevpulseT = pulseT;
    trig++;
    if (2 * trig >= Npulses) { trig = 0; digitalWrite(13, !digitalRead(13)); }
  }
}
