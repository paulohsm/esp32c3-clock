// esp32c3-clock phone app: talks to the clocks through HiveMQ Cloud over MQTT/WebSocket.
'use strict';

const BROKER_URL = 'wss://4b9a673f16e84df093793b8d8768d7f6.s1.eu.hivemq.cloud:8884/mqtt';
const STORE_KEY = 'esp32c3-clock.creds';
const LAST_KEY = 'esp32c3-clock.last';

const DAY_NAMES = ['Dom', 'Seg', 'Ter', 'Qua', 'Qui', 'Sex', 'Sáb'];
const TIMBRES = ['Clássico', 'Agudo', 'Suave', 'Carrilhão'];

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

let client = null;
const devices = {};      // id -> { online, info, config, schedules }
let current = null;      // selected device id

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
  const dev = devices[id] || (devices[id] = { online: false, info: {}, config: null, schedules: [] });

  let data = null;
  if (kind !== 'online') {
    try { data = JSON.parse(text); } catch (_) { return; }
  }

  switch (kind) {
    case 'online': dev.online = text === '1'; break;
    case 'info': dev.info = data || {}; break;
    case 'config': dev.config = data; break;
    case 'schedules': dev.schedules = Array.isArray(data) ? data : []; break;
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
  sync: 'Dados atualizados.',
  reboot: 'Reiniciando…',
};
function handleAck(a) {
  if (!a) return;
  if (a.ok) toast(ACK_OK[a.cmd] || 'OK');
  else toast('Erro: ' + (a.error || a.cmd), true);
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
  if (!kind || kind === 'info' || kind === 'online') renderInfo(d);
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

// ------------------------------------------------------------ actions

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
  for (const sel of $$('select.hours')) {
    for (let h = 0; h < 24; h++) sel.append(el('option', { value: h }, String(h).padStart(2, '0') + 'h'));
  }
  TIMBRES.forEach((name, i) => {
    $('#cfgTimbre').append(el('option', { value: i }, name));
    $('#schedTimbre').append(el('option', { value: i }, name));
  });
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
  $('#testBeep').addEventListener('click', () => publish('cmd/beep', { timbre: Number($('#cfgTimbre').value) }));
  $('#syncBtn').addEventListener('click', () => publish('cmd/sync', ''));
  $('#rebootBtn').addEventListener('click', () => { if (confirm('Reiniciar o relógio?')) publish('cmd/reboot', ''); });
}

function init() {
  buildStaticControls();
  wire();
  if ('serviceWorker' in navigator) navigator.serviceWorker.register('sw.js').catch(() => {});

  const saved = load(STORE_KEY);
  if (saved && saved.user) {
    $('#user').value = saved.user;
    $('#pass').value = saved.pass || '';
    $('#remember').checked = true;
    connect(saved.user, saved.pass);
  }
}

document.addEventListener('DOMContentLoaded', init);
