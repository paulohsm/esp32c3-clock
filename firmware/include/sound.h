#pragma once
#include <Arduino.h>

// Buzzer passivo tocado por PWM (LEDC), sem bloquear o loop.
namespace sound {

void begin();
void update();                  // chamar em todo loop()

void click();                   // retorno curto de toque
void chime();                   // bipe de hora (depende do timbre escolhido)
void confirm();
void error();
bool isPlaying();

const char* timbreName(uint8_t index);

}  // namespace sound
