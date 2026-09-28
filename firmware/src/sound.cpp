#include "sound.h"
#include <esp_arduino_version.h>
#include "config.h"
#include "pins.h"

namespace {

struct Note {
  uint16_t freq;  // Hz (0 = rest)
  uint16_t ms;    // duration; {0,0} ends the sequence
};

// Hourly chime timbres
const Note T_CLASSIC[]  = {{2000, 70}, {0, 60}, {2000, 70}, {0, 0}};
const Note T_HIGH[]     = {{2700, 160}, {0, 0}};
const Note T_SOFT[]     = {{880, 120}, {0, 40}, {660, 180}, {0, 0}};
const Note T_CHIMES[] = {{1319, 180}, {1047, 180}, {1175, 180}, {784, 380}, {0, 0}};

const Note* const TIMBRES[cfg::TIMBRE_COUNT] = {T_CLASSIC, T_HIGH, T_SOFT, T_CHIMES};
const char* const TIMBRE_NAMES[cfg::TIMBRE_COUNT] = {"classic", "high", "soft", "chimes"};

const Note S_CONFIRM[] = {{1500, 50}, {0, 30}, {2200, 80}, {0, 0}};
const Note S_ERROR[]   = {{400, 220}, {0, 0}};
// Emergency siren: two alternating tones, then a short pause.
const Note S_SIREN[]   = {{1800, 220}, {1200, 220}, {1800, 220}, {1200, 220},
                          {1800, 220}, {1200, 220}, {0, 350}, {0, 0}};
Note s_click[] = {{2000, 25}, {0, 0}};

// Volume = pulse width (8-bit duty). 128 = 50% = loudest.
const uint8_t DUTY[cfg::VOLUME_MAX] = {3, 8, 20, 50, 128};

const Note* seq = nullptr;
bool loud = false;  // play at full volume regardless of the setting (emergency)
uint8_t idx = 0;
uint32_t noteStart = 0;

#if ESP_ARDUINO_VERSION_MAJOR < 3
constexpr uint8_t LEDC_CH = 0;
#endif

void output(uint16_t freq) {
  uint8_t duty = loud ? DUTY[cfg::VOLUME_MAX - 1] : DUTY[cfg::s.volume - 1];
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

void start(const Note* s, bool atFullVolume = false) {
  loud = atFullVolume;
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
  if (seq[idx].freq == 0 && seq[idx].ms == 0) {  // end
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
void chime(uint8_t timbre) { start(TIMBRES[timbre < cfg::TIMBRE_COUNT ? timbre : 0]); }
void confirm() { start(S_CONFIRM); }
void error()   { start(S_ERROR); }
void siren()   { start(S_SIREN, true); }
bool isPlaying() { return seq != nullptr; }

const char* timbreName(uint8_t index) {
  return index < cfg::TIMBRE_COUNT ? TIMBRE_NAMES[index] : "?";
}

}  // namespace sound
