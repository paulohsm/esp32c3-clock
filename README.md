# esp32c3-clock

*[Leia em português](LEIAME.md)*

Desk clock built with an ESP32-C3 SuperMini and a 32×8 MAX7219 LED matrix. It can be controlled from anywhere over MQTT (HiveMQ Cloud).

## Layout

```
esp32c3-clock/
├── firmware/   # PlatformIO — ESP32-C3
├── web/        # phone app (PWA, MQTT over WebSocket 8884) — stage 3
├── worker/     # Cloudflare Worker: weather, FX rates, crypto → MQTT — stage 4
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
| Tap | Next screen: time → date → long date |
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

## MQTT

Each clock has an id derived from its chip, e.g. `clock-a1b2c3`, printed on the serial monitor at boot and shown on a double tap. All topics live under `clock/<id>/`.

| Topic | Direction | Payload |
|---|---|---|
| `online` | clock → | `1` / `0` (retained; `0` is the Last Will) |
| `info` | clock → | `{"name","fw","ip","ssid","rssi","uptime","schedules"}` (retained) |
| `config` | clock → | full settings (retained) |
| `schedules` | clock → | list of schedules (retained) |
| `ack` | clock → | reply to each command: `{"cmd","ok","error?","id?"}` |
| `cmd/msg` | → clock | plain text, or `{"text":"...","beep":true,"repeat":2}` |
| `cmd/schedule` | → clock | see below |
| `cmd/beep` | → clock | empty, or `{"timbre":3}` |
| `config/set` | → clock | any subset of the settings, e.g. `{"brightness":4,"rotated":true}` |
| `cmd/sync` | → clock | republish `info`, `config`, `schedules` |
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

**Settings keys**: `name`, `brightness` (0–6), `rotated`, `hourlyBeep`, `timbre` (0–3), `volume` (1–5), `nightEnabled`, `nightStart`, `nightEnd`, `clockIcon` (0–2).

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

## Serial commands

`help`, `info`, `name`, `bri`, `rot`, `beep`, `timbre`, `vol`, `night`, `icon`, `msg`, `test`, `wifireset`. Portuguese aliases also work: `nome`, `noite`, `icone`, `teste`.

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
- [ ] **Stage 3**: phone app (PWA on Cloudflare Pages) with login
- [ ] **Stage 4**: Worker: weather (Open-Meteo: rain, UV, sunrise/sunset), USD/EUR, Ibovespa, crypto, YouTube subscribers
- [ ] **Stage 5**: moon phase, real Sun/Moon position from coordinates, animated icons (weather, moon, Game of Life, pixel rain), pomodoro, stopwatch, countdown
- [ ] **Stage 6**: OTA via GitHub Releases, gate status, phone notifications (ntfy)
