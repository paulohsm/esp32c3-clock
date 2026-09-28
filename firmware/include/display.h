#pragma once
#include <Arduino.h>

// Dois modos de desenho:
//  • TEXTO  (MD_Parola): mensagens em tela cheia, fonte 5x7, com rolagem.
//  • QUADRO (framebuffer próprio 32x8): ícone + fonte 4x6 de largura fixa.
namespace display {

constexpr uint8_t WIDTH  = 32;
constexpr uint8_t HEIGHT = 8;

void begin();
void setBrightness(uint8_t level);   // limitado a cfg::BRIGHT_MAX
void setRotated(bool rotated);       // true = invertida 180°

// ---- modo TEXTO ----
void showStatic(const char* text);                 // texto fixo centralizado
void scroll(const char* text, uint16_t speedMs = 35);
bool isScrolling();

// Chamar em todo loop(). Retorna true uma única vez quando um scroll termina.
bool update();

// ---- modo QUADRO ----
void fbClear();
void fbPixel(int x, int y, bool on = true);
void fbIcon(int x, const uint8_t icon[8]);         // ícone 8x8 (ver icons.h)
uint8_t fbTextWidth(const char* text);             // largura em colunas na fonte 4x6
void fbText(int x, int y, const char* text);       // desenha na fonte 4x6
void fbPush();                                     // envia à matriz (só se mudou)

}  // namespace display
