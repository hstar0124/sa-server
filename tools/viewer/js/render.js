// Canvas rendering. Map metres -> canvas pixels with Y up.
const COLORS = {
  grid: '#1a2027', obstacle: '#3a4452', self: '#ffd166', other: '#6ee7b7', extrap: '#f4a261', hold: '#8a94a3',
};
const GRID_STEP_M = 25;
const PLAYER_RADIUS_PX = 5;

export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
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

    const modes = { interp: 0, extrap: 0, hold: 0 };
    for (const entity of world.entities.values()) {
      const p = world.positionAt(entity, renderTime, allowExtrapolation);
      modes[p.mode] += 1;
      const [x, y] = toPx(p.x, p.y);
      ctx.fillStyle = COLORS[p.mode === 'interp' ? 'other' : p.mode];
      ctx.beginPath(); ctx.arc(x, y, PLAYER_RADIUS_PX, 0, Math.PI * 2); ctx.fill();
    }
    const [sx, sy] = toPx(world.self.x, world.self.y);
    ctx.fillStyle = COLORS.self;
    ctx.beginPath(); ctx.arc(sx, sy, PLAYER_RADIUS_PX + 1, 0, Math.PI * 2); ctx.fill();
    return modes;
  }
}
