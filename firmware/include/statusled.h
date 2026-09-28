#pragma once
#include <Arduino.h>

// On-board blue LED (GPIO 8) as a status indicator.
//   FAST    = connecting to Wi-Fi
//   SLOW    = Wi-Fi ok, waiting for time (NTP) or MQTT
//   OFF     = everything connected
//   SOLID   = Wi-Fi setup portal open
//   flash() = short blink: command received (only while OFF)
namespace statusled {

enum Mode : uint8_t { OFF, FAST, SLOW, SOLID };

void begin();
void set(Mode mode);
void flash();

}  // namespace statusled
