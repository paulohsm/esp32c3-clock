#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Sports games followed from the app.
//
// A "follow" is either a team (the clock looks up its next game by itself) or one
// specific game. Each follow has one game slot.
//
// Two sources:
//  • ESPN's public JSON API (site.web.api.espn.com, no key needed), fetched by the
//    clock: next game, then the score every minute while the game runs. Football
//    (soccer) teams use the league "all": every competition, friendlies included.
//  • Sofascore, for teams ESPN doesn't have (lower divisions, state leagues...).
//    Sofascore refuses devices, so the phone app looks the games up and sends the
//    next one to the clock (league "sofascore"): warning before the game and its
//    start time, but no live score.
namespace sports {

constexpr uint8_t MAX_FOLLOWS = 8;

struct Follow {
  char sport[16]  = "";  // ESPN sport slug: "soccer", "basketball", ...
  char league[32] = "";  // ESPN league slug ("all", "bra.1", "nba"...) or "sofascore"
  char team[10]   = "";  // team id ("" when following one game)
  char event[14]  = "";  // game id ("" when following an ESPN team)
  char label[40]  = "";  // shown in the app, e.g. "Fortaleza · Brasileirão"
  // One game, or a Sofascore team's next game: what the app sent (kept across reboots).
  uint32_t start = 0;
  char home[24] = "", away[24] = "";
  char homeAbbr[6] = "", awayAbbr[6] = "";
};

enum State : uint8_t { PRE, LIVE, POST, UNKNOWN = 255 };

struct Game {
  bool     valid = false;
  char     id[14] = "";
  char     league[32] = "";   // ESPN league of this game (polled for the score)
  uint32_t start = 0;         // epoch seconds (UTC)
  char     home[24] = "", away[24] = "";  // short team names (UTF-8)
  char     homeAbbr[6] = "", awayAbbr[6] = "";
  int16_t  hs = -1, as = -1;  // scores (-1 = unknown)
  uint8_t  state = UNKNOWN;
  uint8_t  period = 0;
  char     detail[24] = "";   // e.g. "67'", "HT", "Q3 5:21"
  uint32_t endedAt = 0;       // when the clock saw the final whistle
  bool     preDone = false, startDone = false, finalDone = false;  // alerts already given
};

extern Follow follows[MAX_FOLLOWS];
extern Game games[MAX_FOLLOWS];  // games[i] belongs to follows[i]
extern uint8_t followCount;

void load();   // from flash
void save();

// cmd/sports from the app. Returns false (and an error) when the command is refused.
//   {"action":"add","sport":"soccer","league":"bra.1","team":"6272","label":"..."}
//   {"action":"add","sport":"soccer","league":"bra.1","event":"401841169","label":"...",
//    "start":1790982000,"home":"São Paulo","away":"Santos","homeAbbr":"SAO","awayAbbr":"SAN"}
//   {"action":"remove","index":2}   {"action":"clear"}   {"action":"refresh"}
//   {"action":"next","team":"32672","event":"...","start":...,"home":"...","away":"..."}
//     the next game of a Sofascore team (no "event" = none scheduled); sent by the app
bool command(JsonObjectConst cmd, uint32_t now, const char*& error);

void toJson(JsonObject o);  // {"follows":[...],"games":[...]}

// What happened in a game (for the alerts). scorer: 0 home, 1 away, -1 unknown.
enum Event : uint8_t { EV_PRE, EV_START, EV_SCORE, EV_PERIOD, EV_FINAL };
using Notify = void (*)(Event ev, const Follow& f, const Game& g, int8_t scorer);

// Call often. Does at most one network request per call (each blocks 1–4 s).
// Returns true when something changed and should be republished.
bool loop(uint32_t now, uint16_t preMinutes, Notify notify);

// Sports where each score is news (goals): alert on every score change.
// In the others (basketball, American football...) the score is shown when a period ends.
bool everyScore(const Follow& f);

// Follow fed by the phone app (Sofascore): schedule only, no live score.
bool appFed(const Follow& f);

// Football (soccer): "soccer" at ESPN, "football" at Sofascore.
bool isSoccer(const Follow& f);

bool hasGames();
bool anyLive();
// Text for the "games" screen, e.g. "Fortaleza 1 x 0 Ceara 67'   Flamengo x Bahia sab 16:00".
void ticker(char* out, size_t len, uint32_t now);

}  // namespace sports
