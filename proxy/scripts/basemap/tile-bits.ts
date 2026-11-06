// The four source bits per mirrored tile (docs/basemap-route.md A1.4, ruled 2026-10-26/28). PURE.
//
//   SRTM       the tile's footprint meets land south of 60° N
//   GMTED2010  it meets land north of 60° N
//   ETOPO1     it meets ocean, buffered 2 km
//   NRCAN      it meets Canada (admin-0), buffered 2 km
//
// "Meets" is polygon-box intersection under the even-odd rule: a ring vertex inside the box, a ring edge
// crossing it, or the box's centre inside the polygon. The 2 km buffer widens the box: 2/111° of latitude and
// 2/(111 cos φ)° of longitude at the box's poleward edge, so the widening is never less than 2 km.
//
// Edges are bucketed by z9 tile so each test touches only nearby geometry; the containment test casts a ray
// along the box centre's latitude through the edges bucketed by tile ROW.
import { z9X, z9Y } from "./terrain-plan";

export const BIT = { SRTM: 1, GMTED2010: 2, ETOPO1: 4, NRCAN: 8 } as const;

const PI = Math.PI;
export const lonOfX = (x: number) => (x / 512) * 360 - 180;
export const latOfY = (y: number) => (Math.atan(Math.sinh(PI * (1 - (2 * y) / 512))) * 180) / PI;
/** A tile's footprint [west, south, east, north] in degrees. */
export const footprint = (x: number, y: number): [number, number, number, number] => [lonOfX(x), latOfY(y + 1), lonOfX(x + 1), latOfY(y)];

type Box = [number, number, number, number];

/** A polygon layer (rings, even-odd) indexed for box tests. */
export class Layer {
  private byTile = new Map<number, number[]>(); // tile -> edge indices
  private byRow = new Map<number, number[]>();  // tile row -> edge indices
  private e: number[] = [];                     // x1, y1, x2, y2 per edge
  constructor(rings: Float64Array[]) {
    for (const xy of rings) {
      const n = xy.length / 2;
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        const x1 = xy[i * 2]!, y1 = xy[i * 2 + 1]!, x2 = xy[j * 2]!, y2 = xy[j * 2 + 1]!;
        if (x1 === x2 && y1 === y2) continue;
        const k = this.e.length / 4;
        this.e.push(x1, y1, x2, y2);
        const tx0 = Math.floor(z9X(Math.min(x1, x2)) / 256), tx1 = Math.floor(z9X(Math.max(x1, x2)) / 256);
        const ty0 = Math.floor(z9Y(Math.max(y1, y2)) / 256), ty1 = Math.floor(z9Y(Math.min(y1, y2)) / 256);
        for (let ty = ty0; ty <= ty1; ty++) {
          (this.byRow.get(ty) ?? this.byRow.set(ty, []).get(ty)!).push(k);
          for (let tx = tx0; tx <= tx1; tx++) { const t = tx * 512 + ty; (this.byTile.get(t) ?? this.byTile.set(t, []).get(t)!).push(k); }
        }
      }
    }
  }
  /** Whether the polygon meets the box. `tiles` are the tiles whose edge buckets can reach the box. */
  meets(box: Box, tiles: number[]): boolean {
    const [w, s, ea, n] = box;
    const seen = new Set<number>();
    for (const t of tiles) for (const k of this.byTile.get(t) ?? []) {
      if (seen.has(k)) continue; seen.add(k);
      const x1 = this.e[k * 4]!, y1 = this.e[k * 4 + 1]!, x2 = this.e[k * 4 + 2]!, y2 = this.e[k * 4 + 3]!;
      if ((x1 >= w && x1 <= ea && y1 >= s && y1 <= n) || segmentMeetsBox(x1, y1, x2, y2, box)) return true;
    }
    // no edge reaches the box: it is wholly inside or wholly outside; decide by its centre (even-odd)
    const cx = (w + ea) / 2, cy = (s + n) / 2;
    const row = Math.floor(z9Y(cy) / 256);
    let inside = false;
    for (const k of this.byRow.get(row) ?? []) {
      const x1 = this.e[k * 4]!, y1 = this.e[k * 4 + 1]!, x2 = this.e[k * 4 + 2]!, y2 = this.e[k * 4 + 3]!;
      if ((y1 > cy) !== (y2 > cy) && cx < x1 + ((cy - y1) * (x2 - x1)) / (y2 - y1)) inside = !inside;
    }
    return inside;
  }
}

/** Liang-Barsky: whether the segment meets the closed box. */
export function segmentMeetsBox(x1: number, y1: number, x2: number, y2: number, [w, s, e, n]: Box): boolean {
  let t0 = 0, t1 = 1;
  const dx = x2 - x1, dy = y2 - y1;
  for (const [p, q] of [[-dx, x1 - w], [dx, e - x1], [-dy, y1 - s], [dy, n - y1]] as const) {
    if (p === 0) { if (q < 0) return false; continue; }
    const r = q / p;
    if (p < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
    else { if (r < t0) return false; if (r < t1) t1 = r; }
  }
  return true;
}

/** The box widened by `km` (A1.4's 2 km), at its poleward edge. */
export function widen([w, s, e, n]: Box, km: number): Box {
  const dLat = km / 111.0, pole = Math.max(Math.abs(s), Math.abs(n));
  const dLon = km / (111.0 * Math.max(Math.cos((pole * PI) / 180), 0.01));
  return [w - dLon, s - dLat, e + dLon, n + dLat];
}

/** The four bits of one tile. */
export function tileBits(x: number, y: number, land: Layer, ocean: Layer, canada: Layer): number {
  const fp = footprint(x, y);
  const near: number[] = [];
  for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++) near.push((((x + dx) % 512) + 512) % 512 * 512 + (y + dy));
  let bits = 0;
  if (fp[1] < 60 && land.meets([fp[0], fp[1], fp[2], Math.min(fp[3], 60)], near)) bits |= BIT.SRTM;
  if (fp[3] > 60 && land.meets([fp[0], Math.max(fp[1], 60), fp[2], fp[3]], near)) bits |= BIT.GMTED2010;
  if (ocean.meets(widen(fp, 2), near)) bits |= BIT.ETOPO1;
  if (canada.meets(widen(fp, 2), near)) bits |= BIT.NRCAN;
  return bits;
}

/** terrain-sources.bin: "BMTS", u32 version 1, u32 count; per tile u16 x, u16 y, u8 bits; sorted by (x, y). */
export function encodeTileBits(entries: Array<{ x: number; y: number; bits: number }>): Uint8Array {
  const s = [...entries].sort((a, b) => a.x - b.x || a.y - b.y);
  const out = new Uint8Array(12 + 5 * s.length), dv = new DataView(out.buffer);
  out.set([0x42, 0x4d, 0x54, 0x53]); dv.setUint32(4, 1, true); dv.setUint32(8, s.length, true);
  s.forEach((t, i) => { dv.setUint16(12 + 5 * i, t.x, true); dv.setUint16(14 + 5 * i, t.y, true); dv.setUint8(16 + 5 * i, t.bits); });
  return out;
}
