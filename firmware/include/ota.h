#pragma once
#include <Arduino.h>

// Remote firmware update (OTA) from this project's GitHub Releases.
namespace ota {

// Only URLs under this prefix are accepted, so a broker credential alone cannot
// make the clock install firmware from anywhere else.
constexpr const char* ALLOWED_PREFIX =
    "https://github.com/paulohsm/esp32c3-clock/releases/download/";

bool urlAllowed(const char* url);

// Downloads and installs the firmware, calling onProgress(percent) along the way.
// Returns true on success (the caller then reboots); on failure fills 'error'.
bool update(const char* url, void (*onProgress)(int percent), char* error, size_t errorLen);

}  // namespace ota
