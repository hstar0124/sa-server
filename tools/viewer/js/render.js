// Canvas rendering. Map metres -> canvas pixels with Y up.
const COLORS = {
  grid: '#1a2027', obstacle: '#3a4452', self: '#ffd166', other: '#6ee7b7', extrap: '#f4a261', hold: '#8a94a3',
  monster: '#ff6b6b', projectile: '#e6e9ee', item: '#c084fc', portal: '#38bdf8', hpBack: '#2a313a',
  hp: '#6ee7b7', hpLow: '#ff6b6b',
};
const ITEM_SIZE_PX = 6;
const GRID_STEP_M = 25;
const PLAYER_RADIUS_PX = 5;

export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.kinds = {};
  }

  // Entity kind numbers come from the contracts (EntityKind), never from literals here.
  useKinds(entityKindValues) {
    this.kinds = entityKindValues;
  }

  #hpBar(x, y, hp, maxHp) {
    const { ctx } = this;
    if (!maxHp) return;
    const w = 18, h = 3, ratio = Math.max(0, Math.min(1, hp / maxHp));
    ctx.fillStyle = COLORS.hpBack; ctx.fillRect(x - w / 2, y - 11, w, h);
    ctx.fillStyle = ratio < 0.3 ? COLORS.hpLow : COLORS.hp; ctx.fillRect(x - w / 2, y - 11, w * ratio, h);
  }

  draw(world, renderTime, allowExtrapolation, obstacles) {
    const { ctx, canvas } = this;
    const scale = world.mapSize.x > 0 ? canvas.width / world.mapSize.x : 1;
    const toPx = (x, y) => [x * scale, canvas.height - y * scale];

    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.strokeStyle = COLORS.grid;
    ctx.lineWidth = 1;
    for (let m = 0; m <= world.mapSize.x; m += GRID_STEP_M) {
      const [x] = toPx(m, 0); ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke();
      const [, y] = toPx(0, m); ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke();
    }
    ctx.fillStyle = COLORS.obstacle;
    for (const [x0, y0, x1, y1] of obstacles) {
      const [px, py] = toPx(x0, y1);
      ctx.fillRect(px, py, (x1 - x0) * scale, (y1 - y0) * scale);
    }

    // Portals: a ring of the real radius, labelled with the destination zone.
    ctx.strokeStyle = COLORS.portal;
    ctx.fillStyle = COLORS.portal;
    ctx.lineWidth = 2;
    ctx.font = '11px sans-serif';
    for (const portal of world.portals) {
      const [x, y] = toPx(portal.x, portal.y);
      ctx.beginPath(); ctx.arc(x, y, portal.radius * scale, 0, Math.PI * 2); ctx.stroke();
      ctx.fillText(`→ zone ${portal.toZone}`, x + 6, y - 6);
    }

    const modes = { interp: 0, extrap: 0, hold: 0 };
    for (const entity of world.entities.values()) {
      const p = world.positionAt(entity, renderTime, allowExtrapolation);
      modes[p.mode] += 1;
      const [x, y] = toPx(p.x, p.y);
      if (entity.kind === this.kinds.ENTITY_KIND_PROJECTILE) {
        ctx.fillStyle = COLORS.projectile;
        ctx.beginPath(); ctx.arc(x, y, 2, 0, Math.PI * 2); ctx.fill();
        continue;
      }
      if (entity.kind === this.kinds.ENTITY_KIND_DROPPED_ITEM) {
        ctx.fillStyle = COLORS.item;
        ctx.fillRect(x - ITEM_SIZE_PX / 2, y - ITEM_SIZE_PX / 2, ITEM_SIZE_PX, ITEM_SIZE_PX);
        continue;
      }
      const base = entity.kind === this.kinds.ENTITY_KIND_MONSTER ? COLORS.monster : COLORS.other;
      ctx.fillStyle = p.mode === 'interp' ? base : COLORS[p.mode];
      ctx.beginPath(); ctx.arc(x, y, PLAYER_RADIUS_PX, 0, Math.PI * 2); ctx.fill();
      this.#hpBar(x, y, entity.hp, entity.maxHp);
    }
    const [sx, sy] = toPx(world.self.x, world.self.y);
    ctx.fillStyle = COLORS.self;
    ctx.beginPath(); ctx.arc(sx, sy, PLAYER_RADIUS_PX + 1, 0, Math.PI * 2); ctx.fill();
    this.#hpBar(sx, sy, world.vitals.hp, world.vitals.maxHp);
    return modes;
  }
}
