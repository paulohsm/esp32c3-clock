// esp32c3-clock — desk clock with a 32x8 MAX7219 matrix, controlled over MQTT.
//
// Stage 2: NTP clock, touch navigation, hourly chime, night mode, status LED,
// fixed-width 4x6 font with icons, and remote control over MQTT/TLS (HiveMQ Cloud):
// instant messages, scheduled messages/alarms and remote settings.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <OneButton.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "icons.h"
#include "mqtt_link.h"
#include "pins.h"
#include "schedule.h"
#include "sound.h"
#include "statusled.h"

#define FW_VERSION "0.3.5"

static const char* AP_NAME = "Relogio-Config";
static const char* AP_PASS = "relogio123";  // setup network password (min. 8 chars)
static const char* TZ_INFO = "<-03>3";      // Fortaleza: UTC-3, no daylight saving

// Shown on the matrix, so kept in Portuguese (no accents: ASCII font).
static const char* const WEEKDAYS[] = {"Domingo", "Segunda", "Terca", "Quarta",
                                       "Quinta",  "Sexta",   "Sabado"};
static const char* const MONTHS[] = {"janeiro", "fevereiro", "marco",    "abril",
                                     "maio",    "junho",     "julho",    "agosto",
                                     "setembro", "outubro",  "novembro", "dezembro"};

static const uint8_t* const ICON_ENVELOPE[] = {icons::ENVELOPE};
static const uint8_t* const ICON_BELL[]     = {icons::BELL_L, icons::BELL_R};
static const uint8_t* const ICON_WIFI[]     = {icons::WIFI};
static const uint8_t* const ICON_NOTE[]     = {icons::NOTE};
static const uint8_t* const ICON_CALENDAR[] = {icons::CALENDAR};
static const uint8_t* const ICON_GEAR[]     = {icons::GEAR};

enum Screen : uint8_t { SCR_CLOCK, SCR_DATE, SCR_LONGDATE, SCR_COUNT };

static WiFiManager wm;
static OneButton touch(PIN_TOUCH, false, false);  // active HIGH, no pull-up

static char     deviceId[16];
static Screen   screen       = SCR_CLOCK;
static uint32_t screenSince  = 0;
static bool     overlay      = false;  // a temporary message is scrolling
static bool     timeOk       = false;
static bool     nightActive  = false;
static int      lastBeepHour = -1;
static int      lastMinute   = -1;

// Alarm state
static bool     alarmActive  = false;
static uint32_t alarmStarted = 0;
static uint32_t alarmLastRing = 0;
static uint8_t  alarmTimbre  = 0;
static constexpr uint32_t ALARM_MAX_MS  = 60000;  // stops by itself after 1 min
static constexpr uint32_t ALARM_RING_MS = 3000;

static constexpr uint32_t SCREEN_TIMEOUT_MS = 10000;  // secondary screens return to clock
static constexpr uint32_t INFO_PERIOD_MS    = 300000; // republish device info every 5 min

// ------------------------------------------------------------- helpers

static bool getLocalTm(tm& t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;  // no NTP yet
  localtime_r(&now, &t);
  return true;
}

static bool isNightHour(int h) {
  const auto& s = cfg::s;
  if (!s.nightEnabled || s.nightStart == s.nightEnd) return false;
  if (s.nightStart < s.nightEnd) return h >= s.nightStart && h < s.nightEnd;
  return h >= s.nightStart || h < s.nightEnd;  // wraps around midnight
}

static void applyBrightness() {
  display::setBrightness(nightActive ? 0 : cfg::s.brightness);
}

static void showMessage(const char* text, const uint8_t* const* frames, uint8_t frameCount = 1,
                        uint8_t loops = 1) {
  overlay = true;
  display::scrollStart(text, frames, frameCount, loops);
}

static void goToScreen(Screen s) {
  screen = s;
  screenSince = millis();
  if (s == SCR_LONGDATE) {
    static char txt[64];
    tm t;
    if (getLocalTm(t)) {
      snprintf(txt, sizeof(txt), "%s, %d de %s de %d", WEEKDAYS[t.tm_wday], t.tm_mday,
               MONTHS[t.tm_mon], t.tm_year + 1900);
    } else {
      strlcpy(txt, "Sem hora (NTP)", sizeof(txt));
    }
    display::scrollStart(txt, ICON_CALENDAR);
  } else if (display::isScrolling()) {
    display::scrollStop();
  }
}

// ------------------------------------------------------------- MQTT out

static void publishJson(const char* suffix, JsonDocument& doc, bool retained) {
  static char buf[2048];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n > 0 && n < sizeof(buf)) mqtt_link::publish(suffix, buf, retained);
}

static void publishInfo() {
  JsonDocument doc;
  doc["name"] = cfg::s.name;
  doc["fw"] = FW_VERSION;
  doc["ip"] = WiFi.localIP().toString();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  doc["schedules"] = sched::count();
  publishJson("info", doc, true);
}

static void publishConfig() {
  JsonDocument doc;
  cfg::toJson(doc.to<JsonObject>());
  publishJson("config", doc, true);
}

static void publishSchedules() {
  JsonDocument doc;
  sched::toJson(doc.to<JsonArray>());
  publishJson("schedules", doc, true);
}

static void publishAck(const char* cmd, bool ok, const char* error = nullptr, int id = -1) {
  JsonDocument doc;
  doc["cmd"] = cmd;
  doc["ok"] = ok;
  if (error) doc["error"] = error;
  if (id >= 0) doc["id"] = id;
  publishJson("ack", doc, false);
}

static void onMqttConnect() {
  publishInfo();
  publishConfig();
  publishSchedules();
}

// Called after any settings change (serial or MQTT).
static void settingsChanged() {
  cfg::clamp();
  cfg::save();
  applyBrightness();
  display::setRotated(cfg::s.rotated);
  publishConfig();
}

// --------------------------------------------------------------- alarm

static void startAlarm(const char* text, uint8_t timbre) {
  alarmActive = true;
  alarmStarted = millis();
  alarmLastRing = 0;  // ring right away
  alarmTimbre = timbre;
  showMessage(text, ICON_BELL, 2, 0);  // loops until stopped
}

static void stopAlarm() {
  alarmActive = false;
  overlay = false;
  display::scrollStop();
}

static void onScheduleFire(const sched::Entry& e) {
  statusled::flash();
  if (e.alarm) {
    startAlarm(e.text, e.timbre);
  } else {
    sound::chime(e.timbre);
    showMessage(e.text, ICON_ENVELOPE, 1, 3);
  }
  Serial.printf("Schedule #%u fired: %s\n", e.id, e.text);
}

// -------------------------------------------------------------- touch

static void onClick() {
  statusled::flash();
  sound::click();
  if (alarmActive) { stopAlarm(); return; }
  if (overlay) {  // a touch dismisses the message
    overlay = false;
    goToScreen(SCR_CLOCK);
    return;
  }
  goToScreen(static_cast<Screen>((screen + 1) % SCR_COUNT));
}

static void onDoubleClick() {
  if (alarmActive) { stopAlarm(); return; }
  static char txt[128];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(txt, sizeof(txt), "%s  IP %s  Sinal %d dBm  MQTT %s  ID %s  v%s", cfg::s.name,
             WiFi.localIP().toString().c_str(), WiFi.RSSI(),
             mqtt_link::connected() ? "ok" : "off", deviceId, FW_VERSION);
  } else {
    snprintf(txt, sizeof(txt), "Sem Wi-Fi  ID %s  v%s", deviceId, FW_VERSION);
  }
  sound::click();
  showMessage(txt, ICON_WIFI);
}

static void onLongPress() {
  if (alarmActive) { stopAlarm(); return; }
  cfg::s.hourlyBeep = !cfg::s.hourlyBeep;
  settingsChanged();
  sound::confirm();
  showMessage(cfg::s.hourlyBeep ? "Bipe de hora ligado" : "Bipe de hora desligado", ICON_NOTE);
}

// ----------------------------------------------------------- MQTT in

static void handleMessageCmd(const char* payload) {
  const char* text = payload;
  bool beep = true;
  uint8_t repeat = 1;
  JsonDocument doc;
  if (payload[0] == '{') {
    if (deserializeJson(doc, payload)) { publishAck("msg", false, "invalid JSON"); return; }
    text = doc["text"] | "";
    beep = doc["beep"] | true;
    repeat = constrain(doc["repeat"] | 1, 1, 10);
  }
  if (!text[0]) { publishAck("msg", false, "empty text"); return; }
  if (alarmActive) stopAlarm();
  if (beep) sound::chime();
  showMessage(text, ICON_ENVELOPE, 1, repeat);
  publishAck("msg", true);
}

static void handleScheduleCmd(const char* payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) { publishAck("schedule", false, "invalid JSON"); return; }
  const char* action = doc["action"] | "add";

  if (strcmp(action, "list") == 0) {
    publishSchedules();
    publishAck("schedule", true);
  } else if (strcmp(action, "clear") == 0) {
    sched::clear();
    publishSchedules();
    publishAck("schedule", true);
  } else if (strcmp(action, "delete") == 0) {
    int id = doc["id"] | 0;
    bool ok = sched::remove(id);
    if (ok) publishSchedules();
    publishAck("schedule", ok, ok ? nullptr : "id not found", id);
  } else if (strcmp(action, "add") == 0) {
    tm now;
    if (!getLocalTm(now)) { publishAck("schedule", false, "clock not set yet"); return; }
    const char* error = nullptr;
    uint8_t id = sched::addFromJson(doc.as<JsonObjectConst>(), now, error);
    if (id) {
      publishSchedules();
      sound::confirm();
      showMessage("Agendado", ICON_BELL, 1, 1);
    }
    publishAck("schedule", id != 0, error, id);
  } else {
    publishAck("schedule", false, "unknown action");
  }
}

static void onMqttMessage(const char* topic, const char* payload, size_t len) {
  statusled::flash();
  Serial.printf("MQTT <- %s: %s\n", topic, payload);

  if (strcmp(topic, "cmd/msg") == 0) {
    handleMessageCmd(payload);
  } else if (strcmp(topic, "cmd/schedule") == 0) {
    handleScheduleCmd(payload);
  } else if (strcmp(topic, "cmd/beep") == 0) {
    JsonDocument doc;
    uint8_t timbre = cfg::s.timbre;
    if (len && !deserializeJson(doc, payload)) timbre = doc["timbre"] | timbre;
    sound::chime(timbre);
    publishAck("beep", true);
  } else if (strcmp(topic, "cmd/sync") == 0) {
    onMqttConnect();
    publishAck("sync", true);
  } else if (strcmp(topic, "cmd/reboot") == 0) {
    publishAck("reboot", true);
    delay(500);
    ESP.restart();
  } else if (strcmp(topic, "config/set") == 0) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) || !doc.is<JsonObject>()) {
      publishAck("config", false, "invalid JSON");
      return;
    }
    if (cfg::fromJson(doc.as<JsonObjectConst>())) settingsChanged();
    else publishConfig();
    sound::click();
    publishAck("config", true);
  }
}

// -------------------------------------------------------------- screens

static int centerX(const char* text) {
  return display::CONTENT_X + (display::CONTENT_W - display::fbTextWidth(text)) / 2;
}

// Clock icon: 0 = day pie (fraction of 24 h, midnight at the top, clockwise),
// 1 = static clock face, 2 = current quarter of the day.
static void drawClockIcon(const tm& t) {
  if (cfg::s.clockIcon == 1) {
    display::fbIcon(0, icons::CLOCK);
    return;
  }
  display::fbIcon(0, icons::DIAL_RING);
  const float TWO_PI_F = 6.2831853f;
  float dayFrac = (t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec) / 86400.0f;
  int quadrant = t.tm_hour / 6;

  // 6x6 dial in columns/rows 1–6: the fillable interior is the 4x4 block 2–5.
  for (int y = 2; y <= 5; y++) {
    for (int x = 2; x <= 5; x++) {
      float dx = x + 0.5f - 4.0f;
      float dy = y + 0.5f - 4.0f;
      float a = atan2f(dx, -dy);                        // 0 = top, grows clockwise
      if (a < 0) a += TWO_PI_F;
      bool on = (cfg::s.clockIcon == 0)
                    ? a < dayFrac * TWO_PI_F
                    : (a >= quadrant * TWO_PI_F / 4 && a < (quadrant + 1) * TWO_PI_F / 4);
      if (on) display::fbPixel(x, y);
    }
  }
}

static void renderClock(bool ok, const tm& t) {
  display::fbClear();
  const int y = 1;  // 6-row font centered in the 8-row matrix
  if (!ok) {
    display::fbIcon(0, icons::CLOCK);
    display::fbText(centerX("--:--"), y, "--:--");
    return;
  }
  drawClockIcon(t);

  // Fixed "HH:MM" grid (21 columns): positions never depend on the time.
  // Hours below 10 leave the tens slot blank (no leading zero).
  int x = display::CONTENT_X + (display::CONTENT_W - 21) / 2;
  char d[2] = {0, 0};
  if (t.tm_hour >= 10) {
    d[0] = '0' + t.tm_hour / 10;
    display::fbText(x, y, d);
  }
  d[0] = '0' + t.tm_hour % 10;
  display::fbText(x + 5, y, d);
  if (t.tm_sec % 2 == 0) display::fbText(x + 10, y, ":");
  char mm[3];
  snprintf(mm, sizeof(mm), "%02d", t.tm_min);
  display::fbText(x + 12, y, mm);
}

static void renderDate(bool ok, const tm& t) {
  display::fbClear();
  display::fbIcon(0, icons::CALENDAR);
  char txt[8];
  if (ok) snprintf(txt, sizeof(txt), "%02d/%02d", t.tm_mday, t.tm_mon + 1);
  else strlcpy(txt, "--/--", sizeof(txt));
  display::fbText(centerX(txt), 1, txt);
}

static void renderScreen() {
  if (overlay || display::isScrolling()) return;
  tm t;
  bool ok = getLocalTm(t);
  switch (screen) {
    case SCR_CLOCK: renderClock(ok, t); break;
    case SCR_DATE:  renderDate(ok, t);  break;
    default: return;
  }
  display::fbPush();
}

// ------------------------------------------------------- serial commands

static void printSettings() {
  const auto& s = cfg::s;
  static const char* const ICONS[] = {"day pie", "clock", "quadrant"};
  Serial.printf("name=\"%s\"  id=%s  topics=%s/...\n", s.name, deviceId, mqtt_link::baseTopic());
  Serial.printf("brightness=%u (max %u)  rotated=%s  hourlyBeep=%s\n", s.brightness,
                cfg::BRIGHT_MAX, s.rotated ? "yes" : "no", s.hourlyBeep ? "on" : "off");
  Serial.printf("timbre=%u (%s)  volume=%u/%u  night=%s %02u-%02u (now: %s)\n", s.timbre,
                sound::timbreName(s.timbre), s.volume, cfg::VOLUME_MAX,
                s.nightEnabled ? "on" : "off", s.nightStart, s.nightEnd,
                nightActive ? "yes" : "no");
  Serial.printf("clockIcon=%u (%s)  schedules=%u\n", s.clockIcon, ICONS[s.clockIcon],
                sched::count());
  Serial.printf("wifi=%s ip=%s rssi=%d  ntp=%s  mqtt=%s  fw=%s\n",
                WiFi.status() == WL_CONNECTED ? "ok" : "down", WiFi.localIP().toString().c_str(),
                WiFi.RSSI(), timeOk ? "ok" : "waiting", mqtt_link::connected() ? "ok" : "down",
                FW_VERSION);
}

static void printHelp() {
  Serial.println(F(
      "Commands (Portuguese aliases in brackets):\n"
      "  info                show settings and status\n"
      "  name <text>         friendly name [nome]\n"
      "  bri <0-6>           brightness\n"
      "  rot <0|1>           orientation: 0 normal, 1 rotated 180\n"
      "  beep <0|1>          hourly chime\n"
      "  timbre <0-3>        0 classic, 1 high, 2 soft, 3 chimes\n"
      "  vol <1-5>           buzzer volume\n"
      "  night <0|1>         automatic night mode [noite]\n"
      "  night <start> <end> night mode hours, e.g. night 22 6\n"
      "  icon <0-2>          clock icon: 0 day pie, 1 clock, 2 quadrant [icone]\n"
      "  msg <text>          scroll a message\n"
      "  test                play the hourly chime [teste]\n"
      "  wifireset           forget Wi-Fi and reboot into the setup portal"));
}

static void runCommand(String line) {
  line.trim();
  int sp = line.indexOf(' ');
  String cmd = (sp < 0) ? line : line.substring(0, sp);
  String arg = (sp < 0) ? "" : line.substring(sp + 1);
  cmd.toLowerCase();
  arg.trim();
  auto& s = cfg::s;

  if (cmd == "nome") cmd = "name";
  else if (cmd == "noite") cmd = "night";
  else if (cmd == "icone") cmd = "icon";
  else if (cmd == "teste") cmd = "test";
  else if (cmd == "ajuda") cmd = "help";

  if (cmd == "help" || cmd == "?") { printHelp(); return; }
  if (cmd == "info") { printSettings(); return; }
  if (cmd == "test") { sound::chime(); return; }
  if (cmd == "msg") {
    static char msgBuf[160];
    strlcpy(msgBuf, arg.c_str(), sizeof(msgBuf));
    sound::click();
    showMessage(msgBuf, ICON_ENVELOPE);
    return;
  }
  if (cmd == "wifireset") {
    Serial.println("Forgetting Wi-Fi and rebooting...");
    wm.resetSettings();
    delay(300);
    ESP.restart();
  }

  if (cmd == "name") {
    strlcpy(s.name, arg.c_str(), cfg::NAME_LEN);
  } else if (cmd == "bri") {
    s.brightness = constrain(arg.toInt(), 0, cfg::BRIGHT_MAX);
  } else if (cmd == "rot") {
    s.rotated = arg.toInt() != 0;
  } else if (cmd == "beep") {
    s.hourlyBeep = arg.toInt() != 0;
  } else if (cmd == "timbre") {
    s.timbre = constrain(arg.toInt(), 0, cfg::TIMBRE_COUNT - 1);
    sound::chime();
  } else if (cmd == "vol") {
    s.volume = constrain(arg.toInt(), 1, cfg::VOLUME_MAX);
    sound::chime();
  } else if (cmd == "night") {
    int sp2 = arg.indexOf(' ');
    if (sp2 < 0) {
      s.nightEnabled = arg.toInt() != 0;
    } else {
      s.nightStart = constrain(arg.substring(0, sp2).toInt(), 0, 23);
      s.nightEnd = constrain(arg.substring(sp2 + 1).toInt(), 0, 23);
      s.nightEnabled = true;
    }
  } else if (cmd == "icon") {
    s.clockIcon = constrain(arg.toInt(), 0, cfg::CLOCK_ICON_COUNT - 1);
  } else {
    Serial.println("Unknown command. Type: help");
    return;
  }
  settingsChanged();
  printSettings();
}

static void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) runCommand(line);
      line = "";
    } else if (line.length() < 160) {
      line += c;
    }
  }
}

// --------------------------------------------------------------- Wi-Fi

static bool touchHeldAtBoot() {
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  pinMode(PIN_TOUCH, INPUT);
  uint32_t t0 = millis();
  while (millis() - t0 < 1500) {
    bool held = digitalRead(PIN_TOUCH) == HIGH || digitalRead(PIN_BOOT_BTN) == LOW;
    if (!held) return false;
    delay(20);
  }
  return true;
}

static void connectWiFi(bool forcePortal) {
  wm.setHostname(deviceId);
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(180);  // no setup within 3 min → reboot
  wm.setAPCallback([](WiFiManager*) {
    statusled::set(statusled::SOLID);
    display::showStatic("WIFI");
    Serial.printf("Setup portal open: join \"%s\" (password %s) and open 192.168.4.1\n",
                  AP_NAME, AP_PASS);
  });

  statusled::set(statusled::FAST);
  display::showStatic("WiFi..");

  bool ok = forcePortal ? wm.startConfigPortal(AP_NAME, AP_PASS)
                        : wm.autoConnect(AP_NAME, AP_PASS);
  if (!ok) {
    display::showStatic("ERRO");
    Serial.println("Wi-Fi not configured. Rebooting...");
    delay(1500);
    ESP.restart();
  }
  WiFi.setAutoReconnect(true);
  Serial.printf("Wi-Fi ok: %s  IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// --------------------------------------------------------- setup / loop

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n== esp32c3-clock v%s ==\n", FW_VERSION);

  cfg::load();
  sched::load();
  statusled::begin();
  sound::begin();
  display::begin();
  display::setRotated(cfg::s.rotated);
  applyBrightness();
  display::showStatic("Ola!");

  // Device id from the last 3 bytes of the MAC: stable and unique per board.
  WiFi.mode(WIFI_STA);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(deviceId, sizeof(deviceId), "clock-%02x%02x%02x", mac[3], mac[4], mac[5]);
  WiFi.setHostname(deviceId);
  Serial.printf("Device id: %s\n", deviceId);

  bool forcePortal = touchHeldAtBoot();
  if (forcePortal) Serial.println("Touch held at boot: opening Wi-Fi setup portal.");
  connectWiFi(forcePortal);

  configTzTime(TZ_INFO, "a.st1.ntp.br", "pool.ntp.org", "time.google.com");
  mqtt_link::begin(deviceId, onMqttMessage, onMqttConnect);

  touch.setPressMs(800);
  touch.attachClick(onClick);
  touch.attachDoubleClick(onDoubleClick);
  touch.attachLongPressStart(onLongPress);

  statusled::set(statusled::SLOW);
  sound::confirm();
  printHelp();
}

void loop() {
  touch.tick();
  sound::update();
  handleSerial();

  bool wifiUp = WiFi.status() == WL_CONNECTED;
  mqtt_link::loop(wifiUp && timeOk);

  // A scroll finished: back to the clock.
  if (display::update()) {
    overlay = false;
    if (screen == SCR_LONGDATE) screen = SCR_CLOCK;
  }

  // Alarm: ring every few seconds until touched or timed out.
  if (alarmActive) {
    if (millis() - alarmStarted > ALARM_MAX_MS) {
      stopAlarm();
    } else if (alarmLastRing == 0 || millis() - alarmLastRing > ALARM_RING_MS) {
      alarmLastRing = millis();
      sound::chime(alarmTimbre);
    }
  }

  // Secondary screens return to the clock by themselves.
  if (screen == SCR_DATE && millis() - screenSince > SCREEN_TIMEOUT_MS) {
    goToScreen(SCR_CLOCK);
  }

  // Every 200 ms.
  static uint32_t lastTick = 0;
  if (millis() - lastTick < 200) return;
  lastTick = millis();

  tm t;
  bool ok = getLocalTm(t);
  if (ok && !timeOk) {
    timeOk = true;
    lastBeepHour = t.tm_hour;  // no chime just because we booted
    lastMinute = t.tm_min;
    Serial.println("Time synchronized (NTP).");
  }

  // Status LED
  if (!wifiUp) statusled::set(statusled::FAST);
  else if (!timeOk || !mqtt_link::connected()) statusled::set(statusled::SLOW);
  else statusled::set(statusled::OFF);

  if (ok) {
    bool night = isNightHour(t.tm_hour);
    if (night != nightActive) {
      nightActive = night;
      applyBrightness();
    }
    if (t.tm_min == 0 && t.tm_hour != lastBeepHour) {
      lastBeepHour = t.tm_hour;
      if (cfg::s.hourlyBeep && !nightActive && !alarmActive) sound::chime();
    }
    if (t.tm_min != lastMinute) {  // once per minute
      lastMinute = t.tm_min;
      if (sched::check(t, onScheduleFire)) publishSchedules();
    }
  }

  static uint32_t lastInfo = 0;
  if (mqtt_link::connected() && millis() - lastInfo > INFO_PERIOD_MS) {
    lastInfo = millis();
    publishInfo();
  }

  renderScreen();
}
