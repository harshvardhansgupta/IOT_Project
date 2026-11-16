#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_system.h>

// Change this to 1, 2, or 3 in each floor's Wokwi project.
#define FLOOR_ID 3

const char* WIFI_NAME = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int MQTT_PORT = 1883;
const char* ROOT = "harsh_smartpark"; // Use a team-unique prefix on the public broker.

const int trigPin[4] = {5, 17, 18, 19};
const int echoPin[4] = {34, 35, 25, 23}; // input-only pins are ideal for echo
const int greenPin[4] = {13, 26, 32, 4};
const int redPin[4] = {14, 27, 33, 16};
const int OCCUPIED_CM = 20;
const unsigned long HOLD_MS = 450;
const unsigned long REFRESH_MS = 5000;
const unsigned long HEARTBEAT_MS = 3000;

WiFiClient net;
PubSubClient mqtt(net);
LiquidCrystal_I2C lcd(0x27, 16, 2);

bool slot[4] = {0, 0, 0, 0};
bool candidate[4] = {0, 0, 0, 0};
bool seen[4] = {0, 0, 0, 0};
unsigned long candidateSince[4] = {0, 0, 0, 0};
unsigned long lastRefresh = 0;
unsigned long lastBeat = 0;
bool changed = true;
String mqttClientId;
unsigned long nextWifiAttempt = 0;
unsigned long nextMqttAttempt = 0;

String topic(const char* leaf) {
  return String(ROOT) + "/floor" + String(FLOOR_ID) + "/" + leaf;
}

float readCm(int i) {
  digitalWrite(trigPin[i], LOW);
  delayMicroseconds(3);
  digitalWrite(trigPin[i], HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin[i], LOW);
  unsigned long us = pulseIn(echoPin[i], HIGH, 30000);
  if (us == 0) return 400.0; // no echo or out of range => treat as empty
  return us / 58.0;
}

bool publishSlots() {
  if (!mqtt.connected()) return false;
  char msg[100];
  snprintf(msg, sizeof(msg), "{\"floor\":%d,\"slots\":[%d,%d,%d,%d],\"ts\":%lu}",
           FLOOR_ID, slot[0], slot[1], slot[2], slot[3], millis());
  bool sent = mqtt.publish(topic("slots").c_str(), msg, true);
  if (sent) Serial.printf("F%d slots published: [%d,%d,%d,%d]\n", FLOOR_ID,
                slot[0], slot[1], slot[2], slot[3]);
  return sent;
}

void showLocal() {
  int freeCount = 0;
  for (int i = 0; i < 4; i++) {
    freeCount += !slot[i];
    digitalWrite(greenPin[i], slot[i] ? LOW : HIGH);
    digitalWrite(redPin[i], slot[i] ? HIGH : LOW);
  }
  lcd.setCursor(0, 0);
  lcd.print("F"); lcd.print(FLOOR_ID); lcd.print(" Free: ");
  lcd.print(freeCount); lcd.print("/4     ");
  lcd.setCursor(0, 1);
  lcd.print(mqtt.connected() ? "MQTT: ONLINE    " : "MQTT: CONNECTING");
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED || millis() < nextWifiAttempt) return;
  nextWifiAttempt = millis() + 10000;
  Serial.println("WiFi disconnected; retrying connection");
  WiFi.begin(WIFI_NAME, WIFI_PASS, 6);
}

void connectMqtt() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED || millis() < nextMqttAttempt) return;
  nextMqttAttempt = millis() + 3000;
  Serial.print("MQTT connecting... ");
  String statusTopic = topic("status");
  if (mqtt.connect(mqttClientId.c_str(), statusTopic.c_str(), 1, true, "offline")) {
    Serial.println("connected");
    mqtt.publish(statusTopic.c_str(), "online", true);
    changed = true;
    lastBeat = millis();
  } else {
    Serial.printf("failed (%d); retrying\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("SMART PARKING");
  lcd.setCursor(0, 1); lcd.print("Floor "); lcd.print(FLOOR_ID); lcd.print(" starting");
  for (int i = 0; i < 4; i++) {
    pinMode(trigPin[i], OUTPUT); pinMode(echoPin[i], INPUT);
    pinMode(greenPin[i], OUTPUT); pinMode(redPin[i], OUTPUT);
  }
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(256);
  mqtt.setKeepAlive(60);
  mqtt.setSocketTimeout(15);
  mqttClientId = "park-f" + String(FLOOR_ID) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX)
                 + "-" + String((uint32_t)esp_random(), HEX);
  WiFi.mode(WIFI_STA);
  connectWifi();
  Serial.printf("FLOOR %d ready. Drag an HC-SR04 distance below 20 cm to simulate a car.\n", FLOOR_ID);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  else {
    if (!mqtt.connected()) connectMqtt();
    mqtt.loop();
  }

  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    float d = readCm(i);
    bool raw = d < OCCUPIED_CM;
    if (!seen[i]) {
      candidate[i] = raw; candidateSince[i] = now; seen[i] = true;
    }
    if (raw != candidate[i]) { candidate[i] = raw; candidateSince[i] = now; }
    if (candidate[i] != slot[i] && now - candidateSince[i] >= HOLD_MS) {
      slot[i] = candidate[i]; changed = true;
      Serial.printf("F%d-S%d -> %s (%.1f cm)\n", FLOOR_ID, i + 1,
                    slot[i] ? "OCCUPIED" : "FREE", d);
    }
    delay(60); // let ultrasonic echoes settle before reading next slot
  }

  showLocal();
  if (changed || now - lastRefresh >= REFRESH_MS) {
    if (publishSlots()) { changed = false; lastRefresh = now; }
  }
  if (now - lastBeat >= HEARTBEAT_MS && mqtt.connected()) {
    mqtt.publish(topic("status").c_str(), "online", true);
    lastBeat = now;
  }
  delay(100);
}
