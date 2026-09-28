// esp32c3-clock — Etapa 1
// Relógio NTP na matriz 32x8, navegação por toque, bipe de hora,
// modo noite, LED de status, brilho limitado e orientação configurável.
// Configuração provisória pelo monitor serial (o app via MQTT vem na etapa 2).

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <OneButton.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "pins.h"
#include "sound.h"
#include "statusled.h"

#define FW_VERSION "0.1.0"

static const char* HOSTNAME = "esp32c3-clock";
static const char* AP_NAME  = "Relogio-Config";
static const char* AP_PASS  = "relogio123";     // senha da rede de configuração (mín. 8)
static const char* TZ_INFO  = "<-03>3";         // Fortaleza: UTC-3, sem horário de verão

static const char* const DIAS[]  = {"Domingo", "Segunda", "Terca", "Quarta",
                                    "Quinta", "Sexta", "Sabado"};
static const char* const MESES[] = {"janeiro", "fevereiro", "marco", "abril", "maio", "junho",
                                    "julho", "agosto", "setembro", "outubro", "novembro",
                                    "dezembro"};

enum Screen : uint8_t { SCR_CLOCK, SCR_DATE, SCR_LONGDATE, SCR_COUNT };

static WiFiManager wm;
static OneButton touch(PIN_TOUCH, false, false);  // ativo em HIGH, sem pull-up

static Screen   screen       = SCR_CLOCK;
static uint32_t screenSince  = 0;
static bool     overlay      = false;   // mensagem temporária rolando
static bool     timeOk       = false;
static bool     nightActive  = false;
static int      lastBeepHour = -1;

static constexpr uint32_t SCREEN_TIMEOUT_MS = 10000;  // volta ao relógio sozinho

// ---------------------------------------------------------------- utilidades

static bool getLocalTm(tm& t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;  // ainda sem NTP
  localtime_r(&now, &t);
  return true;
}

static bool isNightHour(int h) {
  const auto& s = cfg::s;
  if (!s.nightEnabled || s.nightStart == s.nightEnd) return false;
  if (s.nightStart < s.nightEnd) return h >= s.nightStart && h < s.nightEnd;
  return h >= s.nightStart || h < s.nightEnd;  // atravessa a meia-noite
}

static void applyBrightness() {
  display::setBrightness(nightActive ? 0 : cfg::s.brightness);
}

static void showMessage(const char* text) {
  overlay = true;
  display::scroll(text);
}

static void goToScreen(Screen s) {
  screen = s;
  screenSince = millis();
  if (s == SCR_LONGDATE) {
    static char txt[64];
    tm t;
    if (getLocalTm(t)) {
      snprintf(txt, sizeof(txt), "%s, %d de %s de %d", DIAS[t.tm_wday], t.tm_mday,
               MESES[t.tm_mon], t.tm_year + 1900);
    } else {
      strlcpy(txt, "Sem hora (NTP)", sizeof(txt));
    }
    display::scroll(txt);
  } else if (display::isScrolling()) {
    display::showStatic(" ");  // interrompe a rolagem em andamento
  }
}

// ----------------------------------------------------------------- telas

static void renderStatic() {
  if (overlay || display::isScrolling()) return;
  char txt[16];
  tm t;
  bool ok = getLocalTm(t);

  switch (screen) {
    case SCR_CLOCK:
      if (!ok) {
        strlcpy(txt, "--:--", sizeof(txt));
      } else {
        char sep = (t.tm_sec % 2 == 0) ? ':' : CHAR_COLON_OFF;
        snprintf(txt, sizeof(txt), "%02d%c%02d", t.tm_hour, sep, t.tm_min);
      }
      break;
    case SCR_DATE:
      if (!ok) strlcpy(txt, "--/--", sizeof(txt));
      else snprintf(txt, sizeof(txt), "%02d/%02d", t.tm_mday, t.tm_mon + 1);
      break;
    default:
      return;
  }
  display::showStatic(txt);
}

// ------------------------------------------------------------------ toque

static void onClick() {
  statusled::flash();
  sound::click();
  if (overlay) {  // toque cancela a mensagem
    overlay = false;
    goToScreen(SCR_CLOCK);
    display::showStatic("");
    return;
  }
  goToScreen(static_cast<Screen>((screen + 1) % SCR_COUNT));
}

static void onDoubleClick() {
  static char txt[96];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(txt, sizeof(txt), "IP %s  Sinal %d dBm  v%s", WiFi.localIP().toString().c_str(),
             WiFi.RSSI(), FW_VERSION);
  } else {
    snprintf(txt, sizeof(txt), "Sem Wi-Fi  v%s", FW_VERSION);
  }
  sound::click();
  showMessage(txt);
}

static void onLongPress() {
  cfg::s.hourlyBeep = !cfg::s.hourlyBeep;
  cfg::save();
  sound::confirm();
  showMessage(cfg::s.hourlyBeep ? "Bipe de hora ligado" : "Bipe de hora desligado");
}

// ----------------------------------------------------- comandos pela serial

static void printSettings() {
  const auto& s = cfg::s;
  Serial.printf("brilho=%u (max %u)  orientacao=%s  bipe_hora=%s\n", s.brightness,
                cfg::BRIGHT_MAX, s.rotated ? "invertida" : "normal", s.hourlyBeep ? "on" : "off");
  Serial.printf("timbre=%u (%s)  volume=%u/%u  modo_noite=%s %02u-%02u  noite_agora=%s\n",
                s.timbre, sound::timbreName(s.timbre), s.volume, cfg::VOLUME_MAX,
                s.nightEnabled ? "on" : "off", s.nightStart, s.nightEnd,
                nightActive ? "sim" : "nao");
  Serial.printf("wifi=%s ip=%s rssi=%d  ntp=%s  versao=%s\n",
                WiFi.status() == WL_CONNECTED ? "ok" : "desconectado",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), timeOk ? "ok" : "aguardando",
                FW_VERSION);
}

static void printHelp() {
  Serial.println(F(
      "Comandos:\n"
      "  info              mostra configuracoes e estado\n"
      "  bri <0-6>         brilho\n"
      "  rot <0|1>         orientacao: 0 normal, 1 invertida 180 graus\n"
      "  beep <0|1>        bipe a cada hora\n"
      "  timbre <0-3>      0 classico, 1 agudo, 2 suave, 3 carrilhao\n"
      "  vol <1-5>         volume do buzzer\n"
      "  noite <0|1>       modo noite automatico\n"
      "  noite <ini> <fim> horario do modo noite (ex.: noite 22 6)\n"
      "  msg <texto>       exibe texto rolando\n"
      "  teste             toca o bipe de hora\n"
      "  wifireset         apaga o Wi-Fi salvo e reinicia no portal"));
}

static void runCommand(String line) {
  line.trim();
  int sp = line.indexOf(' ');
  String cmd = (sp < 0) ? line : line.substring(0, sp);
  String arg = (sp < 0) ? "" : line.substring(sp + 1);
  cmd.toLowerCase();
  arg.trim();
  auto& s = cfg::s;
  bool changed = false;

  if (cmd == "help" || cmd == "ajuda" || cmd == "?") {
    printHelp();
    return;
  } else if (cmd == "info") {
    printSettings();
    return;
  } else if (cmd == "bri") {
    s.brightness = constrain(arg.toInt(), 0, cfg::BRIGHT_MAX);
    applyBrightness();
    changed = true;
  } else if (cmd == "rot") {
    s.rotated = arg.toInt() != 0;
    display::setRotated(s.rotated);
    changed = true;
  } else if (cmd == "beep") {
    s.hourlyBeep = arg.toInt() != 0;
    changed = true;
  } else if (cmd == "timbre") {
    s.timbre = constrain(arg.toInt(), 0, cfg::TIMBRE_COUNT - 1);
    sound::chime();
    changed = true;
  } else if (cmd == "vol") {
    s.volume = constrain(arg.toInt(), 1, cfg::VOLUME_MAX);
    sound::chime();
    changed = true;
  } else if (cmd == "noite") {
    int sp2 = arg.indexOf(' ');
    if (sp2 < 0) {
      s.nightEnabled = arg.toInt() != 0;
    } else {
      s.nightStart = constrain(arg.substring(0, sp2).toInt(), 0, 23);
      s.nightEnd = constrain(arg.substring(sp2 + 1).toInt(), 0, 23);
      s.nightEnabled = true;
    }
    changed = true;
  } else if (cmd == "msg") {
    static char msgBuf[160];
    strlcpy(msgBuf, arg.c_str(), sizeof(msgBuf));
    statusled::flash();
    sound::click();
    showMessage(msgBuf);
    return;
  } else if (cmd == "teste") {
    sound::chime();
    return;
  } else if (cmd == "wifireset") {
    Serial.println("Apagando Wi-Fi salvo e reiniciando...");
    wm.resetSettings();
    delay(300);
    ESP.restart();
  } else {
    Serial.println("Comando desconhecido. Digite: help");
    return;
  }

  if (changed) {
    cfg::save();
    printSettings();
  }
}

static void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) runCommand(line);
      line = "";
    } else if (line.length() < 160) {
      line += c;
    }
  }
}

// ------------------------------------------------------------------ Wi-Fi

static bool touchHeldAtBoot() {
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  pinMode(PIN_TOUCH, INPUT);
  uint32_t t0 = millis();
  while (millis() - t0 < 1500) {
    bool held = digitalRead(PIN_TOUCH) == HIGH || digitalRead(PIN_BOOT_BTN) == LOW;
    if (!held) return false;
    delay(20);
  }
  return true;
}

static void connectWiFi(bool forcePortal) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  wm.setHostname(HOSTNAME);
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(180);  // sem configuração em 3 min → reinicia
  wm.setAPCallback([](WiFiManager*) {
    statusled::set(statusled::SOLID);
    display::showStatic("WIFI");
    Serial.printf("Portal aberto: conecte na rede \"%s\" (senha %s) e acesse 192.168.4.1\n",
                  AP_NAME, AP_PASS);
  });

  statusled::set(statusled::FAST);
  display::showStatic("WiFi..");

  bool ok = forcePortal ? wm.startConfigPortal(AP_NAME, AP_PASS)
                        : wm.autoConnect(AP_NAME, AP_PASS);
  if (!ok) {
    display::showStatic("ERRO");
    Serial.println("Wi-Fi nao configurado. Reiniciando...");
    delay(1500);
    ESP.restart();
  }
  WiFi.setAutoReconnect(true);
  Serial.printf("Wi-Fi ok: %s  IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// ------------------------------------------------------------ setup / loop

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n== esp32c3-clock v%s ==\n", FW_VERSION);

  cfg::load();
  statusled::begin();
  sound::begin();
  display::begin();
  display::setRotated(cfg::s.rotated);
  applyBrightness();
  display::showStatic("Ola!");

  bool forcePortal = touchHeldAtBoot();
  if (forcePortal) Serial.println("Toque segurado no boot: abrindo portal de Wi-Fi.");
  connectWiFi(forcePortal);

  configTzTime(TZ_INFO, "a.st1.ntp.br", "pool.ntp.org", "time.google.com");

  touch.setPressMs(800);
  touch.attachClick(onClick);
  touch.attachDoubleClick(onDoubleClick);
  touch.attachLongPressStart(onLongPress);

  statusled::set(statusled::SLOW);
  sound::confirm();
  printHelp();
}

void loop() {
  touch.tick();
  sound::update();
  handleSerial();

  // Fim de rolagem: volta ao relógio.
  if (display::update()) {
    overlay = false;
    if (screen == SCR_LONGDATE) screen = SCR_CLOCK;
  }

  // Telas secundárias voltam sozinhas ao relógio.
  if (screen == SCR_DATE && millis() - screenSince > SCREEN_TIMEOUT_MS) {
    goToScreen(SCR_CLOCK);
  }

  // Tarefas a cada 200 ms.
  static uint32_t lastTick = 0;
  if (millis() - lastTick < 200) return;
  lastTick = millis();

  tm t;
  bool ok = getLocalTm(t);
  if (ok && !timeOk) {
    timeOk = true;
    lastBeepHour = t.tm_hour;  // não bipa por causa do boot
    Serial.println("Hora sincronizada (NTP).");
  }

  // LED de status
  if (WiFi.status() != WL_CONNECTED) statusled::set(statusled::FAST);
  else if (!timeOk) statusled::set(statusled::SLOW);
  else statusled::set(statusled::OFF);

  if (ok) {
    // Modo noite
    bool night = isNightHour(t.tm_hour);
    if (night != nightActive) {
      nightActive = night;
      applyBrightness();
    }
    // Bipe de hora cheia
    if (t.tm_min == 0 && t.tm_hour != lastBeepHour) {
      lastBeepHour = t.tm_hour;
      if (cfg::s.hourlyBeep && !nightActive) sound::chime();
    }
  }

  renderStatic();
}
