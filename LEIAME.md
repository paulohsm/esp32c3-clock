# esp32c3-clock

*[Read in English](README.md)*

Relógio de mesa com ESP32-C3 SuperMini e matriz de LEDs MAX7219 32×8. É controlado de qualquer lugar via MQTT (HiveMQ Cloud).

## Estrutura

```
esp32c3-clock/
├── firmware/   # PlatformIO — ESP32-C3
├── web/        # app do celular (PWA, MQTT via WebSocket 8884)
├── worker/     # (futuro) dados que exigem chave de API, como Ibovespa e YouTube
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
| Toque curto | Próxima tela: hora → data → data por extenso → tempo → chuva → UV → nascer/pôr do sol → cotações. Telas sem dados ou desligadas no app são puladas, e cada uma volta sozinha para a hora após 10 s. |
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

Na matriz, quando não há internet (Wi-Fi caído, ou broker inacessível há mais de 1 minuto), um ícone de Wi-Fi riscado pisca duas vezes a cada 5 segundos no lugar do ícone do relógio.

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
| `data/weather` | relógio → | meteorologia mais recente (retido): `temp`, `feels`, `humidity`, `code` (código WMO), `isDay`, `uv`, `uvMax`, `rainNext`, `rainDay`, `tMax`, `tMin`, `sunrise`, `sunset`, `at` |
| `data/quotes` | relógio → | cotações mais recentes em reais (retido): `{"USD":{"bid","pct","at"},…}` |
| `ack` | relógio → resposta a cada comando: `{"cmd","ok","error?","id?"}` |
| `cmd/msg` | → relógio | texto puro, ou `{"text":"...","beep":true,"repeat":2}` |
| `cmd/schedule` | → relógio | veja abaixo |
| `cmd/alert` | → relógio | alerta de emergência: `{"text":"...","seconds":120}` (10 a 3600 s); `{"cancel":true}` para. O texto rola com um ícone de alerta piscando e sirene no volume máximo até alguém tocar no sensor, o app cancelar ou o tempo acabar |
| `alert` | relógio → | estado do alerta (retido): `{"active":true,"text","started","until"}` ou `{"active":false,"text","endedBy":"touch"\|"app"\|"timeout","at"}` |
| `cmd/show` | → relógio | mostra uma tela agora: `date`, `longdate`, `weather`, `rain`, `uv`, `sun` ou `quotes` |
| `cmd/beep` | → relógio | vazio, ou `{"timbre":3}` |
| `config/set` | → relógio | qualquer parte das configurações, por exemplo `{"brightness":4,"rotated":true}` |
| `cmd/sync` | → relógio | busca de novo tempo e cotações e republica tudo |
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

**Chaves de configuração**: `name`, `brightness` (0–6), `rotated`, `hourlyBeep`, `timbre` (0–9: clássico, ding-dong, campainha, Big Ben, cuco, micro-ondas, notificação, moeda, suave, passarinho), `volume` (1–5), `nightEnabled`, `nightStart`, `nightEnd`, `clockIcon` (0–2), `lat`, `lon`, `place`, `weatherMin` / `quotesMin` (5–120 minutos), `quotesAtNight`, `quotes` (soma de bits: 1 dólar, 2 euro, 4 libra, 8 bitcoin, 16 ethereum), `screens` (soma de bits: 1 data, 2 data por extenso, 4 tempo, 8 chuva, 16 UV, 32 sol, 64 cotações), `rainAlert`, `rainHour`, `anim` (números que rolam, telas que deslizam, segundos no ícone), `scrollSpeed` (1–5), `autoEvery` (período do carrossel em segundos, 0 = desligado), `autoFor` (segundos por tela do carrossel), `autoScreens` (mesmos bits de `screens`, sem a data por extenso).

## Dados da internet

O próprio relógio busca os dados por HTTPS, sem precisar de chave de API:

- **Meteorologia**, do [Open-Meteo](https://open-meteo.com), para o local configurado, a cada `weatherMin` minutos: temperatura, condição do tempo, chance de chuva (no dia e nas próximas 3 h), UV, nascer e pôr do sol. O local é definido pelo botão de GPS do app.
- **Cotações**, da [AwesomeAPI](https://docs.awesomeapi.com.br), a cada `quotesMin` minutos. Não são atualizadas no modo noite, a menos que `quotesAtNight` esteja ligado.
- **Aviso de chuva**: às `rainHour`:00, se a chance de chuva no dia for de 60% ou mais, rola "Leve guarda-chuva!" com um bipe.

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

## App do celular (`web/`)

É uma página web que funciona como app (PWA): só HTML, CSS e JS, sem etapa de compilação. Ela se conecta ao HiveMQ por WebSocket (porta 8884). O login usa uma credencial do HiveMQ, como a `clock_app`. A opção "Lembrar neste aparelho" guarda usuário e senha no navegador (localStorage).

Para testar no computador:

```bash
cd ~/Projetos/esp32c3-clock/web && python3 -m http.server 8000
# abra http://localhost:8000
```

O app é publicado no GitHub Pages pelo arquivo `.github/workflows/pages.yml`, a cada `push` na `main` que altere a pasta `web/`. É preciso ativar uma vez: repositório → Settings → Pages → Source: **GitHub Actions**. No celular, abra a página e use "Adicionar à tela inicial".

## Ícones

Os ícones são desenhos 6×6 no arquivo `tools/gen_icons.py` (`#` = LED aceso). A maioria tem de 2 a 6 quadros de animação. Depois de editar, gere de novo o arquivo do firmware:

```bash
cd ~/Projetos/esp32c3-clock && python3 tools/gen_icons.py
```

## Comandos pelo serial

`help`, `info`, `name`, `bri`, `rot`, `beep`, `timbre`, `vol`, `night`, `icon`, `loc <lat> <lon>`, `fetch`, `anim`, `speed`, `auto <seg> [dur]`, `msg`, `alert <seg> <texto>` (`alert 0` para), `test`, `wifireset`. Os nomes em português também funcionam: `nome`, `noite`, `icone`, `atualizar`, `animacao`, `velocidade`, `alerta`, `teste`.

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
- [x] **Etapa 3**: app do celular (PWA no GitHub Pages) com login
- [x] **Etapa 4**: meteorologia (Open-Meteo) e cotações (AwesomeAPI) buscadas pelo próprio relógio; localização por GPS, intervalos, telas e cotações escolhidos no app; aviso de chuva
- [ ] **Etapa 4.1**: Ibovespa e inscritos do YouTube (exigem chave de API → lado do servidor)
- [ ] **Etapa 5**: fase da lua, posição real do Sol e da Lua pelas coordenadas, ícones animados (clima, lua, Jogo da Vida, chuva de pixels), pomodoro, cronômetro, contagem regressiva
- [ ] **Etapa 6**: OTA via GitHub Releases, aviso do portão, notificações no celular (ntfy)
