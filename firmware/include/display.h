#pragma once
#include <Arduino.h>

// Two drawing paths:
//  • TEXT   (MD_Parola): static full-width text, used only during boot / Wi-Fi setup.
//  • CANVAS (own 32x8 framebuffer): icons, 4x6 fixed-width font and the scroller.
namespace display {

constexpr uint8_t WIDTH  = 32;
constexpr uint8_t HEIGHT = 8;
constexpr uint8_t CONTENT_X = 9;                    // icon 0–7, gap 8, content 9–31
constexpr uint8_t CONTENT_W = WIDTH - CONTENT_X;    // 23 columns

void begin();
void setBrightness(uint8_t level);   // capped at cfg::BRIGHT_MAX
void setRotated(bool rotated);       // true = rotated 180°

// ---- TEXT mode ----
void showStatic(const char* text);   // centered static text, drawn immediately

// ---- CANVAS mode ----
void fbClear();
void fbPixel(int x, int y, bool on = true);
void fbIcon(int x, const uint8_t icon[8]);
uint8_t fbTextWidth(const char* text);         // width in columns (4x6 font)
void fbText(int x, int y, const char* text);   // draw with the 4x6 font
void fbPush();                                 // send to the matrix (only if changed)

// ---- Scroller: 5x7 font text scrolling right→left, optionally beside an icon ----
// iconFrames: nullptr for full width, or an array of 8x8 icons animated every framePeriodMs.
// loops: how many passes (0 = until scrollStop()).
void scrollStart(const char* utf8Text, const uint8_t* const* iconFrames = nullptr,
                 uint8_t frameCount = 1, uint8_t loops = 1, uint16_t speedMs = 35,
                 uint16_t framePeriodMs = 300);
void scrollStop();
bool isScrolling();

// Call on every loop(). Returns true once when a scroll finishes on its own.
bool update();

}  // namespace display
