#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <HTTPClient.h>
#include <ModbusMaster.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <math.h>

#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_SCK 12
#define TFT_BL 21
#define RS485_RX 16
#define RS485_TX 15
#define RS485_DE_RE 14

constexpr uint8_t PH_SLAVE = 0x03;
constexpr uint8_t DO_SLAVE = 0x0A;
constexpr uint8_t ENV_SLAVE = 0x02;
constexpr uint32_t SAMPLE_MS = 2000;
constexpr uint32_t POST_MS = 15000;

// ponytail: kalibrasi linear cukup untuk commissioning; ganti dengan kurva multi-titik bila hasil lapangan membutuhkannya.
constexpr float PH_SCALE = 1.0f, PH_OFFSET = 0.0f;
constexpr float DO_SCALE = 1.0f, DO_OFFSET = 0.0f;
constexpr float WATER_TEMP_OFFSET = 0.0f, AIR_TEMP_OFFSET = 0.0f;
constexpr float HUMIDITY_OFFSET = 0.0f, LUX_SCALE = 1.0f;

#ifndef AQUANUSA_API_URL
#define AQUANUSA_API_URL "http://192.168.1.10:8000/api/v1/telemetry"
#endif
#ifndef AQUANUSA_DEVICE_KEY
#define AQUANUSA_DEVICE_KEY "change-me"
#endif
#ifndef AQUANUSA_DEVICE_UID
#define AQUANUSA_DEVICE_UID "AQUANUSA-001"
#endif

const char *WIFI_PORTAL_NAME = "AquaNusa-Setup";
const char *API_URL = AQUANUSA_API_URL;
const char *DEVICE_UID = AQUANUSA_DEVICE_UID;
const char *DEVICE_KEY = AQUANUSA_DEVICE_KEY;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
HardwareSerial rs485(2);
ModbusMaster phNode, doNode, envNode;

struct Telemetry {
  float waterTemp = NAN;
  float airTemp = NAN;
  float dissolvedOxygen = NAN;
  float ph = NAN;
  float humidity = NAN;
  float lux = NAN;
};

void beforeTransmission() { digitalWrite(RS485_DE_RE, HIGH); }
void afterTransmission() { digitalWrite(RS485_DE_RE, LOW); }

float decodeFloat(uint16_t high, uint16_t low) {
  const uint32_t bits = (static_cast<uint32_t>(high) << 16) | low;
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

bool readFloatSensor(ModbusMaster &node, float &value, float &temperature) {
  if (node.readHoldingRegisters(0, 6) != node.ku8MBSuccess) return false;
  value = decodeFloat(node.getResponseBuffer(0), node.getResponseBuffer(1));
  temperature = decodeFloat(node.getResponseBuffer(4), node.getResponseBuffer(5));
  return isfinite(value) && isfinite(temperature);
}

bool readEnvironment(Telemetry &data) {
  if (envNode.readHoldingRegisters(0, 7) != envNode.ku8MBSuccess) return false;
  data.humidity = envNode.getResponseBuffer(0) * 0.1f + HUMIDITY_OFFSET;
  data.airTemp = static_cast<int16_t>(envNode.getResponseBuffer(1)) * 0.1f + AIR_TEMP_OFFSET;
  data.lux = ((static_cast<uint32_t>(envNode.getResponseBuffer(2)) << 16) | envNode.getResponseBuffer(3)) * LUX_SCALE;
  return true;
}

void printMetric(int x, int y, const char *label, float value, const char *unit, uint16_t color) {
  tft.setTextColor(0x9D77, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(label);
  tft.setTextColor(color, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(x, y + 14);
  if (isfinite(value)) tft.print(value, strcmp(unit, "Lux") == 0 ? 0 : 1); else tft.print("--");
  tft.setTextSize(1);
  tft.print(' ');
  tft.print(unit);
}

void drawDashboard(const Telemetry &data, const char *network) {
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, 320, 38, 0x09A7);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(12, 11);
  tft.print("AquaNusa");
  tft.setTextSize(1);
  tft.setCursor(236, 15);
  tft.print(network);
  tft.drawFastHLine(0, 118, 320, 0x3228);
  tft.drawFastVLine(160, 38, 202, 0x3228);
  printMetric(12, 52, "WATER TEMP", data.waterTemp, "C", 0x6F7F);
  printMetric(174, 52, "AIR TEMP", data.airTemp, "C", 0xFD20);
  printMetric(12, 132, "DISSOLVED OXYGEN", data.dissolvedOxygen, "mg/L", 0x4DFF);
  printMetric(174, 132, "PH", data.ph, "pH", 0xB59F);
  printMetric(12, 194, "HUMIDITY", data.humidity, "%RH", 0x67EC);
  printMetric(174, 194, "ILLUMINANCE", data.lux, "Lux", 0xFFE0);
}

bool valid(const Telemetry &d) {
  return isfinite(d.waterTemp) && isfinite(d.airTemp) && isfinite(d.dissolvedOxygen) &&
         isfinite(d.ph) && isfinite(d.humidity) && isfinite(d.lux);
}

bool postTelemetry(const Telemetry &d) {
  if (WiFi.status() != WL_CONNECTED || !valid(d)) return false;
  char json[320];
  snprintf(json, sizeof(json), "{\"uid\":\"%s\",\"water_temp_c\":%.2f,\"air_temp_c\":%.2f,\"do_mg_l\":%.2f,\"ph\":%.2f,\"humidity_rh\":%.2f,\"illuminance_lux\":%.0f}", DEVICE_UID, d.waterTemp, d.airTemp, d.dissolvedOxygen, d.ph, d.humidity, d.lux);
  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(API_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", DEVICE_KEY);
  const int status = http.POST(reinterpret_cast<uint8_t *>(json), strlen(json));
  http.end();
  return status >= 200 && status < 300;
}

void setup() {
  Serial.begin(115200);
  pinMode(RS485_DE_RE, OUTPUT);
  afterTransmission();
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(64, 106);
  tft.print("AquaNusa starting");

  rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  for (auto node : {&phNode, &doNode, &envNode}) {
    node->preTransmission(beforeTransmission);
    node->postTransmission(afterTransmission);
  }
  phNode.begin(PH_SLAVE, rs485);
  doNode.begin(DO_SLAVE, rs485);
  envNode.begin(ENV_SLAVE, rs485);

  WiFi.mode(WIFI_STA);
  WiFiManager manager;
  manager.setConfigPortalTimeout(180);
  manager.autoConnect(WIFI_PORTAL_NAME);
}

void loop() {
  static Telemetry data;
  static uint32_t lastSample = 0, lastPost = 0;
  const uint32_t now = millis();
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    float ph = NAN, phTemp = NAN, dissolvedOxygen = NAN, doTemp = NAN;
    if (readFloatSensor(phNode, ph, phTemp)) data.ph = ph * PH_SCALE + PH_OFFSET;
    if (readFloatSensor(doNode, dissolvedOxygen, doTemp)) {
      data.dissolvedOxygen = dissolvedOxygen * DO_SCALE + DO_OFFSET;
      data.waterTemp = doTemp + WATER_TEMP_OFFSET;
    } else if (isfinite(phTemp)) data.waterTemp = phTemp + WATER_TEMP_OFFSET;
    readEnvironment(data);
    drawDashboard(data, WiFi.status() == WL_CONNECTED ? "ONLINE" : "OFFLINE");
  }
  if (now - lastPost >= POST_MS) {
    lastPost = now;
    Serial.println(postTelemetry(data) ? "Telemetry sent" : "Telemetry skipped/failed");
  }
  delay(20);
}
