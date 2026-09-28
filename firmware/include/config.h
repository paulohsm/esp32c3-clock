#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Persistent settings (stored in flash NVS).
namespace cfg {

constexpr uint8_t BRIGHT_MAX       = 6;   // brightness cap (MAX7219 scale 0..15) — spares the USB supply
constexpr uint8_t BRIGHT_DEFAULT   = 2;
constexpr uint8_t TIMBRE_COUNT     = 4;
constexpr uint8_t VOLUME_MAX       = 5;
constexpr uint8_t CLOCK_ICON_COUNT = 3;
constexpr size_t  NAME_LEN         = 24;

struct Settings {
  char    name[NAME_LEN] = "Clock";  // friendly name shown in the app
  uint8_t brightness   = BRIGHT_DEFAULT;  // 0..BRIGHT_MAX
  bool    rotated      = false;           // false = normal, true = rotated 180°
  bool    hourlyBeep   = true;            // chime on every full hour
  uint8_t timbre       = 0;               // 0..TIMBRE_COUNT-1
  uint8_t volume       = 3;               // 1..VOLUME_MAX
  bool    nightEnabled = true;            // automatic night mode
  uint8_t nightStart   = 22;              // night mode start hour
  uint8_t nightEnd     = 6;               // night mode end hour
  uint8_t clockIcon    = 0;               // 0 day pie, 1 static clock, 2 day quadrant
};

extern Settings s;

void load();
void save();
void clamp();                             // force every field into its valid range

void toJson(JsonObject obj);              // full settings → JSON
bool fromJson(JsonObjectConst obj);       // partial update from JSON; true if anything changed

}  // namespace cfg
