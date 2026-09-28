#include "display.h"
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include "config.h"
#include "pins.h"

// A maioria dos módulos 4-em-1 azuis é FC16_HW. Se os caracteres aparecerem
// espelhados, embaralhados ou "de lado", teste GENERIC_HW ou ICSTATION_HW.
#define HW_TYPE MD_MAX72XX::FC16_HW

// SPI por software: funciona em quaisquer pinos do C3.
static MD_Parola P(HW_TYPE, PIN_MTX_DIN, PIN_MTX_CLK, PIN_MTX_CS, MTX_DEVICES);
static MD_MAX72XX* mx = nullptr;

static char buf[200];
static bool scrolling = false;
static bool textMode  = true;   // true = MD_Parola manda na matriz
static bool rotated   = false;

// Framebuffer: fb[x] = coluna x (0 = esquerda); bit y = linha y (0 = topo).
static uint8_t fb[display::WIDTH];
static uint8_t shown[display::WIDTH];
static bool fbDirty = true;

// ------------------------------------------------------------ fonte 4x6
// Cada linha usa 4 bits; bit 3 = coluna da esquerda. 'w' = largura em colunas.
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
  {'*', 3, {0b1110, 0b1010, 0b1110, 0b0000, 0b0000, 0b0000}},  // usar como "°"
  {'!', 1, {0b1000, 0b1000, 0b1000, 0b1000, 0b0000, 0b1000}},
  {'?', 4, {0b0110, 0b1001, 0b0010, 0b0100, 0b0000, 0b0100}},
};

static const Glyph* findGlyph(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;  // só maiúsculas
  for (const auto& g : FONT) {
    if (g.c == c) return &g;
  }
  return findGlyph('?');
}

// ------------------------------------------------------------- matriz

static uint8_t reverseBits(uint8_t v) {
  v = (v & 0xF0) >> 4 | (v & 0x0F) << 4;
  v = (v & 0xCC) >> 2 | (v & 0x33) << 2;
  v = (v & 0xAA) >> 1 | (v & 0x55) << 1;
  return v;
}

namespace display {

void begin() {
  P.begin();
  mx = P.getGraphicObject();
  P.setIntensity(0);
  P.setCharSpacing(1);
  P.displayClear();
  buf[0] = '\0';
  memset(fb, 0, sizeof(fb));
  memset(shown, 0, sizeof(shown));
}

void setBrightness(uint8_t level) {
  if (level > cfg::BRIGHT_MAX) level = cfg::BRIGHT_MAX;
  P.setIntensity(level);
}

void setRotated(bool r) {
  rotated = r;
  // Modo texto: espelhar nos dois eixos = girar 180°.
  P.setZoneEffect(0, r, PA_FLIP_UD);
  P.setZoneEffect(0, r, PA_FLIP_LR);
  if (textMode) {
    P.displayReset();  // redesenha o texto atual na nova orientação
  } else {
    fbDirty = true;
    fbPush();
  }
}

// ---------------------------------------------------------- modo TEXTO

void showStatic(const char* text) {
  if (textMode && !scrolling && strcmp(buf, text) == 0) return;
  textMode = true;
  scrolling = false;
  strlcpy(buf, text, sizeof(buf));
  P.displayText(buf, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  // Desenha já, para funcionar também em trechos bloqueantes (ex.: portal Wi-Fi).
  for (uint8_t i = 0; i < 50 && !P.displayAnimate(); i++) {
    delay(1);
  }
}

void scroll(const char* text, uint16_t speedMs) {
  textMode = true;
  scrolling = true;
  strlcpy(buf, text, sizeof(buf));
  P.displayText(buf, PA_LEFT, speedMs, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

bool isScrolling() { return scrolling; }

bool update() {
  if (!textMode) return false;
  bool done = P.displayAnimate();
  if (scrolling && done) {
    scrolling = false;
    buf[0] = '\0';
    return true;
  }
  return false;
}

// ---------------------------------------------------------- modo QUADRO

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
    if (p[1]) w += 1;  // espaço entre caracteres
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
    // Sai do modo texto: interrompe qualquer animação do Parola.
    textMode = false;
    scrolling = false;
    buf[0] = '\0';
    P.displayClear();
  }
  if (!wasText && !fbDirty && memcmp(fb, shown, sizeof(fb)) == 0) return;

  // MD_MAX72XX: coluna 0 fica na extremidade DIREITA da matriz.
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  for (uint8_t x = 0; x < WIDTH; x++) {
    if (rotated) mx->setColumn(x, reverseBits(fb[x]));
    else mx->setColumn(WIDTH - 1 - x, fb[x]);
  }
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

  memcpy(shown, fb, sizeof(fb));
  fbDirty = false;
}

}  // namespace display
