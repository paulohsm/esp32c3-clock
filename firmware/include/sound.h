#pragma once
#include <Arduino.h>

// Passive buzzer driven by PWM (LEDC). Sequences run on a timer, so they never
// get stuck on a tone while the main loop is busy.
namespace sound {

void begin();
void update();                  // no-op (kept so existing calls still compile)

void click();                   // short touch feedback
void chime();                   // hourly chime using the configured timbre
void chime(uint8_t timbre);     // chime with a specific timbre
void confirm();
void error();
void siren();                   // emergency siren, always at full volume
bool isPlaying();

const char* timbreName(uint8_t index);

}  // namespace sound
