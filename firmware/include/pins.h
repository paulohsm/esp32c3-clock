#pragma once
#include <Arduino.h>

// Matriz MAX7219 32x8 (4 módulos 8x8 em cascata)
constexpr uint8_t PIN_MTX_DIN = 6;
constexpr uint8_t PIN_MTX_CS  = 7;
constexpr uint8_t PIN_MTX_CLK = 4;
constexpr uint8_t MTX_DEVICES = 4;

// Entradas
constexpr uint8_t PIN_TOUCH    = 5;   // TTP223: HIGH quando tocado
constexpr uint8_t PIN_BOOT_BTN = 9;   // botão BOOT da placa: LOW quando pressionado

// Saídas
constexpr uint8_t PIN_BUZZER = 1;     // buzzer passivo (PWM/LEDC)
constexpr uint8_t PIN_LED    = 8;     // LED azul da placa
constexpr bool    LED_ACTIVE_LOW = true;  // LOW = aceso
