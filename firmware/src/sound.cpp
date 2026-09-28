#include "sound.h"
#include <Ticker.h>
#include <esp_arduino_version.h>
#include "config.h"
#include "pins.h"

namespace {

struct Note {
  uint16_t freq;  // Hz (0 = rest)
  uint16_t ms;    // duration; {0,0} ends the sequence
};

// Note frequencies (Hz). Passive buzzers are loudest around 2–4 kHz, so most
// melodies live in octaves 6 and 7.
enum : uint16_t {
  G5 = 784, A5 = 880, B5 = 988,
  C6 = 1047, D6 = 1175, E6 = 1319, F6 = 1397, G6 = 1568, A6 = 1760, B6 = 1976,
  C7 = 2093, D7 = 2349, E7 = 2637, G7 = 3136,
};

// Hourly chime / alarm tones ("timbres"). {0, n} = rest; {0, 0} ends.
const Note T_CLASSIC[]  = {{2093, 90}, {0, 70}, {2093, 90}, {0, 0}};
const Note T_DINGDONG[] = {{E7, 350}, {C7, 550}, {0, 0}};
const Note T_DOORBELL[] = {{C6, 140}, {E6, 140}, {G6, 140}, {C7, 320}, {0, 0}};
const Note T_BIGBEN[]   = {{E6, 280}, {C6, 280}, {D6, 280}, {G5, 520}, {0, 120},
                           {G5, 280}, {D6, 280}, {E6, 280}, {C6, 600}, {0, 0}};
const Note T_CUCKOO[]   = {{E7, 180}, {0, 60}, {C7, 320}, {0, 220},
                           {E7, 180}, {0, 60}, {C7, 320}, {0, 0}};
const Note T_MICROWAVE[] = {{2400, 180}, {0, 120}, {2400, 180}, {0, 120}, {2400, 180}, {0, 0}};
const Note T_NOTIFY[]   = {{G6, 70}, {0, 30}, {C7, 160}, {0, 0}};
const Note T_COIN[]     = {{B6, 80}, {E7, 380}, {0, 0}};
const Note T_SOFT[]     = {{G6, 200}, {E6, 200}, {C6, 380}, {0, 0}};
const Note T_BIRD[]     = {{3000, 35}, {3400, 35}, {3800, 50}, {0, 70},
                           {3000, 35}, {3400, 35}, {3800, 50}, {0, 160},
                           {3600, 40}, {3200, 40}, {2800, 70}, {0, 0}};

const Note* const TIMBRES[cfg::TIMBRE_COUNT] = {
    T_CLASSIC, T_DINGDONG, T_DOORBELL, T_BIGBEN, T_CUCKOO,
    T_MICROWAVE, T_NOTIFY, T_COIN, T_SOFT, T_BIRD};
const char* const TIMBRE_NAMES[cfg::TIMBRE_COUNT] = {
    "classic", "ding-dong", "doorbell", "big ben", "cuckoo",
    "microwave", "notification", "coin", "soft", "bird"};

const Note S_CONFIRM[] = {{1500, 50}, {0, 30}, {2200, 80}, {0, 0}};
const Note S_ERROR[]   = {{400, 220}, {0, 0}};
// Connection established: ascending C–E–G–C. Connection failed: two falling notes.
const Note S_CONNECTED[] = {{C6, 90}, {E6, 90}, {G6, 90}, {C7, 200}, {0, 0}};
const Note S_FAILED[]    = {{A6, 160}, {0, 40}, {D6, 280}, {0, 0}};
// Emergency siren: two alternating tones, then a short pause.
const Note S_SIREN[]   = {{1800, 220}, {1200, 220}, {1800, 220}, {1200, 220},
                          {1800, 220}, {1200, 220}, {0, 350}, {0, 0}};
Note s_click[] = {{2000, 25}, {0, 0}};

// Volume = pulse width (8-bit duty). 128 = 50% = loudest.
const uint8_t DUTY[cfg::VOLUME_MAX] = {3, 8, 20, 50, 128};

// The sequence advances on a timer (Ticker), not in loop(): a note must
// end on time even while the main program is blocked (Wi-Fi, TLS, HTTP), or the
// buzzer would stay stuck on one continuous tone.
Ticker noteTimer;
const Note* volatile seq = nullptr;
bool loud = false;  // play at full volume regardless of the setting (emergency)
volatile uint8_t idx = 0;
volatile uint32_t noteStart = 0;

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

// Called every 5 ms by the timer task: ends the note when its time is up.
void tick() {
  const Note* s = seq;
  if (!s) return;
  if (millis() - noteStart < s[idx].ms) return;
  uint8_t next = idx + 1;
  if (s[next].freq == 0 && s[next].ms == 0) {  // end of the sequence
    output(0);
    seq = nullptr;
    return;
  }
  idx = next;
  noteStart = millis();
  output(s[next].freq);
}

void start(const Note* s, bool atFullVolume = false) {
  seq = nullptr;  // pause the timer's work while we switch sequences
  loud = atFullVolume;
  idx = 0;
  noteStart = millis();
  output(s[0].freq);
  seq = s;
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
  noteTimer.attach_ms(5, tick);
}

// Kept for compatibility; the timer does the work now.
void update() {}

void click() {
  s_click[0].freq = TIMBRES[cfg::s.timbre][0].freq;
  start(s_click);
}

void chime()   { start(TIMBRES[cfg::s.timbre]); }
void chime(uint8_t timbre) { start(TIMBRES[timbre < cfg::TIMBRE_COUNT ? timbre : 0]); }
void confirm() { start(S_CONFIRM); }
void error()   { start(S_ERROR); }
void siren()   { start(S_SIREN, true); }
void connected() { start(S_CONNECTED); }
void failed()    { start(S_FAILED); }
bool isPlaying() { return seq != nullptr; }

const char* timbreName(uint8_t index) {
  return index < cfg::TIMBRE_COUNT ? TIMBRE_NAMES[index] : "?";
}

}  // namespace sound
