# esp32c3-clock

*[Read in English](README.md)*

Relógio de mesa com ESP32-C3 SuperMini e matriz de LEDs MAX7219 32×8. É controlado de qualquer lugar via MQTT (HiveMQ Cloud).

## Estrutura

```
esp32c3-clock/
├── firmware/   # PlatformIO — ESP32-C3
├── web/        # app do celular (PWA, MQTT via WebSocket 8884) — etapa 3
├── worker/     # Cloudflare Worker: clima, cotações, cripto → MQTT — etapa 4
└── docs/       # esquemas, fotos
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
cd ~/Projetos/esp32c3-clock/firmware
cp include/secrets.example.h include/secrets.h   # só na 1ª vez; depois edite a senha do MQTT
pio run -t upload
pio device monitor
```

Se o upload não achar a placa: segure **BOOT**, aperte e solte **RESET**, solte **BOOT** e grave de novo. No Fedora, a porta é `/dev/ttyACM0`. Se der erro de permissão, rode `sudo usermod -aG dialout $USER`, saia da sessão e entre de novo.

## Primeiro uso (Wi-Fi)

1. A matriz mostra **WIFI** e o LED azul fica aceso.
2. No celular, conecte na rede **Relogio-Config** (senha `relogio123`).
3. Escolha sua rede no portal. Se ele não abrir sozinho, acesse `192.168.4.1`.

Para trocar de rede depois, **segure o toque (ou o BOOT) ao ligar**, ou mande `wifireset` pelo serial.

## Toque

| Gesto | Ação |
|---|---|
| Toque curto | Próxima tela: hora → data → data por extenso |
| Toque duplo | Nome, IP, sinal Wi-Fi, estado do MQTT, ID do relógio, versão |
| Toque longo | Liga/desliga o bipe de hora |
| Qualquer toque durante um alarme | Para o alarme |

## LED azul

| Padrão | Significado |
|---|---|
| Pisca rápido | Conectando ao Wi-Fi |
| Pisca lento | Wi-Fi ok, aguardando hora (NTP) ou MQTT |
| Apagado | Tudo conectado |
| Piscada curta | Comando recebido |
| Aceso fixo | Portal de configuração de Wi-Fi aberto |

## MQTT em poucas palavras

- **Broker**: o "correio" central, que é o seu HiveMQ Cloud. Os aparelhos não falam entre si diretamente; todos se conectam ao broker. Por isso o relógio funciona de fora da sua rede: tanto ele quanto o celular saem para a internet até o broker.
- **Tópico**: o "endereço" de uma mensagem, com partes separadas por `/`, por exemplo `clock/clock-a1b2c3/cmd/msg`.
- **Publicar**: enviar uma mensagem para um tópico.
- **Assinar**: pedir ao broker para receber tudo o que chegar em um tópico. O `#` funciona como curinga: `clock/#` recebe tudo que começa com `clock/`.
- **Mensagem retida**: o broker guarda a última mensagem do tópico e a entrega a quem assinar depois. É assim que o app sabe as configurações atuais assim que abre.
- **Last Will (testamento)**: ao conectar, o relógio deixa com o broker um recado: "se eu sumir, publique `0` em `online`". Se a energia cair, o broker avisa por ele.
- **TLS (porta 8883)**: a conexão é criptografada, como o HTTPS. O relógio confere o certificado do HiveMQ usando o certificado raiz ISRG Root X1, que está gravado no firmware.

## Tópicos

Cada relógio tem um ID tirado do chip, por exemplo `clock-a1b2c3`. Ele aparece no monitor serial ao ligar e no toque duplo. Todos os tópicos ficam sob `clock/<id>/`.

| Tópico | Sentido | Conteúdo |
|---|---|---|
| `online` | relógio → | `1` / `0` (retido; o `0` é o Last Will) |
| `info` | relógio → | `{"name","fw","ip","ssid","rssi","uptime","schedules"}` (retido) |
| `config` | relógio → | todas as configurações (retido) |
| `schedules` | relógio → | lista de agendamentos (retido) |
| `ack` | relógio → | resposta a cada comando: `{"cmd","ok","error?","id?"}` |
| `cmd/msg` | → relógio | texto puro, ou `{"text":"...","beep":true,"repeat":2}` |
| `cmd/schedule` | → relógio | veja abaixo |
| `cmd/beep` | → relógio | vazio, ou `{"timbre":3}` |
| `config/set` | → relógio | qualquer parte das configurações, por exemplo `{"brightness":4,"rotated":true}` |
| `cmd/sync` | → relógio | republica `info`, `config` e `schedules` |
| `cmd/reboot` | → relógio | reinicia |

O relógio **lê** `config/set` e **publica** `config`. São dois tópicos separados para que ele não receba de volta a própria mensagem retida.

**Agendamentos** (`cmd/schedule`):

```json
{"action":"add","time":"07:30","text":"Acordar","alarm":true,"days":[1,2,3,4,5]}
{"action":"add","time":"12:00","date":"2026-10-01","text":"Prova"}
{"action":"add","time":"18:00","text":"Ligar para a mãe"}
{"action":"delete","id":3}
{"action":"clear"}
{"action":"list"}
```

- `days`: 0 = domingo … 6 = sábado (repete toda semana).
- `date`: dispara uma vez só, na data marcada.
- Sem `days` nem `date`: dispara uma vez, na próxima vez que o horário chegar.
- Com `"alarm": true`: toca e rola o texto até alguém tocar no sensor, ou por no máximo 1 minuto.

Os agendamentos ficam gravados no relógio e funcionam mesmo sem internet.

**Chaves de configuração**: `name`, `brightness` (0–6), `rotated`, `hourlyBeep`, `timbre` (0–3), `volume` (1–5), `nightEnabled`, `nightStart`, `nightEnd`, `clockIcon` (0–2).

### Testando pelo Fedora

```bash
sudo dnf install mosquitto     # traz mosquitto_pub / mosquitto_sub
H=4b9a673f16e84df093793b8d8768d7f6.s1.eu.hivemq.cloud
CA=/etc/pki/tls/certs/ca-bundle.crt

# ver tudo o que os relógios publicam (deixe rodando em um terminal)
mosquitto_sub -h $H -p 8883 --cafile $CA -u clock_app -P 'SENHA' -t 'clock/#' -v

# em outro terminal, mandar uma mensagem (troque o ID)
mosquitto_pub -h $H -p 8883 --cafile $CA -u clock_app -P 'SENHA' \
  -t 'clock/clock-a1b2c3/cmd/msg' -m 'Olá de fora de casa!'
```

## Comandos pelo serial

`help`, `info`, `name`, `bri`, `rot`, `beep`, `timbre`, `vol`, `night`, `icon`, `msg`, `test`, `wifireset`. Os nomes em português também funcionam: `nome`, `noite`, `icone`, `teste`.

## Credenciais MQTT (HiveMQ)

| Usuário | Permissão | Uso |
|---|---|---|
| `esp32c3sm_clock` | Publish and Subscribe | o relógio |
| `clock_app` | Publish and Subscribe | app do celular / testes |
| `clock_worker` | Publish only | serviço de dados |

A senha do relógio fica em `firmware/include/secrets.h`, que não vai para o git.

## Roteiro

- [x] **Etapa 1**: NTP, telas de hora e data, toque, bipe de hora (4 timbres), modo noite, brilho limitado (6/15), orientação, LED de status, portal Wi-Fi
- [x] **Etapa 1.1**: fonte 4×6 de largura fixa, tela com ícone + conteúdo, ícone de progresso do dia (a barrinha de segundos foi removida depois)
- [x] **Etapa 2**: MQTT com TLS: mensagens na hora, agendamentos/alarmes gravados na flash, configurações remotas, status online (LWT)
- [ ] **Etapa 3**: app do celular (PWA no Cloudflare Pages) com login
- [ ] **Etapa 4**: Worker: clima (Open-Meteo: chuva, UV, nascer/pôr do sol), dólar/euro, Ibovespa, cripto, inscritos do YouTube
- [ ] **Etapa 5**: fase da lua, posição real do Sol e da Lua pelas coordenadas, ícones animados (clima, lua, Jogo da Vida, chuva de pixels), pomodoro, cronômetro, contagem regressiva
- [ ] **Etapa 6**: OTA via GitHub Releases, aviso do portão, notificações no celular (ntfy)
