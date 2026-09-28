#include "ota.h"
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

namespace ota {

static void (*progressCb)(int) = nullptr;
static int lastPercent = -1;

bool urlAllowed(const char* url) {
  size_t n = strlen(ALLOWED_PREFIX);
  if (strncmp(url, ALLOWED_PREFIX, n) != 0) return false;
  // No tricks like "../" to climb out of the releases folder.
  return strstr(url + n, "..") == nullptr && strchr(url + n, '?') == nullptr;
}

static void onProgress(int current, int total) {
  if (total <= 0 || !progressCb) return;
  int pct = (int)((int64_t)current * 100 / total);
  if (pct != lastPercent) {
    lastPercent = pct;
    progressCb(pct);
  }
}

bool update(const char* url, void (*onProgressPct)(int), char* error, size_t errorLen) {
  if (!urlAllowed(url)) {
    strlcpy(error, "URL not allowed", errorLen);
    return false;
  }
  progressCb = onProgressPct;
  lastPercent = -1;

  // GitHub redirects release downloads to its file servers, whose certificates
  // change over time; the URL itself is restricted to this repository above.
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20);

  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);  // we show a message and reboot ourselves
  httpUpdate.onProgress(onProgress);

  Serial.printf("OTA: downloading %s\n", url);
  t_httpUpdate_return r = httpUpdate.update(client, url);
  switch (r) {
    case HTTP_UPDATE_OK:
      Serial.println("OTA: success");
      return true;
    case HTTP_UPDATE_NO_UPDATES:
      strlcpy(error, "no update", errorLen);
      break;
    default:
      snprintf(error, errorLen, "%s (%d)", httpUpdate.getLastErrorString().c_str(),
               httpUpdate.getLastError());
      break;
  }
  Serial.printf("OTA: failed: %s\n", error);
  return false;
}

}  // namespace ota
