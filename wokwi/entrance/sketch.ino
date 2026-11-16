#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

const char* WIFI_NAME = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int MQTT_PORT = 1883;
const char* SUMMARY_TOPIC = "harsh_smartpark/summary";

WiFiClient net;
PubSubClient mqtt(net);
LiquidCrystal_I2C lcd(0x27, 20, 4);
int freeCount[3] = {0, 0, 0};
bool online[3] = {false, false, false};
int totalFree = 0;
int bestFloor = 0;
bool haveSummary = false;
unsigned long nextWifiAttempt = 0;
unsigned long nextMqttAttempt = 0;

void printLine(uint8_t row, const String& text) {
  lcd.setCursor(0, row);
  lcd.print(text.substring(0, 20));
  for (int i = min((int)text.length(), 20); i < 20; i++) lcd.print(' ');
}

void draw() {
  printLine(0, "SMART PARKING GATE");
  String floorLine;
  for (int i = 0; i < 3; i++) {
    if (i) floorLine += " ";
    floorLine += "F" + String(i + 1) + ":";
    floorLine += online[i] ? String(freeCount[i]) : "X";
  }
  printLine(1, floorLine);
  printLine(2, "Free total: " + String(totalFree) + " / 12");
  String destination;
  if (!haveSummary) destination = "Waiting for edge...";
  else if (totalFree == 0) destination = "PARKING FULL";
  else destination = "GO TO FLOOR " + String(bestFloor);
  printLine(3, destination);
}

void onMessage(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) { Serial.println("Bad summary JSON"); return; }

  int newTotal = doc["totalFree"] | 0;
  int newBest = doc["bestFloor"] | 0;
  bool changed = !haveSummary || newTotal != totalFree || newBest != bestFloor;
  JsonArray floors = doc["floors"].as<JsonArray>();
  for (JsonObject f : floors) {
    int id = f["floor"] | 0;
    if (id >= 1 && id <= 3) {
      int i = id - 1;
      int newFree = f["free"] | 0;
      bool newOnline = !(f["offline"] | true);
      if (freeCount[i] != newFree || online[i] != newOnline) changed = true;
      freeCount[i] = newFree;
      online[i] = newOnline;
    }
  }
  totalFree = newTotal;
  bestFloor = newBest;
  haveSummary = true;
  if (changed) draw();
  Serial.printf("Edge summary: %d free, recommended floor %d\n", totalFree, bestFloor);
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
  String id = "harsh-park-entry-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("MQTT connecting... ");
  if (mqtt.connect(id.c_str())) {
    Serial.println("connected");
    mqtt.subscribe(SUMMARY_TOPIC);
  } else {
    Serial.printf("failed (%d); retrying\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  draw();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(60);
  mqtt.setSocketTimeout(15);
  WiFi.mode(WIFI_STA);
  connectWifi();
  Serial.println("Entrance display ready; subscribed to summary.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  else {
    if (!mqtt.connected()) connectMqtt();
    mqtt.loop();
  }
  delay(20);
}
