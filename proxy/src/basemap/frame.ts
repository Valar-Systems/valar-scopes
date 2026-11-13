// The package's frame and geometry (docs/basemap-package-format.md §2, §6; §13's RD5-RD7 and RD22-RD31; the route
// doc's MR rows). Pure; double throughout; every rounding to an integer is floor(v + 0.5), written out (§1).
//
// Every DECISION that compares a distance -- Douglas-Peucker's keep test, the disc test, the 3 km de-duplication --
// goes through planarDist(), RD31's sqrt(x*x + y*y), so no site can take a second path. hypot is never used here; it
// appears only inside the hillshade (terrain.ts), under RD28.

export type Pt = [number, number];
export const K = 111.0;
export const QMAX = 32767;

export interface Frame { lat0: number; lon0: number; c0: number; rMm: number; rKm: number; epsM: number }

/** §2 (and the reference builder's sources.frame): the origin from its micro-degrees, R and ε from their integers. */
export function makeFrame(lat0Micro: number, lon0Micro: number, radiusMm: number, toleranceMm: number): Frame {
  const lat0 = lat0Micro / 1e6, lon0 = lon0Micro / 1e6;
  const c0 = Math.max(Math.cos(lat0 * (Math.PI / 180)), 0.01);
  return { lat0, lon0, c0, rMm: radiusMm, rKm: radiusMm / 1e6, epsM: toleranceMm / 1000 };
}

/** §2: (E, S) in km, east and SOUTH positive. */
export const toFrameKm = (f: Frame, lon: number, lat: number): Pt => [(lon - f.lon0) * K * f.c0, (f.lat0 - lat) * K];

/** RD31: a decision's distance, the x term first, in double. Never hypot. */
export const planarDist = (ex: number, ey: number) => Math.sqrt(ex * ex + ey * ey);

/** RD9 + RD31: the 3.0 km de-duplication's test, inclusive. (The disc test is layers.ts's withinMm, its one path.) */
export const within3km = (ex: number, ey: number) => planarDist(ex, ey) <= 3.0;

// ------------------------------------------------------------------------------------------------ clipping (§2)

/** RD22: Liang-Barsky against [-r, r]^2 (closed). An endpoint the clip did not move is the source vertex itself. */
function liangBarsky(a: Pt, b: Pt, r: number): [Pt, number, Pt, number] | null {
  const [x0, y0] = a, dx = b[0] - x0, dy = b[1] - y0;
  let u0 = 0, u1 = 1;
  for (const [p, q] of [[-dx, x0 + r], [dx, r - x0], [-dy, y0 + r], [dy, r - y0]] as const) {
    if (p === 0) { if (q < 0) return null; continue; }
    const t = q / p;
    if (p < 0) { if (t > u1) return null; if (t > u0) u0 = t; }
    else { if (t < u0) return null; if (t < u1) u1 = t; }
  }
  const P: Pt = u0 === 0 ? a : [x0 + u0 * dx, y0 + u0 * dy];
  const Q: Pt = u1 === 1 ? b : [x0 + u1 * dx, y0 + u1 * dy];
  return [P, u0, Q, u1];
}

/** RD5: each in-square run of a line is its own polyline, in along-line order. */
export function clipLine(pts: Pt[], r: number): Pt[][] {
  const runs: Pt[][] = [];
  let cur: Pt[] | null = null;
  for (let i = 0; i + 1 < pts.length; i++) {
    const c = liangBarsky(pts[i]!, pts[i + 1]!, r);
    if (!c) { if (cur) runs.push(cur); cur = null; continue; }
    const [p, u0, q, u1] = c;
    if (!cur || u0 > 0) { if (cur) runs.push(cur); cur = [p]; }
    cur.push(q);
    if (u1 < 1) { runs.push(cur); cur = null; }
  }
  if (cur) runs.push(cur);
  return runs;
}

/** RD23: Sutherland-Hodgman against the square, closed; `ring` without its closing repeat, returned the same way.
 *  The passes run west (x >= -r), east (x <= r), north (y >= -r), south (y <= r); each is the textbook loop from
 *  prev = the last vertex; each crossing is computed from the PREVIOUS vertex toward the current one. */
export function clipRing(ring: Pt[], r: number): Pt[] {
  let out = ring.slice();
  for (const [axis, sign] of [[0, -1], [0, 1], [1, -1], [1, 1]] as const) {
    if (!out.length) return [];
    const src = out;
    out = [];
    const lim = sign * r, o = 1 - axis;
    const inside = (p: Pt) => (sign < 0 ? p[axis] >= lim : p[axis] <= lim);
    const cross = (p: Pt, q: Pt): Pt => {
      const t = (lim - p[axis]) / (q[axis] - p[axis]);
      const v: Pt = [0, 0];
      v[axis] = lim;
      v[o] = p[o] + t * (q[o] - p[o]);
      return v;
    };
    let prev = src[src.length - 1]!;
    for (const cur of src) {
      if (inside(cur)) { if (!inside(prev)) out.push(cross(prev, cur)); out.push(cur); }
      else if (inside(prev)) out.push(cross(prev, cur));
      prev = cur;
    }
  }
  return out;
}

// ------------------------------------------------------------------------------------------------ simplification (§6)

/** RD6, RD7, RD31: p's distance to the SEGMENT a-b in metres, clamped at its ends; to the point when a == b. */
function segDist(p: Pt, a: Pt, b: Pt): number {
  const dx = b[0] - a[0], dy = b[1] - a[1];
  let ex: number, ey: number;
  if (dx === 0 && dy === 0) { ex = p[0] - a[0]; ey = p[1] - a[1]; }
  else {
    let t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    ex = p[0] - (a[0] + t * dx); ey = p[1] - (a[1] + t * dy);
  }
  return planarDist(ex, ey);
}

/** Douglas-Peucker over a path in metres: the indices kept, ascending. The split is the FIRST vertex at the maximum
 *  distance (§6); a vertex is kept when its distance is > ε (RD7). */
function dpKeep(m: Pt[], epsM: number): number[] {
  const n = m.length;
  if (n <= 2) return [...Array(n).keys()];
  const keep = new Array<boolean>(n).fill(false);
  keep[0] = keep[n - 1] = true;
  const stack: Array<[number, number]> = [[0, n - 1]];
  while (stack.length) {
    const [i, j] = stack.pop()!;
    let best = -1, at = -1;
    for (let k = i + 1; k < j; k++) { const d = segDist(m[k]!, m[i]!, m[j]!); if (d > best) { best = d; at = k; } }
    if (at >= 0 && best > epsM) { keep[at] = true; stack.push([at, j], [i, at]); }
  }
  return keep.flatMap((v, k) => (v ? [k] : []));
}

const toM = (pts: Pt[]): Pt[] => pts.map(([x, y]) => [x * 1000, y * 1000]);

/** RD25: distances on km x 1000 against ε in metres; selects vertices, never moves them. */
export const simplify = (ptsKm: Pt[], epsM: number): Pt[] => dpKeep(toM(ptsKm), epsM).map((k) => ptsKm[k]!);

/** RD6: on the path v0 .. vn-1, v0; the closing v0 is then dropped again (closure is implicit). */
export function simplifyRing(ringKm: Pt[], epsM: number): Pt[] {
  if (ringKm.length < 3) return ringKm.slice();
  const path = [...ringKm, ringKm[0]!];
  const kept = dpKeep(toM(path), epsM);
  return kept.slice(0, -1).map((k) => path[k]!);
}

// ------------------------------------------------------------------------------------------------ quantisation (§2)

export class VertexRange extends Error {}

/** §2: floor(E_mm / R_mm * 32767 + 0.5), from the km value (E_km * 1e6). Past ±32,767 is a builder bug: thrown,
 *  never clamped (a clamp turns an impossible value into a plausible one). */
export function quantise(pKm: Pt, rMm: number): Pt {
  const q = (v: number) => {
    const n = Math.floor(v * 1e6 / rMm * QMAX + 0.5);
    if (n < -QMAX || n > QMAX) throw new VertexRange(`vertex_range: ${n}`);
    return n;
  };
  return [q(pKm[0]), q(pKm[1])];
}

/** §2 step 5 and RD24: consecutive duplicates dropped; for a ring, then a last vertex equal to the first. */
export function dedupe(q: Pt[], ring = false): Pt[] {
  const out: Pt[] = [];
  for (const p of q) { const l = out[out.length - 1]; if (!l || l[0] !== p[0] || l[1] !== p[1]) out.push(p); }
  if (ring) while (out.length > 1 && out[out.length - 1]![0] === out[0]![0] && out[out.length - 1]![1] === out[0]![1]) out.pop();
  return out;
}
export const dedupeRing = (q: Pt[]) => dedupe(q, true);

// ------------------------------------------------------------------------------------------------ orientation, length

/** A source part without its closing repeat (a shapefile writes rings closed). The one place that strips it, so
 *  the orientation test and the clip see the same ring. */
export const openRing = (part: Pt[]): Pt[] =>
  part.length > 1 && part[0]![0] === part[part.length - 1]![0] && part[0]![1] === part[part.length - 1]![1] ? part.slice(0, -1) : part;

/** RD26: a part is OUTER when its shoelace sum on (lon, lat), without its closing repeat, summed from the first
 *  edge, is negative. Zero is not outer. */
export function isOuter(partLonLat: Pt[]): boolean {
  const ring = openRing(partLonLat);
  let s = 0;
  for (let i = 0; i < ring.length; i++) { const [x0, y0] = ring[i]!, [x1, y1] = ring[(i + 1) % ring.length]!; s += x0 * y1 - x1 * y0; }
  return s < 0;
}

/** RD8, RD29, RD31: a line's raw length inside the disc of radius r (closed), in metres, per segment as §13 writes. */
export function inDiscLengthM(ptsKm: Pt[], rKm: number): number {
  let total = 0;
  for (let i = 0; i + 1 < ptsKm.length; i++) {
    const a = ptsKm[i]!, b = ptsKm[i + 1]!, dx = b[0] - a[0], dy = b[1] - a[1];
    const aa = dx * dx + dy * dy;
    if (aa === 0) continue;
    const bb = 2 * (a[0] * dx + a[1] * dy), cc = a[0] * a[0] + a[1] * a[1] - rKm * rKm, disc = bb * bb - 4 * aa * cc;
    if (disc <= 0) continue;
    const s = Math.sqrt(disc), t0 = Math.max(0, (-bb - s) / (2 * aa)), t1 = Math.min(1, (-bb + s) / (2 * aa));
    if (t1 > t0) total += (t1 - t0) * Math.sqrt(aa) * 1000;
  }
  return total;
}

// ------------------------------------------------------------------------------------------------ water rings: tp and the backstop
// (docs/basemap-dense-code.md, Q-W4: the owner's "Water fix: TP"; part 2's edge lock KB8 and repair KB4 (B')). Every water ring, TIGER's and
// the sea's, is simplified by Douglas-Peucker and quantised; then, while the quantised ring crosses itself, each crossing edge gets back
// the original vertex farthest from its chord (DP's own next choice) until none crosses or none can be added ("tp"). A ring that still
// crosses itself after that is REFUSED by the backstop: never drawn, and counted apart from the rings that collapse.

/** ring_stats()'s proper crossings, exact on integers, with pairs pruned by a sweep on x: the crossing edge indices. A shared vertex, a
 *  touch or a collinear overlap is not a proper crossing, so a keyhole bridge's two coincident edges (a contact) are never counted. */
export function properPairs(q: Pt[]): Array<[number, number]> {
  const n = q.length, out: Array<[number, number]> = [];
  if (n < 4) return out;
  const o = (a: Pt, b: Pt, c: Pt) => { const v = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); return v > 0 ? 1 : v < 0 ? -1 : 0; };
  const E = q.map((a, i) => { const b = q[(i + 1) % n]!; return { i, a, b, x0: Math.min(a[0], b[0]), x1: Math.max(a[0], b[0]), y0: Math.min(a[1], b[1]), y1: Math.max(a[1], b[1]) }; });
  const ord = [...E].sort((u, v) => u.x0 - v.x0);
  for (let s = 0; s < ord.length; s++) {
    const e = ord[s]!;
    for (let t = s + 1; t < ord.length && ord[t]!.x0 <= e.x1; t++) {
      const g = ord[t]!, i = Math.min(e.i, g.i), j = Math.max(e.i, g.i);
      if (j < i + 2 || (i === 0 && j === n - 1) || e.y1 < g.y0 || g.y1 < e.y0) continue;
      if (o(e.a, e.b, g.a) * o(e.a, e.b, g.b) < 0 && o(g.a, g.b, e.a) * o(g.a, g.b, e.b) < 0) out.push([i, j]);
    }
  }
  return out;
}
/** Quantise the kept path vertices, then dedupe as dedupe(q, true) does, remembering each survivor's path index. */
function quantKept(path: Pt[], kept: number[], rMm: number): { q: Pt[]; at: number[] } {
  const q: Pt[] = [], at: number[] = [];
  for (const k of kept) { const p = quantise(path[k]!, rMm), l = q[q.length - 1]; if (!l || l[0] !== p[0] || l[1] !== p[1]) { q.push(p); at.push(k); } }
  while (q.length > 1 && q[q.length - 1]![0] === q[0]![0] && q[q.length - 1]![1] === q[0]![1]) { q.pop(); at.pop(); }
  return { q, at };
}
/** tp: Douglas-Peucker as simplifyRing, then, while the quantised ring crosses itself, each crossing edge gets back the original vertex
 *  farthest from its chord, until none crosses or none can be added. */
export function simplifyRingTP(ringKm: Pt[], epsM: number, rMm: number): Pt[] {
  if (ringKm.length < 3) return dedupe(ringKm.map((p) => quantise(p, rMm)), true);
  const path = [...ringKm, ringKm[0]!], m = toM(path), n = ringKm.length;
  const keep = new Set(dpKeep(m, epsM).slice(0, -1));
  for (;;) {
    const kept = [...keep].sort((a, b) => a - b), { q, at } = quantKept(path, kept, rMm), pairs = properPairs(q);
    if (!pairs.length) return q;
    let added = 0;
    for (const s of new Set(pairs.flat())) {
      const a = at[s]!, b = s + 1 < at.length ? at[s + 1]! : n;
      let best = -1, k = -1;
      for (let x = a + 1; x < b; x++) { if (keep.has(x)) continue; const d = segDist(m[x]!, m[a]!, m[b]!); if (d > best) { best = d; k = x; } }
      if (k >= 0) { keep.add(k); added++; }
    }
    if (!added) return q;
  }
}
/** The backstop's refusal: a ring still crossing itself after tp. Callers test identity (`q === REFUSED`), so a refusal is never mistaken
 *  for a ring that collapsed under 3 vertices (the "(C)" counting rule: refusals and collapses are counted apart). */
export const REFUSED: Pt[] = [];
/** A water ring through tp and the backstop: the quantised ring, or REFUSED. */
export function waterQ4(ringKm: Pt[], epsM: number, rMm: number): Pt[] {
  const q = simplifyRingTP(ringKm, epsM, rMm);
  return properPairs(q).length ? REFUSED : q;
}
// EDGE-LOCKED simplification, for the sea path only (KB8). locked[i] marks a vertex on its own cell's boundary (the ingest's cut: a
// meridian or a parallel at a whole degree). The anchors are index 0 (Douglas-Peucker's own anchor, as simplifyRingTP's) and every locked
// vertex; DP runs on each chain between consecutive anchors, its two ends kept; then simplifyRingTP's own topology loop, which only ever
// adds vertices. With no locked vertex this is simplifyRingTP exactly: one chain, 0 .. n, the same dpKeep over the same path.
// The loop's state is returned too (simplifyRingTPLockedState), so the sea path's repair (KB4 (B')) restores vertices on the SAME path by
// the SAME rule (restoreNext, the one implementation of DP's next choice).
export interface TPState { path: Pt[]; m: Pt[]; n: number; rMm: number; keep: Set<number>; q: Pt[]; at: number[] }
/** DP's next choice on quantised edge s (at: each quantised vertex's path index): the unkept original vertex farthest from the edge's
 *  chord, the first maximum winning. 1 if one was restored, else 0. Used by the topology loop and by the repair. */
function restoreNext(st: TPState, at: number[], s: number): number {
  const a = at[s]!, b = s + 1 < at.length ? at[s + 1]! : st.n;
  let best = -1, k = -1;
  for (let x = a + 1; x < b; x++) { if (st.keep.has(x)) continue; const d = segDist(st.m[x]!, st.m[a]!, st.m[b]!); if (d > best) { best = d; k = x; } }
  if (k >= 0) { st.keep.add(k); return 1; }
  return 0;
}
/** The edge-locked simplify, with its state. */
export function simplifyRingTPLockedState(ringKm: Pt[], epsM: number, rMm: number, locked: boolean[]): TPState {
  if (ringKm.length < 3) return { path: ringKm.slice(), m: [], n: ringKm.length, rMm, keep: new Set(), q: dedupe(ringKm.map((p) => quantise(p, rMm)), true), at: [] };
  const path = [...ringKm, ringKm[0]!], m = toM(path), n = ringKm.length;
  const anchors = [...new Set([0, ...locked.flatMap((v, i) => (v && i < n ? [i] : []))])].sort((a, b) => a - b);
  const keep = new Set<number>(anchors);
  anchors.forEach((a, j) => {
    const b = j + 1 < anchors.length ? anchors[j + 1]! : n;
    for (const k of dpKeep(m.slice(a, b + 1), epsM)) if (a + k < n) keep.add(a + k);
  });
  const st: TPState = { path, m, n, rMm, keep, q: [], at: [] };
  for (;;) {
    const kept = [...keep].sort((a, b) => a - b), { q, at } = quantKept(path, kept, rMm), pairs = properPairs(q);
    st.q = q; st.at = at;
    if (!pairs.length) return st;
    let added = 0;
    for (const s of new Set(pairs.flat())) added += restoreNext(st, at, s);
    if (!added) return st;
  }
}
/** The edge-locked ring through the backstop: the quantised ring, or REFUSED. */
export function waterQ4Locked(ringKm: Pt[], epsM: number, rMm: number, locked: boolean[]): Pt[] {
  const q = simplifyRingTPLockedState(ringKm, epsM, rMm, locked).q;
  return properPairs(q).length ? REFUSED : q;
}
// ---- KB4 (B'), the sea path's step 4a: the repair, between simplify (step 3) and bridge (step 4).
const xo = (a: Pt, b: Pt, c: Pt) => { const v = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); return v > 0 ? 1 : v < 0 ? -1 : 0; };
/** Every edge of two DIFFERENT rings in a PROPER crossing (a touch, a shared vertex or a collinear overlap is not one), by ring index;
 *  exact on the quantised integers. The bounding-box test only skips pairs that cannot cross. */
export function crossRingEdges(rings: Pt[][]): Map<number, Set<number>> {
  const hit = new Map<number, Set<number>>(), add = (k: number, s: number) => { (hit.get(k) ?? hit.set(k, new Set()).get(k)!).add(s); };
  for (let A = 0; A < rings.length; A++) for (let B = A + 1; B < rings.length; B++) {
    const P = rings[A]!, Q = rings[B]!;
    for (let s = 0; s < P.length; s++) {
      const a = P[s]!, b = P[(s + 1) % P.length]!, x0 = Math.min(a[0], b[0]), x1 = Math.max(a[0], b[0]), y0 = Math.min(a[1], b[1]), y1 = Math.max(a[1], b[1]);
      for (let t = 0; t < Q.length; t++) {
        const c = Q[t]!, d = Q[(t + 1) % Q.length]!;
        if (Math.max(c[0], d[0]) < x0 || Math.min(c[0], d[0]) > x1 || Math.max(c[1], d[1]) < y0 || Math.min(c[1], d[1]) > y1) continue;
        if (xo(a, b, c) * xo(a, b, d) < 0 && xo(c, d, a) * xo(c, d, b) < 0) { add(A, s); add(B, t); }
      }
    }
  }
  return hit;
}
/** The frozen limit: vertices restored per piece (KB4 (B'), the owner's ruling of 2026-11-12 16:57Z). */
export const SEA_REPAIR_LIMIT = 64;
export type SeaRepairStopCode = "sea_repair_failed" | "sea_repair_limit";
/** Rounds of DP's next choice on every edge in a crossing (with another ring, or with its own), until nothing crosses. Stops on a round
 *  that restores nothing (sea_repair_failed) or past `limit` restored vertices (sea_repair_limit). `crossed`: the rings of round 1. */
export function repairRings(states: TPState[], limit: number): { q: Pt[][]; restored: number; rounds: number; crossed: number[]; stop: SeaRepairStopCode | null } {
  let restored = 0, rounds = 0, crossed: number[] | null = null;
  for (;;) {
    const qa = states.map((st) => quantKept(st.path, [...st.keep].sort((a, b) => a - b), st.rMm));
    const hit = crossRingEdges(qa.map((x) => x.q));
    qa.forEach((x, k) => { for (const [i, j] of properPairs(x.q)) { const h = hit.get(k) ?? hit.set(k, new Set()).get(k)!; h.add(i); h.add(j); } });
    const q = qa.map((x) => x.q);
    if (!hit.size) return { q, restored, rounds, crossed: crossed ?? [], stop: null };
    crossed ??= [...hit.keys()].sort((a, b) => a - b);
    rounds++;
    let added = 0;
    for (const k of [...hit.keys()].sort((a, b) => a - b)) for (const s of [...hit.get(k)!].sort((a, b) => a - b)) added += restoreNext(states[k]!, qa[k]!.at, s);
    if (!added) return { q, restored, rounds, crossed, stop: "sea_repair_failed" };
    restored += added;
    if (restored > limit) return { q, restored, rounds, crossed, stop: "sea_repair_limit" };
  }
}
