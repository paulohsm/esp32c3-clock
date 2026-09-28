#pragma once
#include <Arduino.h>

// Ícones 8x8. Cada byte é uma linha (de cima para baixo); bit 7 = coluna da esquerda.
namespace icons {

// Contorno do mostrador redondo (usado pelos ícones dinâmicos da hora).
constexpr uint8_t DIAL_RING[8] = {
  0b00111100,
  0b01000010,
  0b10000001,
  0b10000001,
  0b10000001,
  0b10000001,
  0b01000010,
  0b00111100,
};

// Relógio estático com ponteiros.
constexpr uint8_t CLOCK[8] = {
  0b00111100,
  0b01000010,
  0b10010001,
  0b10010001,
  0b10011101,
  0b10000001,
  0b01000010,
  0b00111100,
};

// Folhinha de calendário.
constexpr uint8_t CALENDAR[8] = {
  0b01000010,
  0b11111111,
  0b11111111,
  0b10000001,
  0b10100101,
  0b10000001,
  0b10100101,
  0b11111111,
};

}  // namespace icons
