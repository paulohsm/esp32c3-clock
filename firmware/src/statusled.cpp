#include "statusled.h"
#include <Ticker.h>
#include "pins.h"

namespace {

Ticker blinker;
Ticker flashOff;
statusled::Mode current = statusled::SOLID;  // força a 1ª aplicação
bool lit = false;

void write(bool on) {
  lit = on;
  digitalWrite(PIN_LED, (on ^ LED_ACTIVE_LOW) ? HIGH : LOW);
}

void toggle() { write(!lit); }
void turnOff() { write(false); }

}  // namespace

namespace statusled {

void begin() {
  pinMode(PIN_LED, OUTPUT);
  set(OFF);
}

// Ticker roda em segundo plano: o LED continua piscando
// mesmo durante trechos bloqueantes (conexão Wi-Fi, portal).
void set(Mode mode) {
  if (mode == current) return;
  current = mode;
  blinker.detach();
  switch (mode) {
    case OFF:   write(false); break;
    case SOLID: write(true);  break;
    case FAST:  blinker.attach_ms(100, toggle); break;
    case SLOW:  blinker.attach_ms(600, toggle); break;
  }
}

void flash() {
  if (current != OFF) return;
  write(true);
  flashOff.once_ms(40, turnOff);
}

}  // namespace statusled
