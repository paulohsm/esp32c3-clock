#pragma once
#include <Arduino.h>

// MAX7219 32x8 matrix (4 cascaded 8x8 modules)
constexpr uint8_t PIN_MTX_DIN = 6;
constexpr uint8_t PIN_MTX_CS  = 7;
constexpr uint8_t PIN_MTX_CLK = 4;
constexpr uint8_t MTX_DEVICES = 4;

// Inputs
constexpr uint8_t PIN_TOUCH    = 5;   // TTP223: HIGH while touched
constexpr uint8_t PIN_BOOT_BTN = 9;   // on-board BOOT button: LOW while pressed

// Outputs
constexpr uint8_t PIN_BUZZER = 1;     // passive buzzer (PWM/LEDC)
constexpr uint8_t PIN_LED    = 8;     // on-board blue LED
constexpr bool    LED_ACTIVE_LOW = true;  // LOW = on
