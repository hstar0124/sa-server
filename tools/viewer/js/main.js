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
const TRADE_OFFER_GOLD = 1;  // what Y puts on the table

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
const obstaclesByZone = new Map();  // zone id -> obstacles of its map
let departure = null;  // { id, portal } of a ChangeZone not decided yet; retried under the same id
let rtt = null;
let tickTimer = null;
let pingTimer = null;
let pingSentAt = 0;
let lastDirection = { x: 0, y: 1 };
const keys = new Set();
const pendingRequests = new Map();  // request id -> what was asked, until the final result arrives
const tradeSteps = new Map();  // request id -> what a trade step asked ('accept <trade> v3'), until decided; retried under the same id
let pickupRangeM = 0;  // from game-data/rules/economy.toml; the zone refuses anything farther
let tradeRangeM = 0;   // the same file's [trade] range_m; the zone refuses to open a trade farther

async function loadMapObstacles(zoneId) {
  // The viewer draws the same map the zone loads (game-data/maps/zone-<id>.toml, as the zone
  // configs name them); a tiny TOML subset is enough here.
  const response = await fetch(`/game-data/maps/zone-${zoneId}.toml`);
  if (!response.ok) return [];
  const text = await response.text();
  const match = text.match(/obstacles\s*=\s*\[([\s\S]*?)\n\]/);
  if (!match) return [];
  return [...match[1].matchAll(/\[\s*([\d.]+),\s*([\d.]+),\s*([\d.]+),\s*([\d.]+)\s*\]/g)].map(m => m.slice(1, 5).map(Number));
}

async function loadEconomyRanges() {
  // Same tiny TOML subset as the map: the zone reads [pickup] and [trade] range_m from this file.
  const text = await (await fetch('/game-data/rules/economy.toml')).text();
  const rangeOf = (section) => {
    const match = text.match(new RegExp(`\\[${section}\\][\\s\\S]*?\\nrange_m\\s*=\\s*([\\d.]+)`));
    if (!match) throw new Error(`economy.toml has no [${section}] range_m`);
    return Number(match[1]);
  };
  return { pickup: rangeOf('pickup'), trade: rangeOf('trade') };
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
      onWelcome: (welcome) => {
        const moved = world.zoneId !== 0 && world.zoneId !== welcome.zone_id;
        world.applyWelcome(welcome);
        departure = null;
        tradeSteps.clear();
        showZone(welcome.zone_id);
        status(`in zone ${welcome.zone_id}`, 'ok');
        if (moved) world.note(`arrived in zone ${welcome.zone_id}`);
        startLoops();
      },
      onSnapshot: (snapshot) => world.applySnapshot(snapshot, performance.now(), Number($('loss-pct').value) / 100),
      onPong: () => { rtt = Math.round(performance.now() - pingSentAt); },
      onCommandResult: (result, rejectCode) => {
        const statuses = contracts.CommandStatus.values;
        const name = pendingRequests.get(result.request_id) ?? `request ${result.request_id}`;
        const isTradeStep = tradeSteps.has(result.request_id);
        const report = (line) => { world.note(line); if (isTradeStep) world.tradeResult = line; };
        if (result.status === statuses.COMMAND_STATUS_PENDING) { report(`${name}: waiting for the backend`); return; }
        pendingRequests.delete(result.request_id);
        if (result.status !== statuses.COMMAND_STATUS_UNKNOWN) {
          if (departure?.id === result.request_id) departure = null;
          tradeSteps.delete(result.request_id);
        }
        if (result.status === statuses.COMMAND_STATUS_ACCEPTED) report(`${name}: accepted`);
        else if (result.status === statuses.COMMAND_STATUS_UNKNOWN) report(`${name}: outcome unknown, try again`);
        else report(`${name}: ${rejectCode}`);
      },
      onInventoryUpdate: (update) => world.applyInventory(update),
      onTradeUpdate: (update, tradeStatus) => world.applyTrade(update, tradeStatus),
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
  const target = world.nearest(STRIKE_RANGE_M * 1.5, contracts.EntityKind.values.ENTITY_KIND_MONSTER, true, performance.now());
  if (target === null) { world.note(`${name}: no monster in reach`); return; }
  pendingRequests.set(link.sendCast(skillId, Number(target)), name);
}

// Obstacles follow the zone the session is in; each map is fetched once.
function showZone(zoneId) {
  obstacles = obstaclesByZone.get(zoneId) ?? [];
  if (obstaclesByZone.has(zoneId)) return;
  loadMapObstacles(zoneId).then((loaded) => {
    obstaclesByZone.set(zoneId, loaded);
    if (world.zoneId === zoneId) obstacles = loaded;
  });
}

function changeZone() {
  if (!link) return;
  const portal = world.portalInReach();
  if (portal === null) { world.note('portal: stand inside the ring first'); return; }
  // Until the zone decides, asking again repeats the same request.
  const id = link.sendChangeZone(portal.id, departure?.portal === portal.id ? departure.id : null);
  departure = { id, portal: portal.id };
  pendingRequests.set(id, `portal to zone ${portal.toZone}`);
}

// Trade keys: T opens with the nearest live player within the trade range, Y offers one gold,
// Enter accepts the version on screen, Escape cancels. The zone checks the range, the Backend
// owns the rest (ADR-0013). Until a step is decided, asking for the same thing again repeats the
// same request; anything else is a new one.
function sendTradeStep(name, send) {
  const retry = [...tradeSteps].find(([, asked]) => asked === name)?.[0] ?? null;
  const id = send(retry);
  tradeSteps.set(id, name);
  pendingRequests.set(id, name);
}

function tradeOpen() {
  if (!link) return;
  const target = world.nearest(tradeRangeM, contracts.EntityKind.values.ENTITY_KIND_PLAYER, true, performance.now());
  if (target === null) { world.note('trade: no player within reach'); return; }
  sendTradeStep(`trade with #${target}`, (retry) => link.sendTradeOpen(Number(target), retry));
}

function openTradeOrNote() {
  if (!link) return null;
  if (world.trade === null) world.note('trade: nothing open (T next to a player)');
  return world.trade;
}

function tradeOffer() {
  const trade = openTradeOrNote();
  if (trade === null) return;
  sendTradeStep(`offer ${TRADE_OFFER_GOLD}g in ${trade.short}`, (retry) => link.sendTradeOffer(trade.id, TRADE_OFFER_GOLD, retry));
}

function tradeAccept() {
  const trade = openTradeOrNote();
  if (trade === null) return;
  sendTradeStep(`accept ${trade.short} v${trade.version}`, (retry) => link.sendTradeAccept(trade.id, trade.version, retry));
}

function tradeCancel() {
  const trade = openTradeOrNote();
  if (trade === null) return;
  sendTradeStep(`cancel ${trade.short}`, (retry) => link.sendTradeCancel(trade.id, retry));
}

function pickup() {
  if (!link) return;
  const item = world.nearest(pickupRangeM, contracts.EntityKind.values.ENTITY_KIND_DROPPED_ITEM, false, performance.now());
  if (item === null) { world.note('pickup: nothing within reach'); return; }
  pendingRequests.set(link.sendPickup(Number(item)), `pickup #${item}`);
}

function stopLoops() {
  clearInterval(tickTimer); clearInterval(pingTimer);
  tickTimer = pingTimer = null;
}

function describeTrade(trade) {
  if (trade === null) return '- (T next to a player)';
  const party = (p) => `${p.gold}g ${p.items} items${p.accepted ? ' ✓' : ''}`;
  const other = trade.other.entityId === '0' ? 'away' : `#${trade.other.entityId}`;
  return `${trade.short} open v${trade.version}<br>you ${party(trade.you)} / ${other} ${party(trade.other)}`;
}

function frame() {
  const bufferMs = Number($('buffer-ms').value);
  const modes = renderer.draw(world, performance.now() - bufferMs, $('extrapolate').checked, obstacles);
  $('buffer-value').textContent = bufferMs;
  $('loss-value').textContent = $('loss-pct').value;
  const stats = {
    'zone': world.zoneId,
    'server tick': world.serverTick,
    'acked input': `${world.lastAckedInput} / ${link?.inputSeq ?? 0}`,
    'visible': world.entities.size,
    'interp / extrap / hold': `${modes.interp} / ${modes.extrap} / ${modes.hold}`,
    'snapshots applied / dropped': `${world.snapshots} / ${world.dropped}`,
    'integrity errors': world.integrityErrors,
    'ping rtt': rtt === null ? '-' : `${rtt} ms`,
    'self': `${world.self.x.toFixed(1)}, ${world.self.y.toFixed(1)}`,
    'hp / mp': `${world.vitals.hp}/${world.vitals.maxHp}  ${world.vitals.mp}/${world.vitals.maxMp}`,
    'inventory': `${world.inventory.items.length}/${world.inventory.capacity} (v${world.inventory.version})  gold ${world.inventory.gold}`,
    'items': world.inventory.items.map(i => `#${i.item_template_id}@${i.slot_no}`).join(' ') || '-',
    'trade': describeTrade(world.trade),
    'trade result': world.tradeResult,
    'log': world.log.join('<br>'),
  };
  $('stats').innerHTML = Object.entries(stats).map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
  requestAnimationFrame(frame);
}

async function main() {
  contracts = await loadContracts();
  renderer.useKinds(contracts.EntityKind.values);
  ({ pickup: pickupRangeM, trade: tradeRangeM } = await loadEconomyRanges());
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
    if (e.key.toLowerCase() === 'f') { pickup(); return; }
    if (e.key.toLowerCase() === 'g') { changeZone(); return; }
    if (e.key.toLowerCase() === 't') { tradeOpen(); return; }
    if (e.key.toLowerCase() === 'y') { tradeOffer(); return; }
    // Enter would also press a focused button (such as the character's Enter).
    if (e.key === 'Enter') { e.preventDefault(); tradeAccept(); return; }
    if (e.key === 'Escape') { tradeCancel(); return; }
    keys.add(e.key.toLowerCase());
  });
  window.addEventListener('keyup', (e) => keys.delete(e.key.toLowerCase()));
  requestAnimationFrame(frame);
  status('ready');
}

main().catch((e) => status(e.message, 'error'));
