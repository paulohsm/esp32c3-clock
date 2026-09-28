#pragma once
#include <Arduino.h>

// Caractere especial: "dois-pontos apagado" (mesma largura do ':'),
// usado para piscar o separador do relógio sem deslocar os dígitos.
constexpr char CHAR_COLON_OFF = '\x01';

namespace display {

void begin();
void setBrightness(uint8_t level);   // limitado a cfg::BRIGHT_MAX
void setRotated(bool rotated);       // true = invertida 180°

// Texto fixo centralizado. Só redesenha se o texto mudou (sem cintilação).
void showStatic(const char* text);

// Texto rolando da direita para a esquerda (uma passagem).
void scroll(const char* text, uint16_t speedMs = 35);

bool isScrolling();

// Chamar em todo loop(). Retorna true uma única vez quando um scroll termina.
bool update();

}  // namespace display
