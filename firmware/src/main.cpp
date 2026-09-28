// esp32c3-clock — desk clock with a 32x8 MAX7219 matrix, controlled over MQTT.
//
// NTP clock, touch navigation, hourly chime, night mode, status LED, fixed-width
// 4x6 font with 6x6 icons, remote control over MQTT/TLS (HiveMQ Cloud) — instant
// messages, schedules/alarms, remote settings — and internet data fetched by the
// clock itself: weather (Open-Meteo) and currency/crypto quotes (AwesomeAPI).

#include <Arduino.h>
#include <ArduinoJson.h>
#include <OneButton.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "feeds.h"
#include "icons.h"
#include "mqtt_link.h"
#include "pins.h"
#include "schedule.h"
#include "sound.h"
#include "statusled.h"

#define FW_VERSION "0.8.0"

static const char* AP_NAME = "Relogio-Config";
static const char* AP_PASS = "relogio123";  // setup network password (min. 8 chars)
static const char* TZ_INFO = "<-03>3";      // Fortaleza: UTC-3, no daylight saving

// Shown on the matrix, so kept in Portuguese (no accents: ASCII font).
static const char* const WEEKDAYS[] = {"Domingo", "Segunda", "Terca", "Quarta",
                                       "Quinta",  "Sexta",   "Sabado"};
static const char* const MONTHS[] = {"janeiro", "fevereiro", "marco",    "abril",
                                     "maio",    "junho",     "julho",    "agosto",
                                     "setembro", "outubro",  "novembro", "dezembro"};


// Icons of the quotes, in the order of feeds::QUOTE_CODES.
static const uint8_t* const QUOTE_ICONS[feeds::QUOTE_COUNT] = {
    icons::DOLLAR, icons::EURO, icons::POUND, icons::BITCOIN, icons::ETHER};

// Touch cycle. Screens without data (or disabled in the settings) are skipped.
enum Screen : uint8_t {
  SCR_CLOCK, SCR_DATE, SCR_LONGDATE, SCR_WEATHER, SCR_RAIN, SCR_UV, SCR_SUN, SCR_QUOTE0,
};
static constexpr uint8_t SCR_COUNT = SCR_QUOTE0 + feeds::QUOTE_COUNT;

static WiFiManager wm;
static OneButton touch(PIN_TOUCH, false, false);  // active HIGH, no pull-up

static char     deviceId[16];
static uint8_t  screen       = SCR_CLOCK;
static uint32_t screenSince  = 0;
static bool     overlay      = false;  // a temporary message is scrolling
static bool     timeOk       = false;
static bool     nightActive  = false;
static bool     internetOk   = true;   // Wi-Fi up and MQTT reachable
static int      lastBeepHour = -1;
static int      lastMinute   = -1;

// Alarm state
static bool     alarmActive  = false;
static uint32_t alarmStarted = 0;
static uint32_t alarmLastRing = 0;
static uint8_t  alarmTimbre  = 0;
static constexpr uint32_t ALARM_MAX_MS  = 60000;  // stops by itself after 1 min
static constexpr uint32_t ALARM_RING_MS = 3000;

// Emergency alert state
static bool     emergencyActive = false;
static uint32_t emergencyStart  = 0;
static uint32_t emergencyMs     = 0;
static char     emergencyText[160];
static constexpr uint32_t EMERGENCY_MAX_S = 3600;

static constexpr uint32_t SCREEN_TIMEOUT_MS = 10000;  // secondary screens return to clock
static constexpr uint32_t INFO_PERIOD_MS    = 300000; // republish device info every 5 min
static constexpr uint32_t ALTERNATE_MS      = 3000;   // value ↔ change / sunrise ↔ sunset
static constexpr uint32_t RETRY_MS          = 60000;  // retry a failed fetch after 1 min
static constexpr uint8_t  RAIN_ALERT_PCT    = 60;     // morning warning threshold

// Next time (millis) each feed is due, plus "fetch now" flags.
static uint32_t nextWeatherAt = 0;
static uint32_t nextQuotesAt  = 0;
static bool     weatherDue    = true;

// Carousel: every autoEvery seconds, show the chosen screens for autoFor seconds each.
static bool     carouselOn    = false;
static uint8_t  carouselNext  = 0;      // next candidate screen to show
static int32_t  lastCarouselSlot = -1;  // time slot that already triggered
static uint8_t  carouselMask  = 0;      // screens of the running carousel
static uint32_t carouselDurMs = 2000;   // time per screen of the running carousel
static constexpr uint32_t SHOW_MS = 6000;  // "show now" from the app: time per screen
static bool     quotesDue     = true;

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

// Scroll speed setting 1..5 → milliseconds per column.
static uint16_t scrollMs() {
  static const uint16_t MS[5] = {60, 45, 35, 25, 16};
  return MS[constrain(cfg::s.scrollSpeed, 1, 5) - 1];
}

static void showMessage(const char* text, const icons::Anim& icon, uint8_t loops = 1) {
  overlay = true;
  carouselOn = false;
  display::scrollStart(text, icon.frames, cfg::s.anim ? icon.count : 1, loops, scrollMs(),
                       icon.periodMs);
}

static void goToScreen(uint8_t s) {
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
    const auto& a = icons::A_CALENDAR;
    display::scrollStart(txt, a.frames, cfg::s.anim ? a.count : 1, 1, scrollMs(), a.periodMs);
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
  doc["place"] = cfg::s.place;
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

static void publishWeather() {
  JsonDocument doc;
  feeds::weatherToJson(doc.to<JsonObject>());
  publishJson("data/weather", doc, true);
}

static void publishQuotes() {
  JsonDocument doc;
  feeds::quotesToJson(doc.to<JsonObject>());
  publishJson("data/quotes", doc, true);
}

// Sounds for connection events are muted in night mode and never cut into an
// alarm or emergency.
static bool connectionSoundsOk() { return !nightActive && !alarmActive && !emergencyActive; }

static void onMqttConnected() {
  if (connectionSoundsOk()) sound::connected();
}

static void onMqttFailed(int state) {
  if (connectionSoundsOk()) sound::failed();
  if (!overlay && !display::isScrolling() && !alarmActive && !emergencyActive) {
    showMessage("Falha na conexao", icons::A_WIFI_FAIL, 1);
  }
}

static void onMqttConnect() {
  publishInfo();
  publishConfig();
  publishSchedules();
  if (feeds::weather.valid) publishWeather();
  publishQuotes();
}

// Every new MQTT session: publish the state and play the "connected" tones.
static void onMqttSessionStart() {
  onMqttConnect();
  onMqttConnected();
}

// Called after any settings change (serial or MQTT).
static void settingsChanged() {
  // A new location or quote selection is fetched right away.
  static float lastLat = NAN, lastLon = NAN;
  static int lastQuotes = -1;
  cfg::clamp();
  if (cfg::s.lat != lastLat || cfg::s.lon != lastLon) weatherDue = true;
  if (cfg::s.quotes != lastQuotes) quotesDue = true;
  lastLat = cfg::s.lat;
  lastLon = cfg::s.lon;
  lastQuotes = cfg::s.quotes;
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
  showMessage(text, icons::A_BELL, 0);  // loops until stopped
}

static void stopAlarm() {
  alarmActive = false;
  overlay = false;
  display::scrollStop();
}

// --------------------------------------------------------- emergency

// Retained, so the app shows whether the alert is running or who stopped it.
static void publishAlertState(const char* endedBy) {
  JsonDocument doc;
  doc["active"] = emergencyActive;
  doc["text"] = emergencyText;
  time_t now = time(nullptr);
  if (emergencyActive) {
    doc["started"] = now - (millis() - emergencyStart) / 1000;
    doc["until"] = now + (emergencyMs - (millis() - emergencyStart)) / 1000;
  } else {
    doc["endedBy"] = endedBy;  // "touch", "app" or "timeout"
    doc["at"] = now;
  }
  publishJson("alert", doc, true);
}

static void startEmergency(const char* text, uint32_t seconds) {
  if (alarmActive) { alarmActive = false; }
  strlcpy(emergencyText, text, sizeof(emergencyText));
  emergencyActive = true;
  emergencyStart = millis();
  if (seconds < 10) seconds = 10;
  if (seconds > EMERGENCY_MAX_S) seconds = EMERGENCY_MAX_S;
  emergencyMs = seconds * 1000UL;
  display::setBrightness(cfg::BRIGHT_MAX);  // even in night mode
  showMessage(emergencyText, icons::A_ALERT, 0);  // scrolls until stopped
  sound::siren();
  publishAlertState(nullptr);
  Serial.printf("EMERGENCY for %lu s: %s\n", (unsigned long)(emergencyMs / 1000), emergencyText);
}

static void stopEmergency(const char* by) {
  if (!emergencyActive) return;
  emergencyActive = false;
  overlay = false;
  display::scrollStop();
  applyBrightness();
  publishAlertState(by);
  Serial.printf("Emergency ended by %s\n", by);
}

static void onScheduleFire(const sched::Entry& e) {
  if (emergencyActive) {  // an emergency owns the screen and the buzzer
    Serial.printf("Schedule #%u skipped (emergency active): %s\n", e.id, e.text);
    return;
  }
  statusled::flash();
  if (e.alarm) {
    startAlarm(e.text, e.timbre);
  } else {
    sound::chime(e.timbre);
    showMessage(e.text, icons::A_ENVELOPE, 3);
  }
  Serial.printf("Schedule #%u fired: %s\n", e.id, e.text);
}

// -------------------------------------------------------------- touch

// Settings bit that enables a screen (0 for the clock, which is always on).
static uint8_t screenBit(uint8_t s) {
  switch (s) {
    case SCR_CLOCK:    return 0;
    case SCR_DATE:     return cfg::SB_DATE;
    case SCR_LONGDATE: return cfg::SB_LONGDATE;
    case SCR_WEATHER:  return cfg::SB_WEATHER;
    case SCR_RAIN:     return cfg::SB_RAIN;
    case SCR_UV:       return cfg::SB_UV;
    case SCR_SUN:      return cfg::SB_SUN;
    default:           return cfg::SB_QUOTES;
  }
}

// Does the screen have something to show right now?
static bool screenHasData(uint8_t s) {
  const bool w = feeds::weather.valid;
  switch (s) {
    case SCR_CLOCK: case SCR_DATE: case SCR_LONGDATE: return true;
    case SCR_WEATHER: case SCR_RAIN: case SCR_UV: return w;
    case SCR_SUN: return w && feeds::weather.sunrise[0];
    default: {
      uint8_t q = s - SCR_QUOTE0;
      return (cfg::s.quotes & (1 << q)) && feeds::quotes[q].valid;
    }
  }
}

static bool screenAvailable(uint8_t s) {
  return s == SCR_CLOCK || ((cfg::s.screens & screenBit(s)) && screenHasData(s));
}

static bool inCarousel(uint8_t s) {
  return s != SCR_CLOCK && s != SCR_LONGDATE && (carouselMask & screenBit(s)) &&
         screenHasData(s);
}

static void startCarousel(uint8_t mask, uint32_t durMs) {
  carouselMask = mask;
  carouselDurMs = durMs;
  carouselNext = SCR_CLOCK + 1;
  carouselOn = true;
  screenSince = millis() - durMs;  // show the first screen right away
}

static uint8_t nextScreen() {
  for (uint8_t k = 1; k <= SCR_COUNT; k++) {
    uint8_t c = (screen + k) % SCR_COUNT;
    if (screenAvailable(c)) return c;
  }
  return SCR_CLOCK;
}

static void onClick() {
  statusled::flash();
  sound::click();
  if (emergencyActive) { stopEmergency("touch"); sound::confirm(); return; }
  if (alarmActive) { stopAlarm(); return; }
  if (overlay) {  // a touch dismisses the message
    overlay = false;
    goToScreen(SCR_CLOCK);
    return;
  }
  carouselOn = false;  // manual navigation takes over
  goToScreen(nextScreen());
}

static void onDoubleClick() {
  if (emergencyActive) { stopEmergency("touch"); sound::confirm(); return; }
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
  showMessage(txt, icons::A_WIFI);
}

static void onLongPress() {
  if (emergencyActive) { stopEmergency("touch"); sound::confirm(); return; }
  if (alarmActive) { stopAlarm(); return; }
  cfg::s.hourlyBeep = !cfg::s.hourlyBeep;
  settingsChanged();
  sound::confirm();
  showMessage(cfg::s.hourlyBeep ? "Bipe de hora ligado" : "Bipe de hora desligado", icons::A_NOTE);
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
  if (emergencyActive) { publishAck("msg", false, "emergency alert active"); return; }
  if (alarmActive) stopAlarm();
  if (beep) sound::chime();
  showMessage(text, icons::A_ENVELOPE, repeat);
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
      showMessage("Agendado", icons::A_BELL, 1);
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
    weatherDue = quotesDue = true;  // also refresh the internet data
    onMqttConnect();
    publishAck("sync", true);
  } else if (strcmp(topic, "cmd/alert") == 0) {
    // {"text":"...","seconds":120} starts; {"cancel":true} stops.
    JsonDocument doc;
    if (deserializeJson(doc, payload)) { publishAck("alert", false, "invalid JSON"); return; }
    if (doc["cancel"] | false) {
      bool was = emergencyActive;
      stopEmergency("app");
      publishAck("alert", true, was ? nullptr : "no alert running");
      return;
    }
    const char* text = doc["text"] | "";
    if (!text[0]) { publishAck("alert", false, "empty text"); return; }
    startEmergency(text, doc["seconds"] | 60);
    publishAck("alert", true);
  } else if (strcmp(topic, "cmd/show") == 0) {
    // Show a screen now: payload "weather" | "rain" | "uv" | "sun" | "quotes" | "date" |
    // "longdate" (plain text or {"screen":"..."}).
    JsonDocument doc;
    const char* what = payload;
    if (payload[0] == '{' && !deserializeJson(doc, payload)) what = doc["screen"] | "";
    static const struct { const char* name; uint8_t bit; } SHOW[] = {
        {"date", cfg::SB_DATE}, {"weather", cfg::SB_WEATHER}, {"rain", cfg::SB_RAIN},
        {"uv", cfg::SB_UV}, {"sun", cfg::SB_SUN}, {"quotes", cfg::SB_QUOTES}};
    if (emergencyActive) { publishAck("show", false, "emergency alert active"); return; }
    if (alarmActive) stopAlarm();
    overlay = false;
    if (strcmp(what, "longdate") == 0) {
      carouselOn = false;
      goToScreen(SCR_LONGDATE);
      publishAck("show", true);
      return;
    }
    uint8_t bit = 0;
    for (const auto& e : SHOW) if (strcmp(what, e.name) == 0) bit = e.bit;
    bool any = false;
    for (uint8_t sc = SCR_CLOCK + 1; sc < SCR_COUNT; sc++) {
      if (sc != SCR_LONGDATE && (screenBit(sc) & bit) && screenHasData(sc)) any = true;
    }
    if (!bit || !any) {
      publishAck("show", false, bit ? "no data yet" : "unknown screen");
      return;
    }
    if (display::isScrolling()) display::scrollStop();
    startCarousel(bit, SHOW_MS);
    publishAck("show", true);
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
  if (cfg::s.anim) {
    // Seconds: a dark gap walks clockwise around the 16-pixel ring, one step per second.
    static const uint8_t RING[16][2] = {{4, 1}, {5, 1}, {6, 2}, {6, 3}, {6, 4}, {6, 5},
                                        {5, 6}, {4, 6}, {3, 6}, {2, 6}, {1, 5}, {1, 4},
                                        {1, 3}, {1, 2}, {2, 1}, {3, 1}};
    const uint8_t* p = RING[t.tm_sec % 16];
    display::fbPixel(p[0], p[1], false);
  }
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

// Fixed "HH:MM" grid (21 columns): positions never depend on the time.
// Hours below 10 leave the tens slot blank (no leading zero).
static void drawTime(int hour, int minute, bool colon) {
  const int y = 1;
  int x = display::CONTENT_X + (display::CONTENT_W - 21) / 2;
  char d[2] = {0, 0};
  if (hour >= 10) {
    d[0] = '0' + hour / 10;
    display::fbText(x, y, d);
  }
  d[0] = '0' + hour % 10;
  display::fbText(x + 5, y, d);
  if (colon) display::fbText(x + 10, y, ":");
  char mm[3];
  snprintf(mm, sizeof(mm), "%02d", minute);
  display::fbText(x + 12, y, mm);
}

// Icon + centered text: the layout shared by most info screens.
static void drawIconText(const uint8_t icon[8], const char* text) {
  display::fbClear();
  display::fbIcon(0, icon);
  display::fbText(centerX(text), 1, text);
}

// Decimal comma, and K/M suffixes so every value fits the 23 content columns.
static void formatValue(float v, char* out, size_t len) {
  float a = fabsf(v);
  if (a < 10) snprintf(out, len, "%.2f", v);
  else if (a < 100) snprintf(out, len, "%.1f", v);
  else if (a < 10000) snprintf(out, len, "%.0f", v);
  else if (a < 100000) snprintf(out, len, "%.1fK", v / 1000);
  else if (a < 1000000) snprintf(out, len, "%.0fK", v / 1000);
  else if (a < 10000000) snprintf(out, len, "%.2fM", v / 1000000);
  else snprintf(out, len, "%.1fM", v / 1000000);
  for (char* p = out; *p; p++) if (*p == '.') *p = ',';
}

static void formatPct(float pct, char* out, size_t len) {
  if (fabsf(pct) < 10) snprintf(out, len, "%+.1f%%", pct);
  else snprintf(out, len, "%+.0f%%", pct);
  for (char* p = out; *p; p++) if (*p == '.') *p = ',';
}

// WMO weather code → icon.
static const icons::Anim& weatherIcon(int code, bool isDay) {
  if (code <= 1) return isDay ? icons::A_SUN : icons::A_MOON;
  if (code == 2) return isDay ? icons::A_PARTLY : icons::A_CLOUD;
  if (code == 45 || code == 48) return icons::A_FOG;
  if (code >= 95) return icons::A_STORM;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return icons::A_RAIN;
  return icons::A_CLOUD;  // 3 overcast, snow codes (not expected in Fortaleza)
}

static const uint8_t* animFrame(const icons::Anim& a) { return icons::frame(a, cfg::s.anim); }

static void renderInfoScreen() {
  const auto& w = feeds::weather;
  char txt[16];
  bool second = ((millis() - screenSince) / ALTERNATE_MS) % 2 == 1;
  switch (screen) {
    case SCR_WEATHER:
      snprintf(txt, sizeof(txt), "%d*", (int)lroundf(w.temp));
      drawIconText(animFrame(weatherIcon(w.code, w.isDay)), txt);
      break;
    case SCR_RAIN:
      snprintf(txt, sizeof(txt), "%u%%", w.rainDay);
      drawIconText(animFrame(icons::A_UMBRELLA), txt);
      break;
    case SCR_UV:
      snprintf(txt, sizeof(txt), "UV %d", (int)lroundf(w.isDay ? w.uv : w.uvMax));
      drawIconText(animFrame(icons::A_SUN), txt);
      break;
    case SCR_SUN: {
      const char* hhmm = second ? w.sunset : w.sunrise;
      display::fbClear();
      display::fbIcon(0, animFrame(second ? icons::A_SUNSET : icons::A_SUNRISE));
      drawTime(atoi(hhmm), atoi(hhmm + 3), true);
      break;
    }
    default: {
      uint8_t q = screen - SCR_QUOTE0;
      if (second) {  // daily change, with an arrow bouncing up or down
        formatPct(feeds::quotes[q].pct, txt, sizeof(txt));
        drawIconText(animFrame(feeds::quotes[q].pct >= 0 ? icons::A_UP : icons::A_DOWN), txt);
      } else {
        formatValue(feeds::quotes[q].bid, txt, sizeof(txt));
        drawIconText(QUOTE_ICONS[q], txt);
      }
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
  // No internet: the crossed Wi-Fi icon blinks twice every 5 seconds.
  uint32_t phase = millis() % 5000;
  bool warn = !internetOk && (phase < 300 || (phase >= 600 && phase < 900));
  if (warn) display::fbIcon(0, icons::WIFI_OFF);
  else drawClockIcon(t);
  drawTime(t.tm_hour, t.tm_min, t.tm_sec % 2 == 0);
}

static void renderDate(bool ok, const tm& t) {
  display::fbClear();
  display::fbIcon(0, animFrame(icons::A_CALENDAR));
  char txt[8];
  if (ok) snprintf(txt, sizeof(txt), "%02d/%02d", t.tm_mday, t.tm_mon + 1);
  else strlcpy(txt, "--/--", sizeof(txt));
  display::fbText(centerX(txt), 1, txt);
}

static void renderScreen() {
  if (overlay || display::isScrolling()) return;
  static int lastScreen = -1, lastMin = -1;
  tm t;
  bool ok = getLocalTm(t);

  // Pick the transition: slide on screen change, roll when digits change.
  display::Transition tr = display::NONE;
  if (cfg::s.anim) {
    if (screen != lastScreen) tr = display::SLIDE;
    else if (screen != SCR_CLOCK || (ok && t.tm_min != lastMin)) tr = display::ROLL;
  }
  lastScreen = screen;
  if (ok) lastMin = t.tm_min;
  switch (screen) {
    case SCR_CLOCK: renderClock(ok, t); break;
    case SCR_DATE:  renderDate(ok, t);  break;
    case SCR_LONGDATE: return;
    default:
      if (!screenAvailable(screen)) {  // data went away (e.g. setting changed)
        goToScreen(SCR_CLOCK);
        return;
      }
      renderInfoScreen();
  }
  display::fbPush(tr);
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
  Serial.printf("clockIcon=%u (%s)  schedules=%u  anim=%s  scrollSpeed=%u\n", s.clockIcon,
                ICONS[s.clockIcon], sched::count(), s.anim ? "on" : "off", s.scrollSpeed);
  Serial.printf("carousel: every %u s, %u s per screen, screens mask 0x%02x\n", s.autoEvery,
                s.autoFor, s.autoScreens);
  Serial.printf("place=\"%s\" (%.4f, %.4f)  weather every %u min, quotes every %u min%s\n",
                s.place, s.lat, s.lon, s.weatherMin, s.quotesMin,
                s.quotesAtNight ? " (also at night)" : "");
  const auto& w = feeds::weather;
  if (w.valid) {
    Serial.printf("weather: %.1fC (feels %.1f) code %d  rain %u%% next 3h / %u%% today  "
                  "uv %.1f (max %.1f)  sun %s-%s\n",
                  w.temp, w.feels, w.code, w.rainNext, w.rainDay, w.uv, w.uvMax, w.sunrise,
                  w.sunset);
  }
  for (uint8_t i = 0; i < feeds::QUOTE_COUNT; i++) {
    if (feeds::quotes[i].valid) {
      Serial.printf("%s: R$ %.4f (%+.2f%%)\n", feeds::QUOTE_CODES[i], feeds::quotes[i].bid,
                    feeds::quotes[i].pct);
    }
  }
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
      "  timbre <0-9>        0 classic, 1 ding-dong, 2 doorbell, 3 big ben, 4 cuckoo,\n"
      "                      5 microwave, 6 notification, 7 coin, 8 soft, 9 bird\n"
      "  vol <1-5>           buzzer volume\n"
      "  night <0|1>         automatic night mode [noite]\n"
      "  night <start> <end> night mode hours, e.g. night 22 6\n"
      "  icon <0-2>          clock icon: 0 day pie, 1 clock, 2 quadrant [icone]\n"
      "  loc <lat> <lon>     weather location, e.g. loc -3.73 -38.53\n"
      "  fetch               refresh weather and quotes now [atualizar]\n"
      "  anim <0|1>          animations (rolling digits, sliding screens) [animacao]\n"
      "  speed <1-5>         scroll speed [velocidade]\n"
      "  auto <sec> [dur]    carousel every <sec> seconds (0 = off), <dur> s per screen\n"
      "  msg <text>          scroll a message\n"
      "  alert <sec> <text>  emergency alert (touch to dismiss); 'alert 0' stops it [alerta]\n"
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
  else if (cmd == "atualizar") cmd = "fetch";
  else if (cmd == "animacao") cmd = "anim";
  else if (cmd == "velocidade") cmd = "speed";
  else if (cmd == "alerta") cmd = "alert";

  if (cmd == "help" || cmd == "?") { printHelp(); return; }
  if (cmd == "info") { printSettings(); return; }
  if (cmd == "test") { sound::chime(); return; }
  if (cmd == "fetch") { weatherDue = quotesDue = true; return; }
  if (cmd == "msg") {
    static char msgBuf[160];
    strlcpy(msgBuf, arg.c_str(), sizeof(msgBuf));
    sound::click();
    showMessage(msgBuf, icons::A_ENVELOPE);
    return;
  }
  if (cmd == "alert") {
    int sp2 = arg.indexOf(' ');
    long sec = (sp2 < 0 ? arg : arg.substring(0, sp2)).toInt();
    if (sec <= 0) { stopEmergency("app"); return; }
    static char alertBuf[160];
    strlcpy(alertBuf, sp2 < 0 ? "ALERTA" : arg.substring(sp2 + 1).c_str(), sizeof(alertBuf));
    startEmergency(alertBuf, sec);
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
  } else if (cmd == "loc") {
    int sp2 = arg.indexOf(' ');
    if (sp2 < 0) { Serial.println("Usage: loc <lat> <lon>"); return; }
    s.lat = arg.substring(0, sp2).toFloat();
    s.lon = arg.substring(sp2 + 1).toFloat();
    snprintf(s.place, cfg::PLACE_LEN, "%.3f, %.3f", s.lat, s.lon);
  } else if (cmd == "anim") {
    s.anim = arg.toInt() != 0;
  } else if (cmd == "speed") {
    s.scrollSpeed = constrain(arg.toInt(), 1, 5);
  } else if (cmd == "auto") {
    int sp2 = arg.indexOf(' ');
    s.autoEvery = constrain((sp2 < 0 ? arg : arg.substring(0, sp2)).toInt(), 0, 3600);
    if (sp2 >= 0) s.autoFor = constrain(arg.substring(sp2 + 1).toInt(), 1, 30);
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
  mqtt_link::begin(deviceId, onMqttMessage, onMqttSessionStart, onMqttFailed);

  touch.setPressMs(800);
  touch.attachClick(onClick);
  touch.attachDoubleClick(onDoubleClick);
  touch.attachLongPressStart(onLongPress);

  statusled::set(statusled::SLOW);
  printHelp();
}

void loop() {
  touch.tick();
  sound::update();
  handleSerial();

  bool wifiUp = WiFi.status() == WL_CONNECTED;
  // New connection attempts block for a few seconds, so they wait for a short
  // message to finish scrolling (alarms and emergencies still reconnect).
  bool busyScrolling = display::isScrolling() && !alarmActive && !emergencyActive;
  mqtt_link::loop(wifiUp && timeOk && !busyScrolling);

  // A scroll finished: back to the clock.
  if (display::update()) {
    overlay = false;
    if (screen == SCR_LONGDATE) screen = SCR_CLOCK;
  }

  // Emergency: siren without pause until touched, cancelled or timed out.
  if (emergencyActive) {
    if (millis() - emergencyStart >= emergencyMs) stopEmergency("timeout");
    else if (!sound::isPlaying()) sound::siren();
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
  if (carouselOn) {
    if (millis() - screenSince >= carouselDurMs) {
      // Next carousel screen, or back to the clock after the last one.
      uint8_t s = SCR_CLOCK;
      while (carouselNext < SCR_COUNT) {
        uint8_t c = carouselNext++;
        if (inCarousel(c)) { s = c; break; }
      }
      if (s == SCR_CLOCK) carouselOn = false;
      goToScreen(s);
    }
  } else if (screen != SCR_CLOCK && screen != SCR_LONGDATE && !overlay &&
      millis() - screenSince > SCREEN_TIMEOUT_MS) {
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

  // Carousel trigger: in the middle of each period (e.g. at :30 of every minute),
  // so it never covers the moment the minute changes.
  if (ok && cfg::s.autoEvery && screen == SCR_CLOCK && !overlay && !alarmActive &&
      !display::isScrolling()) {
    int32_t secOfDay = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    int32_t slot = secOfDay / cfg::s.autoEvery;
    if (secOfDay % cfg::s.autoEvery >= cfg::s.autoEvery / 2 && slot != lastCarouselSlot) {
      lastCarouselSlot = slot;
      startCarousel(cfg::s.autoScreens, cfg::s.autoFor * 1000UL);
    }
  }

  // "No internet": Wi-Fi down, or the broker unreachable for over a minute.
  static uint32_t mqttLostAt = 0;
  if (mqtt_link::connected()) mqttLostAt = 0;
  else if (!mqttLostAt) mqttLostAt = millis() | 1;
  internetOk = wifiUp && (!mqttLostAt || millis() - mqttLostAt < 60000);

  // Status LED
  if (!wifiUp) statusled::set(statusled::FAST);
  else if (!timeOk || !mqtt_link::connected()) statusled::set(statusled::SLOW);
  else statusled::set(statusled::OFF);

  if (ok) {
    bool night = isNightHour(t.tm_hour);
    if (night != nightActive) {
      nightActive = night;
      if (!emergencyActive) applyBrightness();
    }
    if (t.tm_min == 0 && t.tm_hour != lastBeepHour) {
      lastBeepHour = t.tm_hour;
      if (cfg::s.hourlyBeep && !nightActive && !alarmActive && !emergencyActive) sound::chime();
    }
    if (t.tm_min != lastMinute) {  // once per minute
      lastMinute = t.tm_min;
      if (sched::check(t, onScheduleFire)) publishSchedules();

      // Morning rain warning.
      const auto& w = feeds::weather;
      if (cfg::s.rainAlert && t.tm_hour == cfg::s.rainHour && t.tm_min == 0 && w.valid &&
          w.rainDay >= RAIN_ALERT_PCT && !alarmActive && !emergencyActive) {
        static char txt[64];
        snprintf(txt, sizeof(txt), "Leve guarda-chuva! Chuva %u%% hoje", w.rainDay);
        sound::chime();
        showMessage(txt, icons::A_UMBRELLA, 3);
      }
    }
  }

  // Internet data: one fetch at a time, never in the middle of a scroll or alarm
  // (a fetch blocks for a second or two).
  if (wifiUp && timeOk && !display::isScrolling() && !alarmActive && !emergencyActive) {
    uint32_t now = millis();
    if (weatherDue || (int32_t)(now - nextWeatherAt) >= 0) {
      weatherDue = false;
      bool okW = feeds::fetchWeather(cfg::s.lat, cfg::s.lon);
      nextWeatherAt = millis() + (okW ? cfg::s.weatherMin * 60000UL : RETRY_MS);
      if (okW) publishWeather();
    } else if (quotesDue || (int32_t)(now - nextQuotesAt) >= 0) {
      quotesDue = false;
      if (!cfg::s.quotes || (nightActive && !cfg::s.quotesAtNight)) {
        nextQuotesAt = now + RETRY_MS;  // nothing to fetch now; check again later
      } else {
        bool okQ = feeds::fetchQuotes(cfg::s.quotes);
        nextQuotesAt = millis() + (okQ ? cfg::s.quotesMin * 60000UL : RETRY_MS);
        if (okQ) publishQuotes();
      }
    }
  }

  static uint32_t lastInfo = 0;
  if (mqtt_link::connected() && millis() - lastInfo > INFO_PERIOD_MS) {
    lastInfo = millis();
    publishInfo();
  }

  renderScreen();
}
