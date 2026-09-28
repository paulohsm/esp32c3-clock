#include "schedule.h"
#include <Preferences.h>
#include "config.h"

namespace sched {

static Entry entries[MAX_ENTRIES];
static Preferences prefs;
static const char* NS = "sched";
static constexpr uint8_t STORAGE_VERSION = 1;  // bump when Entry changes layout
static constexpr uint8_t TIMBRE_DEFAULT = 0xFF; // "use the clock's current timbre"

static uint32_t dateOf(const tm& t) {
  return (t.tm_year + 1900) * 10000UL + (t.tm_mon + 1) * 100UL + t.tm_mday;
}

static void save() {
  prefs.begin(NS, false);
  prefs.putUChar("ver", STORAGE_VERSION);
  prefs.putBytes("list", entries, sizeof(entries));
  prefs.end();
}

void load() {
  memset(entries, 0, sizeof(entries));
  prefs.begin(NS, false);
  if (prefs.getUChar("ver", 0) == STORAGE_VERSION &&
      prefs.getBytesLength("list") == sizeof(entries)) {
    prefs.getBytes("list", entries, sizeof(entries));
  }
  prefs.end();
  for (auto& e : entries) e.text[TEXT_LEN - 1] = '\0';
}

static uint8_t nextId() {
  for (uint16_t candidate = 1; candidate < 256; candidate++) {
    bool used = false;
    for (const auto& e : entries) {
      if (e.id == candidate) { used = true; break; }
    }
    if (!used) return candidate;
  }
  return 0;
}

uint8_t addFromJson(JsonObjectConst o, const tm& now, const char*& error) {
  Entry e{};
  int h = -1, m = -1;
  const char* time = o["time"] | "";
  if (sscanf(time, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
    error = "invalid 'time' (use \"HH:MM\")";
    return 0;
  }
  e.hour = h;
  e.minute = m;
  e.alarm = o["alarm"] | false;
  e.timbre = o["timbre"] | TIMBRE_DEFAULT;
  strlcpy(e.text, o["text"] | (e.alarm ? "Alarme" : ""), TEXT_LEN);
  if (e.text[0] == '\0') {
    error = "missing 'text'";
    return 0;
  }

  if (o["days"].is<JsonArrayConst>()) {
    for (JsonVariantConst d : o["days"].as<JsonArrayConst>()) {
      int day = d | -1;
      if (day >= 0 && day <= 6) e.days |= (1 << day);
    }
    if (!e.days) {
      error = "invalid 'days' (use 0=Sun … 6=Sat)";
      return 0;
    }
  } else if (o["date"].is<const char*>()) {
    int y, mo, d;
    if (sscanf(o["date"].as<const char*>(), "%d-%d-%d", &y, &mo, &d) != 3 || mo < 1 ||
        mo > 12 || d < 1 || d > 31) {
      error = "invalid 'date' (use \"YYYY-MM-DD\")";
      return 0;
    }
    e.date = y * 10000UL + mo * 100UL + d;
  } else {
    // Next occurrence: today if the time is still ahead, otherwise tomorrow.
    tm when = now;
    if (h * 60 + m <= now.tm_hour * 60 + now.tm_min) {
      when.tm_mday += 1;
      when.tm_hour = 12;  // avoid any DST edge when normalizing
      mktime(&when);
    }
    e.date = dateOf(when);
  }

  for (auto& slot : entries) {
    if (slot.id == 0) {
      e.id = nextId();
      if (e.id == 0) break;
      slot = e;
      save();
      return e.id;
    }
  }
  error = "schedule list is full";
  return 0;
}

bool remove(uint8_t id) {
  for (auto& e : entries) {
    if (id != 0 && e.id == id) {
      memset(&e, 0, sizeof(e));
      save();
      return true;
    }
  }
  return false;
}

void clear() {
  memset(entries, 0, sizeof(entries));
  save();
}

uint8_t count() {
  uint8_t n = 0;
  for (const auto& e : entries) n += (e.id != 0);
  return n;
}

void toJson(JsonArray arr) {
  for (const auto& e : entries) {
    if (!e.id) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = e.id;
    char t[8];
    snprintf(t, sizeof(t), "%02u:%02u", e.hour, e.minute);
    o["time"] = t;
    o["text"] = e.text;
    o["alarm"] = e.alarm;
    if (e.timbre != TIMBRE_DEFAULT) o["timbre"] = e.timbre;
    if (e.date) {
      char d[16];
      snprintf(d, sizeof(d), "%04lu-%02lu-%02lu", (unsigned long)(e.date / 10000),
               (unsigned long)(e.date / 100 % 100), (unsigned long)(e.date % 100));
      o["date"] = d;
    } else {
      JsonArray days = o["days"].to<JsonArray>();
      for (uint8_t i = 0; i < 7; i++) {
        if (e.days & (1 << i)) days.add(i);
      }
    }
  }
}

bool check(const tm& now, void (*fire)(const Entry&)) {
  uint32_t today = dateOf(now);
  bool changed = false;
  for (auto& e : entries) {
    if (!e.id) continue;
    if (e.date && e.date < today) {  // missed while powered off: discard
      memset(&e, 0, sizeof(e));
      changed = true;
      continue;
    }
    if (e.hour != now.tm_hour || e.minute != now.tm_min) continue;
    bool due = e.date ? (e.date == today) : (e.days & (1 << now.tm_wday));
    if (!due) continue;

    Entry copy = e;
    if (copy.timbre == TIMBRE_DEFAULT) copy.timbre = cfg::s.timbre;
    if (e.date) {  // one-shot: remove before firing
      memset(&e, 0, sizeof(e));
      changed = true;
    }
    fire(copy);
  }
  if (changed) save();
  return changed;
}

}  // namespace sched
