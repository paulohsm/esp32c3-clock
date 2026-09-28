# esp32c3-clock

Relógio de mesa com ESP32-C3 SuperMini e matriz de LEDs MAX7219 32×8. É controlado de qualquer lugar via MQTT (HiveMQ Cloud).

## Estrutura

```
esp32c3-clock/
├── firmware/   # PlatformIO — ESP32-C3 (etapa 1 pronta)
├── web/        # app do celular (PWA, MQTT via WebSocket 8884) — etapa 3
├── worker/     # Cloudflare Worker: clima, cotações, cripto → MQTT — etapa 4
└── docs/       # esquemas, tópicos, fotos
```

## Ligações

| Componente | Pino | ESP32-C3 |
|---|---|---|
| Matriz | VCC / GND | 5V / GND |
| Matriz | DIN / CS / CLK | GPIO 6 / 7 / 4 |
| Toque TTP223 | VCC / GND / SIG | 3.3V / GND / GPIO 5 |
| Buzzer passivo | + / − | GPIO 1 / GND |
| LED azul (placa) | — | GPIO 8 (LOW = aceso) |
| Botão BOOT (placa) | — | GPIO 9 |

## Compilar e gravar

```bash
cd firmware
pio run                              # compila
pio run -t upload                    # grava
pio device monitor                   # monitor serial (115200)
```

Se o upload não achar a placa: segure **BOOT**, aperte e solte **RESET**, solte **BOOT** e grave de novo. No Fedora, a porta aparece como `/dev/ttyACM0`. Se der erro de permissão:
`sudo usermod -aG dialout $USER` (e faça logout/login).

## Primeiro uso (Wi-Fi)

1. A matriz mostra **WIFI** e o LED azul fica aceso.
2. No celular, conecte na rede **Relogio-Config** (senha `relogio123`).
3. O portal abre sozinho; se não abrir, acesse `192.168.4.1`. Escolha sua rede e digite a senha.

Para trocar de rede depois, **segure o toque (ou o botão BOOT) ao ligar**. Outra opção é mandar `wifireset` pelo monitor serial.

## Toque

| Gesto | Ação |
|---|---|
| Toque curto | Próxima tela: hora → data → data por extenso |
| Toque duplo | IP, sinal Wi-Fi e versão |
| Toque longo | Liga/desliga o bipe de hora |

## LED azul

| Padrão | Significado |
|---|---|
| Pisca rápido | Conectando ao Wi-Fi |
| Pisca lento | Wi-Fi ok, aguardando hora (NTP) / MQTT |
| Apagado | Tudo conectado |
| Piscada curta | Comando recebido |
| Aceso fixo | Portal de configuração de Wi-Fi aberto |

## Comandos pelo monitor serial (provisórios, até o app ficar pronto)

`help`, `info`, `bri 0-6`, `rot 0|1`, `beep 0|1`, `timbre 0-3`, `vol 1-5`, `noite 0|1`, `noite 22 6`, `icone 0-2`, `segundos 0|1`, `msg texto`, `teste`, `wifireset`.

## Roteiro

- [x] **Etapa 1** — NTP, telas de hora e data, toque, bipe de hora com 4 timbres, modo noite, brilho limitado (máx. 6/15), orientação normal/invertida, LED de status, portal Wi-Fi
- [x] **Etapa 1.1** — fonte 4×6 de largura fixa, tela dividida (ícone 8×8 + conteúdo), ícone da hora (pizza do dia / relógio / quadrante), barrinha de segundos, ícone de calendário
- [ ] **Etapa 2** — MQTT com TLS (HiveMQ): mensagem agora, agendamentos salvos na NVS, configurações remotas, status online/offline (LWT)
- [ ] **Etapa 3** — App do celular (PWA no Cloudflare Pages) com login
- [ ] **Etapa 4** — Worker: clima (Open-Meteo: chuva, UV, nascer/pôr do sol), dólar/euro, Ibovespa, cripto, inscritos do YouTube
- [ ] **Etapa 5** — Fases da lua, posição real do Sol e da Lua pelas coordenadas, ícones e animações (clima, lua, Jogo da Vida, chuva de pixels), pomodoro, cronômetro, contagem regressiva
- [ ] **Etapa 6** — OTA via GitHub Releases, aviso do portão, notificações (ntfy)

## Credenciais MQTT (HiveMQ)

| Usuário | Permissão | Uso |
|---|---|---|
| `esp32c3sm_clock` | Publish and Subscribe | o relógio |
| `clock_app` | Publish and Subscribe | app do celular |
| `clock_worker` | Publish only | serviço de dados |

As senhas ficam em `firmware/include/secrets.h`, que não vai para o git. Veja o modelo em `secrets.example.h`.
