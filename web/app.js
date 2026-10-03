// esp32c3-clock phone app: talks to the clocks through HiveMQ Cloud over MQTT/WebSocket.
'use strict';

const BROKER_URL = 'wss://4b9a673f16e84df093793b8d8768d7f6.s1.eu.hivemq.cloud:8884/mqtt';
const REPO = 'paulohsm/esp32c3-clock';          // firmware releases live here
const UPDATE_CHECK_MS = 6 * 3600 * 1000;
const STORE_KEY = 'esp32c3-clock.creds';
const LAST_KEY = 'esp32c3-clock.last';

const DAY_NAMES = ['Dom', 'Seg', 'Ter', 'Qua', 'Qui', 'Sex', 'Sáb'];
const TIMBRES = ['Clássico (bip-bip)', 'Ding-dong', 'Campainha', 'Big Ben', 'Cuco',
  'Micro-ondas', 'Notificação', 'Moeda', 'Suave', 'Passarinho'];
const MINUTES = [5, 10, 15, 20, 30, 60];
// ASCII expressions: the matrix font has no emoji, but these read well on it.
const EMOTES = [':)', ':D', ';)', ':(', ":'(", ':P', ':O', ':*', '<3', 'xD', '^_^', '-_-',
  'o_O', 'B)', '\\o/', '(y)', 'zzz', '\\(^o^)/', '(>_<)', '(^_^)/', '<(^_^)>', '(-_-)zzz'];
const QUOTE_NAMES = { USD: 'Dólar', EUR: 'Euro', GBP: 'Libra', BTC: 'Bitcoin', ETH: 'Ethereum' };

// "Jogos" tab: teams are searched by name in two sources.
//  • ESPN: the clock itself fetches the games and the live score (big leagues, national teams).
//  • Sofascore: everything else (lower divisions, state leagues, other sports). It refuses
//    devices, so this app sends the next game to the clock whenever it is opened.
const ESPN_SITE = 'https://site.web.api.espn.com/apis/site/v2/sports';
const ESPN_SEARCH = 'https://site.web.api.espn.com/apis/common/v3/search';
const SOFA = 'https://api.sofascore.com/api/v1';
const ESPN_SPORTS = { soccer: 'Futebol', basketball: 'Basquete', football: 'Futebol americano',
  baseball: 'Beisebol', hockey: 'Hóquei no gelo' };
const SOFA_SPORTS = { football: 'Futebol', futsal: 'Futsal', basketball: 'Basquete', volleyball: 'Vôlei',
  handball: 'Handebol', 'american-football': 'Futebol americano', baseball: 'Beisebol',
  'ice-hockey': 'Hóquei no gelo', rugby: 'Rúgbi', waterpolo: 'Polo aquático' };
const SYNC_MS = 20 * 60 * 1000;  // resend a Sofascore team's next game at most this often
const MAX_FOLLOWS = 8;

// WMO weather codes (Open-Meteo) → Portuguese description.
function weatherText(code) {
  if (code === 0) return 'Céu limpo';
  if (code === 1) return 'Poucas nuvens';
  if (code === 2) return 'Parcialmente nublado';
  if (code === 3) return 'Nublado';
  if (code === 45 || code === 48) return 'Neblina';
  if (code >= 51 && code <= 57) return 'Garoa';
  if (code >= 61 && code <= 67) return 'Chuva';
  if (code >= 80 && code <= 82) return 'Pancadas de chuva';
  if (code >= 95) return 'Trovoada';
  if (code >= 71 && code <= 86) return 'Neve';
  return '—';
}

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

let client = null;
const devices = {};      // id -> { online, info, config, schedules, weather, quotes }
let current = null;      // selected device id
let latest = null;       // newest release: { version, url, notes } (null = unknown)
let updateState = '';    // '' | 'checking' | 'error' | 'installing'
let installingFrom = null;  // firmware version the clock had when we asked it to update

// ------------------------------------------------------------ storage

function store(key, value) {
  try {
    if (value === null) localStorage.removeItem(key);
    else localStorage.setItem(key, JSON.stringify(value));
  } catch (_) { /* private mode etc. */ }
}
function load(key) {
  try { return JSON.parse(localStorage.getItem(key)); } catch (_) { return null; }
}

// ------------------------------------------------------------ UI helpers

let toastTimer;
function toast(text, isError = false) {
  const t = $('#toast');
  t.textContent = text;
  t.classList.toggle('err', isError);
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 2600);
}

function el(tag, attrs = {}, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v);
  }
  for (const c of children) e.append(c);
  return e;
}

function show(view) {
  for (const v of ['loginView', 'listView', 'deviceView']) $('#' + v).hidden = v !== view;
  $('#backBtn').hidden = view !== 'deviceView';
  $('#logoutBtn').hidden = view === 'loginView';
  if (view !== 'deviceView') $('#title').textContent = 'Relógio';
}

function setConn(on) {
  const d = $('#connDot');
  d.classList.toggle('on', on);
  d.title = on ? 'Conectado ao broker' : 'Desconectado';
}

function signal(rssi) {
  if (rssi == null) return '';
  if (rssi >= -55) return 'ótimo';
  if (rssi >= -67) return 'bom';
  if (rssi >= -75) return 'fraco';
  return 'muito fraco';
}

function uptime(s) {
  if (s == null) return '—';
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + 'd ' : '') + (d || h ? h + 'h ' : '') + m + 'min';
}

// ------------------------------------------------------------ MQTT

function connect(user, pass) {
  $('#loginError').hidden = true;
  $('#loginBtn').disabled = true;
  $('#loginBtn').textContent = 'Conectando…';

  if (typeof mqtt === 'undefined') {
    loginFailed('Não foi possível carregar a biblioteca MQTT. Verifique a internet.');
    return;
  }

  let everConnected = false;
  client = mqtt.connect(BROKER_URL, {
    username: user,
    password: pass,
    clientId: 'app-' + Math.random().toString(16).slice(2, 10),
    clean: true,
    connectTimeout: 10000,
    reconnectPeriod: 3000,
  });

  client.on('connect', () => {
    everConnected = true;
    setConn(true);
    client.subscribe('clock/+/online', { qos: 1 });
    client.subscribe('clock/+/info', { qos: 1 });
    client.subscribe('clock/+/config', { qos: 1 });
    client.subscribe('clock/+/schedules', { qos: 1 });
    client.subscribe('clock/+/ack', { qos: 1 });
    client.subscribe('clock/+/data/+', { qos: 1 });
    client.subscribe('clock/+/alert', { qos: 1 });
    client.subscribe('clock/+/sports', { qos: 1 });
    if ($('#loginView').hidden === false) {
      if ($('#remember').checked) store(STORE_KEY, { user, pass });
      const last = load(LAST_KEY);
      show('listView');
      renderList();
      if (last) setTimeout(() => { if (devices[last]) openDevice(last); }, 800);
    }
  });

  client.on('error', (err) => {
    const msg = String(err && err.message || err);
    if (!everConnected) {
      client.end(true);
      loginFailed(/auth|password|user/i.test(msg) ? 'Usuário ou senha incorretos.' : 'Falha ao conectar: ' + msg);
    }
  });

  client.on('offline', () => setConn(false));
  client.on('close', () => setConn(false));
  client.on('message', onMessage);
}

function loginFailed(text) {
  $('#loginBtn').disabled = false;
  $('#loginBtn').textContent = 'Conectar';
  $('#loginError').textContent = text;
  $('#loginError').hidden = false;
  setConn(false);
}

function logout() {
  if (client) client.end(true);
  client = null;
  store(STORE_KEY, null);
  for (const k of Object.keys(devices)) delete devices[k];
  current = null;
  $('#pass').value = '';
  $('#loginBtn').disabled = false;
  $('#loginBtn').textContent = 'Conectar';
  setConn(false);
  show('loginView');
}

function publish(suffix, payload) {
  if (!client || !client.connected || !current) {
    toast('Sem conexão com o broker.', true);
    return;
  }
  const body = typeof payload === 'string' ? payload : JSON.stringify(payload);
  client.publish(`clock/${current}/${suffix}`, body, { qos: 1 });
}

function onMessage(topic, buf) {
  const m = /^clock\/([^/]+)\/(.+)$/.exec(topic);
  if (!m) return;
  const [, id, kind] = m;
  const text = buf.toString();
  const dev = devices[id] || (devices[id] = {
    online: false, info: {}, config: null, schedules: [], weather: null, quotes: {}, alert: null,
    sports: null,
  });

  let data = null;
  if (kind !== 'online') {
    try { data = JSON.parse(text); } catch (_) { return; }
  }

  switch (kind) {
    case 'online': dev.online = text === '1'; break;
    case 'info': dev.info = data || {}; break;
    case 'config': dev.config = data; break;
    case 'schedules': dev.schedules = Array.isArray(data) ? data : []; break;
    case 'data/weather': dev.weather = data && data.valid ? data : null; break;
    case 'data/quotes': dev.quotes = data || {}; break;
    case 'alert': dev.alert = data; break;
    case 'sports': dev.sports = data; syncSofaTeams(id); break;
    case 'ack':
      if (id === current) handleAck(data);
      return;
    default: return;
  }

  renderList();
  if (id === current) renderDevice(kind);
}

const ACK_OK = {
  msg: 'Mensagem enviada.',
  schedule: 'Agenda atualizada.',
  config: 'Ajuste salvo.',
  beep: 'Tocando…',
  sync: 'Atualizando dados…',
  ota: 'Relógio baixando o firmware…',
  reboot: 'Reiniciando…',
  show: 'Mostrando no relógio.',
  alert: 'Alerta atualizado.',
  sports: 'Lista de jogos atualizada.',
};
const ERRORS = {
  'already followed': 'Já está na lista.',
  'list full (8)': 'Lista cheia: no máximo 8 itens.',
  'index not found': 'Item não encontrado; atualize a lista.',
};
function handleAck(a) {
  if (!a) return;
  if (a.cmd === 'ota' && !a.ok) { updateState = ''; installingFrom = null; renderUpdate(); }
  if (a.ok) toast(ACK_OK[a.cmd] || 'OK');
  else toast('Erro: ' + (ERRORS[a.error] || a.error || a.cmd), true);
}

// ------------------------------------------------------------ list view

function renderList() {
  const list = $('#deviceList');
  list.replaceChildren();
  const ids = Object.keys(devices).filter((id) => devices[id].info.name || devices[id].config);
  $('#emptyList').hidden = ids.length > 0;
  ids.sort((a, b) => (devices[b].online - devices[a].online) || a.localeCompare(b));
  for (const id of ids) {
    const d = devices[id];
    const name = (d.config && d.config.name) || d.info.name || id;
    const sub = d.online
      ? `Online · Wi-Fi ${signal(d.info.rssi)} · v${d.info.fw || '?'}`
      : 'Offline';
    list.append(el('button', { class: 'device', onclick: () => openDevice(id) },
      el('span', { class: 'dot' + (d.online ? ' on' : '') }),
      el('span', { class: 'grow' }, el('div', { class: 'name' }, name), el('div', { class: 'sub' }, sub)),
      el('span', { class: 'chev' }, '›')));
  }
}

// ------------------------------------------------------------ device view

function openDevice(id) {
  current = id;
  store(LAST_KEY, id);
  show('deviceView');
  selectTab('msg');
  renderDevice();
}

function selectTab(name) {
  for (const b of $$('.tabs button')) b.classList.toggle('active', b.dataset.tab === name);
  for (const p of $$('[data-panel]')) p.hidden = p.dataset.panel !== name;
}

function renderDevice(kind) {
  const d = devices[current];
  if (!d) return;
  $('#title').textContent = (d.config && d.config.name) || d.info.name || current;
  $('#offlineBanner').hidden = d.online;
  if (!kind || kind === 'config') renderConfig(d.config);
  if (!kind || kind === 'schedules') renderSchedules(d.schedules);
  if (!kind || kind === 'info' || kind === 'online') { renderInfo(d); renderUpdate(); }
  if (!kind || kind.startsWith('data/')) renderNow(d);
  if (!kind || kind === 'alert') renderAlert(d.alert);
  if (!kind || kind === 'sports') renderSports(d.sports);
}

function renderConfig(c) {
  if (!c) return;
  for (const input of $$('[data-key]')) {
    const key = input.dataset.key;
    if (!(key in c)) continue;
    if (document.activeElement === input && input.type !== 'range') continue;  // don't fight the user
    if (input.type === 'checkbox') input.checked = !!c[key];
    else input.value = c[key];
    if (key === 'brightness' && c.brightnessMax != null) input.max = c.brightnessMax;
  }
  for (const v of $$('[data-val]')) v.textContent = c[v.dataset.val] ?? '';
  for (const group of $$('[data-mask]')) {
    const mask = c[group.dataset.mask] ?? 0;
    for (const cb of group.querySelectorAll('input[data-bit]')) {
      cb.checked = (mask >> Number(cb.dataset.bit)) & 1;
    }
  }
  if (c.morningHour != null && document.activeElement !== $('#morningTime')) {
    $('#morningTime').value = `${String(c.morningHour).padStart(2, '0')}:${String(c.morningMin).padStart(2, '0')}`;
  }
  $('#dialOptions').hidden = Number(c.clockIcon) !== 0;
  $('#blinkOption').hidden = Number(c.fillStyle) !== 0;
  $('#placeName').textContent = c.place || '—';
  $('#placeCoords').textContent = c.lat != null ? `${c.lat.toFixed(4)}, ${c.lon.toFixed(4)}` : '';
}

function fmtNum(v, digits) {
  return v.toLocaleString('pt-BR', { minimumFractionDigits: digits, maximumFractionDigits: digits });
}

function renderNow(d) {
  const dl = $('#nowList');
  dl.replaceChildren();
  const w = d.weather;
  const row = (k, ...v) => dl.append(el('dt', {}, k), el('dd', {}, ...v));
  if (w) {
    row('Tempo', `${Math.round(w.temp)} °C, ${weatherText(w.code).toLowerCase()}`);
    row('Sensação', `${Math.round(w.feels)} °C · umidade ${w.humidity}%`);
    row('Hoje', `mín ${Math.round(w.tMin)} °C · máx ${Math.round(w.tMax)} °C`);
    row('Chuva', `${w.rainDay}% hoje · ${w.rainNext}% nas próximas 3h`);
    row('UV', `${Math.round(w.uv)} agora · máx ${Math.round(w.uvMax)}`);
    row('Sol', `nasce ${w.sunrise} · põe ${w.sunset}`);
  } else {
    row('Tempo', 'aguardando dados…');
  }
  for (const code of Object.keys(QUOTE_NAMES)) {
    const q = d.quotes && d.quotes[code];
    if (!q) continue;
    const digits = q.bid < 100 ? 2 : 0;
    const pct = el('span', { class: q.pct >= 0 ? 'up' : 'down' },
      ` ${q.pct >= 0 ? '▲' : '▼'} ${fmtNum(Math.abs(q.pct), 2)}%`);
    row(QUOTE_NAMES[code], `R$ ${fmtNum(q.bid, digits)}`, pct);
  }
  const at = [w && w.at, ...Object.values(d.quotes || {}).map((q) => q.at)].filter(Boolean);
  if (at.length) {
    const t = new Date(Math.max(...at) * 1000);
    row('Atualizado', t.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' }));
  }
}

const ENDED_BY = { touch: 'desarmado no relógio (toque)', app: 'cancelado pelo app', timeout: 'terminou por tempo' };

function hhmm(epoch) {
  return new Date(epoch * 1000).toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' });
}

function renderAlert(a) {
  const st = $('#alertStatus');
  $('#alertCancel').hidden = !(a && a.active);
  $('#alertSend').hidden = !!(a && a.active);
  if (!a || !a.text) { st.hidden = true; return; }
  st.hidden = false;
  st.classList.toggle('on', !!a.active);
  st.textContent = a.active
    ? `ATIVO até ${hhmm(a.until)}: "${a.text}"`
    : `Último alerta "${a.text}": ${ENDED_BY[a.endedBy] || a.endedBy} às ${hhmm(a.at)}.`;
}

function sendAlert() {
  const text = $('#alertText').value.trim();
  if (!text) { toast('Escreva o texto do alerta.', true); return; }
  const secs = Number($('#alertSecs').value);
  if (!confirm(`Disparar alerta de emergência por ${$('#alertSecs').selectedOptions[0].text}?`)) return;
  publish('cmd/alert', { text, seconds: secs });
}

// A group may hold only some bits of a mask (e.g. the "Jogos" screen in its own tab):
// the other bits keep the clock's current value.
function onMaskChange(group) {
  const key = group.dataset.mask;
  const c = devices[current] && devices[current].config;
  let mask = (c && c[key]) || 0;
  for (const cb of group.querySelectorAll('input[data-bit]')) {
    const bit = 1 << Number(cb.dataset.bit);
    mask = cb.checked ? mask | bit : mask & ~bit;
  }
  if (c) c[key] = mask;  // so a quick second change builds on this one
  publish('config/set', { [key]: mask });
}

async function useGps() {
  if (!('geolocation' in navigator)) { toast('Este navegador não oferece localização.', true); return; }
  const btn = $('#gpsBtn');
  btn.disabled = true;
  btn.textContent = 'Obtendo localização…';
  try {
    const pos = await new Promise((ok, fail) =>
      navigator.geolocation.getCurrentPosition(ok, fail, { enableHighAccuracy: false, timeout: 15000 }));
    const lat = Math.round(pos.coords.latitude * 10000) / 10000;
    const lon = Math.round(pos.coords.longitude * 10000) / 10000;
    let place = `${lat.toFixed(3)}, ${lon.toFixed(3)}`;
    try {
      const r = await fetch('https://api.bigdatacloud.net/data/reverse-geocode-client'
        + `?latitude=${lat}&longitude=${lon}&localityLanguage=pt`);
      const g = await r.json();
      const city = g.city || g.locality || '';
      const local = g.locality && g.locality !== city ? g.locality + ', ' : '';
      if (city) place = (local + city).slice(0, 31);
    } catch (_) { /* keep coordinates as the name */ }
    publish('config/set', { lat, lon, place });
  } catch (err) {
    toast(err && err.code === 1 ? 'Permissão de localização negada.' : 'Não foi possível obter a localização.', true);
  } finally {
    btn.disabled = false;
    btn.textContent = 'Usar minha localização (GPS)';
  }
}

function describeWhen(s) {
  if (s.date) {
    const [y, m, d] = s.date.split('-');
    return `em ${d}/${m}/${y}`;
  }
  const days = s.days || [];
  if (days.length === 7) return 'todos os dias';
  if (days.join() === '1,2,3,4,5') return 'seg a sex';
  if (days.join() === '0,6') return 'fins de semana';
  return days.map((i) => DAY_NAMES[i]).join(', ');
}

function renderSchedules(list) {
  const box = $('#schedList');
  box.replaceChildren();
  $('#schedEmpty').hidden = list.length > 0;
  const sorted = [...list].sort((a, b) => a.time.localeCompare(b.time));
  for (const s of sorted) {
    const title = el('div', { class: 'text' }, s.text);
    if (s.alarm) title.append(el('span', { class: 'tag' }, 'alarme'));
    box.append(el('div', { class: 'sched' },
      el('span', { class: 'time' }, s.time),
      el('span', { class: 'grow' }, title, el('div', { class: 'sub' }, describeWhen(s))),
      el('button', {
        class: 'danger',
        'aria-label': 'Apagar',
        onclick: () => {
          if (confirm(`Apagar "${s.text}" (${s.time})?`)) publish('cmd/schedule', { action: 'delete', id: s.id });
        },
      }, 'Apagar')));
  }
}

function renderInfo(d) {
  const i = d.info || {};
  const rows = [
    ['Estado', d.online ? 'Online' : 'Offline'],
    ['ID', current],
    ['Firmware', i.fw ? 'v' + i.fw : '—'],
    ['Wi-Fi', i.ssid ? `${i.ssid} (${i.rssi} dBm, ${signal(i.rssi)})` : '—'],
    ['IP local', i.ip || '—'],
    ['Ligado há', uptime(i.uptime)],
    ['Agendamentos', i.schedules ?? (d.schedules || []).length],
  ];
  const dl = $('#infoList');
  dl.replaceChildren();
  for (const [k, v] of rows) dl.append(el('dt', {}, k), el('dd', {}, String(v)));
}

// ------------------------------------------------------------ sports

function fmtWhen(epoch) {
  const t = new Date(epoch * 1000);
  const today = new Date();
  const days = Math.round((new Date(t.getFullYear(), t.getMonth(), t.getDate())
    - new Date(today.getFullYear(), today.getMonth(), today.getDate())) / 86400000);
  const hm = t.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' });
  if (days === 0) return `hoje, ${hm}`;
  if (days === 1) return `amanhã, ${hm}`;
  const wd = t.toLocaleDateString('pt-BR', { weekday: 'short' }).replace('.', '');
  return `${wd} ${t.toLocaleDateString('pt-BR', { day: '2-digit', month: '2-digit' })}, ${hm}`;
}

function describeGame(f, g) {
  if (!g || !g.id) {
    if (f.league === 'sofascore') return 'Sem jogo marcado (o app confere ao ser aberto)';
    return f.team ? 'Procurando o próximo jogo…' : '—';
  }
  const sc = `${g.home} ${g.hs ?? 0} x ${g.as ?? 0} ${g.away}`;
  if (g.state === 'in') return sc + (g.detail ? ` · ${g.detail}` : '');
  if (g.state === 'post') return 'Fim: ' + sc;
  if (f.league === 'sofascore' && Date.now() / 1000 >= g.start) return `${g.home} x ${g.away} · em andamento`;
  return `${g.home} x ${g.away} · ${fmtWhen(g.start)}`;
}

function renderSports(sp) {
  const box = $('#sportList');
  box.replaceChildren();
  const follows = (sp && sp.follows) || [];
  const games = (sp && sp.games) || [];
  $('#sportEmpty').hidden = follows.length > 0;
  follows.forEach((f, i) => {
    const g = games[i] || {};
    const title = el('div', { class: 'text' }, f.label || f.team || f.event);
    title.append(el('span', { class: 'tag' }, f.team ? 'time' : 'jogo'));
    if (g.state === 'in') title.append(el('span', { class: 'tag live' }, 'ao vivo'));
    box.append(el('div', { class: 'sched game' },
      el('span', { class: 'grow' }, title, el('div', { class: 'sub score' }, describeGame(f, g))),
      el('button', {
        class: 'danger',
        onclick: () => {
          if (confirm(`Deixar de seguir "${f.label}"?`)) publish('cmd/sports', { action: 'remove', index: i });
        },
      }, 'Remover')));
  });
}

const normName = (n) => n.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase().trim();

async function getJson(url) {
  const r = await fetch(url);
  if (r.status === 404) return null;  // Sofascore: "no games"
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

async function espnSearch(q) {
  const data = await getJson(`${ESPN_SEARCH}?query=${encodeURIComponent(q)}&type=team&limit=15&lang=pt&region=br`);
  return ((data && data.items) || [])
    .filter((t) => t.type === 'team' && ESPN_SPORTS[t.sport])
    .map((t) => ({
      src: 'espn', id: String(t.id), name: t.displayName, sport: t.sport,
      league: t.league || t.defaultLeagueSlug || '',
    }));
}

function countryName(c) {
  if (!c) return '';
  try { if (c.alpha2) return new Intl.DisplayNames(['pt-BR'], { type: 'region' }).of(c.alpha2); } catch (_) { /* old browser */ }
  return c.name || '';
}

async function sofaSearch(q) {
  const data = await getJson(`${SOFA}/search/all?q=${encodeURIComponent(q)}`);
  return ((data && data.results) || [])
    .filter((r) => r.type === 'team' && r.entity && r.entity.sport && SOFA_SPORTS[r.entity.sport.slug])
    .map(({ entity: e }) => ({
      src: 'sofa', id: String(e.id), name: e.name, sport: e.sport.slug,
      country: countryName(e.country), women: e.gender === 'F',
    }));
}

function teamNote(t) {
  if (t.src === 'espn') {
    const where = t.sport === 'soccer' ? 'todas as competições' : t.league.toUpperCase();
    return `${ESPN_SPORTS[t.sport]} · ${where} · placar ao vivo`;
  }
  const bits = [SOFA_SPORTS[t.sport] + (t.women ? ' feminino' : ''), t.country].filter(Boolean);
  return `${bits.join(' · ')} · só agenda (sem placar ao vivo)`;
}

let picked = null;  // team chosen in the search results

async function searchTeams(e) {
  e.preventDefault();
  const q = $('#spQuery').value.trim();
  if (q.length < 2) { toast('Digite ao menos 2 letras.', true); return; }
  const box = $('#spResults');
  $('#spPicked').hidden = true;
  box.replaceChildren(el('p', { class: 'muted' }, 'Buscando…'));
  const [espn, sofa] = await Promise.allSettled([espnSearch(q), sofaSearch(q)]);
  const a = espn.status === 'fulfilled' ? espn.value : [];
  const seen = new Set(a.map((t) => normName(t.name)));
  // A team ESPN has (with live score) is not repeated from Sofascore.
  const b = (sofa.status === 'fulfilled' ? sofa.value : []).filter((t) => !seen.has(normName(t.name)));
  box.replaceChildren();
  for (const t of [...a, ...b]) {
    box.append(el('div', { class: 'pick' },
      el('span', { class: 'grow' }, el('div', {}, t.name), el('div', { class: 'sub' }, teamNote(t))),
      el('button', { class: 'small', onclick: () => pickTeam(t) }, 'Escolher')));
  }
  if (!a.length && !b.length) box.append(el('p', { class: 'muted' }, 'Nenhum time encontrado.'));
  if (sofa.status === 'rejected') {
    box.append(el('p', { class: 'muted' }, 'O Sofascore não respondeu agora: times de divisões menores podem faltar. Tente de novo mais tarde.'));
  }
}

// Upcoming games of a team, soonest first.
async function teamGames(t) {
  const now = Date.now() / 1000;
  if (t.src === 'sofa') {
    const data = await getJson(`${SOFA}/team/${t.id}/events/next/0`);
    return ((data && data.events) || []).map((e) => ({
      id: String(e.id), start: e.startTimestamp,
      home: e.homeTeam.shortName || e.homeTeam.name, away: e.awayTeam.shortName || e.awayTeam.name,
      homeAbbr: e.homeTeam.nameCode || '', awayAbbr: e.awayTeam.nameCode || '',
      comp: (e.tournament && e.tournament.name) || '', league: 'sofascore',
    })).filter((g) => g.start > now - 3 * 3600).sort((x, y) => x.start - y.start).slice(0, 10);
  }
  const path = t.sport === 'soccer'
    ? `soccer/all/teams/${t.id}/schedule?fixture=true`      // every competition, friendlies too
    : `${t.sport}/${t.league}/teams/${t.id}/schedule?x=1`;
  const data = await getJson(`${ESPN_SITE}/${path}&lang=pt&region=br`);
  return ((data && data.events) || []).map((ev) => {
    const c = (ev.competitions || [])[0] || {};
    const side = (ha) => ((c.competitors || []).find((x) => x.homeAway === ha) || {}).team || {};
    const h = side('home'), w = side('away');
    return {
      id: ev.id, start: Math.round(Date.parse(ev.date) / 1000),
      done: !!(c.status && c.status.type && c.status.type.completed),
      home: h.shortDisplayName || h.displayName || '?', away: w.shortDisplayName || w.displayName || '?',
      homeAbbr: h.abbreviation || '', awayAbbr: w.abbreviation || '',
      comp: (ev.league && ev.league.name) || '', league: (ev.league && ev.league.slug) || t.league,
    };
  }).filter((g) => !g.done && g.start > now - 3 * 3600).sort((x, y) => x.start - y.start).slice(0, 10);
}

async function pickTeam(t) {
  picked = t;
  $('#spPicked').hidden = false;
  $('#spPickedName').textContent = t.name;
  $('#spPickedNote').textContent = teamNote(t);
  const box = $('#spGames');
  box.replaceChildren(el('p', { class: 'muted' }, 'Carregando jogos…'));
  $('#spPicked').scrollIntoView({ behavior: 'smooth', block: 'start' });
  try {
    const games = await teamGames(t);
    if (picked !== t) return;
    t.games = games;
    box.replaceChildren();
    if (!games.length) {
      box.append(el('p', { class: 'muted' }, 'Nenhum jogo marcado agora. Seguindo o time, o relógio mostra o próximo assim que for marcado.'));
      return;
    }
    for (const g of games) {
      box.append(el('div', { class: 'pick' },
        el('span', { class: 'grow' }, el('div', {}, `${g.home} x ${g.away}`),
          el('div', { class: 'sub' }, [fmtWhen(g.start), g.comp].filter(Boolean).join(' · '))),
        el('button', { class: 'small', onclick: () => followGame(t, g) }, 'Seguir')));
    }
  } catch (e) {
    box.replaceChildren(el('p', { class: 'muted' }, 'Não foi possível carregar os jogos.'));
  }
}

function listFull() {
  const sp = devices[current] && devices[current].sports;
  if (sp && (sp.follows || []).length >= MAX_FOLLOWS) {
    toast('Lista cheia: remova um item antes.', true);
    return true;
  }
  return false;
}

const gameFields = (g) => g
  ? { event: g.id, start: g.start, home: g.home, away: g.away, homeAbbr: g.homeAbbr, awayAbbr: g.awayAbbr }
  : {};

function followTeam() {
  const t = picked;
  if (!t || listFull()) return;
  if (t.src === 'espn') {
    publish('cmd/sports', {
      action: 'add', sport: t.sport, league: t.sport === 'soccer' ? 'all' : t.league, team: t.id,
      label: `${t.name} · ${t.sport === 'soccer' ? 'todos os jogos' : t.league.toUpperCase()}`,
    });
  } else {
    publish('cmd/sports', Object.assign({
      action: 'add', sport: t.sport, league: 'sofascore', team: t.id, label: `${t.name} · só agenda`,
    }, gameFields((t.games || [])[0])));
  }
}

function followGame(t, g) {
  if (listFull()) return;
  publish('cmd/sports', Object.assign({
    action: 'add', sport: t.sport, league: g.league,
    label: `${g.home} x ${g.away}${g.comp ? ' · ' + g.comp : ''}`,
  }, gameFields(g)));
}

// Sofascore teams: the clock can't look their games up, so the app sends the next one.
const lastSync = {};  // "clock/team" -> ms
async function syncSofaTeams(id) {
  const d = devices[id];
  if (!client || !client.connected || !d || !d.sports) return;
  for (const f of d.sports.follows || []) {
    if (f.league !== 'sofascore' || !f.team) continue;
    const key = `${id}/${f.team}`;
    if (Date.now() - (lastSync[key] || 0) < SYNC_MS) continue;
    lastSync[key] = Date.now();
    try {
      const g = (await teamGames({ src: 'sofa', id: f.team }))[0];
      if ((g ? g.id : '') === (f.event || '') && (!g || g.start === f.start)) continue;  // up to date
      client.publish(`clock/${id}/cmd/sports`,
        JSON.stringify(Object.assign({ action: 'next', team: f.team }, gameFields(g))), { qos: 1 });
    } catch (_) { lastSync[key] = 0; /* try again next time */ }
  }
}

// ------------------------------------------------------------ firmware update

function cmpVersion(a, b) {
  const pa = String(a).replace(/^v/, '').split('.').map(Number);
  const pb = String(b).replace(/^v/, '').split('.').map(Number);
  for (let i = 0; i < 3; i++) {
    const d = (pa[i] || 0) - (pb[i] || 0);
    if (d) return d;
  }
  return 0;
}

async function checkUpdate(manual = false) {
  updateState = 'checking';
  renderUpdate();
  try {
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`,
      { headers: { Accept: 'application/vnd.github+json' } });
    if (r.status === 404) { latest = { version: '0.0.0' }; updateState = ''; renderUpdate(); return; }
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const rel = await r.json();
    const asset = (rel.assets || []).find((a) => a.name === 'firmware.bin');
    latest = asset
      ? { version: rel.tag_name.replace(/^v/, ''), url: asset.browser_download_url, notes: rel.name || rel.tag_name }
      : { version: '0.0.0' };
    updateState = '';
    if (manual && !updateAvailable()) toast('O firmware já está na versão mais recente.');
  } catch (e) {
    updateState = 'error';
  }
  renderUpdate();
}

function updateAvailable(d = devices[current]) {
  const fw = d && d.info && d.info.fw;
  return !!(latest && latest.url && fw && cmpVersion(latest.version, fw) > 0);
}

function renderUpdate() {
  const d = devices[current];
  const fw = d && d.info && d.info.fw;
  const avail = updateAvailable(d);
  const text = $('#updateText');
  const btn = $('#updateBtn');
  $('#updateCard').classList.toggle('available', avail && updateState !== 'installing');
  $('.tabs button[data-tab="info"]').classList.toggle('badge', avail && updateState !== 'installing');

  if (updateState === 'installing') {
    if (fw && installingFrom && fw !== installingFrom) {
      toast(`Firmware atualizado para v${fw}.`);
      updateState = '';
      installingFrom = null;
      return renderUpdate();
    }
    text.textContent = 'Instalando… o relógio baixa o arquivo e reinicia sozinho (cerca de 1 minuto). Não o desligue.';
    btn.hidden = true;
    return;
  }
  btn.hidden = !avail;
  if (updateState === 'checking') text.textContent = 'Verificando atualizações…';
  else if (updateState === 'error') text.textContent = 'Não foi possível consultar o GitHub. Tente de novo mais tarde.';
  else if (!latest) text.textContent = 'Verificando atualizações…';
  else if (avail) text.textContent = `Atualização disponível: v${latest.version} (instalada: v${fw}).`;
  else text.textContent = fw ? `Firmware em dia (v${fw}).` : 'Aguardando informações do relógio…';
}

function startUpdate() {
  const d = devices[current];
  if (!updateAvailable(d)) return;
  if (!d.online) { toast('O relógio está offline.', true); return; }
  if (!confirm(`Instalar o firmware v${latest.version} no relógio? Ele vai reiniciar.`)) return;
  installingFrom = d.info.fw;
  updateState = 'installing';
  publish('cmd/ota', { url: latest.url, version: latest.version });
  renderUpdate();
}

// ------------------------------------------------------------ actions

// Insert at the cursor, with spaces around so it doesn't glue to words.
function insertEmote(e) {
  const ta = $('#msgText');
  const start = ta.selectionStart ?? ta.value.length;
  const end = ta.selectionEnd ?? ta.value.length;
  const before = ta.value.slice(0, start);
  const after = ta.value.slice(end);
  const pre = before && !before.endsWith(' ') ? ' ' : '';
  const post = after.startsWith(' ') ? '' : ' ';
  const text = pre + e + post;
  if ((before + text + after).length > ta.maxLength) { toast('Mensagem no limite de tamanho.', true); return; }
  ta.value = before + text + after;
  const pos = before.length + text.length;
  ta.focus();
  ta.setSelectionRange(pos, pos);
}

function sendMessage() {
  const text = $('#msgText').value.trim();
  if (!text) { toast('Escreva uma mensagem.', true); return; }
  publish('cmd/msg', { text, beep: $('#msgBeep').checked, repeat: Number($('#msgRepeat').value) });
  $('#msgText').value = '';
}

function onConfigChange(e) {
  const input = e.target;
  const key = input.dataset.key;
  let value;
  if (input.type === 'checkbox') value = input.checked;
  else if (input.type === 'range' || input.tagName === 'SELECT') value = Number(input.value);
  else value = input.value.trim();
  if (key === 'name' && !value) return;
  publish('config/set', { [key]: value });
  // Show only the options that apply, right away (the clock confirms a moment later).
  if (key === 'clockIcon') $('#dialOptions').hidden = value !== 0;
  if (key === 'fillStyle') $('#blinkOption').hidden = value !== 0;
}

function addSchedule(e) {
  e.preventDefault();
  const alarm = document.querySelector('input[name=kind]:checked').value === 'alarm';
  const time = $('#schedTime').value;
  const text = $('#schedText').value.trim();
  if (!time) { toast('Escolha o horário.', true); return; }
  if (!alarm && !text) { toast('Escreva o texto da mensagem.', true); return; }

  const cmd = { action: 'add', time, alarm };
  if (text) cmd.text = text;
  const when = $('#schedWhen').value;
  if (when === 'date') {
    if (!$('#schedDate').value) { toast('Escolha a data.', true); return; }
    cmd.date = $('#schedDate').value;
  } else if (when === 'days') {
    const days = $$('#schedDaysWrap input:checked').map((i) => Number(i.value));
    if (!days.length) { toast('Marque ao menos um dia.', true); return; }
    cmd.days = days;
  }
  const timbre = $('#schedTimbre').value;
  if (timbre !== '') cmd.timbre = Number(timbre);

  publish('cmd/schedule', cmd);
  $('#schedText').value = '';
}

// ------------------------------------------------------------ setup

function buildStaticControls() {
  for (const sel of $$('select.minutes')) {
    for (const m of MINUTES) sel.append(el('option', { value: m }, m < 60 ? `a cada ${m} min` : 'a cada 1 h'));
  }
  for (const sel of $$('select.hours')) {
    for (let h = 0; h < 24; h++) sel.append(el('option', { value: h }, String(h).padStart(2, '0') + 'h'));
  }
  TIMBRES.forEach((name, i) => {
    $('#cfgTimbre').append(el('option', { value: i }, name));
    $('#schedTimbre').append(el('option', { value: i }, name));
  });
  for (const e of EMOTES) {
    $('#emotes').append(el('button', { type: 'button', onclick: () => insertEmote(e) }, e));
  }
  DAY_NAMES.forEach((name, i) => {
    $('#schedDaysWrap').append(el('label', {},
      el('input', Object.assign({ type: 'checkbox', value: i }, i >= 1 && i <= 5 ? { checked: '' } : {})), name));
  });
}

function wire() {
  $('#loginForm').addEventListener('submit', (e) => {
    e.preventDefault();
    connect($('#user').value.trim(), $('#pass').value);
  });
  $('#logoutBtn').addEventListener('click', () => { if (confirm('Sair e esquecer a senha?')) logout(); });
  $('#backBtn').addEventListener('click', () => { current = null; show('listView'); renderList(); });

  for (const b of $$('.tabs button')) b.addEventListener('click', () => selectTab(b.dataset.tab));

  $('#msgSend').addEventListener('click', sendMessage);
  $('#alertSend').addEventListener('click', sendAlert);
  $('#alertCancel').addEventListener('click', () => publish('cmd/alert', { cancel: true }));
  for (const b of $$('[data-show]')) b.addEventListener('click', () => publish('cmd/show', b.dataset.show));
  $('#schedForm').addEventListener('submit', addSchedule);
  $('#schedWhen').addEventListener('change', () => {
    $('#schedDateWrap').hidden = $('#schedWhen').value !== 'date';
    $('#schedDaysWrap').hidden = $('#schedWhen').value !== 'days';
  });

  for (const input of $$('[data-key]')) {
    input.addEventListener('change', onConfigChange);
    if (input.type === 'range') {
      input.addEventListener('input', () => {
        const v = $(`[data-val="${input.dataset.key}"]`);
        if (v) v.textContent = input.value;
      });
    }
  }
  for (const group of $$('[data-mask]')) {
    for (const cb of group.querySelectorAll('input[data-bit]')) cb.addEventListener('change', () => onMaskChange(group));
  }
  $('#gpsBtn').addEventListener('click', useGps);
  $('#spSearch').addEventListener('submit', searchTeams);
  $('#spFollowTeam').addEventListener('click', followTeam);
  $('#updateBtn').addEventListener('click', startUpdate);
  $('#checkBtn').addEventListener('click', () => checkUpdate(true));
  $('#morningTime').addEventListener('change', () => {
    const [h, m] = $('#morningTime').value.split(':').map(Number);
    if (!Number.isNaN(h) && !Number.isNaN(m)) publish('config/set', { morningHour: h, morningMin: m });
  });
  $('#testBeep').addEventListener('click', () => publish('cmd/beep', { timbre: Number($('#cfgTimbre').value) }));
  $('#syncBtn').addEventListener('click', () => publish('cmd/sync', ''));
  $('#rebootBtn').addEventListener('click', () => { if (confirm('Reiniciar o relógio?')) publish('cmd/reboot', ''); });
}

function init() {
  buildStaticControls();
  wire();
  if ('serviceWorker' in navigator) navigator.serviceWorker.register('sw.js').catch(() => {});
  checkUpdate();
  setInterval(checkUpdate, UPDATE_CHECK_MS);

  const saved = load(STORE_KEY);
  if (saved && saved.user) {
    $('#user').value = saved.user;
    $('#pass').value = saved.pass || '';
    $('#remember').checked = true;
    connect(saved.user, saved.pass);
  }
}

document.addEventListener('DOMContentLoaded', init);
