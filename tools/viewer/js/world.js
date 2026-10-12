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
    this.log = [];              // last combat lines, newest first
    this.serverTick = 0;
    this.lastAckedInput = 0;
    this.snapshots = 0;
    this.dropped = 0;
    this.integrityErrors = 0;
  }

  applyWelcome(welcome) {
    this.selfId = String(welcome.entity_id);
    this.mapSize = { x: welcome.map_size.x, y: welcome.map_size.y };
  }

  // `lossProbability` simulates packet loss on the receive side: the snapshot is discarded
  // before it is applied, exactly as if the datagram never arrived.
  applySnapshot(snapshot, now, lossProbability) {
    if (Math.random() < lossProbability) {
      this.dropped += 1;
      return;
    }
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
      entity.samples.push({ t: now, x: update.position.x, y: update.position.y, vx: update.velocity?.x ?? 0, vy: update.velocity?.y ?? 0 });
      while (entity.samples.length > 2 && entity.samples[0].t < now - SAMPLE_WINDOW_MS) entity.samples.shift();
    }
    for (const id of snapshot.despawned ?? []) {
      if (!this.entities.delete(String(id))) this.integrityErrors += 1;
    }
  }

  // Nearest live monster to self within `range`, for the default attack target.
  nearestMonster(range, monsterKind) {
    let best = null, bestSq = range * range;
    for (const [id, entity] of this.entities) {
      if (entity.kind !== monsterKind || entity.hp <= 0) continue;
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
