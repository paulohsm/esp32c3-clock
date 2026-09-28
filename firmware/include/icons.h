#pragma once
#include <Arduino.h>

// 8x8 icons. One byte per row (top to bottom); bit 7 = leftmost column.
namespace icons {

using Icon = uint8_t[8];

// Round dial outline (used by the dynamic clock icons).
constexpr Icon DIAL_RING = {
  0b00111100,
  0b01000010,
  0b10000001,
  0b10000001,
  0b10000001,
  0b10000001,
  0b01000010,
  0b00111100,
};

// Static clock face with hands.
constexpr Icon CLOCK = {
  0b00111100,
  0b01000010,
  0b10010001,
  0b10010001,
  0b10011101,
  0b10000001,
  0b01000010,
  0b00111100,
};

constexpr Icon CALENDAR = {
  0b01000010,
  0b11111111,
  0b11111111,
  0b10000001,
  0b10100101,
  0b10000001,
  0b10100101,
  0b11111111,
};

constexpr Icon ENVELOPE = {
  0b00000000,
  0b11111111,
  0b11000011,
  0b10100101,
  0b10011001,
  0b10000001,
  0b11111111,
  0b00000000,
};

// Bell swinging: two frames for the alarm animation.
constexpr Icon BELL_L = {
  0b00001000,
  0b00011100,
  0b00111100,
  0b00111100,
  0b01111100,
  0b11111110,
  0b00000000,
  0b00010000,
};
constexpr Icon BELL_R = {
  0b00010000,
  0b00111000,
  0b00111100,
  0b00111100,
  0b00111110,
  0b01111111,
  0b00000000,
  0b00001000,
};

constexpr Icon WIFI = {
  0b00000000,
  0b00111100,
  0b01000010,
  0b10011001,
  0b00100100,
  0b00000000,
  0b00011000,
  0b00011000,
};

constexpr Icon NOTE = {
  0b00001111,
  0b00001001,
  0b00001001,
  0b00001001,
  0b00001001,
  0b01101011,
  0b11110011,
  0b01100000,
};

constexpr Icon GEAR = {
  0b00011000,
  0b01011010,
  0b00111100,
  0b11100111,
  0b11100111,
  0b00111100,
  0b01011010,
  0b00011000,
};

}  // namespace icons
