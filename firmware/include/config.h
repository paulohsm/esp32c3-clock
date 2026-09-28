#pragma once
#include <Arduino.h>

// Configurações persistentes (gravadas na NVS da flash).
namespace cfg {

constexpr uint8_t BRIGHT_MAX     = 6;   // teto de brilho (escala 0..15 do MAX7219) — poupa a USB
constexpr uint8_t BRIGHT_DEFAULT = 2;
constexpr uint8_t TIMBRE_COUNT   = 4;
constexpr uint8_t VOLUME_MAX     = 5;
constexpr uint8_t CLOCK_ICON_COUNT = 3;

struct Settings {
  uint8_t brightness   = BRIGHT_DEFAULT;  // 0..BRIGHT_MAX
  bool    rotated      = false;           // false = normal, true = invertida 180°
  bool    hourlyBeep   = true;            // bipe a cada hora cheia
  uint8_t timbre       = 0;               // 0..TIMBRE_COUNT-1
  uint8_t volume       = 3;               // 1..VOLUME_MAX
  bool    nightEnabled = true;            // modo noite automático
  uint8_t nightStart   = 22;              // hora de início do modo noite
  uint8_t nightEnd     = 6;               // hora de fim do modo noite
  uint8_t clockIcon    = 0;               // 0 pizza do dia, 1 relógio estático, 2 quadrante
  bool    secondsBar   = true;            // barrinha de segundos na linha de baixo
};

extern Settings s;

void load();
void save();

}  // namespace cfg
