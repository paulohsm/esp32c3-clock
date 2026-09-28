#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

// Scheduled messages and alarms, stored in NVS so they fire even without internet.
namespace sched {

constexpr uint8_t MAX_ENTRIES = 16;
constexpr size_t  TEXT_LEN    = 64;

struct Entry {
  uint8_t  id;              // 1..255, 0 = free slot
  bool     alarm;           // true = alarm (rings until touched), false = message
  uint8_t  hour, minute;
  uint8_t  days;            // weekly repeat mask: bit0 = Sunday … bit6 = Saturday (0 = none)
  uint32_t date;            // one-shot date as YYYYMMDD (0 = uses 'days')
  uint8_t  timbre;          // chime timbre used when it fires
  char     text[TEXT_LEN];  // UTF-8
};

void load();

// Add from JSON: {"time":"07:30","text":"...","alarm":true,"days":[1,2,3,4,5]}
// or {"time":"07:30","date":"2026-10-01",...}. Without days/date: next occurrence.
// Returns the new id, or 0 and sets 'error'.
uint8_t addFromJson(JsonObjectConst obj, const tm& now, const char*& error);

bool remove(uint8_t id);
void clear();
uint8_t count();

void toJson(JsonArray arr);

// Call once per minute with the current local time. Invokes 'fire' for each
// matching entry; one-shot entries are removed. Returns true if the list changed.
bool check(const tm& now, void (*fire)(const Entry&));

}  // namespace sched
