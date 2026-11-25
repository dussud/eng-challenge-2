#include <WiFi.h>
#include <PicoMQTT.h>
#include "arduino_secrets.h"

// ---------------------------------------------------------------------------
// MQTT topic schema (must match the Node-RED flow)
//
//   bioreactor/team13/telemetry/<temperature|ph|rpm>   ESP32 -> dashboard   {"value": 30.1}
//   bioreactor/team13/setpoint/<temperature|ph|rpm>    dashboard -> ESP32   30.0   (retained)
//   bioreactor/team13/status                           ESP32 -> dashboard   online/offline (retained)
// ---------------------------------------------------------------------------
#define TOPIC_BASE "bioreactor/team13"

const char *TOPIC_STATUS         = TOPIC_BASE "/status";
const char *TOPIC_SETPOINT_ALL   = TOPIC_BASE "/setpoint/#";
const char *TOPIC_TELEMETRY_BASE = TOPIC_BASE "/telemetry/";

const unsigned long TELEMETRY_INTERVAL_MS = 2000;

PicoMQTT::Client mqtt(
  SECRET_MQTT_HOST,
  SECRET_MQTT_PORT,
  "team13-nano-esp32-1",   // client id: must be unique on the broker
  SECRET_MQTT_USER,
  SECRET_MQTT_PASSWORD
);

// ---------------------------------------------------------------------------
// UART link to the Uno (Kona328) on D0 (RX) / D1 (TX), 9600 baud.
// One message per line:
//
//   Uno  -> ESP32   NAME:value          reading, e.g. "TEMP:23.50"
//   Uno  -> ESP32   READY               Uno booted, resend all setpoints
//   Uno  -> ESP32   ACK:NAME:value      Uno applied a setpoint
//   ESP32 -> Uno    SET:NAME:value      new setpoint from the dashboard
//
// The Uno uses short names (TEMP/PH/STIR), MQTT uses temperature/ph/rpm.
// ---------------------------------------------------------------------------
const long LINK_BAUD = 9600;
const unsigned long READING_MAX_AGE_MS = 10000;   // don't publish older readings
const unsigned long SETPOINT_RETRY_MS  = 3000;    // resend un-ACKed setpoints

struct Param {
  const char *mqttName;   // telemetry/setpoint topic suffix
  const char *linkName;   // name used on the UART link
  float target;           // NAN until the first setpoint arrives (the broker
                          // sends the retained ones right after we subscribe)
  bool targetAcked;       // Uno confirmed it has `target`
  float reading;          // latest reading from the Uno
  unsigned long readingMs;
};

Param params[] = {
  {"temperature", "TEMP", NAN, true, NAN, 0},
  {"ph",          "PH",   NAN, true, NAN, 0},
  {"rpm",         "STIR", NAN, true, NAN, 0},
};
const size_t PARAM_COUNT = sizeof(params) / sizeof(params[0]);

Param *findByMqtt(const char *name) {
  for (size_t i = 0; i < PARAM_COUNT; i++)
    if (strcmp(params[i].mqttName, name) == 0) return &params[i];
  return nullptr;
}

Param *findByLink(const char *name) {
  for (size_t i = 0; i < PARAM_COUNT; i++)
    if (strcmp(params[i].linkName, name) == 0) return &params[i];
  return nullptr;
}

void sendSetpointToUno(Param &p) {
  if (isnan(p.target)) return;
  Serial0.printf("SET:%s:%.2f\n", p.linkName, p.target);
  Serial.printf("[to Uno] SET:%s:%.2f\n", p.linkName, p.target);
}

// Handles one complete line from the Uno.
void handleLinkLine(char *line) {
  Serial.printf("[from Uno] %s\n", line);

  if (strcmp(line, "READY") == 0) {
    for (size_t i = 0; i < PARAM_COUNT; i++) {
      params[i].targetAcked = isnan(params[i].target);
      sendSetpointToUno(params[i]);
    }
    return;
  }

  bool isAck = strncmp(line, "ACK:", 4) == 0;
  char *name = isAck ? line + 4 : line;
  char *sep = strchr(name, ':');
  if (!sep) return;
  *sep = '\0';
  char *valueStr = sep + 1;

  char *end;
  float value = strtof(valueStr, &end);
  if (end == valueStr) return;

  Param *p = findByLink(name);
  if (!p) {
    Serial.printf("  unknown name '%s'\n", name);
    return;
  }

  if (isAck) {
    // Only counts if it matches the latest target (2 decimals on the wire)
    if (fabsf(value - p->target) < 0.005f) p->targetAcked = true;
  } else {
    p->reading = value;
    p->readingMs = millis();
  }
}

// Non-blocking line reader for Serial0
void checkLink() {
  static char buffer[64];
  static size_t len = 0;

  while (Serial0.available()) {
    char c = Serial0.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buffer[len] = '\0';
      if (len > 0) handleLinkLine(buffer);
      len = 0;
    } else if (len < sizeof(buffer) - 1) {
      buffer[len++] = c;
    }
  }
}

// Resend any setpoint the Uno hasn't confirmed yet
void retrySetpoints() {
  static unsigned long lastRetry = 0;
  if (millis() - lastRetry < SETPOINT_RETRY_MS) return;
  lastRetry = millis();
  for (size_t i = 0; i < PARAM_COUNT; i++)
    if (!params[i].targetAcked) sendSetpointToUno(params[i]);
}


void beginWiFi() {
  if (strlen(SECRET_WIFI_USERNAME) > 0) {
    Serial.printf("Connecting to '%s' (WPA2-Enterprise, PEAP)", SECRET_WIFI_SSID);
    WiFi.begin(
      SECRET_WIFI_SSID,
      WPA2_AUTH_PEAP,
      SECRET_WIFI_USERNAME,   // outer identity (sent unencrypted)
      SECRET_WIFI_USERNAME,   // inner username (sent inside the TLS tunnel)
      SECRET_WIFI_PASSWORD
    );
  } else {
    Serial.printf("Connecting to '%s' (WPA2-Personal)", SECRET_WIFI_SSID);
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  beginWiFi();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - start > 30000) {
      Serial.printf("\nStill not connected (status %d), retrying...\n", WiFi.status());
      WiFi.disconnect();
      delay(1000);
      beginWiFi();
      start = millis();
    }
  }
  Serial.printf("\nWi-Fi connected, IP: %s\n", WiFi.localIP().toString().c_str());
}

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
void onSetpoint(const char *topic, const char *payload) {
  // topic: bioreactor/team13/setpoint/<param>
  const char *param = strrchr(topic, '/');
  if (!param) return;
  param++;

  char *end;
  float value = strtof(payload, &end);
  if (end == payload) {
    // Empty payload = retained message was cleared; anything else is garbage
    Serial.printf("Ignoring setpoint [%s] '%s'\n", topic, payload);
    return;
  }

  Param *p = findByMqtt(param);
  if (!p) {
    Serial.printf("Unknown setpoint '%s'\n", param);
    return;
  }
  Serial.printf("Setpoint %s = %.2f\n", param, value);

  // Forward to the Uno; retrySetpoints() resends until it ACKs
  p->target = value;
  p->targetAcked = false;
  sendSetpointToUno(*p);
}

void publishTelemetry(const char *param, float value) {
  if (isnan(value)) return;
  char topic[64];
  char payload[32];
  snprintf(topic, sizeof(topic), "%s%s", TOPIC_TELEMETRY_BASE, param);
  snprintf(payload, sizeof(payload), "{\"value\":%.2f}", value);
  mqtt.publish(topic, payload);   // QoS 0, not retained
}

void setupMqtt() {
  // Last Will: if we drop off without saying goodbye, the broker
  // publishes "offline" to the status topic for us.
  mqtt.will.topic = TOPIC_STATUS;
  mqtt.will.payload = "offline";
  mqtt.will.qos = 1;
  mqtt.will.retain = true;

  mqtt.connected_callback = [] {
    Serial.println("MQTT connected");
    mqtt.publish(TOPIC_STATUS, "online", 1, true);   // QoS 1, retained
  };
  mqtt.disconnected_callback = [] {
    Serial.println("MQTT disconnected");
  };
  mqtt.connection_failure_callback = [] {
    Serial.println("MQTT connection failed, will retry");
  };

  mqtt.subscribe(TOPIC_SETPOINT_ALL, onSetpoint);
  mqtt.begin();
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);       // USB, for the Serial Monitor
  Serial0.begin(LINK_BAUD);   // hardware UART to the Uno on D0 (RX) / D1 (TX)
  delay(1000);

  connectWiFi();
  setupMqtt();
}

void loop() {
  mqtt.loop();  // handles connect/reconnect automatically
  checkLink();
  retrySetpoints();

  static unsigned long lastPublish = 0;
  if (mqtt.connected() && millis() - lastPublish >= TELEMETRY_INTERVAL_MS) {
    lastPublish = millis();
    for (size_t i = 0; i < PARAM_COUNT; i++) {
      Param &p = params[i];
      // Skip parameters the Uno hasn't sent recently
      if (p.readingMs == 0 || millis() - p.readingMs > READING_MAX_AGE_MS) continue;
      publishTelemetry(p.mqttName, p.reading);
    }
  }
}
