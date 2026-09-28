#include "statusled.h"
#include <Ticker.h>
#include "pins.h"

namespace {

Ticker blinker;
Ticker flashOff;
statusled::Mode current = statusled::SOLID;  // forces the first set() to apply
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

// Ticker runs in the background: the LED keeps blinking
// even during blocking code (Wi-Fi connection, portal, TLS handshake).
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
