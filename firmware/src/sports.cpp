#include "sports.h"
#ifndef SPORTS_HOST_TEST
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#endif
#include <time.h>

namespace sports {

Follow follows[MAX_FOLLOWS];
Game games[MAX_FOLLOWS];
uint8_t followCount = 0;

// Runtime scheduling (not saved).
static uint32_t nextRefresh[MAX_FOLLOWS];  // epoch: next team schedule lookup
static uint32_t nextLiveAt = 0;            // millis: next round of live polls
static uint8_t  livePolled = 0;            // follows already polled in this round (bitmask)

static constexpr uint32_t REFRESH_S      = 6 * 3600;  // next-game lookup for each team
static constexpr uint32_t RETRY_S        = 10 * 60;
static constexpr uint32_t LIVE_PERIOD_MS = 60000;     // live poll every minute
static constexpr uint32_t LIVE_BEFORE_S  = 15 * 60;   // start polling 15 min before
static constexpr uint32_t LIVE_MAX_S     = 6 * 3600;  // give up 6 h after the start
static constexpr uint32_t KEEP_FINAL_S   = 3 * 3600;  // the final score stays for 3 h

// ------------------------------------------------------------- helpers

// Copies UTF-8 text without cutting a multi-byte character in half.
static void copyUtf8(char* dst, const char* src, size_t size) {
  if (!src) src = "";
  strlcpy(dst, src, size);
  size_t n = strlen(dst);
  if (n == size - 1) {  // truncated: drop a partial character at the end
    size_t i = n;
    while (i > 0 && (static_cast<uint8_t>(dst[i - 1]) & 0xC0) == 0x80) i--;  // continuation bytes
    if (i > 0 && (static_cast<uint8_t>(dst[i - 1]) & 0x80)) {
      uint8_t lead = static_cast<uint8_t>(dst[i - 1]);
      size_t need = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
      if (n - (i - 1) < need) dst[i - 1] = '\0';
    }
  }
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

// "2026-10-02T23:00:00Z" or "2026-10-09T00:30Z" → epoch seconds (UTC).
static uint32_t parseIso(const char* s) {
  if (!s) return 0;
  int y, mo, d, h, mi, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) return 0;
  return static_cast<uint32_t>(daysFromCivil(y, mo, d)) * 86400UL + h * 3600UL + mi * 60UL + sec;
}

static uint8_t stateOf(const char* s) {
  if (!s) return UNKNOWN;
  if (strcmp(s, "pre") == 0) return PRE;
  if (strcmp(s, "in") == 0) return LIVE;
  if (strcmp(s, "post") == 0) return POST;
  return UNKNOWN;
}

static const char* stateName(uint8_t s) {
  return s == PRE ? "pre" : s == LIVE ? "in" : s == POST ? "post" : nullptr;
}

// First text value among a, b, c (nullptr when none is text).
static const char* firstStr(JsonVariantConst a, JsonVariantConst b,
                            JsonVariantConst c = JsonVariantConst()) {
  if (a.is<const char*>()) return a.as<const char*>();
  if (b.is<const char*>()) return b.as<const char*>();
  if (c.is<const char*>()) return c.as<const char*>();
  return nullptr;
}

static int16_t scoreOf(JsonVariantConst v) {
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    return (s && *s) ? atoi(s) : -1;
  }
  if (v.is<int>()) return v.as<int>();
  if (v["value"].is<float>()) return static_cast<int16_t>(v["value"].as<float>());
  return -1;
}

// ESPN slugs: letters, digits, '.', '_' and '-'.
static bool validSlug(const char* s, size_t maxLen) {
  if (!s || !*s || strlen(s) >= maxLen) return false;
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') return false;
  }
  return true;
}

static bool validId(const char* s, size_t maxLen) {
  if (!s || !*s || strlen(s) >= maxLen) return false;
  for (const char* p = s; *p; p++) if (!isdigit(static_cast<unsigned char>(*p))) return false;
  return true;
}

bool everyScore(const Follow& f) {
  return strcmp(f.sport, "soccer") == 0 || strcmp(f.sport, "hockey") == 0;
}

bool appFed(const Follow& f) { return strcmp(f.league, "sofascore") == 0; }

bool isSoccer(const Follow& f) {
  return strcmp(f.sport, "soccer") == 0 || (appFed(f) && strcmp(f.sport, "football") == 0);
}

// Do follows i and k have their games in the same ESPN league (one poll serves both)?
static bool samePoll(uint8_t i, uint8_t k) {
  return strcmp(follows[i].sport, follows[k].sport) == 0 &&
         strcmp(games[i].league, games[k].league) == 0;
}

static bool sameLeague(const Follow& a, const Follow& b) {
  return strcmp(a.sport, b.sport) == 0 && strcmp(a.league, b.league) == 0;
}

// A one-game follow: its slot comes straight from what the app sent.
static void gameFromFollow(uint8_t i, uint32_t now) {
  const Follow& f = follows[i];
  Game& g = games[i];
  g = Game();
  g.valid = true;
  strlcpy(g.id, f.event, sizeof(g.id));
  strlcpy(g.league, f.league, sizeof(g.league));
  g.start = f.start;
  copyUtf8(g.home, f.home, sizeof(g.home));
  copyUtf8(g.away, f.away, sizeof(g.away));
  strlcpy(g.homeAbbr, f.homeAbbr, sizeof(g.homeAbbr));
  strlcpy(g.awayAbbr, f.awayAbbr, sizeof(g.awayAbbr));
  g.state = now < f.start ? PRE : UNKNOWN;  // already started: the first poll tells
  g.preDone = now >= f.start;
  if (appFed(f)) {  // never polled: time decides
    g.state = PRE;
    g.startDone = now >= f.start;
  }
}

// --------------------------------------------------------- state changes

// New data for a game. Fires the alerts for what changed since the last poll.
static bool apply(uint8_t i, uint32_t now, uint8_t st, int16_t hs, int16_t as, uint8_t period,
                  const char* detail, Notify notify) {
  Game& g = games[i];
  const Follow& f = follows[i];
  if (st == UNKNOWN) return false;
  uint8_t prev = g.state;
  int16_t pHs = g.hs, pAs = g.as;
  uint8_t pPeriod = g.period;
  bool changed = st != g.state || hs != g.hs || as != g.as || period != g.period ||
                 strcmp(detail ? detail : "", g.detail) != 0;
  g.state = st;
  g.hs = hs;
  g.as = as;
  g.period = period;
  copyUtf8(g.detail, detail, sizeof(g.detail));

  if (prev == UNKNOWN) {  // first look at this game (e.g. after a reboot): no alerts
    g.startDone = st != PRE;
    g.finalDone = st == POST;
    if (st != PRE) g.preDone = true;
    if (st == POST && !g.endedAt) g.endedAt = now;
    return changed;
  }
  if (st != PRE && !g.startDone) {
    g.startDone = g.preDone = true;
    if (notify) notify(EV_START, f, g, -1);
  }
  if (st == LIVE && prev == LIVE) {
    if (everyScore(f)) {
      int8_t scorer = -1;
      bool scored = false;
      if (hs > pHs && pHs >= 0) { scorer = 0; scored = true; }
      if (as > pAs && pAs >= 0) { scorer = scored ? -1 : 1; scored = true; }
      if (scored && notify) notify(EV_SCORE, f, g, scorer);
    } else if (period > pPeriod && pPeriod > 0 && notify) {
      notify(EV_PERIOD, f, g, -1);
    }
  }
  if (st == POST && !g.finalDone) {
    g.finalDone = true;
    g.endedAt = now;
    if (notify) notify(EV_FINAL, f, g, -1);
  }
  if (st == POST && f.team[0]) {
    // Look up the team's next game after the final score has been shown for a while.
    uint32_t at = g.endedAt + KEEP_FINAL_S;
    if (nextRefresh[i] > at || nextRefresh[i] < now) nextRefresh[i] = at;
  }
  return changed;
}

// ------------------------------------------------------------- parsers
// Kept apart from the network code so they can be tested on a PC.

// Scoreboard header (site.web.api.espn.com/apis/v2/scoreboard/header): updates every
// followed game of this league found in it. Bit i of 'found' = game i was there.
bool parseHeader(JsonDocument& doc, uint8_t ref, uint32_t now, Notify notify, uint8_t& found) {
  bool changed = false;
  for (JsonObjectConst ev : doc["sports"][0]["leagues"][0]["events"].as<JsonArrayConst>()) {
    const char* id = ev["id"] | "";
    for (uint8_t i = 0; i < followCount; i++) {
      Game& g = games[i];
      if (!g.valid || strcmp(g.id, id) != 0 || !samePoll(i, ref)) continue;
      found |= 1 << i;
      int16_t hs = -1, as = -1;
      for (JsonObjectConst c : ev["competitors"].as<JsonArrayConst>()) {
        bool home = strcmp(c["homeAway"] | "", "home") == 0;
        (home ? hs : as) = scoreOf(c["score"]);
      }
      uint8_t st = stateOf(firstStr(ev["status"], ev["fullStatus"]["type"]["state"]));
      const char* detail = firstStr(ev["fullStatus"]["type"]["shortDetail"], ev["summary"]);
      changed |= apply(i, now, st, hs, as, ev["period"] | 0, detail, notify);
    }
  }
  return changed;
}

// Full scoreboard of one day (fallback when the header no longer lists a game).
bool parseScoreboard(JsonDocument& doc, uint8_t ref, uint32_t now, Notify notify,
                     uint8_t& found) {
  bool changed = false;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    const char* id = ev["id"] | "";
    for (uint8_t i = 0; i < followCount; i++) {
      Game& g = games[i];
      if (!g.valid || strcmp(g.id, id) != 0 || !samePoll(i, ref)) continue;
      found |= 1 << i;
      int16_t hs = -1, as = -1;
      for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
        bool home = strcmp(c["homeAway"] | "", "home") == 0;
        (home ? hs : as) = scoreOf(c["score"]);
      }
      JsonObjectConst status = ev["status"];
      changed |= apply(i, now, stateOf(status["type"]["state"] | ""), hs, as,
                       status["period"] | 0, status["type"]["shortDetail"] | "", notify);
    }
  }
  return changed;
}

// Team schedule: picks the next game that is not over (or the one running now).
bool parseSchedule(JsonDocument& doc, uint8_t i, uint32_t now) {
  JsonObjectConst best;
  uint32_t bestStart = 0;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    JsonObjectConst comp = ev["competitions"][0];
    if (comp["status"]["type"]["completed"] | false) continue;
    uint32_t start = parseIso(ev["date"] | "");
    if (!start || start + LIVE_MAX_S < now) continue;
    if (!bestStart || start < bestStart) {
      bestStart = start;
      best = ev;
    }
  }
  Game& g = games[i];
  if (!bestStart) {  // no game ahead (e.g. season over)
    bool had = g.valid;
    g = Game();
    return had;
  }
  const char* id = best["id"] | "";
  bool same = g.valid && strcmp(g.id, id) == 0;
  if (!same) {
    g = Game();
    g.valid = true;
    strlcpy(g.id, id, sizeof(g.id));
    g.state = stateOf(best["competitions"][0]["status"]["type"]["state"] | "");
    // Already running when first seen (e.g. after a reboot): no start alert.
    g.preDone = g.state != PRE || now >= bestStart;
    g.startDone = g.state == LIVE || g.state == POST;
    g.finalDone = g.state == POST;
  }
  g.start = bestStart;
  // A team followed in "all" competitions: the game's own league is the one to poll.
  const char* lg = best["league"]["slug"] | "";
  strlcpy(g.league, *lg ? lg : follows[i].league, sizeof(g.league));
  for (JsonObjectConst c : best["competitions"][0]["competitors"].as<JsonArrayConst>()) {
    bool home = strcmp(c["homeAway"] | "", "home") == 0;
    JsonObjectConst t = c["team"];
    const char* name = firstStr(t["shortDisplayName"], t["displayName"], t["abbreviation"]);
    copyUtf8(home ? g.home : g.away, name ? name : "?", sizeof(g.home));
    strlcpy(home ? g.homeAbbr : g.awayAbbr, t["abbreviation"] | "", sizeof(g.homeAbbr));
  }
  return true;
}

// ------------------------------------------------------------- list

static void resetRuntime(uint8_t i) {
  nextRefresh[i] = 0;
  livePolled &= ~(1 << i);
}

static void removeAt(uint8_t i) {
  if (i >= followCount) return;
  for (uint8_t k = i; k + 1 < followCount; k++) {
    follows[k] = follows[k + 1];
    games[k] = games[k + 1];
    nextRefresh[k] = nextRefresh[k + 1];
  }
  followCount--;
  follows[followCount] = Follow();
  games[followCount] = Game();
  nextRefresh[followCount] = 0;
  livePolled = 0;
}

static void followToJson(const Follow& f, JsonObject o) {
  o["sport"] = f.sport;
  o["league"] = f.league;
  if (f.team[0]) o["team"] = f.team;
  if (f.event[0]) {  // a single game, or a Sofascore team's next game
    o["event"] = f.event;
    o["start"] = f.start;
    o["home"] = f.home;
    o["away"] = f.away;
    o["homeAbbr"] = f.homeAbbr;
    o["awayAbbr"] = f.awayAbbr;
  }
  o["label"] = f.label;
}

static bool followFromJson(JsonObjectConst o, Follow& f, const char*& error) {
  f = Follow();
  const char* sport = o["sport"] | "";
  const char* league = o["league"] | "";
  const char* team = o["team"] | "";
  const char* event = o["event"] | "";
  if (!validSlug(sport, sizeof(f.sport)) || !validSlug(league, sizeof(f.league))) {
    error = "invalid sport or league";
    return false;
  }
  // ESPN: a team or a game. Sofascore: a team with its next game, or a game.
  bool app = strcmp(league, "sofascore") == 0;
  if ((*team && !validId(team, sizeof(f.team))) || (*event && !validId(event, sizeof(f.event))) ||
      (!*team && !*event) || (*team && *event && !app)) {
    error = "give either a team id or a game id";
    return false;
  }
  strlcpy(f.sport, sport, sizeof(f.sport));
  strlcpy(f.league, league, sizeof(f.league));
  strlcpy(f.team, team, sizeof(f.team));
  strlcpy(f.event, event, sizeof(f.event));
  copyUtf8(f.label, o["label"] | "", sizeof(f.label));
  if (f.event[0]) {
    f.start = o["start"] | 0UL;
    copyUtf8(f.home, o["home"] | "", sizeof(f.home));
    copyUtf8(f.away, o["away"] | "", sizeof(f.away));
    strlcpy(f.homeAbbr, o["homeAbbr"] | "", sizeof(f.homeAbbr));
    strlcpy(f.awayAbbr, o["awayAbbr"] | "", sizeof(f.awayAbbr));
    if (!f.start || !f.home[0] || !f.away[0]) {
      if (f.team[0]) {  // a Sofascore team without a usable next game: just the team
        f.event[0] = '\0';
        f.start = 0;
        return true;
      }
      error = "a game needs start, home and away";
      return false;
    }
  }
  return true;
}

#ifndef SPORTS_HOST_TEST
static const char* NS = "sports";

void load() {
  Preferences prefs;
  prefs.begin(NS, true);
  String json = prefs.getString("list", "[]");
  prefs.end();
  JsonDocument doc;
  followCount = 0;
  if (deserializeJson(doc, json)) return;
  uint32_t now = time(nullptr);
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    if (followCount >= MAX_FOLLOWS) break;
    const char* err = nullptr;
    if (!followFromJson(o, follows[followCount], err)) continue;
    games[followCount] = Game();
    if (follows[followCount].event[0]) gameFromFollow(followCount, now);
    resetRuntime(followCount);
    followCount++;
  }
  Serial.printf("sports: %u followed\n", followCount);
}

void save() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < followCount; i++) followToJson(follows[i], arr.add<JsonObject>());
  String json;
  serializeJson(doc, json);
  Preferences prefs;
  prefs.begin(NS, false);
  prefs.putString("list", json);
  prefs.end();
}
#else
void load() {}
void save() {}
#endif

bool command(JsonObjectConst cmd, uint32_t now, const char*& error) {
  const char* action = cmd["action"] | "";
  if (strcmp(action, "add") == 0) {
    if (followCount >= MAX_FOLLOWS) { error = "list full (8)"; return false; }
    Follow f;
    if (!followFromJson(cmd, f, error)) return false;
    for (uint8_t i = 0; i < followCount; i++) {
      const Follow& o = follows[i];
      if (sameLeague(o, f) && strcmp(o.team, f.team) == 0 && strcmp(o.event, f.event) == 0) {
        error = "already followed";
        return false;
      }
    }
    uint8_t i = followCount++;
    follows[i] = f;
    games[i] = Game();
    if (f.event[0]) gameFromFollow(i, now);
    resetRuntime(i);
    nextLiveAt = 0;
    save();
    return true;
  }
  if (strcmp(action, "next") == 0) {  // a Sofascore team's next game, from the app
    const char* team = cmd["team"] | "";
    for (uint8_t i = 0; i < followCount; i++) {
      Follow& f = follows[i];
      if (!appFed(f) || strcmp(f.team, team) != 0) continue;
      JsonDocument tmp;
      JsonObject o = tmp.to<JsonObject>();
      for (JsonPairConst kv : cmd) o[kv.key()] = kv.value();
      o["sport"] = f.sport;
      o["league"] = f.league;
      o["label"] = f.label;
      Follow n;
      if (!followFromJson(o, n, error)) return false;
      bool same = strcmp(n.event, f.event) == 0 && n.start == f.start &&
                  strcmp(n.home, f.home) == 0 && strcmp(n.away, f.away) == 0;
      if (same) return true;
      f = n;
      if (f.event[0]) gameFromFollow(i, now);
      else games[i] = Game();
      save();
      return true;
    }
    error = "team not followed";
    return false;
  }
  if (strcmp(action, "remove") == 0) {
    int idx = cmd["index"] | -1;
    if (idx < 0 || idx >= followCount) { error = "index not found"; return false; }
    removeAt(idx);
    save();
    return true;
  }
  if (strcmp(action, "clear") == 0) {
    while (followCount) removeAt(followCount - 1);
    save();
    return true;
  }
  if (strcmp(action, "refresh") == 0) {
    for (uint8_t i = 0; i < followCount; i++) nextRefresh[i] = 0;
    nextLiveAt = 0;
    livePolled = 0;
    return true;
  }
  error = "unknown action";
  return false;
}

void toJson(JsonObject o) {
  JsonArray fa = o["follows"].to<JsonArray>();
  JsonArray ga = o["games"].to<JsonArray>();
  for (uint8_t i = 0; i < followCount; i++) {
    followToJson(follows[i], fa.add<JsonObject>());
    const Game& g = games[i];
    JsonObject j = ga.add<JsonObject>();
    if (!g.valid) continue;  // {} = no game known yet
    j["id"] = g.id;
    j["league"] = g.league;
    j["start"] = g.start;
    j["home"] = g.home;
    j["away"] = g.away;
    j["homeAbbr"] = g.homeAbbr;
    j["awayAbbr"] = g.awayAbbr;
    if (g.hs >= 0) j["hs"] = g.hs;
    if (g.as >= 0) j["as"] = g.as;
    if (stateName(g.state)) j["state"] = stateName(g.state);
    if (g.detail[0]) j["detail"] = g.detail;
  }
}

bool hasGames() {
  for (uint8_t i = 0; i < followCount; i++) if (games[i].valid) return true;
  return false;
}

bool anyLive() {
  for (uint8_t i = 0; i < followCount; i++) {
    if (games[i].valid && games[i].state == LIVE) return true;
  }
  return false;
}

// ------------------------------------------------------------- ticker

static const char* const WDAY[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sab"};

static void appendGame(char* out, size_t len, const Game& g, uint32_t now) {
  char part[96];
  if (g.state == LIVE || (g.state == POST && g.hs >= 0)) {
    snprintf(part, sizeof(part), "%s%s %d x %d %s%s%s", g.state == POST ? "Fim: " : "", g.home,
             g.hs < 0 ? 0 : g.hs, g.as < 0 ? 0 : g.as, g.away, g.state == LIVE ? " " : "",
             g.state == LIVE ? g.detail : "");
  } else {
    time_t st = g.start, nw = now;
    tm ts, tn;
    localtime_r(&st, &ts);
    localtime_r(&nw, &tn);
    char when[24];
    int32_t dayDiff = daysFromCivil(ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday) -
                      daysFromCivil(tn.tm_year + 1900, tn.tm_mon + 1, tn.tm_mday);
    if (dayDiff == 0) snprintf(when, sizeof(when), "hoje %02d:%02d", ts.tm_hour, ts.tm_min);
    else if (dayDiff == 1) snprintf(when, sizeof(when), "amanha %02d:%02d", ts.tm_hour, ts.tm_min);
    else if (dayDiff > 1 && dayDiff < 7)
      snprintf(when, sizeof(when), "%s %02d:%02d", WDAY[ts.tm_wday], ts.tm_hour, ts.tm_min);
    else
      snprintf(when, sizeof(when), "%02d/%02d %02d:%02d", ts.tm_mday, ts.tm_mon + 1, ts.tm_hour,
               ts.tm_min);
    if (g.state == PRE && g.startDone && now >= g.start) {  // app-fed: no live score
      snprintf(part, sizeof(part), "%s x %s (em andamento)", g.home, g.away);
    } else {
      snprintf(part, sizeof(part), "%s x %s %s", g.home, g.away, when);
    }
  }
  if (out[0]) strlcat(out, "   ", len);
  strlcat(out, part, len);
}

void ticker(char* out, size_t len, uint32_t now) {
  out[0] = '\0';
  // Soonest first; the same game followed twice (two teams) is shown once.
  bool done[MAX_FOLLOWS] = {};
  for (;;) {
    int best = -1;
    for (uint8_t i = 0; i < followCount; i++) {
      if (done[i] || !games[i].valid) continue;
      if (best < 0 || games[i].start < games[best].start) best = i;
    }
    if (best < 0) break;
    for (uint8_t i = 0; i < followCount; i++) {
      if (games[i].valid && strcmp(games[i].id, games[best].id) == 0) done[i] = true;
    }
    appendGame(out, len, games[best], now);
  }
  if (!out[0]) strlcpy(out, followCount ? "Nenhum jogo marcado" : "Nenhum jogo seguido", len);
}

// ------------------------------------------------------------- network

#ifndef SPORTS_HOST_TEST
static const char* HOST = "https://site.web.api.espn.com";

static bool needsLivePoll(uint8_t i, uint32_t now) {
  const Game& g = games[i];
  return g.valid && !appFed(follows[i]) && g.state != POST && now + LIVE_BEFORE_S >= g.start &&
         now < g.start + LIVE_MAX_S;
}

// ESPN refuses unknown clients, so this identifies as a browser.
static bool getJson(const char* url, JsonDocument& doc, JsonDocument& filter) {
  static WiFiClientSecure tls;
  tls.setInsecure();  // public, read-only data; nothing secret is sent
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding → parse straight from the stream
  http.setTimeout(10000);
  http.setConnectTimeout(8000);
  http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) esp32c3-clock");
  if (!http.begin(tls, url)) return false;
  int status = http.GET();
  bool ok = false;
  if (status == 200) {
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    ok = !err;
    if (err) Serial.printf("sports: JSON error %s\n", err.c_str());
  } else {
    Serial.printf("sports: HTTP %d for %s\n", status, url);
  }
  http.end();
  return ok;
}

static bool fetchSchedule(uint8_t i, uint32_t now) {
  const Follow& f = follows[i];
  char url[224];
  snprintf(url, sizeof(url), "%s/apis/site/v2/sports/%s/%s/teams/%s/schedule?lang=pt&region=br%s",
           HOST, f.sport, f.league, f.team,
           strcmp(f.sport, "soccer") == 0 ? "&fixture=true" : "");
  JsonDocument filter;
  JsonObject ev = filter["events"][0].to<JsonObject>();
  ev["id"] = true;
  ev["date"] = true;
  ev["league"]["slug"] = true;
  JsonObject comp = ev["competitions"][0].to<JsonObject>();
  comp["status"]["type"]["state"] = true;
  comp["status"]["type"]["completed"] = true;
  JsonObject c = comp["competitors"][0].to<JsonObject>();
  c["homeAway"] = true;
  c["team"]["abbreviation"] = true;
  c["team"]["shortDisplayName"] = true;
  c["team"]["displayName"] = true;
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;
  parseSchedule(doc, i, now);
  const Game& g = games[i];
  if (g.valid) Serial.printf("sports: %s next: %s x %s at %lu\n", f.label, g.home, g.away,
                             (unsigned long)g.start);
  else Serial.printf("sports: %s has no game ahead\n", f.label);
  return true;
}

// Scoreboard header of the league of game 'ref' (covers every followed game in it).
static bool fetchHeader(uint8_t ref, uint32_t now, Notify notify, uint8_t& found, bool& changed) {
  const char* sport = follows[ref].sport;
  const char* league = games[ref].league;
  char url[200];
  snprintf(url, sizeof(url), "%s/apis/v2/scoreboard/header?sport=%s&league=%s&lang=pt&region=br",
           HOST, sport, league);
  JsonDocument filter;
  JsonObject ev = filter["sports"][0]["leagues"][0]["events"][0].to<JsonObject>();
  ev["id"] = true;
  ev["status"] = true;
  ev["summary"] = true;
  ev["period"] = true;
  ev["fullStatus"]["type"]["state"] = true;
  ev["fullStatus"]["type"]["shortDetail"] = true;
  JsonObject c = ev["competitors"][0].to<JsonObject>();
  c["homeAway"] = true;
  c["score"] = true;
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;
  changed |= parseHeader(doc, ref, now, notify, found);
  return true;
}

// ESPN groups days by US Eastern time (UTC-4/-5): UTC-4 is right except a 1-hour edge.
static bool fetchScoreboard(uint8_t ref, uint32_t now, Notify notify, uint8_t& found,
                            bool& changed) {
  time_t t = games[ref].start - 4 * 3600;
  tm d;
  gmtime_r(&t, &d);
  char url[220];
  snprintf(url, sizeof(url),
           "%s/apis/site/v2/sports/%s/%s/scoreboard?dates=%04d%02d%02d&lang=pt&region=br", HOST,
           follows[ref].sport, games[ref].league, d.tm_year + 1900, d.tm_mon + 1, d.tm_mday);
  JsonDocument filter;
  JsonObject ev = filter["events"][0].to<JsonObject>();
  ev["id"] = true;
  ev["status"]["period"] = true;
  ev["status"]["type"]["state"] = true;
  ev["status"]["type"]["shortDetail"] = true;
  JsonObject c = ev["competitions"][0]["competitors"][0].to<JsonObject>();
  c["homeAway"] = true;
  c["score"] = true;
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;
  changed |= parseScoreboard(doc, ref, now, notify, found);
  return true;
}
#endif


bool loop(uint32_t now, uint16_t preMinutes, Notify notify) {
  if (now < 1700000000) return false;  // no clock yet
  bool changed = false;

  // 1) Time-based alert: "the game starts in N minutes" (no network needed).
  for (uint8_t i = 0; i < followCount; i++) {
    Game& g = games[i];
    if (!g.valid || g.preDone) continue;
    if (now >= g.start || g.state == LIVE || g.state == POST) {
      g.preDone = true;
    } else if (preMinutes && now + preMinutes * 60UL >= g.start) {
      g.preDone = true;
      // The same game followed twice alerts once.
      bool dup = false;
      for (uint8_t k = 0; k < i; k++) dup |= games[k].valid && strcmp(games[k].id, g.id) == 0;
      if (!dup && notify) notify(EV_PRE, follows[i], g, -1);
    }
  }

  // 2) App-fed games (Sofascore): no score, so the clock goes by the time. "Started" at
  // the start time; 3 h later the game is over and the team waits for its next game.
  for (uint8_t i = 0; i < followCount; i++) {
    Follow& f = follows[i];
    Game& g = games[i];
    if (!appFed(f) || !g.valid) continue;
    if (!g.startDone && now >= g.start) {
      g.startDone = true;
      changed = true;
      if (now < g.start + 1800 && notify) notify(EV_START, f, g, -1);
    }
    if (f.team[0] && now > g.start + KEEP_FINAL_S) {
      f.event[0] = '\0';
      f.start = 0;
      g = Game();
      save();
      changed = true;
    }
  }

  // 3) One-game follows leave the list 3 h after the end (12 h after the start at most;
  //    3 h after the start for app-fed games, whose end the clock can't see).
  for (int i = followCount - 1; i >= 0; i--) {
    const Game& g = games[i];
    if (!follows[i].event[0] || follows[i].team[0] || !g.valid) continue;
    bool over = (g.state == POST && g.endedAt && now > g.endedAt + KEEP_FINAL_S) ||
                now > g.start + (appFed(follows[i]) ? KEEP_FINAL_S : 12 * 3600UL);
    if (over) {
      removeAt(i);
      save();
      changed = true;
    }
  }

#ifndef SPORTS_HOST_TEST
  // 4) Each ESPN team's next game (one request per call).
  for (uint8_t i = 0; i < followCount; i++) {
    if (!follows[i].team[0] || appFed(follows[i]) || now < nextRefresh[i]) continue;
    if (games[i].valid && games[i].state == LIVE) {  // never while its game is running
      nextRefresh[i] = now + RETRY_S;
      continue;
    }
    bool ok = fetchSchedule(i, now);
    nextRefresh[i] = now + (ok ? REFRESH_S : RETRY_S);
    return true;
  }

  // 5) Games about to start or running: one league per call, all leagues once a minute.
  if ((int32_t)(millis() - nextLiveAt) >= 0) {
    for (uint8_t i = 0; i < followCount; i++) {
      if ((livePolled & (1 << i)) || !needsLivePoll(i, now)) continue;
      uint8_t found = 0;
      bool ok = fetchHeader(i, now, notify, found, changed);
      // Games of this league the header no longer lists: ask that day's scoreboard.
      for (uint8_t k = 0; ok && k < followCount; k++) {
        if (!(found & (1 << k)) && needsLivePoll(k, now) && samePoll(k, i) &&
            now >= games[k].start) {
          fetchScoreboard(k, now, notify, found, changed);
          break;
        }
      }
      for (uint8_t k = 0; k < followCount; k++) {
        if (games[k].valid && samePoll(k, i)) livePolled |= 1 << k;
      }
      return changed;
    }
    livePolled = 0;
    nextLiveAt = millis() + LIVE_PERIOD_MS;
  }
#endif
  return changed;
}

}  // namespace sports
