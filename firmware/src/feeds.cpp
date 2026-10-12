#include "feeds.h"
#ifndef FEEDS_HOST_TEST
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif
#include <time.h>

namespace feeds {

Weather weather;
Quote quotes[QUOTE_COUNT];
Fact fact;
const char* const QUOTE_CODES[QUOTE_COUNT] = {"USD", "EUR", "GBP", "BTC", "ETH"};

// "2026-09-28T05:24" → "05:24"
static void timeOf(const char* iso, char out[6]) {
  const char* t = iso ? strchr(iso, 'T') : nullptr;
  if (t && strlen(t) >= 6) strlcpy(out, t + 1, 6);
  else out[0] = '\0';
}

bool parseWeather(JsonDocument& doc) {
  JsonObject cur = doc["current"];
  JsonObject daily = doc["daily"];
  if (cur.isNull() || daily.isNull()) return false;

  Weather w;
  w.temp = cur["temperature_2m"] | 0.0f;
  w.feels = cur["apparent_temperature"] | w.temp;
  w.humidity = cur["relative_humidity_2m"] | 0;
  w.code = cur["weather_code"] | 0;
  w.isDay = (cur["is_day"] | 1) == 1;
  w.uv = cur["uv_index"] | 0.0f;

  uint8_t next = 0;
  for (JsonVariant p : doc["hourly"]["precipitation_probability"].as<JsonArray>()) {
    int v = p | 0;
    if (v > next) next = v;
  }
  w.rainNext = next;
  w.rainDay = daily["precipitation_probability_max"][0] | 0;
  w.uvMax = daily["uv_index_max"][0] | 0.0f;
  w.tMax = daily["temperature_2m_max"][0] | w.temp;
  w.tMin = daily["temperature_2m_min"][0] | w.temp;
  timeOf(daily["sunrise"][0].as<const char*>(), w.sunrise);
  timeOf(daily["sunset"][0].as<const char*>(), w.sunset);
  w.fetchedAt = time(nullptr);
  w.valid = true;
  weather = w;
  Serial.printf("feeds: weather %.1fC code %d rain %u%%/%u%% uv %.1f sun %s-%s\n", w.temp, w.code,
                w.rainNext, w.rainDay, w.uv, w.sunrise, w.sunset);
  return true;
}

bool parseQuotes(JsonDocument& doc, uint8_t mask) {
  uint32_t now = time(nullptr);
  bool any = false;
  for (uint8_t i = 0; i < QUOTE_COUNT; i++) {
    if (!(mask & (1 << i))) continue;
    char key[8];
    snprintf(key, sizeof(key), "%sBRL", QUOTE_CODES[i]);
    JsonObject q = doc[key];
    if (q.isNull()) continue;
    quotes[i].bid = atof(q["bid"] | "0");
    quotes[i].pct = atof(q["pctChange"] | "0");
    quotes[i].valid = quotes[i].bid > 0;
    quotes[i].fetchedAt = now;
    any |= quotes[i].valid;
    Serial.printf("feeds: %s %.4f (%+.2f%%)\n", QUOTE_CODES[i], quotes[i].bid, quotes[i].pct);
  }
  return any;
}

// The sentence follows "Sabia que, na data de hoje em 1500, ...": an article or a
// preposition at the start goes lowercase; names keep their capital letter.
static void lowerLeadingWord(char* t) {
  static const char* const WORDS[] = {"O ", "A ", "Os ", "As ", "Um ", "Uma ", "Uns ", "Umas ",
                                      "Em ", "No ", "Na ", "Nos ", "Nas ", "Durante ", "Após ",
                                      "Com ", "Por ", "Pela ", "Pelo "};
  for (const char* w : WORDS) {
    if (strncmp(t, w, strlen(w)) == 0) {
      t[0] = tolower(static_cast<unsigned char>(t[0]));
      return;
    }
  }
}

// Wikipedia "on this day": {"selected":[{"text","year"},...]} (the main-page list) or,
// if that list is empty, {"events":[...]} (every event of the day).
bool parseFact(JsonDocument& doc, uint8_t month, uint8_t day, uint32_t pick) {
  JsonArrayConst list = doc["selected"];
  if (list.size() == 0) list = doc["events"];
  size_t n = 0;
  for (JsonObjectConst e : list) {
    const char* t = e["text"] | "";
    if (*t && e["year"].is<int>()) n++;
  }
  if (!n) return false;
  size_t want = pick % n, k = 0;
  for (JsonObjectConst e : list) {
    const char* t = e["text"] | "";
    if (!*t || !e["year"].is<int>()) continue;
    if (k++ != want) continue;
    Fact f;
    f.year = e["year"].as<int>();
    strlcpy(f.text, t, sizeof(f.text));
    if (strlen(t) >= sizeof(f.text)) {  // cut: don't leave half a UTF-8 character
      size_t len = strlen(f.text);
      while (len && (static_cast<uint8_t>(f.text[len - 1]) & 0xC0) == 0x80) len--;
      if (len && (static_cast<uint8_t>(f.text[len - 1]) & 0x80)) len--;
      f.text[len] = '\0';
    }
    lowerLeadingWord(f.text);
    f.month = month;
    f.day = day;
    f.choices = n > 255 ? 255 : n;
    f.fetchedAt = time(nullptr);
    f.valid = true;
    fact = f;
    Serial.printf("feeds: fact %d (1 of %u): %s\n", f.year, (unsigned)n, f.text);
    return true;
  }
  return false;
}

#ifndef FEEDS_HOST_TEST
// ------------------------------------------------------------ network

// These are public, read-only data sources and nothing secret is sent, so the
// server certificate is not pinned (their CAs change more often than firmware).
static WiFiClientSecure tls;

static bool getJson(const char* url, JsonDocument& doc, JsonDocument* filter = nullptr,
                    const char* userAgent = nullptr) {
  tls.setInsecure();
  HTTPClient http;
  if (userAgent) http.setUserAgent(userAgent);
  http.useHTTP10(true);  // no chunked encoding → we can parse straight from the stream
  http.setTimeout(8000);
  http.setConnectTimeout(8000);
  if (!http.begin(tls, url)) return false;
  int status = http.GET();
  bool ok = false;
  if (status == 200) {
    DeserializationError err = filter
        ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
        : deserializeJson(doc, http.getStream());
    ok = !err;
    if (err) Serial.printf("feeds: JSON error %s\n", err.c_str());
  } else {
    Serial.printf("feeds: HTTP %d for %s\n", status, url);
  }
  http.end();
  return ok;
}

bool fetchWeather(float lat, float lon) {
  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,is_day,uv_index"
           "&hourly=precipitation_probability&forecast_hours=3"
           "&daily=sunrise,sunset,uv_index_max,precipitation_probability_max,temperature_2m_max,temperature_2m_min"
           "&forecast_days=1&timezone=auto",
           lat, lon);
  JsonDocument doc;
  if (!getJson(url, doc)) return false;
  return parseWeather(doc);
}

bool fetchQuotes(uint8_t mask) {
  if (!mask) return true;
  char url[160] = "https://economia.awesomeapi.com.br/json/last/";
  bool first = true;
  for (uint8_t i = 0; i < QUOTE_COUNT; i++) {
    if (!(mask & (1 << i))) continue;
    if (!first) strlcat(url, ",", sizeof(url));
    strlcat(url, QUOTE_CODES[i], sizeof(url));
    strlcat(url, "-BRL", sizeof(url));
    first = false;
  }

  // Keep only the two fields we use: the response has ~12 fields per pair.
  JsonDocument filter;
  for (uint8_t i = 0; i < QUOTE_COUNT; i++) {
    char key[8];
    snprintf(key, sizeof(key), "%sBRL", QUOTE_CODES[i]);
    filter[key]["bid"] = true;
    filter[key]["pctChange"] = true;
  }
  JsonDocument doc;
  if (!getJson(url, doc, &filter)) return false;
  return parseQuotes(doc, mask);
}

bool fetchFact(uint8_t month, uint8_t day) {
  char url[128];
  snprintf(url, sizeof(url),
           "https://pt.wikipedia.org/api/rest_v1/feed/onthisday/selected/%02u/%02u", month, day);
  // The response also carries each event's linked articles (summaries, pictures):
  // keep only the sentence and the year.
  JsonDocument filter;
  filter["selected"][0]["text"] = true;
  filter["selected"][0]["year"] = true;
  JsonDocument doc;
  // Wikimedia asks every client to identify itself with a way to reach its author.
  if (!getJson(url, doc, &filter,
               "esp32c3-clock (https://github.com/paulohsm/esp32c3-clock)")) {
    return false;
  }
  return parseFact(doc, month, day, esp_random());
}

#endif  // FEEDS_HOST_TEST

void weatherToJson(JsonObject o) {
  const Weather& w = weather;
  o["valid"] = w.valid;
  if (!w.valid) return;
  o["temp"] = w.temp;
  o["feels"] = w.feels;
  o["humidity"] = w.humidity;
  o["code"] = w.code;
  o["isDay"] = w.isDay;
  o["uv"] = w.uv;
  o["uvMax"] = w.uvMax;
  o["rainNext"] = w.rainNext;
  o["rainDay"] = w.rainDay;
  o["tMax"] = w.tMax;
  o["tMin"] = w.tMin;
  o["sunrise"] = w.sunrise;
  o["sunset"] = w.sunset;
  o["at"] = w.fetchedAt;
}

void factToJson(JsonObject o) {
  o["valid"] = fact.valid;
  if (!fact.valid) return;
  o["year"] = fact.year;
  o["text"] = fact.text;
  o["month"] = fact.month;
  o["day"] = fact.day;
  o["choices"] = fact.choices;
  o["at"] = fact.fetchedAt;
}

void quotesToJson(JsonObject o) {
  for (uint8_t i = 0; i < QUOTE_COUNT; i++) {
    if (!quotes[i].valid) continue;
    JsonObject q = o[QUOTE_CODES[i]].to<JsonObject>();
    q["bid"] = quotes[i].bid;
    q["pct"] = quotes[i].pct;
    q["at"] = quotes[i].fetchedAt;
  }
}

}  // namespace feeds
