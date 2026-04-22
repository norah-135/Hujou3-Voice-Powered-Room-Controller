/*
 * ══════════════════════════════════════════════════════════════
 * ESP32 - Voice & Network Controller v4.0
 * إصلاح: mDNS يبدأ بعد اكتمال WiFi + retry ذكي
 * ══════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <time.h>
#include "secrets.h"

// ═══════ Config ═══════
const char* WIFI_SSID     = SECRET_WIFI_SSID;
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;
const char* ARDUINO_HOST  = "noura-hub";

// ═══════ Hub Discovery ═══════
String        arduinoIP      = "";
bool          hubFound       = false;
unsigned long lastResolve    = 0;
const long    RESOLVE_INTERVAL = 30000;

// ═══════ I2S ═══════
#define I2S_SD   25
#define I2S_WS   26
#define I2S_SCK  27
#define I2S_PORT I2S_NUM_0

// ═══════ LED & Button ═══════
#define BUTTON_PIN  2
#define RED_PIN     5
#define GREEN_PIN   14

// ═══════ Audio ═══════
const int SAMPLE_RATE    = 16000;
const int RECORD_SECONDS = 2;
int       VAD_THRESHOLD  = 800;

// ═══════ State ═══════
enum SystemState { STATE_SAFE, STATE_LISTEN };
SystemState   currentState   = STATE_SAFE;
bool          lastButtonState = HIGH;
bool          buttonState     = HIGH;
unsigned long lastDebounce    = 0;
const int     DEBOUNCE_DELAY  = 50;
unsigned long lastBlinkTime   = 0;
bool          greenBlinkOn    = false;
const int     BLINK_INTERVAL  = 600;

// ══════════════════════════════════════════════════════════════
// [Layer 1] WiFi — ينتظر حتى يكتمل الاتصال فعلاً
// ══════════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.print(F("[L1] الاتصال بـ ")); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.print(F(" OK | IP: ")); Serial.println(WiFi.localIP());
}

// ══════════════════════════════════════════════════════════════
// [Layer 2] mDNS + البحث عن الهب
// ══════════════════════════════════════════════════════════════
bool resolveHub() {
  Serial.print(F("[L2] البحث عن ")); Serial.print(ARDUINO_HOST); Serial.print(F(".local ... "));

  IPAddress ip = MDNS.queryHost(ARDUINO_HOST, 3000);

  if (ip != INADDR_NONE) {
    arduinoIP = ip.toString();
    hubFound  = true;
    Serial.print(F("وُجد ← ")); Serial.println(arduinoIP);
    return true;
  }

  hubFound = false;
  arduinoIP = "";
  Serial.println(F("لم يُجد"));
  return false;
}

// ══════════════════════════════════════════════════════════════
// I2S
// ══════════════════════════════════════════════════════════════
void setupI2S() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8, .dma_buf_len = 1024, .use_apll = false
  };
  i2s_pin_config_t pins = { .bck_io_num=I2S_SCK, .ws_io_num=I2S_WS, .data_out_num=I2S_PIN_NO_CHANGE, .data_in_num=I2S_SD };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
}

// ══════════════════════════════════════════════════════════════
// HTTP Command
// ══════════════════════════════════════════════════════════════
bool sendCommandToArduino(String command) {
  if (WiFi.status() != WL_CONNECTED) { Serial.println(F("[HTTP] WiFi مقطوع")); return false; }
  if (!hubFound) {
    Serial.println(F("[HTTP] الهب مو معروف - جاري البحث..."));
    if (!resolveHub()) return false;
  }

  HTTPClient http;
  String url = "http://" + arduinoIP + "/command";
  Serial.print(F("[HTTP] → ")); Serial.print(url); Serial.print(F(" | ")); Serial.println(command);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  StaticJsonDocument<256> doc;
  doc["command"] = command;
  doc["token"]   = HUJOU3_AUTH_TOKEN;
  doc["time"]    = (long)time(nullptr);   // Unix epoch for Replay Attack Protection
  String json; serializeJson(doc, json);

  int code = http.POST(json);
  http.end();

  if (code > 0) {
    Serial.print(F("[HTTP] OK ")); Serial.println(code);
    return true;
  } else {
    Serial.print(F("[HTTP] فشل: ")); Serial.println(http.errorToString(code));
    // hubFound = false;  // ← تم التعليق للمحافظة على الهب
    return false;
  }
}

// ══════════════════════════════════════════════════════════════
// Voice Recording
// ══════════════════════════════════════════════════════════════
void recordAndSendToPython() {
  digitalWrite(GREEN_PIN, HIGH);
  Serial.println(F("[RECORDING]")); Serial.flush();

  bool interrupted = false;
  for (int i = 0; i < SAMPLE_RATE * RECORD_SECONDS; i++) {
    size_t bytesRead; int32_t raw;
    i2s_read(I2S_PORT, &raw, 4, &bytesRead, portMAX_DELAY);
    int16_t s16 = (int16_t)((raw >> 14) & 0xFFFF);
    Serial.write((uint8_t*)&s16, sizeof(s16));
    
    // فحص الزر أثناء التسجيل لمقاطعته
    handleButton();
    if (currentState == STATE_SAFE) {
      Serial.println(F("\n[INTERRUPTED]"));
      interrupted = true;
      break;
    }
    
    // تأخير بسيط إذا لم نستخدم portMAX_DELAY
    delayMicroseconds(2);
  }

  if (!interrupted) {
    Serial.println(F("\n[DONE]")); 
  }
  Serial.flush();
  if (currentState == STATE_LISTEN) digitalWrite(GREEN_PIN, LOW);
}

// ══════════════════════════════════════════════════════════════
// Button
// ══════════════════════════════════════════════════════════════
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounce = millis();
  if ((millis() - lastDebounce) > DEBOUNCE_DELAY && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) {
      if (currentState == STATE_SAFE) {
        currentState = STATE_LISTEN;
        digitalWrite(RED_PIN, LOW);
        Serial.println(F("\n[زر] وضع الاستماع"));
        // ← تحديث البحث عن الهب عند التفعيل
        if (!hubFound) resolveHub();
      } else {
        currentState = STATE_SAFE;
        digitalWrite(RED_PIN, HIGH); digitalWrite(GREEN_PIN, LOW);
        Serial.println(F("\n[زر] وضع آمن"));
      }
    }
  }
  lastButtonState = reading;
}

// ══════════════════════════════════════════════════════════════
// Menu & Serial
// ══════════════════════════════════════════════════════════════
void showMenu() {
  Serial.println(F("\n========== ESP32 v4.0 =========="));
  Serial.print  (F("  WiFi  : ")); Serial.println(WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString() : "غير متصل");
  Serial.print  (F("  Hub   : ")); Serial.println(hubFound ? arduinoIP : "لم يُجد");
  Serial.print  (F("  State : ")); Serial.println(currentState==STATE_SAFE ? "SAFE (احمر)" : "LISTEN (اخضر)");
  Serial.print  (F("  VAD   : ")); Serial.println(VAD_THRESHOLD);
  Serial.println(F("--------------------------------"));
  Serial.println(F("  [m] منيو        [w] فحص WiFi"));
  Serial.println(F("  [f] ابحث Hub    [i] معلومات"));
  Serial.println(F("  [c] ارسل امر    [t] ضبط VAD"));
  Serial.println(F("  أو اكتب امر مباشر: L1 L0 LW LY Bxx Vxx"));
  Serial.println(F("================================\n"));
}

void handleSerialCommand(String input) {
  if (input.length() == 1) {
    if (isDigit(input[0])) {
      sendCommandToArduino("PLAY_" + input);
      return;
    }
    switch (input[0]) {
      case 'm': case 'M': showMenu(); return;
      case 'w': case 'W':
        Serial.print(F("[WiFi] ")); 
        Serial.println(WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString() : "غير متصل");
        return;
      case 'f': case 'F':
        resolveHub(); return;
      case 'i': case 'I':
        Serial.println(F("\n--- معلومات ---"));
        Serial.print(F("WiFi: ")); Serial.println(WiFi.status()==WL_CONNECTED?"متصل":"لا");
        Serial.print(F("IP  : ")); Serial.println(WiFi.localIP());
        Serial.print(F("Hub : ")); Serial.println(hubFound ? arduinoIP : "غير موجود");
        Serial.print(F("Hub : ")); Serial.println(hubFound ? arduinoIP : "لم يُجد");
        Serial.print(F("VAD : ")); Serial.println(VAD_THRESHOLD);
        return;
      case 'c': case 'C':
        Serial.println(F("أدخل الأمر:"));
        while (!Serial.available()) delay(10);
        sendCommandToArduino(Serial.readStringUntil('\n'));
        return;
      case 't': case 'T':
        Serial.println(F("أدخل VAD (100-5000):"));
        while (!Serial.available()) delay(10);
        { int v = Serial.readStringUntil('\n').toInt(); if(v>=100&&v<=5000){VAD_THRESHOLD=v; Serial.print(F("VAD=")); Serial.println(v);} }
        return;
    }
  }

  // أوامر مباشرة للأردوينو
  if (input.startsWith("L")      || input.startsWith("B") ||
      input.startsWith("V")      || input.startsWith("WAKE") ||
      input.startsWith("SLEEP")  || input.startsWith("MODE") ||
      input.startsWith("PLAY_")  ||
      input == "TOGGLE_LIGHT"    ||
      input == "DEEP_SLEEP"      || input == "SOUND_BROWSER") {
    sendCommandToArduino(input);
  }
}

// ══════════════════════════════════════════════════════════════
// Setup & Loop
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(921600);
  pinMode(RED_PIN,    OUTPUT);
  pinMode(GREEN_PIN,  OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(RED_PIN, HIGH); digitalWrite(GREEN_PIN, LOW);

  connectWiFi();
  MDNS.begin("esp32-node");

  // ─── NTP Sync (Replay Attack Protection) ─────────────────────
  Serial.print(F("[NTP] Syncing time..."));
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  time_t now = 0;
  unsigned long ntpStart = millis();
  while (now < 1700000000UL && millis() - ntpStart < 10000) {
    delay(200); now = time(nullptr); Serial.print(".");
  }
  if (now > 1700000000UL) {
    Serial.print(F(" OK | epoch=")); Serial.println((long)now);
  } else {
    Serial.println(F(" WARN: NTP timeout — timestamps may be 0"));
  }
  // ─────────────────────────────────────────────────────────────

  resolveHub();
  lastResolve = millis();

  setupI2S();
  showMenu();
}

void loop() {
  handleButton();

  // إعادة اتصال WiFi لو انقطع
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[L1] انقطع - إعادة الاتصال..."));
    connectWiFi();
    MDNS.begin("esp32-node");
    hubFound = false;
    return;
  }

  // تحديث دوري للبحث
  if (millis() - lastResolve > RESOLVE_INTERVAL) {
    if (!hubFound) resolveHub();  // ← فقط لو مو موجود
    lastResolve = millis();
  }

  // Serial
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length()) handleSerialCommand(input);
  }

  // Listening mode
  if (currentState == STATE_LISTEN) {
    if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
      lastBlinkTime = millis(); greenBlinkOn = !greenBlinkOn;
      digitalWrite(GREEN_PIN, greenBlinkOn ? HIGH : LOW);
    }
    size_t bytesRead; int32_t sample = 0;
    i2s_read(I2S_PORT, &sample, 4, &bytesRead, 10/portTICK_PERIOD_MS);
    if (abs(sample >> 14) > VAD_THRESHOLD) {
      recordAndSendToPython();
      delay(500);
    }
  } else {
    delay(20);
  }
}