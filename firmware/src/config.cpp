#include "config.h"
#include <Preferences.h>

namespace cfg {

Settings s;
static Preferences prefs;
static const char* NS = "clock";

void clamp() {
  if (s.brightness > BRIGHT_MAX) s.brightness = BRIGHT_MAX;
  if (s.timbre >= TIMBRE_COUNT) s.timbre = 0;
  if (s.volume < 1) s.volume = 1;
  if (s.volume > VOLUME_MAX) s.volume = VOLUME_MAX;
  if (s.nightStart > 23) s.nightStart = 22;
  if (s.nightEnd > 23) s.nightEnd = 6;
  if (s.clockIcon >= CLOCK_ICON_COUNT) s.clockIcon = 0;
  s.name[NAME_LEN - 1] = '\0';
  if (s.name[0] == '\0') strlcpy(s.name, "Clock", NAME_LEN);
}

void load() {
  prefs.begin(NS, false);
  if (prefs.isKey("name")) prefs.getString("name", s.name, NAME_LEN);
  s.brightness   = prefs.getUChar("bri", BRIGHT_DEFAULT);
  s.rotated      = prefs.getBool("rot", false);
  s.hourlyBeep   = prefs.getBool("hbeep", true);
  s.timbre       = prefs.getUChar("timbre", 0);
  s.volume       = prefs.getUChar("vol", 3);
  s.nightEnabled = prefs.getBool("night", true);
  s.nightStart   = prefs.getUChar("nstart", 22);
  s.nightEnd     = prefs.getUChar("nend", 6);
  s.clockIcon    = prefs.getUChar("cicon", 0);
  prefs.end();
  clamp();
}

void save() {
  prefs.begin(NS, false);
  prefs.putString("name", s.name);
  prefs.putUChar("bri", s.brightness);
  prefs.putBool("rot", s.rotated);
  prefs.putBool("hbeep", s.hourlyBeep);
  prefs.putUChar("timbre", s.timbre);
  prefs.putUChar("vol", s.volume);
  prefs.putBool("night", s.nightEnabled);
  prefs.putUChar("nstart", s.nightStart);
  prefs.putUChar("nend", s.nightEnd);
  prefs.putUChar("cicon", s.clockIcon);
  prefs.end();
}

void toJson(JsonObject o) {
  o["name"]         = s.name;
  o["brightness"]   = s.brightness;
  o["brightnessMax"] = BRIGHT_MAX;
  o["rotated"]      = s.rotated;
  o["hourlyBeep"]   = s.hourlyBeep;
  o["timbre"]       = s.timbre;
  o["volume"]       = s.volume;
  o["nightEnabled"] = s.nightEnabled;
  o["nightStart"]   = s.nightStart;
  o["nightEnd"]     = s.nightEnd;
  o["clockIcon"]    = s.clockIcon;
}

template <typename T>
static bool setIf(JsonObjectConst o, const char* key, T& field) {
  if (!o[key].is<T>()) return false;
  T v = o[key].as<T>();
  if (v == field) return false;
  field = v;
  return true;
}

bool fromJson(JsonObjectConst o) {
  bool changed = false;
  if (o["name"].is<const char*>()) {
    const char* n = o["name"];
    if (strcmp(n, s.name) != 0) {
      strlcpy(s.name, n, NAME_LEN);
      changed = true;
    }
  }
  changed |= setIf(o, "brightness", s.brightness);
  changed |= setIf(o, "rotated", s.rotated);
  changed |= setIf(o, "hourlyBeep", s.hourlyBeep);
  changed |= setIf(o, "timbre", s.timbre);
  changed |= setIf(o, "volume", s.volume);
  changed |= setIf(o, "nightEnabled", s.nightEnabled);
  changed |= setIf(o, "nightStart", s.nightStart);
  changed |= setIf(o, "nightEnd", s.nightEnd);
  changed |= setIf(o, "clockIcon", s.clockIcon);
  clamp();
  return changed;
}

}  // namespace cfg
