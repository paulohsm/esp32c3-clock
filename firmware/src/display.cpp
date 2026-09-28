#include "display.h"
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include "config.h"
#include "pins.h"

// Most blue 4-in-1 modules are FC16_HW. If characters look mirrored, scrambled
// or sideways, try GENERIC_HW or ICSTATION_HW.
#define HW_TYPE MD_MAX72XX::FC16_HW

// Software SPI: works on any C3 pins.
static MD_Parola P(HW_TYPE, PIN_MTX_DIN, PIN_MTX_CLK, PIN_MTX_CS, MTX_DEVICES);
static MD_MAX72XX* mx = nullptr;

static char textBuf[64];
static bool textMode = true;   // true = MD_Parola owns the matrix
static bool rotated  = false;

// Framebuffer: fb[x] = column x (0 = left); bit y = row y (0 = top).
static uint8_t fb[display::WIDTH];
static uint8_t shown[display::WIDTH];
static bool fbDirty = true;

// ------------------------------------------------------------ 4x6 font
// Each row uses 4 bits; bit 3 = leftmost column. 'w' = width in columns.
struct Glyph {
  char c;
  uint8_t w;
  uint8_t rows[6];
};

static const Glyph FONT[] = {
  {'0', 4, {0b0110, 0b1001, 0b1011, 0b1101, 0b1001, 0b0110}},
  {'1', 4, {0b0010, 0b0110, 0b1010, 0b0010, 0b0010, 0b1111}},
  {'2', 4, {0b0110, 0b1001, 0b0001, 0b0010, 0b0100, 0b1111}},
  {'3', 4, {0b1110, 0b0001, 0b0110, 0b0001, 0b0001, 0b1110}},
  {'4', 4, {0b1001, 0b1001, 0b1001, 0b1111, 0b0001, 0b0001}},
  {'5', 4, {0b1111, 0b1000, 0b1110, 0b0001, 0b0001, 0b1110}},
  {'6', 4, {0b0110, 0b1000, 0b1110, 0b1001, 0b1001, 0b0110}},
  {'7', 4, {0b1111, 0b0001, 0b0010, 0b0100, 0b0100, 0b0100}},
  {'8', 4, {0b0110, 0b1001, 0b0110, 0b1001, 0b1001, 0b0110}},
  {'9', 4, {0b0110, 0b1001, 0b1001, 0b0111, 0b0001, 0b0110}},
  {'A', 4, {0b0110, 0b1001, 0b1001, 0b1111, 0b1001, 0b1001}},
  {'B', 4, {0b1110, 0b1001, 0b1110, 0b1001, 0b1001, 0b1110}},
  {'C', 4, {0b0111, 0b1000, 0b1000, 0b1000, 0b1000, 0b0111}},
  {'D', 4, {0b1110, 0b1001, 0b1001, 0b1001, 0b1001, 0b1110}},
  {'E', 4, {0b1111, 0b1000, 0b1110, 0b1000, 0b1000, 0b1111}},
  {'F', 4, {0b1111, 0b1000, 0b1110, 0b1000, 0b1000, 0b1000}},
  {'G', 4, {0b0111, 0b1000, 0b1000, 0b1011, 0b1001, 0b0111}},
  {'H', 4, {0b1001, 0b1001, 0b1111, 0b1001, 0b1001, 0b1001}},
  {'I', 4, {0b1110, 0b0100, 0b0100, 0b0100, 0b0100, 0b1110}},
  {'J', 4, {0b0001, 0b0001, 0b0001, 0b0001, 0b1001, 0b0110}},
  {'K', 4, {0b1001, 0b1010, 0b1100, 0b1010, 0b1001, 0b1001}},
  {'L', 4, {0b1000, 0b1000, 0b1000, 0b1000, 0b1000, 0b1111}},
  {'M', 4, {0b1001, 0b1111, 0b1111, 0b1001, 0b1001, 0b1001}},
  {'N', 4, {0b1001, 0b1101, 0b1101, 0b1011, 0b1011, 0b1001}},
  {'O', 4, {0b0110, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}},
  {'P', 4, {0b1110, 0b1001, 0b1001, 0b1110, 0b1000, 0b1000}},
  {'Q', 4, {0b0110, 0b1001, 0b1001, 0b1001, 0b1010, 0b0101}},
  {'R', 4, {0b1110, 0b1001, 0b1001, 0b1110, 0b1010, 0b1001}},
  {'S', 4, {0b0111, 0b1000, 0b0110, 0b0001, 0b0001, 0b1110}},
  {'T', 4, {0b1111, 0b0100, 0b0100, 0b0100, 0b0100, 0b0100}},
  {'U', 4, {0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}},
  {'V', 4, {0b1001, 0b1001, 0b1001, 0b1001, 0b0110, 0b0110}},
  {'W', 4, {0b1001, 0b1001, 0b1001, 0b1111, 0b1111, 0b1001}},
  {'X', 4, {0b1001, 0b1001, 0b0110, 0b0110, 0b1001, 0b1001}},
  {'Y', 4, {0b1001, 0b1001, 0b0111, 0b0001, 0b0001, 0b0110}},
  {'Z', 4, {0b1111, 0b0001, 0b0010, 0b0100, 0b1000, 0b1111}},
  {' ', 2, {0, 0, 0, 0, 0, 0}},
  {':', 1, {0b0000, 0b1000, 0b0000, 0b0000, 0b1000, 0b0000}},
  {'.', 1, {0b0000, 0b0000, 0b0000, 0b0000, 0b0000, 0b1000}},
  {',', 2, {0b0000, 0b0000, 0b0000, 0b0000, 0b0100, 0b1000}},
  {'/', 3, {0b0010, 0b0010, 0b0100, 0b0100, 0b1000, 0b1000}},
  {'-', 3, {0b0000, 0b0000, 0b1110, 0b0000, 0b0000, 0b0000}},
  {'+', 3, {0b0000, 0b0100, 0b1110, 0b0100, 0b0000, 0b0000}},
  {'%', 4, {0b1001, 0b0001, 0b0010, 0b0100, 0b1000, 0b1001}},
  {'$', 4, {0b0111, 0b1010, 0b0110, 0b0101, 0b1110, 0b0100}},
  {'*', 3, {0b1110, 0b1010, 0b1110, 0b0000, 0b0000, 0b0000}},  // use as "°"
  {'!', 1, {0b1000, 0b1000, 0b1000, 0b1000, 0b0000, 0b1000}},
  {'?', 4, {0b0110, 0b1001, 0b0010, 0b0100, 0b0000, 0b0100}},
};

static const Glyph* findGlyph(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;  // uppercase only
  for (const auto& g : FONT) {
    if (g.c == c) return &g;
  }
  return findGlyph('?');
}

// ------------------------------------------------ UTF-8 → plain ASCII
// The 5x7 font is ASCII only, so accented Portuguese letters lose the accent.
static void utf8ToAscii(const char* in, char* out, size_t outLen) {
  size_t o = 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(in);
  while (*p && o + 1 < outLen) {
    uint8_t c = *p;
    if (c < 0x80) {
      out[o++] = (c >= 0x20) ? c : ' ';
      p++;
      continue;
    }
    if (c == 0xC3 && p[1]) {
      uint8_t d = p[1];
      char r = '?';
      if (d >= 0x80 && d <= 0x85) r = 'A';
      else if (d == 0x87) r = 'C';
      else if (d >= 0x88 && d <= 0x8B) r = 'E';
      else if (d >= 0x8C && d <= 0x8F) r = 'I';
      else if (d == 0x91) r = 'N';
      else if (d >= 0x92 && d <= 0x96) r = 'O';
      else if (d >= 0x99 && d <= 0x9C) r = 'U';
      else if (d >= 0xA0 && d <= 0xA5) r = 'a';
      else if (d == 0xA7) r = 'c';
      else if (d >= 0xA8 && d <= 0xAB) r = 'e';
      else if (d >= 0xAC && d <= 0xAF) r = 'i';
      else if (d == 0xB1) r = 'n';
      else if (d >= 0xB2 && d <= 0xB6) r = 'o';
      else if (d >= 0xB9 && d <= 0xBC) r = 'u';
      out[o++] = r;
      p += 2;
      continue;
    }
    if (c == 0xC2 && p[1] == 0xB0) {  // degree sign
      out[o++] = 'o';
      p += 2;
      continue;
    }
    // Any other multi-byte sequence (emoji etc.): skip it.
    p++;
    while ((*p & 0xC0) == 0x80) p++;
  }
  out[o] = '\0';
}

// ------------------------------------------------------------- scroller
static uint8_t scol[1600];   // pre-rendered text columns
static uint16_t sLen = 0;
static int16_t sPos = 0;     // position of text column 0 relative to the area's left edge
static uint8_t sAreaX = 0, sAreaW = display::WIDTH;
static uint8_t sLoops = 1;
static bool sInfinite = false, sActive = false;
static const uint8_t* const* sFrames = nullptr;
static uint8_t sFrameCount = 1, sFrame = 0;
static uint16_t sSpeed = 35, sFramePeriod = 300;
static uint32_t sLastStep = 0, sLastFrame = 0;

static uint8_t reverseBits(uint8_t v) {
  v = (v & 0xF0) >> 4 | (v & 0x0F) << 4;
  v = (v & 0xCC) >> 2 | (v & 0x33) << 2;
  v = (v & 0xAA) >> 1 | (v & 0x55) << 1;
  return v;
}

namespace display {

static void renderScroll() {
  fbClear();
  if (sFrames) fbIcon(0, sFrames[sFrame]);
  for (int x = 0; x < sAreaW; x++) {
    int i = x - sPos;
    if (i >= 0 && i < sLen) fb[sAreaX + x] = scol[i];
  }
  fbPush();
}

void begin() {
  P.begin();
  mx = P.getGraphicObject();
  P.setIntensity(0);
  P.setCharSpacing(1);
  P.displayClear();
  textBuf[0] = '\0';
  memset(fb, 0, sizeof(fb));
  memset(shown, 0, sizeof(shown));
}

void setBrightness(uint8_t level) {
  if (level > cfg::BRIGHT_MAX) level = cfg::BRIGHT_MAX;
  P.setIntensity(level);
}

void setRotated(bool r) {
  rotated = r;
  // Text mode: flipping both axes = rotating 180°.
  P.setZoneEffect(0, r, PA_FLIP_UD);
  P.setZoneEffect(0, r, PA_FLIP_LR);
  if (textMode) {
    P.displayReset();
    for (uint8_t i = 0; i < 50 && !P.displayAnimate(); i++) delay(1);
  } else {
    fbDirty = true;
    fbPush();
  }
}

// ------------------------------------------------------------ TEXT mode

void showStatic(const char* text) {
  if (textMode && strcmp(textBuf, text) == 0) return;
  textMode = true;
  sActive = false;
  strlcpy(textBuf, text, sizeof(textBuf));
  P.displayText(textBuf, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  // Draw right away so it also works inside blocking code (e.g. Wi-Fi portal).
  for (uint8_t i = 0; i < 50 && !P.displayAnimate(); i++) delay(1);
}

// ---------------------------------------------------------- CANVAS mode

void fbClear() { memset(fb, 0, sizeof(fb)); }

void fbPixel(int x, int y, bool on) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
  if (on) fb[x] |= (1 << y);
  else fb[x] &= ~(1 << y);
}

void fbIcon(int x, const uint8_t icon[8]) {
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      if (icon[row] & (0x80 >> col)) fbPixel(x + col, row);
    }
  }
}

uint8_t fbTextWidth(const char* text) {
  uint8_t w = 0;
  for (const char* p = text; *p; p++) {
    w += findGlyph(*p)->w;
    if (p[1]) w += 1;  // spacing between characters
  }
  return w;
}

void fbText(int x, int y, const char* text) {
  for (const char* p = text; *p; p++) {
    const Glyph* g = findGlyph(*p);
    for (int row = 0; row < 6; row++) {
      for (int col = 0; col < g->w; col++) {
        if (g->rows[row] & (0b1000 >> col)) fbPixel(x + col, y + row);
      }
    }
    x += g->w + 1;
  }
}

void fbPush() {
  bool wasText = textMode;
  if (textMode) {
    textMode = false;
    textBuf[0] = '\0';
    P.displayClear();
  }
  if (!wasText && !fbDirty && memcmp(fb, shown, sizeof(fb)) == 0) return;

  // MD_MAX72XX: column 0 is the RIGHTMOST column of the matrix.
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  for (uint8_t x = 0; x < WIDTH; x++) {
    if (rotated) mx->setColumn(x, reverseBits(fb[x]));
    else mx->setColumn(WIDTH - 1 - x, fb[x]);
  }
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

  memcpy(shown, fb, sizeof(fb));
  fbDirty = false;
}

// ------------------------------------------------------------- scroller

void scrollStart(const char* utf8Text, const uint8_t* const* iconFrames, uint8_t frameCount,
                 uint8_t loops, uint16_t speedMs, uint16_t framePeriodMs) {
  static char ascii[256];
  utf8ToAscii(utf8Text, ascii, sizeof(ascii));

  sLen = 0;
  uint8_t cbuf[8];
  for (const char* p = ascii; *p; p++) {
    uint8_t w = mx->getChar(static_cast<uint8_t>(*p), sizeof(cbuf), cbuf);
    if (sLen + w + 1 >= sizeof(scol)) break;
    memcpy(scol + sLen, cbuf, w);
    sLen += w;
    scol[sLen++] = 0;  // 1-column spacing
  }

  sFrames = iconFrames;
  sFrameCount = frameCount ? frameCount : 1;
  sFrame = 0;
  sAreaX = iconFrames ? CONTENT_X : 0;
  sAreaW = WIDTH - sAreaX;
  sPos = sAreaW;  // start just past the right edge
  sLoops = loops;
  sInfinite = (loops == 0);
  sSpeed = speedMs;
  sFramePeriod = framePeriodMs;
  sLastStep = sLastFrame = millis();
  sActive = true;
  renderScroll();
}

void scrollStop() { sActive = false; }

bool isScrolling() { return sActive; }

bool update() {
  if (textMode) {
    P.displayAnimate();
    return false;
  }
  if (!sActive) return false;

  uint32_t now = millis();
  bool dirty = false;
  if (now - sLastStep >= sSpeed) {
    sLastStep = now;
    sPos--;
    dirty = true;
    if (sPos + static_cast<int>(sLen) <= 0) {  // text left the area
      if (sInfinite || --sLoops > 0) {
        sPos = sAreaW;
      } else {
        sActive = false;
        return true;
      }
    }
  }
  if (sFrames && sFrameCount > 1 && now - sLastFrame >= sFramePeriod) {
    sLastFrame = now;
    sFrame = (sFrame + 1) % sFrameCount;
    dirty = true;
  }
  if (dirty) renderScroll();
  return false;
}

}  // namespace display
