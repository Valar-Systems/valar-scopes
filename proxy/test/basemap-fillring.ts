// R8 (docs/basemap-dense-code.md): FillRing's fill rule in TypeScript, for KB1 in CI. A port of the pinned Python reference
// (part 2's p2-render.py cca7d5c16db2, `fill_ring`, `mapper`, `union` and its kb1 mode), which mirrors the device's FillRing
// (src/weatherscope/WxBasemap.h:80-121 at f3fee349061a, #662): a pixel is filled when its centre is inside a ring by even-odd, each
// ring on its own, the water mask their union; each crossing computed from the edge's LOWER-y endpoint. The arithmetic is the
// reference's, the same IEEE double operations in the same order, so the two give the same masks bit for bit -- shown on the
// synthetics before KB1 runs (R8's parity step). Pure: no imports.
export type Pt = [number, number];
export const N = 240, C = 120.0;
const DEG = Math.PI / 180.0; // CPython's math.radians: x * (pi / 180)

/** One ring's pixels: row-major, 1 = filled. */
export function fillRing(xs: number[], ys: number[], n = N): Uint8Array {
  const m = new Uint8Array(n * n);
  const len = xs.length;
  if (len < 3) return m;
  const x0 = new Float64Array(len), y0 = new Float64Array(len), x1 = new Float64Array(len), y1 = new Float64Array(len);
  let ymin = Infinity, ymax = -Infinity;
  for (let i = 0; i < len; i++) {
    const xa = xs[i]!, ya = ys[i]!, xb = xs[(i + 1) % len]!, yb = ys[(i + 1) % len]!;
    const low = ya < yb; // the lower-y endpoint first
    x0[i] = low ? xa : xb; y0[i] = low ? ya : yb; x1[i] = low ? xb : xa; y1[i] = low ? yb : ya;
    if (ya < ymin) ymin = ya;
    if (ya > ymax) ymax = ya;
  }
  const yLo = Math.max(0, Math.floor(ymin) - 1), yHi = Math.min(n, Math.ceil(ymax) + 2);
  const cx: number[] = [];
  for (let y = yLo; y < yHi; y++) {
    const py = y + 0.5;
    cx.length = 0;
    for (let i = 0; i < len; i++) {
      const ya = ys[i]!, yb = ys[(i + 1) % len]!;
      if ((ya > py) !== (yb > py)) cx.push(x0[i]! + (py - y0[i]!) * (x1[i]! - x0[i]!) / (y1[i]! - y0[i]!));
    }
    if (!cx.length) continue;
    cx.sort((a, b) => a - b);
    for (let k = 0; k < cx.length - 1; k += 2) {
      const lo = Math.max(Math.ceil(cx[k]! - 0.5), 0), hi = Math.min(Math.ceil(cx[k + 1]! - 0.5), n);
      for (let x = lo; x < hi; x++) m[y * n + x] = 1;
    }
  }
  return m;
}

/** Package units to screen pixels: the package's centre and cosine, then the home's view (WxBasemap.h Mapper::ToScreen), rotated as the
 *  map client's AffineFor rotates (x = C + u cos + w sin, y = C - u sin + w cos). */
export function mapper(lat0p: number, lon0p: number, rmm: number, homeLat: number, homeLon: number, rViewKm: number, rotDeg = 0.0) {
  const c0p = Math.max(Math.cos(lat0p * DEG), 0.01), c0h = Math.max(Math.cos(homeLat * DEG), 0.01);
  const k = rmm / 1e6 / 32767.0, rlat = rViewKm / 111.0, rlon = rViewKm / (111.0 * c0h);
  const cs = Math.cos(rotDeg * DEG), sn = Math.sin(rotDeg * DEG);
  return (ring: Pt[]): [number[], number[]] => {
    const xs: number[] = [], ys: number[] = [];
    for (const [qx, qy] of ring) {
      const lat = lat0p - qy * k / 111.0, lon = lon0p + qx * k / (111.0 * c0p);
      const u = (lon - homeLon) / rlon * C, w = (homeLat - lat) / rlat * C;
      xs.push(C + u * cs + w * sn); ys.push(C - u * sn + w * cs);
    }
    return [xs, ys];
  };
}

export function union(rings: Pt[][], f: (r: Pt[]) => [number[], number[]]): Uint8Array {
  const m = new Uint8Array(N * N);
  for (const r of rings) { const [xs, ys] = f(r), g = fillRing(xs, ys); for (let i = 0; i < m.length; i++) m[i]! |= g[i]!; }
  return m;
}

/** KB1's 24 views (r, r/2, r/4 x 0..315 degrees by 45): the drawn sea rings' union against the reference, the SAME quantised outer and
 *  islands by even-odd, unbridged. Per view: [view km, rotation, differing pixels, island pixels (in the outer, not in the reference)],
 *  and the two masks for the parity step. */
export function kb1Views(drawn: Pt[][], q: { outer: Pt[]; holes: Pt[][] }, radiusMm: number, rKm: number) {
  const rows: Array<{ rv: number; rot: number; diff: number; island: number; drawn: Uint8Array; ref: Uint8Array }> = [];
  for (const rv of [rKm, rKm / 2, rKm / 4]) for (let rot = 0; rot < 360; rot += 45) {
    const f = mapper(0.0, 0.0, radiusMm, 0.0, 0.0, rv, rot);
    const d = union(drawn, f), [ox, oy] = f(q.outer), outer = fillRing(ox, oy), ref = outer.slice();
    for (const h of q.holes) { const [hx, hy] = f(h), g = fillRing(hx, hy); for (let i = 0; i < ref.length; i++) ref[i]! ^= g[i]!; }
    let diff = 0, island = 0;
    for (let i = 0; i < ref.length; i++) { diff += d[i]! ^ ref[i]!; island += outer[i]! & (ref[i]! ^ 1); }
    rows.push({ rv, rot, diff, island, drawn: d, ref });
  }
  return rows;
}
