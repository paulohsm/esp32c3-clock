#pragma once
#include <Arduino.h>

// Passive buzzer driven by PWM (LEDC), non-blocking.
namespace sound {

void begin();
void update();                  // call on every loop()

void click();                   // short touch feedback
void chime();                   // hourly chime using the configured timbre
void chime(uint8_t timbre);     // chime with a specific timbre
void confirm();
void error();
bool isPlaying();

const char* timbreName(uint8_t index);

}  // namespace sound
