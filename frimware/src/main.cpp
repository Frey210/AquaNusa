/*
 * Aerasea RTOS UI – ESP32-S3
 * Versi: 4 tombol (UP, DOWN, OK, BACK), tanpa encoder
 * Display: TFT ILI9341 320x240 (SPI)
 * PlatformIO Version
 */

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <ModbusMaster.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <time.h>
#include <math.h>
#include "ui_types.h"
#include "logo.h"

// =======================
//  KONFIG PERANGKAT & NET
// =======================

// TFT ILI9341 (SPI) - landscape
static const uint8_t TFT_ROTATION = 3;
static const uint32_t TFT_SPI_FREQ = 16000000;

// TFT pin mapping (ESP32-S3)
#define TFT_CS   10   // Chip Select
#define TFT_DC   9    // Data/Command
#define TFT_RST  8    // Reset (connect to 3.3V if LCD has reset pin)
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_SCK  12
#define TFT_BL   21   // Backlight control via 2N2222 transistor
#define TFT_BACKLIGHT_ON HIGH

// 16-bit color aliases (RGB565)
#define TFT_BLACK     0x0000
#define TFT_NAVY      0x000F
#define TFT_DARKGREY  0x7BEF
#define TFT_LIGHTGREY 0xC618
#define TFT_WHITE     0xFFFF
#define TFT_ORANGE    0xFD20
#define TFT_YELLOW    0xFFE0
#define TFT_CYAN      0x07FF
#define TFT_GREEN     0x07E0
#define TFT_BLUE      0x001F
#define TFT_MAGENTA   0xF81F

// RGB LED (common cathode)
#define LED_R_PIN 2
#define LED_G_PIN 3
#define LED_B_PIN 4

// LEDC (PWM) channels
#define LEDC_CH_R 0
#define LEDC_CH_G 1
#define LEDC_CH_B 2
#define LEDC_FREQ 5000
#define LEDC_RES  8

// RS485 (shared bus)
#define RS485_RX_PIN    16
#define RS485_TX_PIN    15
#define RS485_DE_RE_PIN 14

#define PH_SLAVE_ID   3
#define DO_SLAVE_ID   10
#define ENV_SLAVE_ID  2

// ponytail: kalibrasi linear cukup untuk commissioning; pakai kurva multi-titik jika hasil lapangan memerlukannya.
constexpr float PH_SCALE = 1.0f, PH_OFFSET = 0.0f;
constexpr float DO_SCALE = 1.0f, DO_OFFSET = 0.0f;
constexpr float WATER_TEMP_OFFSET = 0.0f, AIR_TEMP_OFFSET = 0.0f;
constexpr float HUMIDITY_OFFSET = 0.0f, LUX_SCALE = 1.0f;

// Server & Identitas
#ifndef AQUANUSA_API_URL
#define AQUANUSA_API_URL "http://192.168.1.10:8000/api/v1/telemetry"
#endif
#ifndef AQUANUSA_DEVICE_KEY
#define AQUANUSA_DEVICE_KEY "change-me"
#endif
#ifndef AQUANUSA_DEVICE_UID
#define AQUANUSA_DEVICE_UID "AQUANUSA-001"
#endif

const char* POST_URL   = AQUANUSA_API_URL;
const char* DEVICE_KEY = AQUANUSA_DEVICE_KEY;
const char* UID        = AQUANUSA_DEVICE_UID;
const char* FW_VERSION = "v2.0.0-AN";
const uint32_t POST_INTERVAL_MS = 10000; // 10 detik
const uint32_t HTTP_TIMEOUT_MS  = 3500;
const uint32_t BACKLIGHT_TIMEOUT_MS = 15000; // mati setelah 15 detik tanpa interaksi

const char GTS_ROOT_R4[] PROGMEM = R"CERT(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----)CERT";

// =======================
//  INPUT: 4 BUTTON
// =======================
// Button aktif LOW (INPUT_PULLUP)
#define BTN_UP     5
#define BTN_DOWN   6
#define BTN_OK     7
#define BTN_BACK   17

// =======================
//  OBJEK GLOBAL
// =======================
class TFTWrapper : public Adafruit_ILI9341 {
public:
  using Adafruit_ILI9341::Adafruit_ILI9341;

  void setTextFont(uint8_t size) {
    setTextSize(size);
    setFont(NULL);
  }

  int16_t textWidth(const char* text, uint8_t size) {
    setTextSize(size);
    int16_t x1, y1;
    uint16_t w, h;
    getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return static_cast<int16_t>(w);
  }

  void drawString(const char* text, int16_t x, int16_t y) {
    setCursor(x, y);
    print(text);
  }

  void drawCentreString(const char* text, int16_t x, int16_t y, uint8_t size) {
    setTextSize(size);
    int16_t x1, y1;
    uint16_t w, h;
    getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    setCursor(x - static_cast<int16_t>(w / 2), y);
    print(text);
  }
};

TFTWrapper tft(TFT_CS, TFT_DC, TFT_RST);
HardwareSerial RS485(2);

// (opsional) legacy objects untuk menu kalibrasi lama
ModbusMaster mb_ph, mb_do, mb_env;

struct RuntimeStatus {
  char postStatus[16] = "--";
};

struct TaskInputArgs {
  QueueHandle_t qInput;
};

struct TaskUIArgs {
  QueueHandle_t qInput;
  QueueHandle_t qDisplay;
  QueueHandle_t qCalib;
  QueueHandle_t qCalibResult;
  EventGroupHandle_t flags;
};

struct TaskSensorArgs {
  QueueHandle_t qDisplay;
  QueueHandle_t qTelemetry;
  QueueHandle_t qCalib;
  QueueHandle_t qCalibResult;
  SemaphoreHandle_t rs485;
  EventGroupHandle_t flags;
  RuntimeStatus* status;
};

struct TaskHTTPArgs {
  QueueHandle_t qTelemetry;
  EventGroupHandle_t flags;
  RuntimeStatus* status;
};

// Helper RS485
static inline void rs485Receive()  { digitalWrite(RS485_DE_RE_PIN, LOW); }
static inline void rs485Transmit() { digitalWrite(RS485_DE_RE_PIN, HIGH); }
static void modbusIdle() { vTaskDelay(pdMS_TO_TICKS(1)); }

// =======================
//  RTOS PRIMITIVES
// =======================
QueueHandle_t qInput;      // Input events -> UI
QueueHandle_t qDisplay;    // Snapshot sensor -> UI (len=1, overwrite)
QueueHandle_t qTelemetry;  // Snapshot sensor -> HTTP (len=1, overwrite)
QueueHandle_t qCalib;      // Perintah kalibrasi -> TaskSensors
QueueHandle_t qCalibResult;// Hasil kalibrasi -> TaskUI
SemaphoreHandle_t mRS485;  // Mutex RS485
EventGroupHandle_t egFlags;

// Event flags
#define EG_WIFI_OK   (1<<0)
#define EG_PORTAL_ON (1<<1)
#define EG_WIFI_BUSY (1<<2)

// Input event
enum InputType: uint8_t { ENC_DELTA, BTN_PRESS };
struct InputEvent {
  InputType type;
  int16_t   value;   // delta (+1/-1) atau pin id
};

// Kalibrasi command (diekskusi di TaskSensors)
enum class CalibCmd: uint8_t {
  NONE,
  DO_ZERO, DO_AIR,
  PH_400, PH_700, PH_1000
};
struct CalibMsg {
  CalibCmd cmd;
};

struct CalibResult {
  CalibCmd cmd;
  bool ok;
};

// Register map (from vendor datasheets)
static const uint16_t REG_PH_CAL_FIXED  = 0x0055;
static const uint16_t REG_DO_ZERO       = 0x001C;
static const uint16_t REG_DO_AIR        = 0x001A;

// =======================
//  GLOBAL STATUS
// =======================
RuntimeStatus gStatus;
volatile uint32_t gLastInteractionMs = 0;
volatile bool gBacklightOn = true;

// Battery sensor (SEN-0052)
#define BAT_ADC_PIN 1
static const float BAT_DIV_R1   = 100000.0f; // ohm, top resistor to battery+
static const float BAT_DIV_R2   = 22000.0f;  // ohm, bottom resistor to GND
static const float BAT_DIV_RATIO = (BAT_DIV_R1 + BAT_DIV_R2) / BAT_DIV_R2;
static const float BAT_CAL_FACTOR = 1.0338f;
static const uint8_t BAT_SAMPLES = 8;
static const float BAT_FILTER_ALPHA = 0.15f;
static const float BAT_PCT_DEADBAND = 0.5f;

struct BatterySocPoint {
  float volts;
  float pct;
};

// Kurva aproksimasi 4S Li-ion (tegangan pack) untuk kondisi idle / beban ringan.
static const BatterySocPoint BAT_4S_SOC_TABLE[] = {
  {16.80f, 100.0f},
  {16.40f,  95.0f},
  {16.20f,  90.0f},
  {16.00f,  80.0f},
  {15.80f,  70.0f},
  {15.60f,  60.0f},
  {15.40f,  50.0f},
  {15.20f,  40.0f},
  {15.00f,  30.0f},
  {14.80f,  20.0f},
  {14.40f,  10.0f},
  {14.00f,   5.0f},
  {13.20f,   0.0f}
};

static float batteryVoltageToPercent4S(float vbat){
  if (!isfinite(vbat)) return 0.0f;

  const size_t n = sizeof(BAT_4S_SOC_TABLE) / sizeof(BAT_4S_SOC_TABLE[0]);
  if (vbat >= BAT_4S_SOC_TABLE[0].volts) return 100.0f;
  if (vbat <= BAT_4S_SOC_TABLE[n - 1].volts) return 0.0f;

  for (size_t i = 0; i < n - 1; ++i){
    const BatterySocPoint& hi = BAT_4S_SOC_TABLE[i];
    const BatterySocPoint& lo = BAT_4S_SOC_TABLE[i + 1];
    if (vbat <= hi.volts && vbat >= lo.volts){
      const float spanV = hi.volts - lo.volts;
      if (spanV <= 0.0001f) return lo.pct;
      const float t = (vbat - lo.volts) / spanV;
      return lo.pct + t * (hi.pct - lo.pct);
    }
  }

  return 0.0f;
}

TaskInputArgs   gInputArgs{};
TaskUIArgs      gUIArgs{};
TaskSensorArgs  gSensorArgs{};
TaskHTTPArgs    gHTTPArgs{};

// Menu cursors
int menuCursor=0, calCursor=0, doCalCursor=0, phCalCursor=0;

// Toast (non-blocking)
static uint32_t toastUntil = 0;
static char toastMsg[12] = {0};

// Toast helper
void showToast(const char* msg, uint16_t ms=1200){
  strncpy(toastMsg, msg, sizeof(toastMsg)-1);
  toastMsg[sizeof(toastMsg)-1] = '\0';
  toastUntil = millis() + ms;
}
bool toastActive(){ return toastMsg[0] && millis() < toastUntil; }
void clearToast(){ toastMsg[0] = '\0'; }

// =======================
//  HTTP POST
// =======================
bool postData(const DisplayData& s, RuntimeStatus& status) {
  if (WiFi.status() != WL_CONNECTED) {
    strncpy(status.postStatus, "--", sizeof(status.postStatus)-1);
    status.postStatus[sizeof(status.postStatus)-1] = '\0';
    return false;
  }

  const char* waitStatus = nullptr;
  if (!s.ph_ok || !isfinite(s.ph)) {
    waitStatus = "W-PH";
  } else if (!s.env_ok || !isfinite(s.air_temp) || !isfinite(s.humidity) || !isfinite(s.lux)) {
    waitStatus = "W-ENV";
  } else if (!s.do_ok || !isfinite(s.do_mgL)) {
    waitStatus = "W-DO";
  } else if (!isfinite(s.water_temp)) {
    waitStatus = "W-TEMP";
  }

  if (waitStatus) {
    strncpy(status.postStatus, waitStatus, sizeof(status.postStatus)-1);
    status.postStatus[sizeof(status.postStatus)-1] = '\0';
    Serial.printf("[HTTP] %s PH:%d ENV:%d DO:%d TEMP:%d\n",
                  waitStatus, s.ph_ok, s.env_ok, s.do_ok, isfinite(s.water_temp));
    return false;
  }

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  const bool https = strncmp(POST_URL, "https://", 8) == 0;
  if (https) secureClient.setCACert(GTS_ROOT_R4);
  if (!(https ? http.begin(secureClient, POST_URL) : http.begin(plainClient, POST_URL))) {
    strncpy(status.postStatus, "E-BEGIN", sizeof(status.postStatus)-1);
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("accept", "application/json");
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.setTimeout(HTTP_TIMEOUT_MS);

  String payload = "{";
  payload += "\"uid\":\""              + String(UID)            + "\",";
  payload += "\"water_temp_c\":"       + String(s.water_temp, 2) + ",";
  payload += "\"air_temp_c\":"         + String(s.air_temp, 2)   + ",";
  payload += "\"do_mg_l\":"            + String(s.do_mgL, 2)     + ",";
  payload += "\"ph\":"                 + String(s.ph, 2)         + ",";
  payload += "\"humidity_rh\":"        + String(s.humidity, 1)   + ",";
  payload += "\"illuminance_lux\":"    + String(s.lux, 0);
  payload += "}";

  int code = http.POST(payload);
  if (code > 0) {
    if (code >= 200 && code < 300) {
      strncpy(status.postStatus, "OK", sizeof(status.postStatus)-1);
      status.postStatus[sizeof(status.postStatus)-1] = '\0';
      http.end();
      return true;
    } else {
      String msg = "E" + String(code);
      strncpy(status.postStatus, msg.c_str(), sizeof(status.postStatus)-1);
      status.postStatus[sizeof(status.postStatus)-1] = '\0';
      Serial.printf("[HTTP] %d %s\n", code, http.getString().c_str());
    }
  } else {
    String msg = "E" + String(code);
    strncpy(status.postStatus, msg.c_str(), sizeof(status.postStatus)-1);
    status.postStatus[sizeof(status.postStatus)-1] = '\0';
    Serial.printf("[HTTP] gagal: %s\n", HTTPClient::errorToString(code).c_str());
  }

  http.end();
  return false;
}

// =======================
//  PEMBACAAN SENSOR
// =======================
static bool readBattery(float& vbat, float& pct){
  static bool batFilterInit = false;
  static float vbatFiltered = 0.0f;
  static float pctFiltered = 0.0f;

  uint32_t acc = 0;
  for (uint8_t i = 0; i < BAT_SAMPLES; ++i){
    acc += analogReadMilliVolts(BAT_ADC_PIN);
  }
  float v_adc = (acc / static_cast<float>(BAT_SAMPLES)) / 1000.0f;
  float vbatRaw = v_adc * BAT_DIV_RATIO * BAT_CAL_FACTOR;
  if (vbatRaw < 0.1f || isnan(vbatRaw)){
    pct = 0.0f;
    return false;
  }

  if (!batFilterInit){
    vbatFiltered = vbatRaw;
    pctFiltered = batteryVoltageToPercent4S(vbatRaw);
    batFilterInit = true;
  } else {
    vbatFiltered += BAT_FILTER_ALPHA * (vbatRaw - vbatFiltered);

    const float pctNow = batteryVoltageToPercent4S(vbatFiltered);
    if (fabsf(pctNow - pctFiltered) >= BAT_PCT_DEADBAND){
      pctFiltered = pctNow;
    }
  }

  vbat = vbatFiltered;
  pct = pctFiltered;

#if DEBUG_MODBUS
  Serial.print("[BAT] v_adc=");
  Serial.print(v_adc, 3);
  Serial.print(" V, raw=");
  Serial.print(vbatRaw, 2);
  Serial.print(" V, filt=");
  Serial.print(vbat, 2);
  Serial.print(", pct=");
  Serial.println(pct, 1);
#endif
  return true;
}

float regsToFloatBE(uint16_t r0, uint16_t r1){
  uint32_t raw = (static_cast<uint32_t>(r0) << 16) | static_cast<uint32_t>(r1);
  float f;
  memcpy(&f, &raw, sizeof(f));
  return f;
}

bool readPH(float& t, float& ph){
  // Sensor pH Rika memakai function 0x03, start 0x0000, length 0x0006.
  // Format respons: [pH][internal][temperature], semuanya float32 big-endian.
  uint8_t res = mb_ph.readHoldingRegisters(0x0000, 6);
  if (res == mb_ph.ku8MBSuccess){
    ph = regsToFloatBE(mb_ph.getResponseBuffer(0), mb_ph.getResponseBuffer(1));
    t  = regsToFloatBE(mb_ph.getResponseBuffer(4), mb_ph.getResponseBuffer(5));

    if (!isfinite(ph) || !isfinite(t) || ph < 0.0f || ph > 14.5f || t < -20.0f || t > 80.0f){
#if DEBUG_MODBUS
      Serial.println("[PH] Invalid float payload");
#endif
      return false;
    }

#if DEBUG_MODBUS
    Serial.print("[PH] pH="); Serial.print(ph);
    Serial.print(", T=");     Serial.println(t);
#endif
    return true;
  }

#if DEBUG_MODBUS
  Serial.print("[PH] Modbus error: 0x");
  Serial.println(res, HEX);
#endif
  return false;
}

bool readEnvironment(float& airTemp, float& humidity, float& lux){
  uint8_t res = mb_env.readHoldingRegisters(0x0000, 4);
  if (res != mb_env.ku8MBSuccess) return false;

  humidity = mb_env.getResponseBuffer(0) * 0.1f + HUMIDITY_OFFSET;
  airTemp = static_cast<int16_t>(mb_env.getResponseBuffer(1)) * 0.1f + AIR_TEMP_OFFSET;
  lux = ((static_cast<uint32_t>(mb_env.getResponseBuffer(2)) << 16) |
         mb_env.getResponseBuffer(3)) * LUX_SCALE;
  return isfinite(airTemp) && isfinite(humidity) && isfinite(lux) &&
         airTemp >= -40.0f && airTemp <= 85.0f && humidity >= 0.0f && humidity <= 100.0f;
}

bool readDO(float& do_mg, float& tC){
  // Sensor DO Rika memakai function 0x03, start 0x0000, length 0x0006.
  // Format respons: 3 float 32-bit big-endian
  // [DO mg/L][Saturation %][Temperature C]
  uint8_t res = mb_do.readHoldingRegisters(0x0000, 6);
  if (res == mb_do.ku8MBSuccess){
    do_mg = regsToFloatBE(mb_do.getResponseBuffer(0), mb_do.getResponseBuffer(1));
    float sat = regsToFloatBE(mb_do.getResponseBuffer(2), mb_do.getResponseBuffer(3));
    tC    = regsToFloatBE(mb_do.getResponseBuffer(4), mb_do.getResponseBuffer(5));

    if (!isfinite(do_mg) || !isfinite(tC) || do_mg < 0.0f || tC < -20.0f || tC > 80.0f){
#if DEBUG_MODBUS
      Serial.println("[DO] Invalid float payload");
#endif
      return false;
    }

#if DEBUG_MODBUS
    Serial.print("[DO] DO=");        Serial.print(do_mg);
    Serial.print(" mg/L, Sat=");     Serial.print(sat);
    Serial.print(" %, T=");
    Serial.println(tC);
#endif
    return true;
  } else {
#if DEBUG_MODBUS
    Serial.print("[DO] Modbus error: 0x");
    Serial.println(res, HEX);
#endif
    return false;
  }
}

static void resolveWaterTemperature(DisplayData& cur){
  if (cur.do_ok && isfinite(cur.do_tC)) {
    cur.water_temp = cur.do_tC + WATER_TEMP_OFFSET;
  } else if (cur.ph_ok && isfinite(cur.phT)) {
    cur.water_temp = cur.phT + WATER_TEMP_OFFSET;
  } else {
    cur.water_temp = NAN;
  }
}

static void readOneModularSensor(DisplayData& cur, uint8_t& phase){
  float temp = NAN;
  float value1 = NAN;

  switch (phase){
    case 0:
      if (readPH(temp, value1)){
        cur.ph = value1 * PH_SCALE + PH_OFFSET;
        cur.phT = temp;
        cur.ph_ok = true;
      }
      break;
    case 1:
      if (readDO(value1, temp)){
        cur.do_mgL = value1 * DO_SCALE + DO_OFFSET;
        cur.do_tC = temp;
        cur.do_ok = true;
      }
      break;
    case 2:
    default:
      if (readEnvironment(cur.air_temp, cur.humidity, cur.lux)) cur.env_ok = true;
      break;
  }

  phase = static_cast<uint8_t>((phase + 1) % 3);
}


// =======================
//  MODBUS WRITE HELPER (Calib)
// =======================
static bool writeReg(ModbusMaster& mb, uint16_t reg, uint16_t value){
  uint8_t res = mb.writeSingleRegister(reg, value);
  return res == mb.ku8MBSuccess;
}

static bool handleCalibration(CalibCmd cmd){
  bool ok = false;
  switch (cmd){
    case CalibCmd::DO_ZERO:
      ok = writeReg(mb_do, REG_DO_ZERO, 1);
      break;
    case CalibCmd::DO_AIR:
      ok = writeReg(mb_do, REG_DO_AIR, 1);
      break;
    case CalibCmd::PH_400:
      ok = writeReg(mb_ph, REG_PH_CAL_FIXED, 4);
      break;
    case CalibCmd::PH_700:
      ok = writeReg(mb_ph, REG_PH_CAL_FIXED, 7);
      break;
    case CalibCmd::PH_1000:
      ok = writeReg(mb_ph, REG_PH_CAL_FIXED, 10);
      break;
    default:
      break;
  }
  return ok;
}

// =======================
//  INPUT / DEBOUNCE
// =======================
bool debounceRead(uint8_t pin){
  static uint8_t last[64] = {1};
  static uint32_t tLast[64] = {0};
  uint8_t raw = digitalRead(pin);
  uint32_t now = millis();
  if (raw != last[pin] && now - tLast[pin] > 25){
    tLast[pin] = now; last[pin] = raw;
    if (raw == LOW) return true;
  }
  return false;
}

// =======================
//  MENU DATA
// =======================
const char* MAIN_ITEMS[] = {
  "Dashboard",
  "WiFi Manager",
  "Kalibrasi Sensor"
};
const int MAIN_COUNT = sizeof(MAIN_ITEMS)/sizeof(MAIN_ITEMS[0]);

const char* CAL_ITEMS[] = {
  "Kalibrasi pH",
  "Kalibrasi DO",
  "Kembali"
};
const int CAL_COUNT = sizeof(CAL_ITEMS)/sizeof(CAL_ITEMS[0]);

const char* DO_CAL_ITEMS[] = {
  "Nol Oksigen (Na2SO3)",
  "Udara (100% saturasi)",
  "Kembali"
};
const int DO_CAL_COUNT = sizeof(DO_CAL_ITEMS)/sizeof(DO_CAL_ITEMS[0]);

const char* PH_CAL_ITEMS[] = {
  "Buffer pH 7.00 (awal)",
  "Buffer pH 4.00",
  "Buffer pH 10.00",
  "Kembali"
};
const int PH_CAL_COUNT = sizeof(PH_CAL_ITEMS)/sizeof(PH_CAL_ITEMS[0]);

enum class CalSensor: uint8_t { PH, DO };
enum class CalWizardStage: uint8_t { PREPARE, WAIT_STABLE, SENDING, RESULT };

struct CalibrationOption {
  CalibCmd cmd;
  CalSensor sensor;
  const char* title;
  const char* instruction1;
  const char* instruction2;
  const char* instruction3;
  uint16_t waitSeconds;
};

static const CalibrationOption CAL_PH_OPTIONS[] = {
  {CalibCmd::PH_700,  CalSensor::PH, "pH 7.00",  "Bilas probe dengan air DI", "Celupkan ke buffer pH 7.00",  "Gunakan 30-50 mL larutan", 60},
  {CalibCmd::PH_400,  CalSensor::PH, "pH 4.00",  "Bilas probe dengan air DI", "Celupkan ke buffer pH 4.00",  "Lakukan setelah kalibrasi pH 7", 60},
  {CalibCmd::PH_1000, CalSensor::PH, "pH 10.00", "Bilas probe dengan air DI", "Celupkan ke buffer pH 10.00", "Langkah ini bersifat opsional", 60}
};

static const CalibrationOption CAL_DO_OPTIONS[] = {
  {CalibCmd::DO_ZERO, CalSensor::DO, "DO Nol Oksigen", "Larutkan 5g Na2SO3/100mL", "Celupkan probe ke larutan", "Pastikan probe terendam", 180},
  {CalibCmd::DO_AIR,  CalSensor::DO, "DO Udara 100%", "Bilas dan keringkan probe", "Letakkan probe di udara bebas", "Jangan sentuh membran probe", 180}
};

const char* WIFI_MGR_ITEMS[] = { "Start Portal", "Back" };
const int WIFI_MGR_COUNT = sizeof(WIFI_MGR_ITEMS)/sizeof(WIFI_MGR_ITEMS[0]);

// =======================
//  DRAW HELPERS (UI)
// =======================
struct DashboardCache {
  char water_temp[16];
  char air_temp[16];
  char do_mg[16];
  char ph[16];
  char humidity[16];
  char lux[16];
  char status[48];
  bool valid = false;
};

static void formatDashboardStrings(const DisplayData& d, DashboardCache& out){
  if (isfinite(d.water_temp)) snprintf(out.water_temp, sizeof(out.water_temp), "%.2f", d.water_temp);
  else                        snprintf(out.water_temp, sizeof(out.water_temp), "--");
  if (d.env_ok) snprintf(out.air_temp, sizeof(out.air_temp), "%.2f", d.air_temp);
  else          snprintf(out.air_temp, sizeof(out.air_temp), "--");
  if (d.do_ok)  snprintf(out.do_mg, sizeof(out.do_mg), "%.2f", d.do_mgL);
  else          snprintf(out.do_mg, sizeof(out.do_mg), "--");
  if (d.ph_ok)  snprintf(out.ph, sizeof(out.ph), "%.2f", d.ph);
  else          snprintf(out.ph, sizeof(out.ph), "--");
  if (d.env_ok) snprintf(out.humidity, sizeof(out.humidity), "%.1f", d.humidity);
  else          snprintf(out.humidity, sizeof(out.humidity), "--");
  if (d.env_ok) snprintf(out.lux, sizeof(out.lux), "%.0f", d.lux);
  else          snprintf(out.lux, sizeof(out.lux), "--");

  char wifi[8];
  if (d.wifiOK) snprintf(wifi, sizeof(wifi), "OK");
  else          snprintf(wifi, sizeof(wifi), "E%d", d.wifiStatus);

  if (d.bat_ok){
    char volts[8];
    snprintf(volts, sizeof(volts), "%.1f", d.bat_v);
    char* decimal = strchr(volts, '.');
    if (decimal) *decimal = ',';
    snprintf(out.status, sizeof(out.status), "W:%s S:%s B:%d%% %sV",
             wifi, d.postStatus, static_cast<int>(d.bat_pct + 0.5f), volts);
  } else {
    snprintf(out.status, sizeof(out.status), "W:%s S:%s B:EADC", wifi, d.postStatus);
  }
}

static void drawToastOverlay(){
  if (!toastActive()){
    return;
  }
  tft.fillRect(0, 24, 320, 18, TFT_ORANGE);
  tft.setTextColor(TFT_BLACK, TFT_ORANGE);
  tft.setTextFont(2);
  tft.drawString(toastMsg, 6, 26);
}

static void drawStatusBar(const DisplayData& d){
  tft.fillRect(0, 0, 320, 24, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

  DashboardCache tmp{};
  formatDashboardStrings(d, tmp);
  const uint8_t font = tft.textWidth(tmp.status, 2) <= 312 ? 2 : 1;
  tft.setTextFont(font);
  tft.drawString(tmp.status, 4, font == 2 ? 4 : 8);
}

static void drawMetricBox(int x, int y, int w, int h, const char* label, const char* unit,
                          const char* value, uint16_t color){
  tft.fillRect(x, y, w, h, TFT_BLACK);
  tft.drawRect(x, y, w, h, TFT_DARKGREY);
  tft.setTextFont(2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(label, x + 6, y + 4);

  if (unit && unit[0]){
    tft.setTextFont(1);
    const int16_t unitW = tft.textWidth(unit, 1);
    tft.drawString(unit, x + w - unitW - 6, y + 8);
  }

  uint8_t valueFont = 4;
  const int maxTextW = w - 12;
  while (valueFont > 2 && tft.textWidth(value, valueFont) > maxTextW){
    --valueFont;
  }

  int16_t valueW = tft.textWidth(value, valueFont);
  int16_t valueX = x + 6;
  if (valueW < maxTextW){
    valueX = x + (w - valueW) / 2;
  }

  const int valueY = (valueFont >= 4) ? (y + 24) : (y + 28);
  tft.setTextFont(valueFont);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(value, valueX, valueY);
}

static void drawWiFiBusyScreen(const char* line1, const char* line2){
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("WiFi", 160, 72, 4);
  tft.setTextFont(2);
  tft.drawCentreString(line1, 160, 120, 2);
  tft.drawCentreString(line2, 160, 144, 2);
}

static void drawSplashStatic(){
  const uint16_t splashBg = tft.color565(0x19, 0x8A, 0xFF);
  tft.fillScreen(splashBg);
  tft.drawXBitmap(0, 0, logo_bitmap, 320, 240, TFT_WHITE);

  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, splashBg);
  char ln1[28]; snprintf(ln1, sizeof(ln1), "UID: %s", UID);
  char ln2[20]; snprintf(ln2, sizeof(ln2), "FW : %s", FW_VERSION);
  tft.drawString(ln1, 4, 4);
  tft.drawString(ln2, 4, 14);

  tft.drawRect(40, 222, 240, 14, TFT_WHITE);
}

static void drawSplashDynamic(uint8_t pct, bool wifiOK, bool sendOK){
  const uint16_t splashBg = tft.color565(0x19, 0x8A, 0xFF);

  tft.fillRect(60, 204, 100, 16, splashBg);
  tft.fillRect(160, 204, 100, 16, splashBg);
  tft.setTextColor(TFT_WHITE, splashBg);
  tft.drawCentreString(wifiOK ? "WiFi: OK" : "WiFi: --", 110, 204, 2);
  tft.drawCentreString(sendOK ? "SEND: OK" : "SEND: --", 210, 204, 2);

  tft.fillRect(42, 224, 236, 10, splashBg);
  tft.fillRect(42, 224, map(pct, 0, 100, 0, 236), 10, TFT_WHITE);
}

static void drawDashboardFrame(const DisplayData& d){
  tft.fillScreen(TFT_BLACK);
  drawStatusBar(d);

  const int gap = 6;
  const int colW = (320 - gap * 3) / 2;
  const int rowH = 60;
  const int y0 = 28;

  char buf[16];

  if (isfinite(d.water_temp)) snprintf(buf, sizeof(buf), "%.2f", d.water_temp);
  else                  snprintf(buf, sizeof(buf), "--");
  drawMetricBox(gap, y0, colW, rowH, "Water Temp", "C", buf, TFT_YELLOW);

  if (d.env_ok) snprintf(buf, sizeof(buf), "%.2f", d.air_temp);
  else         snprintf(buf, sizeof(buf), "--");
  drawMetricBox(gap * 2 + colW, y0, colW, rowH, "Air Temp", "C", buf, TFT_CYAN);

  if (d.do_ok) snprintf(buf, sizeof(buf), "%.2f", d.do_mgL);
  else         snprintf(buf, sizeof(buf), "--");
  drawMetricBox(gap, y0 + rowH + gap, colW, rowH, "DO", "mg/L", buf, TFT_GREEN);

  if (d.ph_ok) snprintf(buf, sizeof(buf), "%.2f", d.ph);
  else         snprintf(buf, sizeof(buf), "--");
  drawMetricBox(gap * 2 + colW, y0 + rowH + gap, colW, rowH, "pH", "", buf, TFT_ORANGE);

  if (d.env_ok) snprintf(buf, sizeof(buf), "%.1f", d.humidity);
  else         snprintf(buf, sizeof(buf), "--");
  drawMetricBox(gap, y0 + (rowH + gap) * 2, colW, rowH, "Humidity", "%RH", buf, TFT_BLUE);

  if (d.env_ok) snprintf(buf, sizeof(buf), "%.0f", d.lux);
  else         snprintf(buf, sizeof(buf), "--");
  drawMetricBox(gap * 2 + colW, y0 + (rowH + gap) * 2, colW, rowH, "Illuminance", "Lux", buf, TFT_MAGENTA);

  drawToastOverlay();
}

static void drawDashboardUpdate(const DisplayData& d, DashboardCache& cache, bool force){
  DashboardCache now{};
  formatDashboardStrings(d, now);

  if (force || !cache.valid || strncmp(cache.status, now.status, sizeof(now.status)) != 0){
    drawStatusBar(d);
  }

  const int gap = 6;
  const int colW = (320 - gap * 3) / 2;
  const int rowH = 60;
  const int y0 = 28;

  if (force || !cache.valid || strncmp(cache.water_temp, now.water_temp, sizeof(now.water_temp)) != 0){
    drawMetricBox(gap, y0, colW, rowH, "Water Temp", "C", now.water_temp, TFT_YELLOW);
  }
  if (force || !cache.valid || strncmp(cache.air_temp, now.air_temp, sizeof(now.air_temp)) != 0){
    drawMetricBox(gap * 2 + colW, y0, colW, rowH, "Air Temp", "C", now.air_temp, TFT_CYAN);
  }
  if (force || !cache.valid || strncmp(cache.do_mg, now.do_mg, sizeof(now.do_mg)) != 0){
    drawMetricBox(gap, y0 + rowH + gap, colW, rowH, "DO", "mg/L", now.do_mg, TFT_GREEN);
  }
  if (force || !cache.valid || strncmp(cache.ph, now.ph, sizeof(now.ph)) != 0){
    drawMetricBox(gap * 2 + colW, y0 + rowH + gap, colW, rowH, "pH", "", now.ph, TFT_ORANGE);
  }
  if (force || !cache.valid || strncmp(cache.humidity, now.humidity, sizeof(now.humidity)) != 0){
    drawMetricBox(gap, y0 + (rowH + gap) * 2, colW, rowH, "Humidity", "%RH", now.humidity, TFT_BLUE);
  }
  if (force || !cache.valid || strncmp(cache.lux, now.lux, sizeof(now.lux)) != 0){
    drawMetricBox(gap * 2 + colW, y0 + (rowH + gap) * 2, colW, rowH, "Illuminance", "Lux", now.lux, TFT_MAGENTA);
  }

  cache = now;
  cache.valid = true;
  drawToastOverlay();
}

void drawMenu(const char* title, const char* items[], int nItems, int cursor){
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, 24, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextFont(2);
  tft.drawString(title, 6, 4);

  int y = 34;
  const int itemH = 28;
  for (int i = 0; i < nItems; ++i){
    if (i == cursor){
      tft.fillRect(6, y - 4, 308, itemH, TFT_BLUE);
      tft.setTextColor(TFT_WHITE, TFT_BLUE);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tft.setTextFont(2);
    tft.drawString(items[i], 12, y);
    y += itemH;
  }

  drawToastOverlay();
}

static void drawMenuItem(const char* items[], int index, bool selected){
  const int itemH = 28;
  const int y = 34 + index * itemH;
  tft.fillRect(6, y - 4, 308, itemH, selected ? TFT_BLUE : TFT_BLACK);
  tft.setTextColor(TFT_WHITE, selected ? TFT_BLUE : TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString(items[index], 12, y);
}

static bool calibrationLiveValue(const CalibrationOption& option, const DisplayData& d,
                                 float& value, const char*& unit){
  switch (option.sensor){
    case CalSensor::PH:
      value = d.ph;
      unit = "pH";
      return d.ph_ok && isfinite(value);
    case CalSensor::DO:
      value = d.do_mgL;
      unit = "mg/L";
      return d.do_ok && isfinite(value);
  }
  return false;
}

static void drawWizardInstruction(const char* prefix, const char* text, int16_t y){
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(prefix, 8, y);

  uint8_t size = tft.textWidth(text, 2) <= 282 ? 2 : 1;
  tft.setTextFont(size);
  tft.drawString(text, 28, y + (size == 1 ? 4 : 0));
}

static void drawCalibrationWizard(const CalibrationOption& option, CalWizardStage stage,
                                  const DisplayData& d, uint32_t elapsedSeconds, bool resultOK){
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, 28, TFT_NAVY);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString(option.title, 8, 6);

  if (stage == CalWizardStage::PREPARE){
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Langkah 1/3 - Persiapan", 8, 38);
    drawWizardInstruction("1.", option.instruction1, 72);
    drawWizardInstruction("2.", option.instruction2, 102);
    drawWizardInstruction("3.", option.instruction3, 132);
    tft.fillRect(0, 205, 320, 35, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawString("OK: mulai   BACK: batal", 24, 216);
    return;
  }

  if (stage == CalWizardStage::WAIT_STABLE){
    float value = NAN;
    const char* unit = "";
    const bool valid = calibrationLiveValue(option, d, value, unit);
    const uint32_t wait = option.waitSeconds;
    const uint32_t remaining = elapsedSeconds >= wait ? 0 : wait - elapsedSeconds;
    const uint32_t progressSeconds = elapsedSeconds < wait ? elapsedSeconds : wait;
    const uint16_t progress = static_cast<uint16_t>(progressSeconds * 280UL / wait);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Langkah 2/3 - Tunggu stabil", 8, 38);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Nilai sensor saat ini", 8, 70);

    char valueText[32];
    if (valid) snprintf(valueText, sizeof(valueText), "%.2f %s", value, unit);
    else       snprintf(valueText, sizeof(valueText), "Sensor belum terbaca");
    tft.setTextFont(valid ? 4 : 2);
    tft.setTextColor(valid ? TFT_CYAN : TFT_ORANGE, TFT_BLACK);
    tft.drawCentreString(valueText, 160, 96, valid ? 4 : 2);

    tft.drawRect(20, 148, 280, 16, TFT_WHITE);
    tft.fillRect(22, 150, progress > 4 ? progress - 4 : 0, 12, TFT_GREEN);
    tft.setTextFont(2);
    char waitText[40];
    if (remaining > 0) snprintf(waitText, sizeof(waitText), "Tunggu %lu detik", static_cast<unsigned long>(remaining));
    else               snprintf(waitText, sizeof(waitText), "Nilai siap dikalibrasi");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(waitText, 160, 174, 2);

    tft.fillRect(0, 205, 320, 35, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.drawCentreString(remaining == 0 && valid ? "OK: simpan  BACK: batal" : "BACK: batal", 160, 216, 2);
    return;
  }

  if (stage == CalWizardStage::SENDING){
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawCentreString("Menyimpan kalibrasi...", 160, 85, 2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawCentreString("Jangan cabut sensor", 160, 120, 2);
    return;
  }

  tft.setTextColor(resultOK ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  tft.drawCentreString(resultOK ? "Kalibrasi berhasil" : "Kalibrasi gagal", 160, 72, 2);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(resultOK ? "Bilas probe sebelum digunakan" : "Periksa sensor dan coba lagi", 160, 125, 2);
  tft.fillRect(0, 205, 320, 35, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawCentreString("OK/BACK: kembali", 160, 216, 2);
}

// =======================
//  LED INDICATOR (RGB)
// =======================
struct LedRGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static inline void ledWrite(const LedRGB& c){
  digitalWrite(LED_R_PIN, c.r ? HIGH : LOW);
  digitalWrite(LED_G_PIN, c.g ? HIGH : LOW);
  digitalWrite(LED_B_PIN, c.b ? HIGH : LOW);
}

static void ledInit(){
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  ledWrite({0,0,0});
}

static void setBacklight(bool on){
  gBacklightOn = on;
#if TFT_BACKLIGHT_ON == HIGH
  digitalWrite(TFT_BL, on ? HIGH : LOW);
#else
  digitalWrite(TFT_BL, on ? LOW : HIGH);
#endif
}

static void noteInteraction(){
  gLastInteractionMs = millis();
  if (!gBacklightOn){
    setBacklight(true);
  }
}

static void initDisplay(){
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(TFT_DC, OUTPUT);
  digitalWrite(TFT_DC, HIGH);
  pinMode(TFT_RST, OUTPUT);

  // Beberapa modul ILI9341 di ESP32-S3 lebih stabil jika di-hard-reset
  // dulu dan dijalankan dengan clock SPI yang lebih konservatif.
  digitalWrite(TFT_RST, HIGH);
  delay(5);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  tft.begin(TFT_SPI_FREQ);
  delay(20);
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(false);
  tft.setTextWrap(false);
  tft.fillScreen(TFT_BLACK);
}

// =======================
//  TASK INPUT (4 BUTTON)
// =======================
void TaskInput(void* param){
  auto* args = static_cast<TaskInputArgs*>(param);
  if (!args || !args->qInput){
    vTaskDelete(NULL);
    return;
  }

  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_OK,    INPUT_PULLUP);
  pinMode(BTN_BACK,  INPUT_PULLUP);

  for(;;){
    // Navigasi: UP / DOWN → ENC_DELTA (-1 / +1)
    if (debounceRead(BTN_UP)){
      noteInteraction();
      InputEvent e{ENC_DELTA, -1};
      xQueueSend(args->qInput, &e, 0);
    }
    if (debounceRead(BTN_DOWN)){
      noteInteraction();
      InputEvent e{ENC_DELTA, +1};
      xQueueSend(args->qInput, &e, 0);
    }

    // Aksi: OK & BACK → BTN_PRESS
    if (debounceRead(BTN_OK)){
      noteInteraction();
      InputEvent e{BTN_PRESS, BTN_OK};
      xQueueSend(args->qInput, &e, 0);
    }
    if (debounceRead(BTN_BACK)){
      noteInteraction();
      InputEvent e{BTN_PRESS, BTN_BACK};
      xQueueSend(args->qInput, &e, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =======================
//  TASK UI
// =======================
void TaskUI(void* param){
  auto* args = static_cast<TaskUIArgs*>(param);
  if (!args || !args->qInput || !args->qDisplay || !args->flags ||
      !args->qCalib || !args->qCalibResult){
    vTaskDelete(NULL);
    return;
  }

  DisplayData disp = {};
  DashboardCache dashCache{};
  uint8_t splashPct = 0;
  uint32_t tSplash = millis();
  bool splashDrawn = false;
  bool wasInSplash = true;
  UIState ui = UIState::DASHBOARD;
  UIState lastUi = UIState::DASHBOARD;
  bool lastWifiOK = false;
  bool lastSendOK = false;
  bool lastWiFiBusy = false;
  bool lastToastActive = false;
  char lastToastMsg[sizeof(toastMsg)] = {0};
  char lastPostStatus[16] = {0};
  int lastMenuCursor = menuCursor;
  int lastCalCursor = calCursor;
  int lastDoCalCursor = doCalCursor;
  int lastPhCalCursor = phCalCursor;
  const CalibrationOption* activeCalibration = nullptr;
  UIState calibrationParent = UIState::CALIB;
  CalWizardStage wizardStage = CalWizardStage::PREPARE;
  uint32_t wizardStartedMs = 0;
  uint32_t lastWizardSecond = UINT32_MAX;
  bool wizardResultOK = false;
  uint8_t modbusFailStreak = 0;
  uint8_t postErrStreak = 0;
  uint32_t ledFlashUntil = 0;
  LedRGB ledFlashColor{0,0,0};

  for(;;){
    bool gotInput = false;
    bool gotDisplay = false;
    bool gotCalibResult = false;
    bool forceRedraw = false;
    uint32_t idleMs = millis() - gLastInteractionMs;
    if (gBacklightOn && idleMs >= BACKLIGHT_TIMEOUT_MS){
      setBacklight(false);
    }

    InputEvent ev;
    while (xQueueReceive(args->qInput, &ev, 0) == pdTRUE){
      gotInput = true;
      if (ev.type == ENC_DELTA){
        int dir = (ev.value > 0) ? 1 : -1;
        switch (ui){
          case UIState::MENU:
            menuCursor = constrain(menuCursor + dir, 0, MAIN_COUNT-1);
            break;
          case UIState::CALIB:
            calCursor = constrain(calCursor + dir, 0, CAL_COUNT-1);
            break;
          case UIState::CAL_DO:
            doCalCursor = constrain(doCalCursor + dir, 0, DO_CAL_COUNT-1);
            break;
          case UIState::CAL_PH:
            phCalCursor = constrain(phCalCursor + dir, 0, PH_CAL_COUNT-1);
            break;
          default:
            break;
        }
      } else if (ev.type == BTN_PRESS){
        if (ev.value == BTN_BACK){
          if (ui == UIState::CAL_WIZARD){
            if (wizardStage != CalWizardStage::SENDING) ui = calibrationParent;
          } else if (ui == UIState::CAL_DO || ui == UIState::CAL_PH){
            ui = UIState::CALIB;
          } else if (ui == UIState::CALIB || ui == UIState::WIFI_MGR){
            ui = UIState::MENU;
          } else if (ui == UIState::MENU){
            ui = UIState::DASHBOARD;
          }
          continue;
        }

        if (ui == UIState::DASHBOARD){
          if (ev.value == BTN_OK){
            ui = UIState::MENU;
            showToast("Menu");
          }
        } else if (ui == UIState::MENU){
          if (ev.value == BTN_OK){
            switch (menuCursor){
              case 0: ui = UIState::DASHBOARD;   break;
              case 1: ui = UIState::WIFI_MGR;    break;
              case 2: ui = UIState::CALIB;       break;
            }
          }
        } else if (ui == UIState::WIFI_MGR){
          if (ev.value == BTN_OK){
            xEventGroupSetBits(args->flags, EG_PORTAL_ON);
            showToast("WiFi Portal");
            ui = UIState::DASHBOARD;
          }
        } else if (ui == UIState::CALIB){
          if (ev.value == BTN_OK){
            switch (calCursor){
              case 0: ui = UIState::CAL_PH; break;
              case 1: ui = UIState::CAL_DO; break;
              case 2: ui = UIState::MENU;   break;
            }
          }
        } else if (ui == UIState::CAL_DO){
          if (ev.value == BTN_OK){
            if (doCalCursor < 2){
              activeCalibration = &CAL_DO_OPTIONS[doCalCursor];
              calibrationParent = UIState::CAL_DO;
              wizardStage = CalWizardStage::PREPARE;
              ui = UIState::CAL_WIZARD;
            } else {
              ui = UIState::CALIB;
            }
          }
        } else if (ui == UIState::CAL_PH){
          if (ev.value == BTN_OK){
            if (phCalCursor < 3){
              activeCalibration = &CAL_PH_OPTIONS[phCalCursor];
              calibrationParent = UIState::CAL_PH;
              wizardStage = CalWizardStage::PREPARE;
              ui = UIState::CAL_WIZARD;
            } else {
              ui = UIState::CALIB;
            }
          }
        } else if (ui == UIState::CAL_WIZARD && activeCalibration && ev.value == BTN_OK){
          if (wizardStage == CalWizardStage::PREPARE){
            wizardStage = CalWizardStage::WAIT_STABLE;
            wizardStartedMs = millis();
            lastWizardSecond = UINT32_MAX;
          } else if (wizardStage == CalWizardStage::WAIT_STABLE){
            const uint32_t elapsed = (millis() - wizardStartedMs) / 1000;
            float liveValue = NAN;
            const char* liveUnit = "";
            if (elapsed >= activeCalibration->waitSeconds &&
                calibrationLiveValue(*activeCalibration, disp, liveValue, liveUnit)){
              CalibMsg m{activeCalibration->cmd};
              if (xQueueSend(args->qCalib, &m, 0) == pdTRUE){
                wizardStage = CalWizardStage::SENDING;
              }
            }
          } else if (wizardStage == CalWizardStage::RESULT){
            ui = calibrationParent;
          }
        }
      }
    }

    DisplayData tmp;
    if (xQueueReceive(args->qDisplay, &tmp, 0) == pdTRUE){
      disp = tmp;
      gotDisplay = true;

      bool sensorsOK = disp.ph_ok && disp.do_ok && disp.env_ok;
      if (sensorsOK){
        modbusFailStreak = 0;
      } else if (modbusFailStreak < 255){
        modbusFailStreak++;
      }

      bool postErr = (disp.postStatus[0] == 'E');
      if (postErr){
        if (postErrStreak < 255) postErrStreak++;
      } else {
        postErrStreak = 0;
      }

      if (strncmp(disp.postStatus, "OK", 2) == 0 && strncmp(lastPostStatus, "OK", 2) != 0){
        ledFlashUntil = millis() + 300;
        ledFlashColor = {0, 192, 192}; // cyan flash
      }
      strncpy(lastPostStatus, disp.postStatus, sizeof(lastPostStatus)-1);
      lastPostStatus[sizeof(lastPostStatus)-1] = '\0';
    }

    CalibResult calibResult{};
    if (xQueueReceive(args->qCalibResult, &calibResult, 0) == pdTRUE){
      if (activeCalibration && calibResult.cmd == activeCalibration->cmd){
        wizardResultOK = calibResult.ok;
        wizardStage = CalWizardStage::RESULT;
        gotCalibResult = true;
      }
    }

    EventBits_t flags = xEventGroupGetBits(args->flags);
    bool wifiOK = (flags & EG_WIFI_OK);
    bool sendOK = (strncmp(disp.postStatus, "OK", 2) == 0);
    bool wifiBusy = (flags & EG_WIFI_BUSY);
    bool toastNow = toastActive();
    bool toastChanged = (toastNow != lastToastActive) ||
                        (strncmp(toastMsg, lastToastMsg, sizeof(toastMsg)) != 0);

    if (toastChanged){
      if (strncmp(toastMsg, "Cal OK", 6) == 0){
        ledFlashUntil = millis() + 350;
        ledFlashColor = {192, 0, 192}; // magenta flash
      } else if (strncmp(toastMsg, "Cal Fail", 8) == 0){
        ledFlashUntil = millis() + 400;
        ledFlashColor = {192, 0, 0}; // red flash
      }
    }

    if (!toastNow && toastChanged){
      forceRedraw = true;
    }

    uint32_t now = millis();
    bool inSplash = (now - tSplash < 2000);
    if (wasInSplash && !inSplash){
      forceRedraw = true;
      dashCache.valid = false;
      splashDrawn = false;
    }
    bool wizardTick = false;
    uint32_t wizardElapsed = 0;
    if (ui == UIState::CAL_WIZARD && activeCalibration && wizardStage == CalWizardStage::WAIT_STABLE){
      wizardElapsed = (now - wizardStartedMs) / 1000;
      if (wizardElapsed != lastWizardSecond){
        lastWizardSecond = wizardElapsed;
        wizardTick = true;
      }
    }
    const bool displayNeedsDraw = gotDisplay && ui != UIState::CAL_WIZARD;
    bool needDraw = forceRedraw || gotInput || displayNeedsDraw || gotCalibResult || toastChanged ||
                    (wifiOK != lastWifiOK) || (sendOK != lastSendOK) ||
                    (wifiBusy != lastWiFiBusy) || wizardTick;

    if (inSplash){
      if (splashPct < 100 && now - tSplash > splashPct*10){
        splashPct++;
        needDraw = true;
      }
      if (!splashDrawn){
        drawSplashStatic();
        splashDrawn = true;
        needDraw = true;
      }
      if (needDraw){
        drawSplashDynamic(splashPct, wifiOK, sendOK);
      }
    } else {
      if (wifiBusy){
        if (needDraw){
          drawWiFiBusyScreen("Radio aktif", "Tunggu koneksi...");
        }
      } else if (needDraw){
        if (ui != lastUi){
          forceRedraw = true;
          lastUi = ui;
          dashCache.valid = false;
        }

        switch (ui){
          case UIState::DASHBOARD:
            if (forceRedraw){
              drawDashboardFrame(disp);
              dashCache.valid = false;
            }
            drawDashboardUpdate(disp, dashCache, forceRedraw);
            break;
          case UIState::MENU:
            if (forceRedraw){
              drawMenu("Main Menu", MAIN_ITEMS, MAIN_COUNT, menuCursor);
              lastMenuCursor = menuCursor;
            } else if (menuCursor != lastMenuCursor){
              drawMenuItem(MAIN_ITEMS, lastMenuCursor, false);
              drawMenuItem(MAIN_ITEMS, menuCursor, true);
              lastMenuCursor = menuCursor;
              drawToastOverlay();
            } else if (toastChanged) {
              drawToastOverlay();
            }
            break;
          case UIState::CALIB:
            if (forceRedraw){
              drawMenu("Kalibrasi Sensor", CAL_ITEMS, CAL_COUNT, calCursor);
              lastCalCursor = calCursor;
            } else if (calCursor != lastCalCursor){
              drawMenuItem(CAL_ITEMS, lastCalCursor, false);
              drawMenuItem(CAL_ITEMS, calCursor, true);
              lastCalCursor = calCursor;
              drawToastOverlay();
            } else if (toastChanged) {
              drawToastOverlay();
            }
            break;
          case UIState::CAL_DO:
            if (forceRedraw){
              drawMenu("Cal DO", DO_CAL_ITEMS, DO_CAL_COUNT, doCalCursor);
              lastDoCalCursor = doCalCursor;
            } else if (doCalCursor != lastDoCalCursor){
              drawMenuItem(DO_CAL_ITEMS, lastDoCalCursor, false);
              drawMenuItem(DO_CAL_ITEMS, doCalCursor, true);
              lastDoCalCursor = doCalCursor;
              drawToastOverlay();
            } else if (toastChanged) {
              drawToastOverlay();
            }
            break;
          case UIState::CAL_PH:
            if (forceRedraw){
              drawMenu("Cal pH (S-PH-01)", PH_CAL_ITEMS, PH_CAL_COUNT, phCalCursor);
              lastPhCalCursor = phCalCursor;
            } else if (phCalCursor != lastPhCalCursor){
              drawMenuItem(PH_CAL_ITEMS, lastPhCalCursor, false);
              drawMenuItem(PH_CAL_ITEMS, phCalCursor, true);
              lastPhCalCursor = phCalCursor;
              drawToastOverlay();
            } else if (toastChanged) {
              drawToastOverlay();
            }
            break;
          case UIState::CAL_WIZARD:
            if (activeCalibration){
              drawCalibrationWizard(*activeCalibration, wizardStage, disp,
                                    wizardElapsed, wizardResultOK);
            }
            break;
          case UIState::WIFI_MGR:
            if (forceRedraw){
              drawMenu("WiFi Manager", WIFI_MGR_ITEMS, WIFI_MGR_COUNT, 0);
            } else if (toastChanged) {
              drawToastOverlay();
            }
            break;
        }
      }
    }

    if (!toastNow){
      clearToast();
    }

    if (needDraw){
      lastWifiOK = wifiOK;
      lastSendOK = sendOK;
      lastWiFiBusy = wifiBusy;
      lastToastActive = toastNow;
      strncpy(lastToastMsg, toastMsg, sizeof(lastToastMsg)-1);
      lastToastMsg[sizeof(lastToastMsg)-1] = '\0';
    }
    wasInSplash = inSplash;

    // ===== LED indicator =====
    const uint32_t nowLed = millis();
    if (wifiBusy){
      ledWrite({0,0,0});
    } else if (ledFlashUntil > nowLed){
      ledWrite(ledFlashColor);
    } else {
      const bool criticalError = (modbusFailStreak >= 3) || (postErrStreak >= 2);
      if (criticalError){
        const bool on = ((nowLed / 200) % 2) == 0;
        ledWrite(on ? LedRGB{192,0,0} : LedRGB{0,0,0}); // fast red blink
      } else if (!wifiOK){
        const bool on = ((nowLed / 1000) % 2) == 0;
        ledWrite(on ? LedRGB{0,0,192} : LedRGB{0,0,0}); // slow blue blink
      } else {
        ledWrite({0,192,0}); // solid green
      }
    }

    vTaskDelay(pdMS_TO_TICKS(wifiBusy ? 150 : 50));
  }
}

// =======================
// TaskSensors (Core 0)
// =======================
void TaskSensors(void* param){
  auto* args = static_cast<TaskSensorArgs*>(param);
  if (!args || !args->qDisplay || !args->qTelemetry || !args->qCalib ||
      !args->qCalibResult || !args->rs485 || !args->status || !args->flags){
    vTaskDelete(NULL);
    return;
  }

  pinMode(RS485_DE_RE_PIN, OUTPUT); rs485Receive();
  RS485.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  auto bindNode = [](ModbusMaster& mb, uint8_t id){
    mb.begin(id, RS485);
    mb.preTransmission(rs485Transmit);
    mb.postTransmission(rs485Receive);
    mb.idle(modbusIdle);
  };

  // Node sensor modular
  bindNode(mb_ph,  PH_SLAVE_ID);
  bindNode(mb_do,  DO_SLAVE_ID);
  bindNode(mb_env, ENV_SLAVE_ID);

  DisplayData cur = {};
  cur.water_temp = NAN;
  cur.air_temp = NAN;
  cur.phT = NAN;
  cur.do_tC = NAN;
  cur.humidity = NAN;
  cur.lux = NAN;
  uint8_t modularPhase = 0;
  const TickType_t rsLockTimeout = pdMS_TO_TICKS(100);

  for(;;){
    EventBits_t loopFlags = xEventGroupGetBits(args->flags);
    bool wifiBusy = (loopFlags & EG_WIFI_BUSY);

    // 1) Proses perintah kalibrasi dari wizard UI.
    CalibMsg msg;
    while (xQueueReceive(args->qCalib, &msg, 0) == pdTRUE){
      CalibResult result{msg.cmd, false};
      if (xSemaphoreTake(args->rs485, rsLockTimeout) == pdTRUE){
        result.ok = handleCalibration(msg.cmd);
        rs485Receive();
        xSemaphoreGive(args->rs485);
      }
      xQueueOverwrite(args->qCalibResult, &result);
    }

    // 2) Poll sensor modular satu per satu agar bus ringan dan watchdog aman
    if (xSemaphoreTake(args->rs485, rsLockTimeout) == pdTRUE){
      readOneModularSensor(cur, modularPhase);
      resolveWaterTemperature(cur);
      rs485Receive();
      xSemaphoreGive(args->rs485);
    }

    float vbat = 0.0f;
    float batPct = 0.0f;
    cur.bat_ok = readBattery(vbat, batPct);
    if (cur.bat_ok){
      cur.bat_v = vbat;
      cur.bat_pct = batPct;
    } else {
      cur.bat_v = 0.0f;
      cur.bat_pct = 0.0f;
    }

    // 4) Tambahan: status WiFi dan POST
    const wl_status_t wifiStatus = WiFi.status();
    cur.wifiOK = (wifiStatus == WL_CONNECTED);
    cur.wifiStatus = static_cast<int8_t>(wifiStatus);
    strncpy(cur.postStatus, args->status->postStatus, sizeof(cur.postStatus)-1);
    cur.postStatus[sizeof(cur.postStatus)-1] = '\0';
    // 5) Kirim snapshot ke UI & HTTP
    xQueueOverwrite(args->qDisplay, &cur);
    xQueueOverwrite(args->qTelemetry, &cur);

    // 6) Delay sampling
    vTaskDelay(pdMS_TO_TICKS(wifiBusy ? 1000 : 250));   // 4 Hz normal, diperlambat saat WiFi aktif
  }
}

// =======================
// TaskHTTP (Core 0, low)
// =======================
void TaskHTTP(void* param){
  auto* args = static_cast<TaskHTTPArgs*>(param);
  if (!args || !args->qTelemetry || !args->status || !args->flags){
    vTaskDelete(NULL);
    return;
  }

  Serial.println("[BOOT] TaskHTTP start");
  WiFi.mode(WIFI_OFF);

  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  wm.setConfigPortalBlocking(false);

  uint32_t lastPost = 0;
  uint32_t lastWiFiAttempt = 0;
  uint32_t bootMs = millis();
  DisplayData snap{};
  bool wifiWasConnected = false;

  for(;;){
    EventBits_t f = xEventGroupGetBits(args->flags);

    // ====== WiFi Manager Portal (dipanggil dari menu) ======
    if (f & EG_PORTAL_ON){
      xEventGroupClearBits(args->flags, EG_PORTAL_ON);

      WiFi.disconnect(true, true);
      vTaskDelay(pdMS_TO_TICKS(200));

      wm.startConfigPortal("AquaNusa-Setup");
      uint32_t portalStart = millis();
      while (wm.getConfigPortalActive()){
        wm.process();
        vTaskDelay(pdMS_TO_TICKS(25));
        if (millis() - portalStart > 300000){
          break;
        }
      }

      bool ok = (WiFi.status() == WL_CONNECTED);
      wifiWasConnected = ok;
      if (ok){
        configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
        xEventGroupSetBits(args->flags, EG_WIFI_OK);
        showToast("WiFi OK");
      } else {
        xEventGroupClearBits(args->flags, EG_WIFI_OK);
        showToast("WiFi Fail");
      }

      WiFi.mode(WIFI_STA);
    }

    // ====== Auto reconnect yang lebih ringan ======
    bool wifiNow = (WiFi.status() == WL_CONNECTED);
    uint32_t now = millis();
    if (!wifiNow && (now - bootMs) >= 15000 && (now - lastWiFiAttempt) >= 30000){
      lastWiFiAttempt = now;
      Serial.println("[BOOT] WiFi connect attempt");
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(true);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
      WiFi.begin();   // pakai kredensial terakhir yang tersimpan
    }

    // ====== Monitor status WiFi biasa (tanpa portal) ======
    wifiNow = (WiFi.status() == WL_CONNECTED);

    if (wifiNow && !wifiWasConnected){
      // Baru saja connect
      wifiWasConnected = true;
      configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
      xEventGroupSetBits(args->flags, EG_WIFI_OK);
    } else if (!wifiNow && wifiWasConnected){
      // Baru saja putus
      wifiWasConnected = false;
      xEventGroupClearBits(args->flags, EG_WIFI_OK);
    }

    EventBits_t flags = xEventGroupGetBits(args->flags);

    // ====== Kirim data ke server ======
    if (millis() - lastPost >= POST_INTERVAL_MS){
      lastPost = millis();
      if (xQueueReceive(args->qTelemetry, &snap, 0)==pdTRUE){

        // WiFi belum OK → tidak usah POST
        if (!(flags & EG_WIFI_OK)){
          strncpy(args->status->postStatus, "--",
                  sizeof(args->status->postStatus)-1);
          args->status->postStatus[sizeof(args->status->postStatus)-1] = '\0';
          vTaskDelay(pdMS_TO_TICKS(200));
          continue;
        }

        if (strncmp(POST_URL, "https://", 8) == 0 && time(nullptr) < 1600000000) {
          strncpy(args->status->postStatus, "W-TIME", sizeof(args->status->postStatus)-1);
          args->status->postStatus[sizeof(args->status->postStatus)-1] = '\0';
        } else {
          postData(snap, *(args->status));
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// =======================
// SETUP & LOOP
// =======================
void setup(){
  Serial.begin(115200);
  delay(100);
  Serial.println("[BOOT] setup begin");

  ledInit();
  Serial.println("[BOOT] LED ready");
  initDisplay();
  Serial.println("[BOOT] TFT ready");
  pinMode(TFT_BL, OUTPUT);
  gLastInteractionMs = millis();
  setBacklight(true);

  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  Serial.println("[BOOT] ADC ready");

  qInput     = xQueueCreate(16, sizeof(InputEvent));
  qDisplay   = xQueueCreate(1,  sizeof(DisplayData));
  qTelemetry = xQueueCreate(1,  sizeof(DisplayData));
  qCalib     = xQueueCreate(4,  sizeof(CalibMsg));
  qCalibResult = xQueueCreate(1, sizeof(CalibResult));
  mRS485     = xSemaphoreCreateMutex();
  egFlags    = xEventGroupCreate();
  Serial.println("[BOOT] RTOS objects ready");

  gInputArgs.qInput = qInput;

  gUIArgs.qInput   = qInput;
  gUIArgs.qDisplay = qDisplay;
  gUIArgs.qCalib   = qCalib;
  gUIArgs.qCalibResult = qCalibResult;
  gUIArgs.flags    = egFlags;
  gSensorArgs.qDisplay   = qDisplay;
  gSensorArgs.qTelemetry = qTelemetry;
  gSensorArgs.qCalib     = qCalib;
  gSensorArgs.qCalibResult = qCalibResult;
  gSensorArgs.rs485      = mRS485;
  gSensorArgs.flags      = egFlags;
  gSensorArgs.status     = &gStatus;

  gHTTPArgs.qTelemetry = qTelemetry;
  gHTTPArgs.flags      = egFlags;
  gHTTPArgs.status     = &gStatus;

  xTaskCreatePinnedToCore(TaskInput,   "TaskInput",   4096,  &gInputArgs,   5, NULL, 1);
  xTaskCreatePinnedToCore(TaskUI,      "TaskUI",      10240, &gUIArgs,      4, NULL, 1);
  xTaskCreatePinnedToCore(TaskSensors, "TaskSensors", 8192,  &gSensorArgs,  1, NULL, 0);
  xTaskCreatePinnedToCore(TaskHTTP,    "TaskHTTP",    8192,  &gHTTPArgs,    1, NULL, 0);
  Serial.println("[BOOT] tasks started");
}

void loop(){
  vTaskDelay(pdMS_TO_TICKS(1000));
}
