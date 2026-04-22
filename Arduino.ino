/*
 * ═══════════════════════════════════════════════════════════════════
 *  Arduino R4 WiFi - Smart Controller v4.5
 *  FSM + Tuya + DFPlayer + mDNS + OLED Display + Battery Monitor
 * ═══════════════════════════════════════════════════════════════════
 */

#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <ArduinoMDNS.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <Crypto.h>
#include <SHA256.h>
#include "DFRobotDFPlayerMini.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

// ═══════════════════════════════════════════════════════════════════
// FSM States
// ═══════════════════════════════════════════════════════════════════
enum State {
  ST_GENERAL, ST_SOUND_BROWSER, ST_PLAYING_MANUAL,
  ST_MODES_MENU, ST_SLEEP_ACTIVE, ST_WAKE_ACTIVE, ST_DEEP_SLEEP,
  ST_SETTINGS, ST_WIFI_SETTINGS
};

State         currentState   = ST_GENERAL;
State         previousState  = ST_GENERAL;
unsigned long stateStartTime = 0;

const char* stateName(State s) {
  switch (s) {
    case ST_GENERAL:        return "GENERAL";
    case ST_SOUND_BROWSER:  return "SOUND_BROWSER";
    case ST_PLAYING_MANUAL: return "PLAYING_MANUAL";
    case ST_MODES_MENU:     return "MODES_MENU";
    case ST_SLEEP_ACTIVE:   return "SLEEP_ACTIVE";
    case ST_WAKE_ACTIVE:    return "WAKE_ACTIVE";
    case ST_DEEP_SLEEP:     return "DEEP_SLEEP";
    case ST_SETTINGS:       return "SETTINGS";
    case ST_WIFI_SETTINGS:  return "WIFI_SETTINGS";
    default:                return "UNKNOWN";
  }
}

// ═══════════════════════════════════════════════════════════════════
// Config
// ═══════════════════════════════════════════════════════════════════
#define WIFI_SSID  SECRET_WIFI_SSID
#define WIFI_PASS  SECRET_WIFI_PASSWORD
#define MDNS_NAME  "noura-hub"

#define TUYA_CLIENT_ID     SECRET_TUYA_CLIENT_ID
#define TUYA_CLIENT_SECRET SECRET_TUYA_CLIENT_SECRET
#define TUYA_DEVICE_ID     SECRET_TUYA_DEVICE_ID
const char* TUYA_HOST = "openapi.tuyaeu.com";
const int   TUYA_PORT = 443;
#define BAUD_RATE 115200

// ═══════════════════════════════════════════════════════════════════
// Objects
// ═══════════════════════════════════════════════════════════════════
WiFiUDP              ntpUDP;
WiFiUDP              mdnsUDP;
MDNS                 mdns(mdnsUDP);
NTPClient            timeClient(ntpUDP, "time.google.com", 0, 60000);
WiFiSSLClient        client;
DFRobotDFPlayerMini  myDFPlayer;
WiFiServer           localServer(80);

// ═══════════════════════════════════════════════════════════════════
// State Variables
// ═══════════════════════════════════════════════════════════════════
String        accessToken     = "";
unsigned long tokenExpireTime = 0;
bool          lightState      = false;
bool          dfPlayerReady   = false;
int           currentVolume   = 30;
int           currentSound    = 0;
String        lightColor      = "white";
int           lightBrightness = 100;
int           lightTemp       = 500;
unsigned long lastModeUpdate  = 0;
unsigned long lastSoundCheck  = 0;
int           browserCursor   = 0;
bool          wifiConnected   = false;
bool          wifiEnabled     = true;

const char* soundNames[] = {
  "إيقاف", "رياح", "طيور", "محيط", "مطر", "نهر", "نوم عميق"
};
const int SOUND_COUNT = 7;

// ═══════════════════════════════════════════════════════════════════
// ── OLED DISPLAY MODULE ──
// ═══════════════════════════════════════════════════════════════════
#define SCREEN_W       128
#define SCREEN_H        64
#define OLED_RESET      -1
#define OLED_ADDRESS  0x3C
#define BTN_NEXT         2
#define BTN_ENTER        3
#define DEBOUNCE_MS     50
#define INACTIVITY_MS 10000

#define BATTERY_PIN    A0
#define VOLTAGE_REF    3.3
#define VOLTAGE_DIVIDER_RATIO 2.0

enum DisplayState {
  DISP_GENERAL, DISP_SOUND_BROWSER, DISP_MODES_MENU, DISP_PLAYING, DISP_SLEEP,
  DISP_SETTINGS, DISP_WIFI_SETTINGS
};

enum DisplayAction {
  ACT_NONE = 0, ACT_PLAY_SOUND, ACT_STOP_SOUND, ACT_PAUSE_SOUND,
  ACT_RESUME_SOUND, ACT_SET_VOLUME, ACT_SLEEP_MODE, ACT_WAKE_MODE,
  ACT_DEEP_SLEEP, ACT_WAKE_UP, ACT_TOGGLE_WIFI, ACT_FORGET_WIFI,
  ACT_TOGGLE_LIGHT
};

Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);

DisplayAction pendingAction    = ACT_NONE;
int           currentDispSound = 0;
int           currentDispVol   = 20;
DisplayState  displayState     = DISP_GENERAL;
int           menuIndex        = 0;
bool          isPaused         = false;
int           volLevel         = 1;
unsigned long lastActivity     = 0;
unsigned long lastBtnNext      = 0;
unsigned long lastBtnEnter     = 0;
bool          lastNextState    = HIGH;
bool          lastEnterState   = HIGH;
bool          needsRedraw      = true;
bool          screenOn         = true;

const char* dispSoundNames[] = {
  "Off", "Wind", "Birds", "Ocean", "Rain", "River", "Deep Sleep"
};
const int DISP_SOUND_COUNT = 7;

// ═══════════════════════════════════════════════════════════════════
// نظام محاكاة البطارية
// ═══════════════════════════════════════════════════════════════════
#define BATTERY_CAPACITY_MAH     1000
#define POWER_IDLE                45
#define POWER_WIFI_ACTIVE         80
#define POWER_WIFI_TRANSMIT      120
#define POWER_OLED_ON             15
#define POWER_OLED_OFF            0
#define POWER_DFPLAYER_IDLE       10
#define POWER_DFPLAYER_PLAYING    50
#define POWER_LED                  5

float         batteryMAhConsumed  = 0.0;
unsigned long lastPowerUpdate     = 0;
float         currentPowerDraw    = 0.0;
float         batteryPercentage   = 100.0;
int           batteryMinutesLeft  = 0;
unsigned long batteryStartTime    = 0;

// ─── OLED forward declarations ───────────────────────────────────
void _drawSplash();
void _drawGeneral();
void _drawSoundBrowser();
void _drawModesMenu();
void _drawPlaying();
void _drawSettings();
void _drawWiFiSettings();
void _renderCurrentState();
void _onNext();
void _onEnter();
void _goDispState(DisplayState s);
void _goSleep();
void _wakeScreen();
void _drawBatteryIcon(int x, int y);
void _drawWiFiIcon(int x, int y);

// ─── دوال البطارية ──────────────────────────────────────────────
float calculateCurrentPowerDraw() {
  float power = POWER_IDLE;
  if (wifiEnabled) {
    power += POWER_WIFI_ACTIVE;
    power += POWER_WIFI_TRANSMIT * 0.1;
  }
  power += screenOn ? POWER_OLED_ON : POWER_OLED_OFF;
  if (dfPlayerReady) {
    power += POWER_DFPLAYER_IDLE;
    if (currentState == ST_PLAYING_MANUAL || 
        currentState == ST_SLEEP_ACTIVE || 
        currentState == ST_WAKE_ACTIVE) {
      power += POWER_DFPLAYER_PLAYING;
    }
  }
  power += POWER_LED;
  return power;
}

void updateBatteryConsumption() {
  unsigned long now = millis();
  if (lastPowerUpdate == 0) {
    lastPowerUpdate = now;
    batteryStartTime = now;
    return;
  }
  float hoursElapsed = (now - lastPowerUpdate) / 3600000.0;
  currentPowerDraw = calculateCurrentPowerDraw();
  batteryMAhConsumed += currentPowerDraw * hoursElapsed;
  batteryPercentage = 100.0 * (1.0 - (batteryMAhConsumed / BATTERY_CAPACITY_MAH));
  batteryPercentage = constrain(batteryPercentage, 0.0, 100.0);
  if (currentPowerDraw > 0) {
    float remainingMAh = BATTERY_CAPACITY_MAH - batteryMAhConsumed;
    batteryMinutesLeft = (remainingMAh / currentPowerDraw) * 60;
  }
  lastPowerUpdate = now;
}

int getBatteryPercentage() {
  updateBatteryConsumption();
  return (int)batteryPercentage;
}

void resetBatterySimulation() {
  batteryMAhConsumed = 0.0;
  batteryPercentage = 100.0;
  batteryStartTime = millis();
  lastPowerUpdate = millis();
}

// ─── رسم أيقونة البطارية ────────────────────────────────────────
void _drawBatteryIcon(int x, int y) {
  int pct = getBatteryPercentage();
  
  // رسم إطار البطارية
  oled.drawRect(x, y, 14, 7, SSD1306_WHITE);
  oled.drawRect(x + 14, y + 2, 2, 3, SSD1306_WHITE);
  
  // ملء مستوى البطارية
  int fillWidth = map(pct, 0, 100, 0, 12);
  if (fillWidth > 0) {
    oled.fillRect(x + 1, y + 1, fillWidth, 5, SSD1306_WHITE);
  }
  
  // النسبة بجانب الأيقونة - نزلناها مع الأيقونة
  oled.setCursor(x + 19, y - 2);  // y + 1 بدل y - 1
  oled.print(pct);
  oled.print("%");
}

// ─── رسم أيقونة الواي فاي (تتغير حسب حالة الاتصال) ──────────────
void _drawWiFiIcon(int x, int y) {
  wifiConnected = (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0);
  int rssi = wifiConnected ? WiFi.RSSI() : -100;
  
  if (wifiConnected) {
    // 4 أشرطة متصاعدة حسب قوة الإشارة
    int bars = 4;
    if (rssi < -80) bars = 1;
    else if (rssi < -70) bars = 2;
    else if (rssi < -60) bars = 3;
    
    for (int i = 0; i < 4; i++) {
      int height = 3 + i * 2;
      int barY = y + 8 - height;
      if (i < bars) {
        oled.fillRect(x + i * 3, barY, 2, height, SSD1306_WHITE);
      } else {
        oled.drawRect(x + i * 3, barY, 2, height, SSD1306_WHITE);
      }
    }
  } else {
    // أشرطة فاضية + دائرة مشطوبة 🚫
    for (int i = 0; i < 4; i++) {
      int height = 3 + i * 2;
      int barY = y + 8 - height;
      oled.drawRect(x + i * 3, barY, 2, height, SSD1306_WHITE);
    }
    // دائرة مشطوبة
    oled.drawCircle(x + 6, y + 4, 6, SSD1306_WHITE);
    oled.drawLine(x + 2, y, x + 10, y + 8, SSD1306_WHITE);
  }
}
// ─── OLED Init ───────────────────────────────────────────────────
bool initDisplay() {
  pinMode(BTN_NEXT,  INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);
  pinMode(BATTERY_PIN, INPUT);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("[OLED] فشل — تحقق SDA=A4 SCL=A5"));
    return false;
  }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.cp437(true);
  _drawSplash();
  delay(2000);
  lastActivity = millis();
  needsRedraw  = true;
  Serial.println(F("[OLED] جاهز | D2=Next D3=Enter"));
  return true;
}

// ─── OLED Update ─────────────────────────────────────────────────
void updateDisplay() {
  unsigned long now = millis();

  bool currentNext = digitalRead(BTN_NEXT);
  if (currentNext == LOW && lastNextState == HIGH && (now - lastBtnNext > DEBOUNCE_MS)) {
    lastBtnNext  = now;
    lastActivity = now;
    if (!screenOn) { _wakeScreen(); }
    else           { _onNext(); needsRedraw = true; }
  }
  lastNextState = currentNext;

  bool currentEnter = digitalRead(BTN_ENTER);
  if (currentEnter == LOW && lastEnterState == HIGH && (now - lastBtnEnter > DEBOUNCE_MS)) {
    lastBtnEnter = now;
    lastActivity = now;
    if (!screenOn) { _wakeScreen(); }
    else           { _onEnter(); needsRedraw = true; }
  }
  lastEnterState = currentEnter;

  if (screenOn && (millis() - lastActivity > INACTIVITY_MS)) { _goSleep(); return; }
  if (needsRedraw && screenOn) { needsRedraw = false; _renderCurrentState(); }
}

// ─── OLED Action reader ──────────────────────────────────────────
void processDisplayActions() {
  if (pendingAction == ACT_NONE) return;
  DisplayAction act = pendingAction;
  pendingAction = ACT_NONE;

  switch (act) {
    case ACT_PLAY_SOUND:
      currentSound = currentDispSound;
      playSound(currentSound);
      changeState(ST_PLAYING_MANUAL);
      break;
    case ACT_STOP_SOUND:
      playSound(0);
      changeState(ST_GENERAL);
      break;
    case ACT_PAUSE_SOUND:
      if (dfPlayerReady) myDFPlayer.pause();
      break;
    case ACT_RESUME_SOUND:
      if (dfPlayerReady) myDFPlayer.start();
      break;
    case ACT_SET_VOLUME:
      setVolume(currentDispVol);
      break;
    case ACT_SLEEP_MODE:
      changeState(ST_SLEEP_ACTIVE);
      break;
    case ACT_WAKE_MODE:
      changeState(ST_WAKE_ACTIVE);
      break;
    case ACT_DEEP_SLEEP:
      changeState(ST_DEEP_SLEEP);
      break;
    case ACT_WAKE_UP:
      changeState(ST_GENERAL);
      _goDispState(DISP_GENERAL);
      break;
    case ACT_TOGGLE_WIFI:
      if (wifiEnabled) {
        WiFi.disconnect();
        wifiEnabled = false;
        wifiConnected = false;
      } else {
        wifiEnabled = true;
        setupWiFi();
      }
      needsRedraw = true;
      break;
    case ACT_FORGET_WIFI:
      WiFi.disconnect();
      wifiConnected = false;
      needsRedraw = true;
      break;
    case ACT_TOGGLE_LIGHT:
      if (lightState) controlLight(false);
      else controlLight(true);
      needsRedraw = true;
      break;
    default: break;
  }
}

// ─── OLED Helpers ────────────────────────────────────────────────
void _goDispState(DisplayState s) {
  displayState = s; menuIndex = 0; needsRedraw = true; lastActivity = millis();
}

void _goSleep() {
  screenOn = false; displayState = DISP_SLEEP; needsRedraw = false;
  pendingAction = ACT_DEEP_SLEEP;
  oled.ssd1306_command(SSD1306_DISPLAYOFF);
}

void _wakeScreen() {
  screenOn = true;
  oled.ssd1306_command(SSD1306_DISPLAYON);
  _goDispState(DISP_GENERAL);
  pendingAction = ACT_WAKE_UP;
}

void _onNext() {
  int maxItems = 3;
  switch (displayState) {
    case DISP_GENERAL:       maxItems = 4; break;
    case DISP_SOUND_BROWSER: maxItems = DISP_SOUND_COUNT + 1; break;
    case DISP_MODES_MENU:    maxItems = 3; break;
    case DISP_PLAYING:       maxItems = 3; break;
    case DISP_SETTINGS:      maxItems = 3; break;
    case DISP_WIFI_SETTINGS: maxItems = 3; break;
    default: break;
  }
  menuIndex = (menuIndex + 1) % maxItems;
}

void _onEnter() {
  switch (displayState) {
    case DISP_GENERAL:
      if      (menuIndex == 0) _goDispState(DISP_SOUND_BROWSER);
      else if (menuIndex == 1) _goDispState(DISP_MODES_MENU);
      else if (menuIndex == 2) _goSleep();
      else if (menuIndex == 3) _goDispState(DISP_SETTINGS);
      break;
    case DISP_SOUND_BROWSER:
      if (menuIndex == DISP_SOUND_COUNT) {
        _goDispState(DISP_GENERAL);
      } else if (menuIndex == 0) {
        currentDispSound = 0; pendingAction = ACT_STOP_SOUND; _goDispState(DISP_GENERAL);
      } else {
        currentDispSound = menuIndex; isPaused = false; volLevel = 1;
        pendingAction = ACT_PLAY_SOUND; _goDispState(DISP_PLAYING);
      }
      break;
    case DISP_MODES_MENU:
      if      (menuIndex == 0) { pendingAction = ACT_SLEEP_MODE; _goDispState(DISP_GENERAL); }
      else if (menuIndex == 1) { pendingAction = ACT_WAKE_MODE;  _goDispState(DISP_GENERAL); }
      else                       _goDispState(DISP_GENERAL);
      break;
    case DISP_PLAYING:
      if (menuIndex == 0) {
        isPaused = !isPaused;
        pendingAction = isPaused ? ACT_PAUSE_SOUND : ACT_RESUME_SOUND;
      } else if (menuIndex == 1) {
        volLevel = (volLevel + 1) % 3;
        currentDispVol = (volLevel == 0) ? 10 : (volLevel == 1) ? 20 : 30;
        pendingAction = ACT_SET_VOLUME;
      } else {
        currentDispSound = 0; pendingAction = ACT_STOP_SOUND; _goDispState(DISP_GENERAL);
      }
      break;
    case DISP_SETTINGS:
      if (menuIndex == 0) {
        _goDispState(DISP_WIFI_SETTINGS);
      } else if (menuIndex == 1) {
        pendingAction = ACT_TOGGLE_LIGHT;
      } else {
        _goDispState(DISP_GENERAL);
      }
      break;
    case DISP_WIFI_SETTINGS:
      if (menuIndex == 0) {
        pendingAction = ACT_TOGGLE_WIFI;
      } else if (menuIndex == 1) {
        pendingAction = ACT_FORGET_WIFI;
      } else {
        _goDispState(DISP_SETTINGS);
      }
      break;
    default: break;
  }
}

// ─── OLED Render ─────────────────────────────────────────────────
void _renderCurrentState() {
  oled.clearDisplay();
  
  switch (displayState) {
    case DISP_GENERAL:       _drawGeneral();      break;
    case DISP_SOUND_BROWSER: _drawSoundBrowser(); break;
    case DISP_MODES_MENU:    _drawModesMenu();    break;
    case DISP_PLAYING:       _drawPlaying();      break;
    case DISP_SETTINGS:      _drawSettings();     break;
    case DISP_WIFI_SETTINGS: _drawWiFiSettings(); break;
    case DISP_SLEEP:                              break;
  }
  oled.display();
}

void _drawSplash() {
  oled.clearDisplay();
  oled.setTextSize(2); oled.setCursor(14, 8);  oled.print(F("Hojoo3"));
  oled.setTextSize(1); oled.setCursor(20, 34); oled.print(F("MP3 Player "));
  oled.setCursor(34, 48); oled.print(F("^_^"));
  oled.display();
}

void _drawGeneral() {
  // شريط الحالة العلوي - الأيقونات نزلناها شوي (y = 3 بدل 0)
  _drawBatteryIcon(5, 3);     // البطارية يسار - نزلناها 3 بكسل
  _drawWiFiIcon(50, 3);       // الواي فاي في المنتصف - نزلناها 3 بكسل
  
  // الخط الفاصل نزلناه شوي بعد
  oled.drawFastHLine(0, 13, 85, SSD1306_WHITE);  // كان 10، صار 13
  
  // H3 فوق
  oled.setTextSize(2); 
  oled.setCursor(95, 12);     // نزلناها شوي (كانت 12، صارت 15)
  oled.print(F("H3"));
  
  // القائمة تنزل شوي بعد
  const char* items[] = {"Sounds", "Modes", "Power Save", "Settings"};
  oled.setTextSize(1);
  for (int i = 0; i < 4; i++) {
    int y = 21 + i * 12;      // كانت 20، صارت 24 (نزلنا القائمة 4 بكسل)
    if (i == menuIndex) {
      oled.fillRoundRect(0, y-2, 78, 12, 2, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
      oled.setCursor(4, y); oled.print(F("> ")); oled.print(items[i]);
      oled.setTextColor(SSD1306_WHITE);
    } else {
      oled.setCursor(4, y); oled.print(F("  ")); oled.print(items[i]);
    }
  }
}

void _drawSoundBrowser() {
  oled.setTextSize(1); oled.setCursor(30, 0); oled.print(F("- Sounds -"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  int totalItems = DISP_SOUND_COUNT + 1;
  int pageStart  = (menuIndex / 4) * 4;
  int pageEnd    = min(pageStart + 4, totalItems);
  for (int i = pageStart; i < pageEnd; i++) {
    int y = 13 + (i - pageStart) * 12;
    const char* label = (i == DISP_SOUND_COUNT) ? "< Back" : dispSoundNames[i];
    bool sel = (i == menuIndex);
    if (sel) {
      oled.fillRoundRect(0, y-1, 118, 12, 1, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
      oled.setCursor(4, y); oled.print(F("> ")); oled.print(label);
      oled.setTextColor(SSD1306_WHITE);
    } else {
      oled.setCursor(4, y); oled.print(F("  ")); oled.print(label);
    }
    if (i == currentDispSound && i > 0) { oled.setCursor(120, y); oled.print(F("*")); }
  }
  oled.setCursor(96, 56);
  oled.print(menuIndex + 1); oled.print('/'); oled.print(totalItems);
}

void _drawModesMenu() {
  oled.setTextSize(1); oled.setCursor(20, 0); oled.print(F("- Smart Modes -"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  const char* items[] = {"Sleep Mode", "Wake Mode", "< Back"};
  const char* descs[] = {"Rain + dim light", "Birds + warm light", "Return to main"};
  for (int i = 0; i < 3; i++) {
    int y = 14 + i * 15;
    bool sel = (i == menuIndex);
    if (sel) {
      oled.fillRoundRect(0, y-1, 128, 13, 2, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
      oled.setCursor(4, y); oled.print(F("> ")); oled.print(items[i]);
      oled.setTextColor(SSD1306_WHITE);
    } else {
      oled.setCursor(4, y); oled.print(F("  ")); oled.print(items[i]);
    }
  }
  oled.setCursor(0, 56); oled.print(descs[menuIndex]);
}

void _drawPlaying() {
  oled.setTextSize(1); oled.setCursor(0, 0);
  oled.print(F(">> ")); oled.print(dispSoundNames[currentDispSound]);
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  const char* volLabels[] = {"Low", "Mid", "High"};

  bool s0 = (menuIndex == 0);
  if (s0) { oled.fillRoundRect(0, 12, 128, 13, 2, SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
  oled.setCursor(2, 13); oled.print(isPaused ? F("  Resume") : F("  Pause"));
  if (s0) oled.setTextColor(SSD1306_WHITE);

  bool s1 = (menuIndex == 1);
  if (s1) { oled.fillRoundRect(0, 27, 128, 13, 2, SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
  oled.setCursor(2, 28); oled.print(F("  Vol: ")); oled.print(volLabels[volLevel]);
  for (int b = 0; b < 3; b++) {
    if (b <= volLevel) oled.fillRect(88 + b*12, 28, 10, 10, s1 ? SSD1306_BLACK : SSD1306_WHITE);
    else               oled.drawRect(88 + b*12, 28, 10, 10, s1 ? SSD1306_BLACK : SSD1306_WHITE);
  }
  if (s1) oled.setTextColor(SSD1306_WHITE);

  bool s2 = (menuIndex == 2);
  if (s2) { oled.fillRoundRect(0, 42, 128, 13, 2, SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
  oled.setCursor(2, 43); oled.print(F("  < Back (stop)"));
  if (s2) oled.setTextColor(SSD1306_WHITE);
}

void _drawSettings() {
  oled.setTextSize(1); oled.setCursor(25, 0); oled.print(F("- Settings -"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  
  const char* items[] = {"WiFi Settings", "Light: ", "< Back"};
  for (int i = 0; i < 3; i++) {
    int y = 14 + i * 16;
    bool sel = (i == menuIndex);
    
    if (sel) {
      oled.fillRoundRect(0, y-1, 128, 14, 2, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    }
    
    oled.setCursor(4, y);
    if (i == 0) {
      oled.print(sel ? "> WiFi Settings" : "  WiFi Settings");
    } else if (i == 1) {
      oled.print(sel ? "> Light: " : "  Light: ");
      oled.print(lightState ? "ON" : "OFF");
    } else {
      oled.print(sel ? "> < Back" : "  < Back");
    }
    
    if (sel) oled.setTextColor(SSD1306_WHITE);
  }
}

void _drawWiFiSettings() {
  oled.setTextSize(1); oled.setCursor(15, 0); oled.print(F("- WiFi Settings -"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  
  oled.setCursor(0, 14);
  oled.print(F("SSID: Norah iPhone"));
  
  const char* items[] = {"Connect", "Forget", "< Back"};
  for (int i = 0; i < 3; i++) {
    int y = 30 + i * 12;
    bool sel = (i == menuIndex);
    
    if (sel) {
      oled.fillRoundRect(0, y-1, 128, 11, 2, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    }
    
    oled.setCursor(4, y);
    if (i == 0) {
      oled.print(sel ? "> " : "  ");
      oled.print(wifiConnected ? "Disconnect" : "Connect");
    } else if (i == 1) {
      oled.print(sel ? "> Forget Network" : "  Forget Network");
    } else {
      oled.print(sel ? "> < Back" : "  < Back");
    }
    
    if (sel) oled.setTextColor(SSD1306_WHITE);
  }
}

// ═══════════════════════════════════════════════════════════════════
// Serial Menu
// ═══════════════════════════════════════════════════════════════════
void printSeparator() { Serial.println(F("--------------------------------------------------")); }

void showMainMenu() {
  Serial.println(F("\n=================================================="));
  Serial.println(F("   Arduino R4 | Smart Hub v4.5 | noura-hub.local"));
  Serial.println(F("=================================================="));
  Serial.print(F("  الحالة : ")); Serial.println(stateName(currentState));
  Serial.print(F("  WiFi   : "));
  if (WiFi.status()==WL_CONNECTED && WiFi.localIP()[0]!=0) Serial.println(WiFi.localIP());
  else Serial.println(F("غير متصل"));
  Serial.print(F("  اللمبة : ")); Serial.println(lightState ? F("مضيئة") : F("مطفأة"));
  Serial.print(F("  الصوت  : ")); Serial.println(soundNames[currentSound]);
  Serial.println(F("--------------------------------------------------"));
  Serial.println(F("  [1] تصفح الاصوات    [2] قائمة الاوضاع"));
  Serial.println(F("  [3] سكون عميق       [4] اعدادات"));
  Serial.println(F("  [m] هذا المنيو"));
  Serial.println(F("  [s] حالة النظام     [w] فحص WiFi"));
  Serial.println(F("  [r] اعادة الاتصال   [tk] تجديد Token"));
  Serial.println(F("  [st] حالة اللمبة"));
  Serial.println(F("--------------------------------------------------"));
  Serial.println(F("  L1  L0  LW  LY  B50  V20"));
  Serial.println(F("  WAKE_START  SLEEP_START  MODE_STOP"));
  Serial.println(F("==================================================\n"));
}

void showSoundBrowser() {
  Serial.println(F("\n========== تصفح الاصوات =========="));
  for (int i = 0; i < SOUND_COUNT; i++) {
    Serial.print(F("  [")); Serial.print(i); Serial.print(F("] "));
    Serial.print(soundNames[i]);
    if (i == currentSound) Serial.print(F(" << يشتغل"));
    Serial.println();
  }
  printSeparator();
  Serial.println(F("  1-6 للتشغيل | 0 للرجوع"));
}

void showModesMenu() {
  Serial.println(F("\n========== الاوضاع الذكية =========="));
  Serial.println(F("  [1] وضع النوم    - مطر + اطفاء تدريجي"));
  Serial.println(F("  [2] وضع الاستيقاظ - طيور + اضاءة تدريجية"));
  Serial.println(F("  [0] رجوع"));
  printSeparator();
}

void showSettingsMenu() {
  Serial.println(F("\n========== الاعدادات =========="));
  Serial.println(F("  [1] اعدادات WiFi"));
  Serial.println(F("  [2] تشغيل/اطفاء اللمبة"));
  Serial.println(F("  [0] رجوع"));
  printSeparator();
}

void showWiFiSettings() {
  Serial.println(F("\n========== اعدادات WiFi =========="));
  Serial.print(F("  الشبكة: ")); Serial.println(WIFI_SSID);
  Serial.print(F("  الحالة: ")); Serial.println(wifiConnected ? "متصل" : "غير متصل");
  Serial.println(F("  [1] اتصال/قطع"));
  Serial.println(F("  [2] نسيان الشبكة"));
  Serial.println(F("  [0] رجوع"));
  printSeparator();
}

void printStatus() {
  Serial.println(F("\n========== حالة النظام =========="));
  Serial.print(F("  FSM State : ")); Serial.println(stateName(currentState));
  Serial.print(F("  WiFi      : "));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(WiFi.localIP()); Serial.print(F("  RSSI:")); Serial.print(WiFi.RSSI()); Serial.println(F("dBm"));
  } else { Serial.println(F("غير متصل")); }
  Serial.print(F("  mDNS      : http://")); Serial.print(MDNS_NAME); Serial.println(F(".local"));
  Serial.print(F("  اللمبة    : ")); Serial.print(lightState ? "مضيئة" : "مطفأة");
  Serial.print(F("  سطوع:")); Serial.print(lightBrightness); Serial.print(F("%  حرارة:")); Serial.println(lightTemp);
  Serial.print(F("  الصوت     : [")); Serial.print(currentSound); Serial.print(F("] ")); Serial.println(soundNames[currentSound]);
  Serial.print(F("  Volume    : ")); Serial.print(currentVolume); Serial.println(F("/30"));
  Serial.print(F("  DFPlayer  : ")); Serial.println(dfPlayerReady ? "جاهز" : "غير متصل");
  Serial.print(F("  Token     : ")); Serial.println(accessToken.length() > 0 ? "موجود" : "غير موجود");
  Serial.print(F("  البطارية  : ")); Serial.print(getBatteryPercentage()); Serial.println(F("%"));
  printSeparator();
}

// ═══════════════════════════════════════════════════════════════════
// FSM
// ═══════════════════════════════════════════════════════════════════
void changeState(State newState) {
  if (currentState == newState) return;
  Serial.print(F("[FSM] ")); Serial.print(stateName(currentState));
  Serial.print(F(" -> ")); Serial.println(stateName(newState));
  previousState = currentState; currentState = newState; stateStartTime = millis();

  switch (newState) {
    case ST_GENERAL:
      _goDispState(DISP_GENERAL);
      showMainMenu();
      break;
    case ST_SOUND_BROWSER:
      browserCursor = 0;
      _goDispState(DISP_SOUND_BROWSER);
      showSoundBrowser();
      break;
    case ST_PLAYING_MANUAL:
      _goDispState(DISP_PLAYING);
      Serial.print(F("[FSM] يشتغل: ")); Serial.println(soundNames[currentSound]);
      break;
    case ST_MODES_MENU:
      _goDispState(DISP_MODES_MENU);
      showModesMenu();
      break;
    case ST_SLEEP_ACTIVE:
      Serial.println(F("[FSM] >> وضع النوم بدأ"));
      if (dfPlayerReady) { myDFPlayer.loop(4); currentSound = 4; }
      setVolume(30); controlLight(false);
      currentDispSound = 4;
      lastModeUpdate = lastSoundCheck = millis();
      break;
    case ST_WAKE_ACTIVE:
      Serial.println(F("[FSM] >> وضع الاستيقاظ بدأ"));
      if (dfPlayerReady) { myDFPlayer.loop(2); currentSound = 2; }
      setVolume(10); controlLight(true);
      delay(500); setLightColor("white");
      delay(500); setLightTemp(1000);
      delay(500); setLightBrightness(10);
      currentDispSound = 2;
      lastModeUpdate = lastSoundCheck = millis();
      break;
    case ST_DEEP_SLEEP:
      if (dfPlayerReady) myDFPlayer.pause();
      currentSound = 0;
      _goSleep();
      break;
    case ST_SETTINGS:
      _goDispState(DISP_SETTINGS);
      showSettingsMenu();
      break;
    case ST_WIFI_SETTINGS:
      _goDispState(DISP_WIFI_SETTINGS);
      showWiFiSettings();
      break;
  }
}

// ═══════════════════════════════════════════════════════════════════
// FSM Handlers
// ═══════════════════════════════════════════════════════════════════
void handleSleepActive() {
  if (currentState != ST_SLEEP_ACTIVE) return;
  unsigned long elapsed  = millis() - stateStartTime;
  unsigned long duration = 2UL * 60UL * 60UL * 1000UL;
  if (elapsed >= duration) {
    if (dfPlayerReady) myDFPlayer.pause(); currentSound = 0;
    controlLight(false); changeState(ST_GENERAL); return;
  }
  if (dfPlayerReady && millis() - lastSoundCheck >= 3000)
    { myDFPlayer.loop(4); currentSound = 4; lastSoundCheck = millis(); }
  if (millis() - lastModeUpdate >= 10UL * 60UL * 1000UL) {
    if (currentVolume   > 5)  setVolume(--currentVolume);
    if (lightBrightness > 10) { lightBrightness -= 10; setLightBrightness(lightBrightness); }
    lastModeUpdate = millis();
  }
}

void handleWakeActive() {
  if (currentState != ST_WAKE_ACTIVE) return;
  unsigned long elapsed  = millis() - stateStartTime;
  unsigned long duration = 2UL * 60UL * 60UL * 1000UL;
  if (elapsed >= duration) { changeState(ST_GENERAL); return; }
  if (dfPlayerReady && millis() - lastSoundCheck >= 3000)
    { myDFPlayer.loop(2); currentSound = 2; lastSoundCheck = millis(); }
  if (millis() - lastModeUpdate >= 10UL * 60UL * 1000UL) {
    if (currentVolume < 30) setVolume(++currentVolume);
    if (lightBrightness < 100) { lightBrightness = min(100, lightBrightness + 10); setLightBrightness(lightBrightness); }
    lastModeUpdate = millis();
  }
}

// ═══════════════════════════════════════════════════════════════════
// NTP
// ═══════════════════════════════════════════════════════════════════
bool syncTime() {
  if (!wifiConnected) return false;
  Serial.println(F("[NTP] ضبط الوقت..."));
  timeClient.begin();
  for (int i = 0; i < 5 && !timeClient.update(); i++) { timeClient.forceUpdate(); delay(1000); }
  if (timeClient.isTimeSet()) {
    unsigned long e = timeClient.getEpochTime();
    Serial.print(F("[NTP] ")); Serial.println(timeClient.getFormattedTime());
    return (e > 1700000000 && e < 1800000000);
  }
  Serial.println(F("[NTP] فشل")); return false;
}

String getCurrentTimestamp() {
  if (!timeClient.isTimeSet()) return "0";
  timeClient.update();
  return String(timeClient.getEpochTime()) + "000";
}

// ═══════════════════════════════════════════════════════════════════
// Tuya Crypto
// ═══════════════════════════════════════════════════════════════════
String hexToString(uint8_t* h, size_t l) {
  String r = "";
  for (size_t i = 0; i < l; i++) { if (h[i]<16) r+="0"; r+=String(h[i],HEX); }
  return r;
}
String calculateSHA256(String data) {
  SHA256 s; uint8_t h[32]; s.reset(); s.update(data.c_str(),data.length()); s.finalize(h,32); return hexToString(h,32);
}
String calculateHMAC(String key, String data) {
  SHA256 s; uint8_t h[32], kb[32];
  if (key.length()>32) { s.reset(); s.update(key.c_str(),key.length()); s.finalize(kb,32); }
  else { memcpy(kb,key.c_str(),key.length()); if(key.length()<32) memset(kb+key.length(),0,32-key.length()); }
  uint8_t ip[64], op[64]; memset(ip,0x36,64); memset(op,0x5c,64);
  for(int i=0;i<32;i++){ip[i]^=kb[i];op[i]^=kb[i];}
  s.reset(); s.update(ip,64); s.update(data.c_str(),data.length()); s.finalize(h,32);
  s.reset(); s.update(op,64); s.update(h,32); s.finalize(h,32);
  return hexToString(h,32);
}

// ═══════════════════════════════════════════════════════════════════
// Tuya API
// ═══════════════════════════════════════════════════════════════════
String sendTuyaRequest(String method, String path, String body = "") {
  if (!wifiConnected) return "";
  if (!client.connect(TUYA_HOST, TUYA_PORT)) { Serial.println(F("[Tuya] فشل الاتصال")); return ""; }
  String ts = getCurrentTimestamp();
  if (ts == "0") { client.stop(); return ""; }
  String sts = method+"\n"+calculateSHA256(body)+"\n\n"+path;
  String sig = calculateHMAC(TUYA_CLIENT_SECRET, TUYA_CLIENT_ID+accessToken+ts+sts);
  sig.toUpperCase();
  String req = method+" "+path+" HTTP/1.1\r\nHost: "+TUYA_HOST+"\r\nclient_id: "+TUYA_CLIENT_ID+"\r\nsign: "+sig+"\r\nt: "+ts+"\r\nsign_method: HMAC-SHA256\r\n";
  if (accessToken.length()>0) req += "access_token: "+accessToken+"\r\n";
  if (body.length()>0) req += "Content-Type: application/json\r\nContent-Length: "+String(body.length())+"\r\n";
  req += "Connection: close\r\n\r\n";
  if (body.length()>0) req += body;
  client.print(req);
  String resp = ""; unsigned long t = millis();
  while (client.connected()||client.available()) {
    if (millis()-t>10000) break;
    if (client.available()) { char c=client.read(); resp+=c; t=millis(); }
  }
  client.stop();
  int js = resp.indexOf("\r\n\r\n");
  return (js>0) ? resp.substring(js+4) : resp;
}

bool ensureToken() {
  if (!wifiConnected) return false;
  if (accessToken.length()==0 || millis()>=tokenExpireTime) return getTuyaAccessToken();
  return true;
}

bool getTuyaAccessToken() {
  if (!wifiConnected) return false;
  Serial.println(F("[Tuya] جاري الحصول على Token..."));
  String r = sendTuyaRequest("GET", "/v1.0/token?grant_type=1");
  if (!r.length()) { Serial.println(F("[Tuya] لا رد")); return false; }
  JsonDocument doc;
  if (deserializeJson(doc,r)) { Serial.println(F("[Tuya] خطأ JSON")); return false; }
  if (!doc["success"]) { Serial.print(F("[Tuya] فشل: ")); Serial.println(doc["msg"].as<String>()); return false; }
  accessToken     = doc["result"]["access_token"].as<String>();
  int exp         = doc["result"]["expire_time"]|0;
  tokenExpireTime = millis()+(exp*1000UL);
  Serial.print(F("[Tuya] Token OK | ")); Serial.print(exp/60); Serial.println(F(" دقيقة"));
  return true;
}

void getDeviceStatus() {
  if (!wifiConnected) return;
  if (!ensureToken()) return;
  String r = sendTuyaRequest("GET", "/v1.0/devices/"+String(TUYA_DEVICE_ID)+"/status");
  if (!r.length()) return;
  JsonDocument doc; if (deserializeJson(doc,r)) return;
  if (doc["success"].as<bool>()) {
    for (JsonObject item : doc["result"].as<JsonArray>()) {
      String code = item["code"].as<String>(), val = item["value"].as<String>();
      if (code=="switch_led") lightState=(val=="true");
    }
    Serial.print(F("[Tuya] اللمبة: ")); Serial.println(lightState?"مضيئة":"مطفأة");
  }
}

bool controlLight(bool on) {
  if (!wifiEnabled) {
    lightState = on;
    needsRedraw = true;
    Serial.println(on ? F("[Light] تشغيل (محلي)...") : F("[Light] إطفاء (محلي)..."));
    return true;
  }
  Serial.println(on ? F("[Light] تشغيل...") : F("[Light] إطفاء..."));
  if (!ensureToken()) return false;
  JsonDocument doc; JsonObject cmd = doc["commands"].to<JsonArray>().add<JsonObject>();
  cmd["code"]="switch_led"; cmd["value"]=on;
  String body; serializeJson(doc,body);
  String r = sendTuyaRequest("POST","/v1.0/devices/"+String(TUYA_DEVICE_ID)+"/commands",body);
  if (!r.length()) return false;
  JsonDocument res; if (deserializeJson(res,r)) return false;
  if (res["success"]) { lightState=on; needsRedraw=true; return true; }
  return false;
}

bool setLightColor(String color) {
  if (!wifiEnabled || !wifiConnected) return false;
  if (!ensureToken()) return false;
  JsonDocument doc; JsonArray cmds = doc["commands"].to<JsonArray>();
  JsonObject c1 = cmds.add<JsonObject>(); c1["code"]="switch_led"; c1["value"]=true;
  JsonObject c2 = cmds.add<JsonObject>();
  if (color=="white")       { c2["code"]="work_mode";   c2["value"]="white"; }
  else if (color=="yellow") { c2["code"]="colour_data"; c2["value"]="ffff00"; }
  else                      { c2["code"]="work_mode";   c2["value"]=color; }
  String body; serializeJson(doc,body);
  String r = sendTuyaRequest("POST","/v1.0/devices/"+String(TUYA_DEVICE_ID)+"/commands",body);
  if (!r.length()) return false;
  JsonDocument res; if (deserializeJson(res,r)) return false;
  if (res["success"]) { lightColor=color; lightState=true; return true; }
  return false;
}

bool setLightTemp(int temp) {
  temp = constrain(temp,0,1000);
  if (!wifiEnabled || !wifiConnected) return false;
  if (!ensureToken()) return false;
  JsonDocument doc; JsonObject cmd = doc["commands"].to<JsonArray>().add<JsonObject>();
  cmd["code"]="temp_value"; cmd["value"]=temp;
  String body; serializeJson(doc,body);
  String r = sendTuyaRequest("POST","/v1.0/devices/"+String(TUYA_DEVICE_ID)+"/commands",body);
  JsonDocument res; if (!r.length()||deserializeJson(res,r)) return false;
  if (res["success"]) { lightTemp=temp; return true; } return false;
}

bool setLightBrightness(int b) {
  b = constrain(b,0,100);
  if (!wifiEnabled || !wifiConnected) {
    lightBrightness = b;
    Serial.print(F("[Light] سطوع (محلي): ")); Serial.print(b); Serial.println(F("%"));
    return true;
  }
  Serial.print(F("[Light] سطوع: ")); Serial.print(b); Serial.println(F("%"));
  if (!ensureToken()) return false;
  JsonDocument doc; JsonObject cmd = doc["commands"].to<JsonArray>().add<JsonObject>();
  cmd["code"]="bright_value"; cmd["value"]=map(b,0,100,0,1000);
  String body; serializeJson(doc,body);
  String r = sendTuyaRequest("POST","/v1.0/devices/"+String(TUYA_DEVICE_ID)+"/commands",body);
  JsonDocument res; if (!r.length()||deserializeJson(res,r)) return false;
  if (res["success"]) { lightBrightness=b; return true; }
  return false;
}

// ═══════════════════════════════════════════════════════════════════
// Audio
// ═══════════════════════════════════════════════════════════════════
void playSound(int n) {
  if (!dfPlayerReady) { Serial.println(F("[Audio] غير جاهز")); return; }
  if (n<0||n>6)       { Serial.println(F("[Audio] رقم خاطئ")); return; }
  currentSound = n;
  if (n==0) { myDFPlayer.pause(); Serial.println(F("[Audio] إيقاف")); }
  else { myDFPlayer.play(n); Serial.print(F("[Audio] ")); Serial.println(soundNames[n]); }
}

void setVolume(int v) {
  if (!dfPlayerReady) return;
  v = constrain(v,0,30); currentVolume = v; myDFPlayer.volume(v);
  Serial.print(F("[Audio] Vol:")); Serial.print(v); Serial.println(F("/30"));
}

// ═══════════════════════════════════════════════════════════════════
// WiFi + mDNS
// ═══════════════════════════════════════════════════════════════════
void setupWiFi() {
  if (!wifiEnabled) {
    Serial.println(F("[WiFi] معطل من الإعدادات"));
    wifiConnected = false;
    return;
  }
  Serial.println(F("\n[L1] WiFi..."));
  if (WiFi.status()==WL_CONNECTED && WiFi.localIP()[0]!=0) { 
    Serial.println(F("[L1] متصل بالفعل")); 
    wifiConnected = true;
    return; 
  }
  WiFi.disconnect(); delay(300);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (millis() - start < 20000) {
    if (WiFi.status()==WL_CONNECTED && WiFi.localIP()[0]!=0) break;
    delay(500); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status()==WL_CONNECTED && WiFi.localIP()[0]!=0) {
    Serial.print(F("[L1] IP: ")); Serial.println(WiFi.localIP());
    mdns.begin(WiFi.localIP(), MDNS_NAME);
    mdns.addServiceRecord("noura-hub._http", 80, MDNSServiceTCP);
    localServer.begin();
    wifiConnected = true;
    Serial.println(F("[Server] port 80 جاهز"));
  } else {
    Serial.println(F("[L1] فشل الاتصال"));
    wifiConnected = false;
  }
}

void reconnect() {
  if (!wifiEnabled) return;
  Serial.println(F("\n[R] إعادة الاتصال..."));
  WiFi.disconnect(); delay(1000);
  setupWiFi();
  if (WiFi.status()==WL_CONNECTED && WiFi.localIP()[0]!=0) {
    if (syncTime()) { delay(1000); if (getTuyaAccessToken()) { delay(500); getDeviceStatus(); } }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Serial Commands
// ═══════════════════════════════════════════════════════════════════
void executeCommand(String input) {
  // إيقاظ الشاشة إذا كانت في وضع السكون عند تلقي أي أمر (ما عدا أمر السكون نفسه)
  if (currentState == ST_DEEP_SLEEP && input != "DEEP_SLEEP") {
    _wakeScreen();
    changeState(ST_GENERAL);
  }

  if (input.startsWith("PLAY_") && input.length() >= 6) {
    int snd = input.substring(5).toInt();
    playSound(snd);
    if (snd > 0) changeState(ST_PLAYING_MANUAL);
    else changeState(ST_GENERAL);
    return;
  }

  if (input == "TOGGLE_LIGHT")   { controlLight(!lightState); return; }
  if (input.startsWith("V") && input.length()>1) { setVolume(input.substring(1).toInt()); return; }
  if (input == "L1") { controlLight(true);  return; }
  if (input == "L0") { controlLight(false); return; }
  if (input == "LW") { setLightColor("white");  setLightTemp(0);    return; }
  if (input == "LY") { setLightColor("yellow"); setLightTemp(1000); return; }
  if (input.startsWith("B") && input.length()>1) { setLightBrightness(input.substring(1).toInt()); return; }
  if (input == "WAKE_START")  { changeState(ST_WAKE_ACTIVE);  return; }
  if (input == "SLEEP_START") { changeState(ST_SLEEP_ACTIVE); return; }
  if (input == "WAKE_STOP" || input == "SLEEP_STOP" || input == "MODE_STOP") {
    if (dfPlayerReady) myDFPlayer.pause(); currentSound = 0; changeState(ST_GENERAL); return;
  }
  if (input == "SOUND_BROWSER") { changeState(ST_SOUND_BROWSER); return; }
  if (input == "DEEP_SLEEP")    { changeState(ST_DEEP_SLEEP);    return; }

  switch (currentState) {
    case ST_GENERAL:
      if      (input=="1") changeState(ST_SOUND_BROWSER);
      else if (input=="2") changeState(ST_MODES_MENU);
      else if (input=="3") changeState(ST_DEEP_SLEEP);
      else if (input=="4") changeState(ST_SETTINGS);
      break;
    case ST_SOUND_BROWSER:
      if (input=="0") changeState(ST_GENERAL);
      else if (input.length()==1 && input[0]>='1' && input[0]<='6')
        { playSound(input[0]-'0'); changeState(ST_PLAYING_MANUAL); }
      break;
    case ST_PLAYING_MANUAL:
      if (input=="0") { playSound(0); changeState(ST_GENERAL); }
      else if (input.length()==1 && input[0]>='1' && input[0]<='6')
        { playSound(input[0]-'0'); }
      break;
    case ST_MODES_MENU:
      if      (input=="0") changeState(ST_GENERAL);
      else if (input=="1") changeState(ST_SLEEP_ACTIVE);
      else if (input=="2") changeState(ST_WAKE_ACTIVE);
      break;
    case ST_SLEEP_ACTIVE: case ST_WAKE_ACTIVE:
      if (input=="0") { if(dfPlayerReady) myDFPlayer.pause(); currentSound=0; changeState(ST_GENERAL); }
      break;
    case ST_DEEP_SLEEP:
      if (input=="3") changeState(ST_GENERAL);
      break;
    case ST_SETTINGS:
      if (input=="0") changeState(ST_GENERAL);
      else if (input=="1") changeState(ST_WIFI_SETTINGS);
      else if (input=="2") { controlLight(!lightState); }
      break;
    case ST_WIFI_SETTINGS:
      if (input=="0") changeState(ST_SETTINGS);
      else if (input=="1") {
        if (wifiConnected) { WiFi.disconnect(); wifiConnected = false; }
        else { setupWiFi(); }
      }
      else if (input=="2") { WiFi.disconnect(); wifiConnected = false; }
      break;
  }
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  String input = Serial.readStringUntil('\n'); input.trim();
  if (!input.length()) { while(Serial.available()) Serial.read(); return; }
  Serial.print(F("\n> ")); Serial.print(input);
  Serial.print(F("  [")); Serial.print(stateName(currentState)); Serial.println(F("]"));
  if (input=="m"||input=="M") { showMainMenu();  return; }
  if (input=="s"||input=="S") { printStatus();   return; }
  if (input=="r"||input=="R") { reconnect();     return; }
  if (input=="w"||input=="W") {
    Serial.print(F("[WiFi] ")); Serial.println(WiFi.status()==WL_CONNECTED?F("متصل"):F("غير متصل"));
    Serial.print(F("[WiFi] IP: ")); Serial.println(WiFi.localIP()); return;
  }
  if (input=="tk"||input=="TK") { getTuyaAccessToken(); return; }
  if (input=="st"||input=="ST") { getDeviceStatus();    return; }
  executeCommand(input);
}

// ═══════════════════════════════════════════════════════════════════
// ESP32 HTTP Handler
// ═══════════════════════════════════════════════════════════════════
void handleESP32Client(WiFiClient esp32Client) {
  String reqLine = "";
  String body    = "";
  bool   isPost  = false;
  int    contLen = 0;

  // ─── 1. Read headers ─────────────────────────────────────────────
  while (esp32Client.connected()) {
    if (!esp32Client.available()) continue;
    String line = esp32Client.readStringUntil('\n');
    line.trim();
    if (reqLine == "" && line.length()) reqLine = line;
    if (line.startsWith("POST"))           isPost  = true;
    if (line.startsWith("Content-Length")) contLen = line.substring(16).toInt();
    if (line.length() == 0) break;
  }

  // ─── 2. Method Filter: reject anything that is not POST ─────────────
  if (!isPost) {
    Serial.print(F("[HTTP] 405 Method Not Allowed: ")); Serial.println(reqLine);
    esp32Client.println(F("HTTP/1.1 405 Method Not Allowed\r\nAllow: POST\r\nConnection: close\r\n\r\n{\"error\":\"method not allowed\"}"));
    esp32Client.stop(); return;
  }
  // ─────────────────────────────────────────────────────────────

  // ─── 3. Read body using Content-Length ──────────────────────────
  if (isPost && contLen > 0) {
    unsigned long t = millis();
    while ((int)body.length() < contLen && millis() - t < 2000) {
      if (esp32Client.available()) body += (char)esp32Client.read();
    }
  }

  // 3. طباعة التأكيد في السيريال
  Serial.print(F("[HTTP] ")); Serial.print(reqLine);
  if (body.length() > 0) { Serial.print(F(" | ")); Serial.println(body); }
  else { Serial.println(); }


  // ─── 4. Route and execute ───────────────────────────────────────
  bool isCmd   = (reqLine.indexOf("POST /light")>=0 || reqLine.indexOf("POST /command")>=0);
  bool isSound = (reqLine.indexOf("POST /sound")>=0);
  int js = body.indexOf('{'), je = body.lastIndexOf('}');
  
  if (js == -1 || je == -1) {
    esp32Client.println(F("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n{\"error\":\"no json\"}"));
    esp32Client.stop(); return;
  }

  JsonDocument doc; deserializeJson(doc, body.substring(js, je+1));

  // ─── Token Validation ─────────────────────────────────────────────
  String receivedToken = doc["token"] | "";
  if (receivedToken != String(HUJOU3_AUTH_TOKEN)) {
    Serial.println(F("[AUTH] 401 Unauthorized — token mismatch"));
    esp32Client.println(F("HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"unauthorized\"}"));
    esp32Client.stop(); return;
  }
  Serial.println(F("[AUTH] Token OK"));
  // ─────────────────────────────────────────────────────

  // ─── Replay Attack Protection ─────────────────────────────
  long msgTime = doc["time"] | 0L;
  if (msgTime > 0 && timeClient.isTimeSet()) {
    long hubTime = (long)timeClient.getEpochTime();
    long delta   = abs(hubTime - msgTime);
    if (delta > 5) {
      Serial.print(F("[AUTH] 408 Replay | delta=")); Serial.print(delta); Serial.println(F("s"));
      esp32Client.println(F("HTTP/1.1 408 Request Timeout\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"replay attack\"}"));
      esp32Client.stop(); return;
    }
    Serial.print(F("[AUTH] Timestamp OK | delta=")); Serial.print(delta); Serial.println(F("s"));
  }
  // ─────────────────────────────────────────────────────

  if (isCmd) {
    executeCommand(doc["command"]|"");
  } else if (isSound) {
    int sound = doc["sound"]|-1;
    if (sound>=0 && sound<=6) { playSound(sound); if(sound>0) changeState(ST_PLAYING_MANUAL); }
  } else {
    String tr = doc["transcription"]|"", ac = doc["action"]|"";
    int sn = doc["sound"]|-1;
    if      (sn>=0&&sn<=6)                          { playSound(sn); if(sn>0) changeState(ST_PLAYING_MANUAL); }
    else if (tr.indexOf("ولع")>=0 || ac=="on")       controlLight(true);
    else if (tr.indexOf("طفي")>=0 || ac=="off")      controlLight(false);
  }
  
  esp32Client.println(F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}"));
  esp32Client.stop();
}

// ═══════════════════════════════════════════════════════════════════
// Setup & Loop
// ═══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(BAUD_RATE); Serial1.begin(9600); delay(2000);
  Serial.println(F("\n================================================"));
  Serial.println(F("  Arduino R4 Smart Controller v4.5"));
  Serial.println(F("  FSM + OLED + mDNS + Tuya + Battery Monitor"));
  Serial.println(F("================================================"));

  initDisplay();

  Serial.println(F("[Audio] DFPlayer..."));
  dfPlayerReady = myDFPlayer.begin(Serial1);
  if (dfPlayerReady) { myDFPlayer.volume(30); Serial.println(F("[Audio] جاهز")); }
  else Serial.println(F("[Audio] فشل - يعمل بدونه"));

  wifiEnabled = true;
  resetBatterySimulation();
  
  changeState(ST_GENERAL);
}

void loop() {
  if (wifiEnabled && wifiConnected) {
    mdns.run();
    timeClient.update();
    if (accessToken.length()>0 && millis()>=tokenExpireTime-60000) getTuyaAccessToken();
  }

  updateDisplay();
  processDisplayActions();
  handleSerialCommands();

  if (wifiEnabled && wifiConnected) {
    WiFiClient c = localServer.available();
    if (c) handleESP32Client(c);
  }

  switch (currentState) {
    case ST_SLEEP_ACTIVE: handleSleepActive(); break;
    case ST_WAKE_ACTIVE:  handleWakeActive();  break;
    case ST_DEEP_SLEEP:   delay(100); return;
    default: break;
  }

  delay(10);
}