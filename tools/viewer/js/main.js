import { BackendApi } from './api.js';
import { GatewayLink, loadContracts } from './net.js';
import { World } from './world.js';
import { Renderer } from './render.js';

const WORLD_ID = 1;
const CLASS_ID = 1;
const CLIENT_TICK_HZ = 20;
const PING_INTERVAL_MS = 1000;

const $ = (id) => document.getElementById(id);
const status = (text, kind = '') => { $('status').textContent = text; $('status').className = `status ${kind}`; };
const show = (id, visible) => $(id).classList.toggle('hidden', !visible);

const world = new World();
const renderer = new Renderer($('world'));
let api = null;
let link = null;
let contracts = null;
let obstacles = [];
let rtt = null;
let tickTimer = null;
let pingTimer = null;
let pingSentAt = 0;
const keys = new Set();

async function loadMapObstacles() {
  // The viewer draws the same map the zone loads; a tiny TOML subset is enough here.
  const text = await (await fetch('/game-data/maps/zone-1.toml')).text();
  const match = text.match(/obstacles\s*=\s*\[([\s\S]*?)\n\]/);
  if (!match) return [];
  return [...match[1].matchAll(/\[\s*([\d.]+),\s*([\d.]+),\s*([\d.]+),\s*([\d.]+)\s*\]/g)].map(m => m.slice(1, 5).map(Number));
}

async function refreshCharacters() {
  const list = $('character-list');
  list.innerHTML = '';
  for (const c of await api.listCharacters(WORLD_ID)) {
    const li = document.createElement('li');
    li.innerHTML = `<span>${c.name} <small>(class ${c.classId})</small></span>`;
    const button = document.createElement('button');
    button.textContent = 'Enter';
    button.onclick = () => enter(c.characterId);
    li.appendChild(button);
    list.appendChild(li);
  }
}

async function enter(characterId) {
  try {
    const grant = await api.enter(WORLD_ID, characterId);
    world.reset();
    link = new GatewayLink(contracts, {
      onWelcome: (welcome) => { world.applyWelcome(welcome); status(`in zone ${welcome.zone_id}`, 'ok'); startLoops(); },
      onSnapshot: (snapshot) => world.applySnapshot(snapshot, performance.now(), Number($('loss-pct').value) / 100),
      onPong: () => { rtt = Math.round(performance.now() - pingSentAt); },
      onKick: (reason) => status(`kicked: ${reason}`, 'error'),
      onClose: () => { stopLoops(); if (!$('status').classList.contains('error')) status('disconnected'); },
    });
    // The Backend advertises the gateway's WebSocket port; ?ws= overrides it for port-forwarded setups.
    const wsPort = Number(new URLSearchParams(location.search).get('ws') ?? grant.gateway.webSocketPort);
    link.connect(`ws://${grant.gateway.host}:${wsPort}/game`, grant.entryTicket);
    show('panel-world', true);
    status('connecting…');
  } catch (e) { status(e.message, 'error'); }
}

function startLoops() {
  stopLoops();
  tickTimer = setInterval(() => {
    const dir = { x: (keys.has('d') ? 1 : 0) - (keys.has('a') ? 1 : 0), y: (keys.has('w') ? 1 : 0) - (keys.has('s') ? 1 : 0) };
    link.sendMove(dir);
  }, 1000 / CLIENT_TICK_HZ);
  pingTimer = setInterval(() => { pingSentAt = performance.now(); link.sendPing(Math.floor(pingSentAt) & 0xffff); }, PING_INTERVAL_MS);
}

function stopLoops() {
  clearInterval(tickTimer); clearInterval(pingTimer);
  tickTimer = pingTimer = null;
}

function frame() {
  const bufferMs = Number($('buffer-ms').value);
  const modes = renderer.draw(world, performance.now() - bufferMs, $('extrapolate').checked, obstacles);
  $('buffer-value').textContent = bufferMs;
  $('loss-value').textContent = $('loss-pct').value;
  const stats = {
    'server tick': world.serverTick,
    'acked input': `${world.lastAckedInput} / ${link?.inputSeq ?? 0}`,
    'visible': world.entities.size,
    'interp / extrap / hold': `${modes.interp} / ${modes.extrap} / ${modes.hold}`,
    'snapshots applied / dropped': `${world.snapshots} / ${world.dropped}`,
    'integrity errors': world.integrityErrors,
    'ping rtt': rtt === null ? '-' : `${rtt} ms`,
    'self': `${world.self.x.toFixed(1)}, ${world.self.y.toFixed(1)}`,
  };
  $('stats').innerHTML = Object.entries(stats).map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
  requestAnimationFrame(frame);
}

async function main() {
  contracts = await loadContracts();
  obstacles = await loadMapObstacles();
  $('btn-register').onclick = async () => {
    api = new BackendApi($('backend-url').value);
    try { await api.register($('username').value, $('password').value); status('registered', 'ok'); }
    catch (e) { status(e.message, 'error'); }
  };
  $('btn-login').onclick = async () => {
    api = new BackendApi($('backend-url').value);
    try {
      await api.login($('username').value, $('password').value);
      status('logged in', 'ok');
      show('panel-characters', true);
      await refreshCharacters();
    } catch (e) { status(e.message, 'error'); }
  };
  $('btn-create').onclick = async () => {
    try { await api.createCharacter(WORLD_ID, $('new-character-name').value, CLASS_ID); await refreshCharacters(); }
    catch (e) { status(e.message, 'error'); }
  };
  $('btn-leave').onclick = () => { link?.close(); show('panel-world', false); };
  window.addEventListener('keydown', (e) => keys.add(e.key.toLowerCase()));
  window.addEventListener('keyup', (e) => keys.delete(e.key.toLowerCase()));
  requestAnimationFrame(frame);
  status('ready');
}

main().catch((e) => status(e.message, 'error'));
