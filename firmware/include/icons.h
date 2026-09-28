#pragma once
#include <Arduino.h>

// Icons: 6x6 artwork centered in an 8x8 cell (1-pixel empty border).
// One byte per row (top to bottom); bit 7 = leftmost column.
namespace icons {

using Icon = uint8_t[8];

// Round dial outline (used by the dynamic clock icons).
constexpr Icon DIAL_RING = {
  0b00000000,
  0b00111100,
  0b01000010,
  0b01000010,
  0b01000010,
  0b01000010,
  0b00111100,
  0b00000000,
};

// Static clock face with hands.
constexpr Icon CLOCK = {
  0b00000000,
  0b00111100,
  0b01010010,
  0b01010010,
  0b01011010,
  0b01000010,
  0b00111100,
  0b00000000,
};

// Calendar page with binder rings.
constexpr Icon CALENDAR = {
  0b00000000,
  0b00100100,
  0b01111110,
  0b01000010,
  0b01011010,
  0b01000010,
  0b01111110,
  0b00000000,
};

// Envelope (messages).
constexpr Icon ENVELOPE = {
  0b00000000,
  0b01111110,
  0b01100110,
  0b01011010,
  0b01000010,
  0b01000010,
  0b01111110,
  0b00000000,
};

// Bell swinging: two frames for the alarm animation.
constexpr Icon BELL_L = {
  0b00000000,
  0b00010000,
  0b00111000,
  0b00111000,
  0b00111000,
  0b01111100,
  0b00010000,
  0b00000000,
};

constexpr Icon BELL_R = {
  0b00000000,
  0b00001000,
  0b00011100,
  0b00011100,
  0b00011100,
  0b00111110,
  0b00001000,
  0b00000000,
};

// Wi-Fi.
constexpr Icon WIFI = {
  0b00000000,
  0b00111100,
  0b01000010,
  0b00011000,
  0b00100100,
  0b00000000,
  0b00011000,
  0b00000000,
};

// Musical note (sound settings).
constexpr Icon NOTE = {
  0b00000000,
  0b00001100,
  0b00001010,
  0b00001000,
  0b00001000,
  0b00111000,
  0b00111000,
  0b00000000,
};

// Gear (settings).
constexpr Icon GEAR = {
  0b00000000,
  0b00100100,
  0b00111100,
  0b01100110,
  0b01100110,
  0b00111100,
  0b00100100,
  0b00000000,
};

}  // namespace icons
