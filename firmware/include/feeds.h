#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Internet data fetched by the clock itself (no API keys needed):
//  • weather: Open-Meteo (api.open-meteo.com)
//  • quotes:  AwesomeAPI (economia.awesomeapi.com.br), prices in BRL
//  • "on this day": Wikipedia in Portuguese (pt.wikipedia.org), the events listed
//    on its main page for today's date; one is drawn at random
namespace feeds {

struct Weather {
  bool     valid = false;
  float    temp = 0, feels = 0;     // °C
  uint8_t  humidity = 0;            // %
  int      code = 0;                // WMO weather code
  bool     isDay = true;
  float    uv = 0, uvMax = 0;
  uint8_t  rainNext = 0;            // max precipitation probability, next 3 hours (%)
  uint8_t  rainDay = 0;             // max precipitation probability, today (%)
  float    tMax = 0, tMin = 0;
  char     sunrise[6] = "", sunset[6] = "";  // "HH:MM" local time
  uint32_t fetchedAt = 0;           // epoch seconds
};

constexpr uint8_t QUOTE_COUNT = 5;
extern const char* const QUOTE_CODES[QUOTE_COUNT];  // "USD","EUR","GBP","BTC","ETH"

struct Quote {
  bool     valid = false;
  float    bid = 0;                 // price in BRL
  float    pct = 0;                 // daily change (%)
  uint32_t fetchedAt = 0;
};

// A historical event that happened on today's date.
constexpr size_t FACT_LEN = 300;
struct Fact {
  bool     valid = false;
  int      year = 0;                // negative = before Christ
  char     text[FACT_LEN] = "";     // UTF-8, e.g. "Cristóvão Colombo chega à América."
  uint8_t  month = 0, day = 0;      // the date it was fetched for
  uint8_t  choices = 0;             // how many events there were to draw from
  uint32_t fetchedAt = 0;
};

extern Weather weather;
extern Quote quotes[QUOTE_COUNT];
extern Fact fact;

bool fetchWeather(float lat, float lon);
bool fetchQuotes(uint8_t mask);   // bit i = QUOTE_CODES[i]
bool fetchFact(uint8_t month, uint8_t day);

// Parsers (separate so they can be unit-tested on a PC).
bool parseWeather(JsonDocument& doc);
bool parseQuotes(JsonDocument& doc, uint8_t mask);
// pick: a random number; the event drawn is pick % (number of events).
bool parseFact(JsonDocument& doc, uint8_t month, uint8_t day, uint32_t pick);

void weatherToJson(JsonObject o);
void quotesToJson(JsonObject o);
void factToJson(JsonObject o);

}  // namespace feeds
