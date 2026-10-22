// Client-side world model: the authoritative self, and other entities as timelines of samples
// so they can be rendered a buffer interval in the past (design note 02).
const SAMPLE_WINDOW_MS = 2000;

export class World {
  constructor() {
    this.reset();
  }

  reset() {
    this.selfId = null;
    this.self = { x: 0, y: 0 };
    this.mapSize = { x: 0, y: 0 };
    this.entities = new Map();  // id -> { kind, templateId, hp, maxHp, samples: [{ t, x, y, vx, vy }] }
    this.vitals = { hp: 0, maxHp: 0, mp: 0, maxMp: 0 };
    this.inventory = { items: [], capacity: 0, version: 0, gold: 0 };  // full state from the last InventoryUpdate
    this.zoneId = 0;
    this.portals = [];          // { id, x, y, radius, toZone } from the zone's Welcome
    this.log = [];              // last combat lines, newest first
    this.serverTick = 0;
    this.lastAckedInput = 0;
    this.snapshots = 0;
    this.dropped = 0;
    this.integrityErrors = 0;
  }

  // Arrives on entry and again after every zone change. Everything visible belonged to one zone,
  // and the new zone numbers its entities and acknowledges inputs on its own.
  applyWelcome(welcome) {
    this.selfId = String(welcome.entity_id);
    this.zoneId = welcome.zone_id;
    this.mapSize = { x: welcome.map_size.x, y: welcome.map_size.y };
    this.entities.clear();
    this.portals = (welcome.portals ?? []).map(p => ({
      id: p.portal_id, x: p.position.x, y: p.position.y, radius: p.radius_m, toZone: p.to_zone_id,
    }));
    this.lastAckedInput = 0;
  }

  // The portal whose circle covers self, or null.
  portalInReach() {
    return this.portals.find(p => (p.x - this.self.x) ** 2 + (p.y - this.self.y) ** 2 <= p.radius ** 2) ?? null;
  }

  // `lossProbability` simulates losing movement data to watch interpolation and extrapolation
  // cope. The link is WebSocket over TCP, so nothing really goes missing, and each snapshot is a
  // delta: discarding it whole would leave ghost or missing entities that the server never
  // caused. So the entity set and health always apply; only this snapshot's position samples
  // are dropped.
  applySnapshot(snapshot, now, lossProbability) {
    const lost = Math.random() < lossProbability;
    if (lost) this.dropped += 1;
    this.snapshots += 1;
    this.serverTick = Number(snapshot.server_tick);
    this.lastAckedInput = snapshot.last_processed_input_seq;
    if (snapshot.self?.position) {
      this.self = { x: snapshot.self.position.x, y: snapshot.self.position.y };
    }
    if (snapshot.self_vitals) {
      const v = snapshot.self_vitals;
      this.vitals = { hp: v.hp, maxHp: v.max_hp, mp: v.mp, maxMp: v.max_mp };
    }
    for (const spawn of snapshot.spawned ?? []) {
      const id = String(spawn.entity_id);
      if (id === this.selfId || this.entities.has(id)) this.integrityErrors += 1;
      this.entities.set(id, { kind: spawn.kind, templateId: spawn.template_id, hp: spawn.hp, maxHp: spawn.max_hp,
        samples: [{ t: now, x: spawn.position.x, y: spawn.position.y, vx: 0, vy: 0 }] });
    }
    for (const update of snapshot.updated ?? []) {
      const entity = this.entities.get(String(update.entity_id));
      if (!entity) { this.integrityErrors += 1; continue; }
      entity.hp = update.hp;
      if (lost) continue;
      entity.samples.push({ t: now, x: update.position.x, y: update.position.y, vx: update.velocity?.x ?? 0, vy: update.velocity?.y ?? 0 });
      while (entity.samples.length > 2 && entity.samples[0].t < now - SAMPLE_WINDOW_MS) entity.samples.shift();
    }
    for (const id of snapshot.despawned ?? []) {
      if (!this.entities.delete(String(id))) this.integrityErrors += 1;
    }
  }

  // Every update carries the whole inventory; an older version must not overwrite a newer one.
  applyInventory(update) {
    if (update.version < this.inventory.version) { this.integrityErrors += 1; return; }
    this.inventory = { items: update.items ?? [], capacity: update.capacity, version: update.version, gold: Number(update.gold) };
  }

  // Nearest entity of `kind` to self within `range`; `aliveOnly` skips corpses (monsters).
  nearest(range, kind, aliveOnly) {
    let best = null, bestSq = range * range;
    for (const [id, entity] of this.entities) {
      if (entity.kind !== kind || (aliveOnly && entity.hp <= 0)) continue;
      const last = entity.samples[entity.samples.length - 1];
      const dSq = (last.x - this.self.x) ** 2 + (last.y - this.self.y) ** 2;
      if (dSq < bestSq) { best = id; bestSq = dSq; }
    }
    return best;
  }

  note(line) {
    this.log.unshift(line);
    if (this.log.length > 6) this.log.pop();
  }

  // Position to draw for an entity at render time `t` (already shifted back by the buffer).
  // Returns { x, y, mode } with mode 'interp' | 'extrap' | 'hold'.
  positionAt(entity, t, allowExtrapolation) {
    const s = entity.samples;
    const last = s[s.length - 1];
    if (t <= s[0].t || s.length === 1) return { x: s[0].x, y: s[0].y, mode: 'hold' };
    if (t >= last.t) {
      if (!allowExtrapolation) return { x: last.x, y: last.y, mode: 'hold' };
      const dt = (t - last.t) / 1000;
      return { x: last.x + last.vx * dt, y: last.y + last.vy * dt, mode: 'extrap' };
    }
    for (let i = s.length - 1; i > 0; i--) {
      if (s[i - 1].t <= t && t <= s[i].t) {
        const a = s[i - 1], b = s[i];
        const u = (t - a.t) / Math.max(1, b.t - a.t);
        return { x: a.x + (b.x - a.x) * u, y: a.y + (b.y - a.y) * u, mode: 'interp' };
      }
    }
    return { x: last.x, y: last.y, mode: 'hold' };
  }
}
