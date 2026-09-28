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
constexpr size_t  PLACE_LEN        = 32;

// Quote selection bits (see feeds.h for codes): USD, EUR, GBP, BTC, ETH.
constexpr uint8_t QUOTES_ALL = 0x1F;
// Screen selection bits for the touch cycle (the clock itself is always on).
enum ScreenBit : uint8_t {
  SB_DATE = 1 << 0, SB_LONGDATE = 1 << 1, SB_WEATHER = 1 << 2, SB_RAIN = 1 << 3,
  SB_UV = 1 << 4, SB_SUN = 1 << 5, SB_QUOTES = 1 << 6,
};
constexpr uint8_t SCREENS_ALL = 0x7F;

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

  // Location for the weather (set from the phone's GPS). Default: Fortaleza.
  float   lat          = -3.7319f;
  float   lon          = -38.5267f;
  char    place[PLACE_LEN] = "Fortaleza";

  uint8_t weatherMin   = 10;              // weather refresh interval (minutes)
  uint8_t quotesMin    = 15;              // quotes refresh interval (minutes)
  bool    quotesAtNight = false;          // also refresh quotes during night mode
  uint8_t quotes       = 0b11011;         // USD, EUR, BTC, ETH
  uint8_t screens      = SCREENS_ALL;     // screens in the touch cycle
  bool    rainAlert    = true;            // morning "take an umbrella" warning
  uint8_t rainHour     = 7;               // hour of the rain warning

  // Motion
  bool     anim        = true;            // rolling digits, sliding screens, seconds dot
  uint8_t  scrollSpeed = 3;               // 1 (slow) .. 5 (fast)
  uint16_t autoEvery   = 60;              // carousel period in seconds (0 = off)
  uint8_t  autoFor     = 2;               // seconds each carousel screen stays
  uint8_t  autoScreens = SB_DATE;         // screens shown by the carousel
};

extern Settings s;

void load();
void save();
void clamp();                             // force every field into its valid range

void toJson(JsonObject obj);              // full settings → JSON
bool fromJson(JsonObjectConst obj);       // partial update from JSON; true if anything changed

}  // namespace cfg
