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
  if (s.weatherMin < 5) s.weatherMin = 5;
  if (s.weatherMin > 120) s.weatherMin = 120;
  if (s.quotesMin < 5) s.quotesMin = 5;
  if (s.quotesMin > 120) s.quotesMin = 120;
  if (s.rainHour > 23) s.rainHour = 7;
  s.quotes &= QUOTES_ALL;
  s.screens &= SCREENS_ALL;
  if (!(s.lat >= -90 && s.lat <= 90 && s.lon >= -180 && s.lon <= 180)) {  // also catches NaN
    s.lat = -3.7319f;
    s.lon = -38.5267f;
  }
  s.place[PLACE_LEN - 1] = '\0';
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
  s.lat          = prefs.getFloat("lat", s.lat);
  s.lon          = prefs.getFloat("lon", s.lon);
  if (prefs.isKey("place")) prefs.getString("place", s.place, PLACE_LEN);
  s.weatherMin   = prefs.getUChar("wmin", 10);
  s.quotesMin    = prefs.getUChar("qmin", 15);
  s.quotesAtNight = prefs.getBool("qnight", false);
  s.quotes       = prefs.getUChar("quotes", 0b11011);
  s.screens      = prefs.getUChar("screens", SCREENS_ALL);
  s.rainAlert    = prefs.getBool("rain", true);
  s.rainHour     = prefs.getUChar("rainh", 7);
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
  prefs.putFloat("lat", s.lat);
  prefs.putFloat("lon", s.lon);
  prefs.putString("place", s.place);
  prefs.putUChar("wmin", s.weatherMin);
  prefs.putUChar("qmin", s.quotesMin);
  prefs.putBool("qnight", s.quotesAtNight);
  prefs.putUChar("quotes", s.quotes);
  prefs.putUChar("screens", s.screens);
  prefs.putBool("rain", s.rainAlert);
  prefs.putUChar("rainh", s.rainHour);
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
  o["lat"]          = s.lat;
  o["lon"]          = s.lon;
  o["place"]        = s.place;
  o["weatherMin"]   = s.weatherMin;
  o["quotesMin"]    = s.quotesMin;
  o["quotesAtNight"] = s.quotesAtNight;
  o["quotes"]       = s.quotes;
  o["screens"]      = s.screens;
  o["rainAlert"]    = s.rainAlert;
  o["rainHour"]     = s.rainHour;
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
  if (o["place"].is<const char*>()) {
    const char* p = o["place"];
    if (strcmp(p, s.place) != 0) {
      strlcpy(s.place, p, PLACE_LEN);
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
  changed |= setIf(o, "lat", s.lat);
  changed |= setIf(o, "lon", s.lon);
  changed |= setIf(o, "weatherMin", s.weatherMin);
  changed |= setIf(o, "quotesMin", s.quotesMin);
  changed |= setIf(o, "quotesAtNight", s.quotesAtNight);
  changed |= setIf(o, "quotes", s.quotes);
  changed |= setIf(o, "screens", s.screens);
  changed |= setIf(o, "rainAlert", s.rainAlert);
  changed |= setIf(o, "rainHour", s.rainHour);
  clamp();
  return changed;
}

}  // namespace cfg
