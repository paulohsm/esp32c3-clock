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

static char buf[200];
static bool scrolling = false;
static uint8_t colonOff[9] = {0};  // [0] = largura, depois colunas (todas apagadas)

namespace display {

void begin() {
  P.begin();
  P.setIntensity(0);
  P.setCharSpacing(1);
  P.displayClear();

  // Cria um caractere "vazio" com a mesma largura do ':'.
  uint8_t cols[8] = {0};
  uint8_t w = P.getGraphicObject()->getChar(':', sizeof(cols), cols);
  if (w == 0 || w > 8) w = 1;
  colonOff[0] = w;
  P.addChar(CHAR_COLON_OFF, colonOff);

  buf[0] = '\0';
}

void setBrightness(uint8_t level) {
  if (level > cfg::BRIGHT_MAX) level = cfg::BRIGHT_MAX;
  P.setIntensity(level);
}

void setRotated(bool rotated) {
  // Espelhar nos dois eixos = girar 180°.
  P.setZoneEffect(0, rotated, PA_FLIP_UD);
  P.setZoneEffect(0, rotated, PA_FLIP_LR);
  P.displayReset();  // redesenha o conteúdo atual já na nova orientação
}

void showStatic(const char* text) {
  if (!scrolling && strcmp(buf, text) == 0) return;
  scrolling = false;
  strlcpy(buf, text, sizeof(buf));
  P.displayText(buf, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  // Desenha já, para funcionar também em trechos bloqueantes (ex.: portal Wi-Fi).
  for (uint8_t i = 0; i < 50 && !P.displayAnimate(); i++) {
    delay(1);
  }
}

void scroll(const char* text, uint16_t speedMs) {
  scrolling = true;
  strlcpy(buf, text, sizeof(buf));
  P.displayText(buf, PA_LEFT, speedMs, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

bool isScrolling() { return scrolling; }

bool update() {
  bool done = P.displayAnimate();
  if (scrolling && done) {
    scrolling = false;
    buf[0] = '\0';
    return true;
  }
  return false;
}

}  // namespace display
