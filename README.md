# esp32c3-clock

*[Leia em português](LEIAME.md)*

Desk clock built with an ESP32-C3 SuperMini and a 32×8 MAX7219 LED matrix. It can be controlled from anywhere over MQTT (HiveMQ Cloud).

## Layout

```
esp32c3-clock/
├── firmware/   # PlatformIO — ESP32-C3
├── web/        # phone app (PWA, MQTT over WebSocket 8884)
├── worker/     # (future) server-side data that needs API keys, e.g. Ibovespa, YouTube
└── docs/       # diagrams, photos
```

## Wiring

| Part | Pin | ESP32-C3 |
|---|---|---|
| Matrix | VCC / GND | 5V / GND |
| Matrix | DIN / CS / CLK | GPIO 6 / 7 / 4 |
| TTP223 touch | VCC / GND / SIG | 3.3V / GND / GPIO 5 |
| Passive buzzer | + / − | GPIO 1 / GND |
| Blue LED (on board) | — | GPIO 8 (LOW = on) |
| BOOT button (on board) | — | GPIO 9 |

## Build and flash

```bash
cd ~/Projetos/esp32c3-clock/firmware
cp include/secrets.example.h include/secrets.h   # once; then edit the MQTT password
pio run -t upload
pio device monitor
```

If the upload can't find the board: hold **BOOT**, press and release **RESET**, release **BOOT**, and flash again. On Fedora the port is `/dev/ttyACM0`. For permission errors, run `sudo usermod -aG dialout $USER`, then log out and back in.

## First boot (Wi-Fi)

1. The matrix shows **WIFI** and the blue LED stays on.
2. On your phone, join **Relogio-Config** (password `relogio123`).
3. Pick your network in the portal. If it doesn't open by itself, go to `192.168.4.1`.

To change networks later, **hold the touch pad (or BOOT) while powering on**, or send `wifireset` over serial.

## Touch

| Gesture | Action |
|---|---|
| Tap | Next screen: time → date → long date → weather → rain → UV → sunrise/sunset → quotes (screens without data or disabled in the app are skipped; they return to the time after 10 s) |
| Double tap | Name, IP, Wi-Fi signal, MQTT status, device id, version |
| Long press | Toggle hourly chime |
| Any, during an alarm | Stop the alarm |

## Blue LED

| Pattern | Meaning |
|---|---|
| Fast blink | Connecting to Wi-Fi |
| Slow blink | Wi-Fi ok, waiting for time (NTP) or MQTT |
| Off | Everything connected |
| Short flash | Command received |
| Solid | Wi-Fi setup portal open |

On the matrix, a crossed-out Wi-Fi icon blinks twice every 5 seconds in place of the clock icon when there is no internet (Wi-Fi down, or the broker unreachable for over a minute).

## MQTT

Each clock has an id derived from its chip, e.g. `clock-a1b2c3`, printed on the serial monitor at boot and shown on a double tap. All topics live under `clock/<id>/`.

| Topic | Direction | Payload |
|---|---|---|
| `online` | clock → | `1` / `0` (retained; `0` is the Last Will) |
| `info` | clock → | `{"name","fw","ip","ssid","rssi","uptime","schedules"}` (retained) |
| `config` | clock → | full settings (retained) |
| `schedules` | clock → | list of schedules (retained) |
| `data/weather` | clock → | latest weather (retained): `temp`, `feels`, `humidity`, `code` (WMO), `isDay`, `uv`, `uvMax`, `rainNext`, `rainDay`, `tMax`, `tMin`, `sunrise`, `sunset`, `at` |
| `data/quotes` | clock → | latest quotes in BRL (retained): `{"USD":{"bid","pct","at"},…}` |
| `ack` | clock → reply to each command: `{"cmd","ok","error?","id?"}` |
| `cmd/msg` | → clock | plain text, or `{"text":"...","beep":true,"repeat":2}` |
| `cmd/schedule` | → clock | see below |
| `cmd/alert` | → clock | emergency alert: `{"text":"...","seconds":120}` (10–3600 s); `{"cancel":true}` stops it. Scrolls with a blinking warning icon and a full-volume siren until a touch on the sensor, cancel or timeout |
| `alert` | clock → | alert state (retained): `{"active":true,"text","started","until"}` or `{"active":false,"text","endedBy":"touch"\|"app"\|"timeout","at"}` |
| `cmd/show` | → clock | show a screen now: `date`, `longdate`, `weather`, `rain`, `uv`, `sun` or `quotes` |
| `cmd/beep` | → clock | empty, or `{"timbre":3}` |
| `config/set` | → clock | any subset of the settings, e.g. `{"brightness":4,"rotated":true}` |
| `cmd/sync` | → clock | refresh weather/quotes and republish everything |
| `cmd/reboot` | → clock | reboot |

**Schedules** (`cmd/schedule`):

```json
{"action":"add","time":"07:30","text":"Wake up","alarm":true,"days":[1,2,3,4,5]}
{"action":"add","time":"12:00","date":"2026-10-01","text":"Exam"}
{"action":"add","time":"18:00","text":"Call mom"}
{"action":"delete","id":3}
{"action":"clear"}
{"action":"list"}
```

`days`: 0 = Sunday … 6 = Saturday (weekly repeat). `date`: one-shot. With neither, the next occurrence of `time` fires once. With `"alarm": true`, it rings and scrolls until touched (or for 1 minute).

**Settings keys**: `name`, `brightness` (0–6), `rotated`, `hourlyBeep`, `timbre` (0–9: classic, ding-dong, doorbell, Big Ben, cuckoo, microwave, notification, coin, soft, bird), `volume` (1–5), `nightEnabled`, `nightStart`, `nightEnd`, `clockIcon` (0–2), `lat`, `lon`, `place`, `weatherMin` / `quotesMin` (5–120 minutes), `quotesAtNight`, `quotes` (bitmask: 1 USD, 2 EUR, 4 GBP, 8 BTC, 16 ETH), `screens` (bitmask: 1 date, 2 long date, 4 weather, 8 rain, 16 UV, 32 sun, 64 quotes), `rainAlert`, `rainHour`, `anim` (rolling digits, sliding screens, seconds dot on the dial), `scrollSpeed` (1–5), `autoEvery` (carousel period in seconds, 0 = off), `autoFor` (seconds per carousel screen), `autoScreens` (same bits as `screens`, long date excluded), `intro` (power-on introduction: greeting, app address, weather, quotes, date), `welcome` (custom greeting; empty = automatic "Bom dia!" plus a weather tip).

## Internet data

The clock fetches its own data over HTTPS, with no API keys:

- **Weather** from [Open-Meteo](https://open-meteo.com) for the configured location (the app's GPS button sets it), every `weatherMin` minutes: temperature, weather, rain chance (today and next 3 h), UV, sunrise/sunset.
- **Quotes** from [AwesomeAPI](https://docs.awesomeapi.com.br) every `quotesMin` minutes, skipped during night mode unless `quotesAtNight`.
- **Rain warning**: at `rainHour`:00, if today's rain chance is 60% or more, it scrolls "Leve guarda-chuva!" with a chime.

### Testing from Fedora

```bash
sudo dnf install mosquitto     # provides mosquitto_pub / mosquitto_sub
H=4b9a673f16e84df093793b8d8768d7f6.s1.eu.hivemq.cloud
CA=/etc/pki/tls/certs/ca-bundle.crt

# watch everything the clocks publish
mosquitto_sub -h $H -p 8883 --cafile $CA -u clock_app -P 'PASSWORD' -t 'clock/#' -v

# send a message (replace the id)
mosquitto_pub -h $H -p 8883 --cafile $CA -u clock_app -P 'PASSWORD' \
  -t 'clock/clock-a1b2c3/cmd/msg' -m 'Hello from outside!'
```

## Phone app (`web/`)

A static PWA (HTML/CSS/JS, no build step) that connects to HiveMQ over WebSocket (port 8884). Log in with an HiveMQ credential such as `clock_app`; "remember on this device" stores it in the browser's localStorage.

Run locally:

```bash
cd ~/Projetos/esp32c3-clock/web && python3 -m http.server 8000
# open http://localhost:8000
```

It is published to GitHub Pages by `.github/workflows/pages.yml` on every push to `main` that changes `web/` (repo → Settings → Pages → Source: GitHub Actions). On the phone, open the page and use "Add to Home screen".

## Icons

Icons are 6×6 drawings in `tools/gen_icons.py` (`#` = LED on). Most have 2–6 animation frames. After editing, regenerate the header:

```bash
cd ~/Projetos/esp32c3-clock && python3 tools/gen_icons.py
```

## Serial commands

`help`, `info`, `name`, `bri`, `rot`, `beep`, `timbre`, `vol`, `night`, `icon`, `loc <lat> <lon>`, `fetch`, `anim`, `speed`, `auto <sec> [dur]`, `msg`, `alert <sec> <text>` (`alert 0` stops), `intro`, `test`, `wifireset`. Portuguese aliases also work: `nome`, `noite`, `icone`, `atualizar`, `animacao`, `velocidade`, `alerta`, `apresentacao`, `teste`.

## MQTT credentials (HiveMQ)

| User | Permission | Used by |
|---|---|---|
| `esp32c3sm_clock` | Publish and Subscribe | the clock |
| `clock_app` | Publish and Subscribe | phone app / testing |
| `clock_worker` | Publish only | data service |

The clock's password goes in `firmware/include/secrets.h`, which is git-ignored.

## Roadmap

- [x] **Stage 1**: NTP, time/date screens, touch, hourly chime (4 timbres), night mode, brightness cap (6/15), orientation, status LED, Wi-Fi portal
- [x] **Stage 1.1**: fixed-width 4×6 font, icon + content layout, day-progress clock icon (seconds bar later removed)
- [x] **Stage 2**: MQTT over TLS: instant messages, schedules/alarms stored in flash, remote settings, online status (LWT)
- [x] **Stage 3**: phone app (PWA on GitHub Pages) with login
- [x] **Stage 4**: weather (Open-Meteo) and quotes (AwesomeAPI) fetched by the clock; GPS location, intervals, screens and quotes chosen in the app; rain warning
- [ ] **Stage 4.1**: Ibovespa and YouTube subscribers (need API keys → server side)
- [ ] **Stage 5**: moon phase, real Sun/Moon position from coordinates, animated icons (weather, moon, Game of Life, pixel rain), pomodoro, stopwatch, countdown
- [ ] **Stage 6**: OTA via GitHub Releases, gate status, phone notifications (ntfy)
