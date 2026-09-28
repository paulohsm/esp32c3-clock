#include "config.h"
#include <Preferences.h>

namespace cfg {

Settings s;
static Preferences prefs;
static const char* NS = "clock";

void load() {
  prefs.begin(NS, false);
  s.brightness   = prefs.getUChar("bri", BRIGHT_DEFAULT);
  s.rotated      = prefs.getBool("rot", false);
  s.hourlyBeep   = prefs.getBool("hbeep", true);
  s.timbre       = prefs.getUChar("timbre", 0);
  s.volume       = prefs.getUChar("vol", 3);
  s.nightEnabled = prefs.getBool("night", true);
  s.nightStart   = prefs.getUChar("nstart", 22);
  s.nightEnd     = prefs.getUChar("nend", 6);
  prefs.end();

  // Sanidade: nunca aceitar valores fora dos limites.
  if (s.brightness > BRIGHT_MAX) s.brightness = BRIGHT_MAX;
  if (s.timbre >= TIMBRE_COUNT) s.timbre = 0;
  if (s.volume < 1 || s.volume > VOLUME_MAX) s.volume = 3;
  if (s.nightStart > 23) s.nightStart = 22;
  if (s.nightEnd > 23) s.nightEnd = 6;
}

void save() {
  prefs.begin(NS, false);
  prefs.putUChar("bri", s.brightness);
  prefs.putBool("rot", s.rotated);
  prefs.putBool("hbeep", s.hourlyBeep);
  prefs.putUChar("timbre", s.timbre);
  prefs.putUChar("vol", s.volume);
  prefs.putBool("night", s.nightEnabled);
  prefs.putUChar("nstart", s.nightStart);
  prefs.putUChar("nend", s.nightEnd);
  prefs.end();
}

}  // namespace cfg
