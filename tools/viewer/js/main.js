import { BackendApi } from './api.js';
import { GatewayLink, loadContracts } from './net.js';
import { World } from './world.js';
import { Renderer } from './render.js';

const WORLD_ID = 1;
const CLASS_ID = 1;
const CLIENT_TICK_HZ = 20;
const PING_INTERVAL_MS = 1000;
// Skill ids from game-data/skills/skills.toml; class 1 (warrior) knows 1 and 2.
const SKILL_STRIKE = 1;
const SKILL_HEAVY_BLOW = 2;
const STRIKE_RANGE_M = 2.5;

const $ = (id) => document.getElementById(id);
const status = (text, kind = '') => { $('status').textContent = text; $('status').className = `status ${kind}`; };
const show = (id, visible) => $(id).classList.toggle('hidden', !visible);

const world = new World();
window.__world = world;  // for automated checks
const renderer = new Renderer($('world'));
let api = null;
let link = null;
let contracts = null;
let obstacles = [];
let rtt = null;
let tickTimer = null;
let pingTimer = null;
let pingSentAt = 0;
let lastDirection = { x: 0, y: 1 };
const keys = new Set();
const pendingCasts = new Map();  // request id -> skill name, until the result arrives

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
    if (link) {
      // Entering again: silence the old link first, or its close would stop the new one's loops.
      const previous = link;
      link = null;
      previous.handlers = {};
      previous.close();
      stopLoops();
    }
    world.reset();
    link = new GatewayLink(contracts, {
      onWelcome: (welcome) => { world.applyWelcome(welcome); status(`in zone ${welcome.zone_id}`, 'ok'); startLoops(); },
      onSnapshot: (snapshot) => world.applySnapshot(snapshot, performance.now(), Number($('loss-pct').value) / 100),
      onPong: () => { rtt = Math.round(performance.now() - pingSentAt); },
      onCommandResult: (result, rejectCode) => {
        const name = pendingCasts.get(result.request_id) ?? `request ${result.request_id}`;
        pendingCasts.delete(result.request_id);
        const accepted = result.status === contracts.CommandStatus.values.COMMAND_STATUS_ACCEPTED;
        world.note(accepted ? `${name}: accepted` : `${name}: ${rejectCode}`);
      },
      onCombatEvent: (e) => {
        const who = String(e.attacker_id) === world.selfId ? 'you' : `#${e.attacker_id}`;
        const whom = String(e.target_id) === world.selfId ? 'you' : `#${e.target_id}`;
        world.note(`${who} hit ${whom} for ${e.damage}${e.killed ? ' (killed)' : ` (${e.target_hp} hp)`}`);
      },
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
    if (dir.x || dir.y) lastDirection = dir;
    link.sendMove(dir);
  }, 1000 / CLIENT_TICK_HZ);
  pingTimer = setInterval(() => { pingSentAt = performance.now(); link.sendPing(Math.floor(pingSentAt) & 0xffff); }, PING_INTERVAL_MS);
}

function cast(skillId, name) {
  if (!link) return;
  const target = world.nearestMonster(STRIKE_RANGE_M * 1.5, contracts.EntityKind.values.ENTITY_KIND_MONSTER);
  if (target === null) { world.note(`${name}: no monster in reach`); return; }
  pendingCasts.set(link.sendCast(skillId, Number(target)), name);
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
    'hp / mp': `${world.vitals.hp}/${world.vitals.maxHp}  ${world.vitals.mp}/${world.vitals.maxMp}`,
    'log': world.log.join('<br>'),
  };
  $('stats').innerHTML = Object.entries(stats).map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
  requestAnimationFrame(frame);
}

async function main() {
  contracts = await loadContracts();
  renderer.useKinds(contracts.EntityKind.values);
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
  window.addEventListener('keydown', (e) => {
    if (e.key === ' ') { e.preventDefault(); cast(SKILL_STRIKE, 'strike'); return; }
    if (e.key.toLowerCase() === 'e') { cast(SKILL_HEAVY_BLOW, 'heavy blow'); return; }
    keys.add(e.key.toLowerCase());
  });
  window.addEventListener('keyup', (e) => keys.delete(e.key.toLowerCase()));
  requestAnimationFrame(frame);
  status('ready');
}

main().catch((e) => status(e.message, 'error'));
