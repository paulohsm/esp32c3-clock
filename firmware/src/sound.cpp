#include "sound.h"
#include <esp_arduino_version.h>
#include "config.h"
#include "pins.h"

namespace {

struct Note {
  uint16_t freq;  // Hz (0 = pausa)
  uint16_t ms;    // duração; {0,0} encerra a sequência
};

// Timbres do bipe de hora
const Note T_CLASSICO[]  = {{2000, 70}, {0, 60}, {2000, 70}, {0, 0}};
const Note T_AGUDO[]     = {{2700, 160}, {0, 0}};
const Note T_SUAVE[]     = {{880, 120}, {0, 40}, {660, 180}, {0, 0}};
const Note T_CARRILHAO[] = {{1319, 180}, {1047, 180}, {1175, 180}, {784, 380}, {0, 0}};

const Note* const TIMBRES[cfg::TIMBRE_COUNT] = {T_CLASSICO, T_AGUDO, T_SUAVE, T_CARRILHAO};
const char* const TIMBRE_NAMES[cfg::TIMBRE_COUNT] = {"classico", "agudo", "suave", "carrilhao"};

const Note S_CONFIRM[] = {{1500, 50}, {0, 30}, {2200, 80}, {0, 0}};
const Note S_ERROR[]   = {{400, 220}, {0, 0}};
Note s_click[] = {{2000, 25}, {0, 0}};

// Volume = largura do pulso (duty) em 8 bits. 128 = 50% = mais alto.
const uint8_t DUTY[cfg::VOLUME_MAX] = {3, 8, 20, 50, 128};

const Note* seq = nullptr;
uint8_t idx = 0;
uint32_t noteStart = 0;

#if ESP_ARDUINO_VERSION_MAJOR < 3
constexpr uint8_t LEDC_CH = 0;
#endif

void output(uint16_t freq) {
  uint8_t duty = DUTY[cfg::s.volume - 1];
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (freq == 0) { ledcWrite(PIN_BUZZER, 0); return; }
  ledcChangeFrequency(PIN_BUZZER, freq, 8);
  ledcWrite(PIN_BUZZER, duty);
#else
  if (freq == 0) { ledcWrite(LEDC_CH, 0); return; }
  ledcChangeFrequency(LEDC_CH, freq, 8);
  ledcWrite(LEDC_CH, duty);
#endif
}

void start(const Note* s) {
  seq = s;
  idx = 0;
  noteStart = millis();
  output(seq[0].freq);
}

}  // namespace

namespace sound {

void begin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BUZZER, 2000, 8);
#else
  ledcSetup(LEDC_CH, 2000, 8);
  ledcAttachPin(PIN_BUZZER, LEDC_CH);
#endif
  output(0);
}

void update() {
  if (!seq) return;
  if (millis() - noteStart < seq[idx].ms) return;
  idx++;
  if (seq[idx].freq == 0 && seq[idx].ms == 0) {  // fim
    output(0);
    seq = nullptr;
    return;
  }
  noteStart = millis();
  output(seq[idx].freq);
}

void click() {
  s_click[0].freq = TIMBRES[cfg::s.timbre][0].freq;
  start(s_click);
}

void chime()   { start(TIMBRES[cfg::s.timbre]); }
void confirm() { start(S_CONFIRM); }
void error()   { start(S_ERROR); }
bool isPlaying() { return seq != nullptr; }

const char* timbreName(uint8_t index) {
  return index < cfg::TIMBRE_COUNT ? TIMBRE_NAMES[index] : "?";
}

}  // namespace sound
