// §6's budget (docs/basemap-package-format.md §6; §13's RD13, RD15, RD18-RD20; the route doc's MB rows). Pure.
//
// Over the cap, in order and never silently: ε doubled ONCE; then water rings dropped smallest-area first; then
// SECONDARY roads dropped shortest first. Primaries are never dropped. Every drop is counted, and the counts are
// written into PROVENANCE by the caller's `assemble`, so each measurement re-renders them (a count's digits are bytes).
// Still over: null, which the route answers as 422 too_large.
//
// Each step re-assembles and re-measures the whole package rather than keeping a running total: slower in the rare
// over-budget case, but there is no second size model to drift from encode().
import { type Section, TYPE, sectionSize } from "./format";
import { type Pt, planarDist } from "./frame";

export const CAP_BYTES = 32 * 1024;
/** §6 step 1's factor, ONE constant: the builder pre-computes its second ε with it, and the header records it (RD13). */
export const EPS_STEP = 2;
const HEADER = 32, ENTRY = 16;

/** RD20: what the cap measures, totalBytes minus the TERRAIN sections' bodies. Their directory entries count. */
export function nonTerrainBytes(sections: Section[]): number {
  let n = HEADER + ENTRY * sections.length;
  for (const s of sections) if (s.type !== TYPE.TERRAIN) n += sectionSize(s);
  return n;
}

/** RD18: the absolute shoelace sum of a ring's quantised vertices (twice its area; exact, as integers). */
export function ringArea2(ring: Pt[]): number {
  let s = 0;
  for (let i = 0; i < ring.length; i++) { const [x0, y0] = ring[i]!, [x1, y1] = ring[(i + 1) % ring.length]!; s += x0 * y1 - x1 * y0; }
  return Math.abs(s);
}

/** RD19: a road's summed segment length along its quantised vertices, in double. */
export function roadLength(pts: Pt[]): number {
  let t = 0;
  for (let i = 1; i < pts.length; i++) t += planarDist(pts[i]![0] - pts[i - 1]![0], pts[i]![1] - pts[i - 1]![1]);
  return t;
}

/** RD18 and RD19's order: the indices of `measure`, smallest first; on a tie, the LATER item first. */
export function dropOrder(measure: number[]): number[] {
  return measure.map((_, i) => i).sort((a, b) => measure[a]! - measure[b]! || b - a);
}

export interface Dropped { water: number; roads: number }
export interface Droppable { water?: Pt[][]; roads?: Array<{ cls: 1 | 2; pts: Pt[] }> }
export interface Fitted<C> { content: C; toleranceMm: number; dropped: Dropped; sections: Section[] }

/** The bytes each non-terrain section adds (its body, as nonTerrainBytes counts it), by layer. An absent layer adds 0. */
export interface LayerBytes { water: number; rivers: number; roads: number; peaks: number; places: number; provenance: number }

/**
 * Step 0 (docs/basemap-dense.md): what the ladder measured, for the builder's bm_build line. Counts only, and nothing new
 * is measured: every value is the nonTerrainBytes of an assembly the ladder already does.
 */
export interface Ladder {
  /** The step that fitted: 0 as built, 1 after ε × 2, 2 while dropping water, 3 while dropping secondary roads; -1 none. */
  rung: number;
  /** On a 422 only: the non-terrain bytes at each rung's FULL step -- [ε, ε × 2, every ring dropped, every secondary
   *  dropped too]. A rung with nothing to drop repeats the value before it. Empty on a 200. */
  nt: number[];
  /** By layer: the package built's sections, or (on a 422) the last rung tried's. */
  sections: LayerBytes;
}
export const newLadder = (): Ladder => ({ rung: 0, nt: [], sections: { water: 0, rivers: 0, roads: 0, peaks: 0, places: 0, provenance: 0 } });

const LAYER_OF: Partial<Record<number, keyof LayerBytes>> = {
  [TYPE.WATER]: "water", [TYPE.RIVERS]: "rivers", [TYPE.ROADS]: "roads", [TYPE.PEAKS]: "peaks", [TYPE.PLACES]: "places", [TYPE.PROVENANCE]: "provenance",
};
export function layerBytes(sections: Section[]): LayerBytes {
  const b = newLadder().sections;
  for (const s of sections) { const k = LAYER_OF[s.type]; if (k) b[k] += sectionSize(s); }
  return b;
}

/**
 * §6. `make(tolMm)` builds the layers at a tolerance; `assemble(content, dropped, tolMm)` writes every section of the
 * package, PROVENANCE included. The content's `water` and `roads` are the items §6 may drop. `ladder`, when given, is
 * filled with what the ladder measured (step 0); it never changes what is built.
 */
export function fitBudget<C extends Droppable>(
  toleranceMm: number, capBytes: number, make: (tolMm: number) => C, assemble: (c: C, d: Dropped, tolMm: number) => Section[],
  ladder?: Ladder,
): Fitted<C> | null {
  const d: Dropped = { water: 0, roads: 0 };
  let last: Section[] = [], lastBytes = 0;
  const fits = (c: C, tol: number) => { const s = assemble(c, d, tol); last = s; lastBytes = nonTerrainBytes(s); return lastBytes <= capBytes ? s : null; };
  const done = (rung: number, f: Fitted<C>): Fitted<C> => {
    if (ladder) { ladder.rung = rung; ladder.nt = []; ladder.sections = layerBytes(f.sections); }
    return f;
  };
  const nt: number[] = [];

  let content = make(toleranceMm);
  let s = fits(content, toleranceMm);
  nt.push(lastBytes);
  if (s) return done(0, { content, toleranceMm, dropped: d, sections: s });

  const tol = toleranceMm * EPS_STEP; // §6 step 1: once (RD13: the header holds the ε used)
  content = make(tol);
  s = fits(content, tol);
  nt.push(lastBytes);
  if (s) return done(1, { content, toleranceMm: tol, dropped: d, sections: s });

  // §6 step 2: water rings, smallest area first (RD18), the survivors kept in source order
  const rings = content.water ?? [];
  const wOrder = dropOrder(rings.map(ringArea2));
  const base = content;
  const withWater = (k: number): C => { const gone = new Set(wOrder.slice(0, k)); return { ...base, water: rings.filter((_, i) => !gone.has(i)) }; };
  const w = smallestFit(rings.length, (k) => { d.water = k; const got = fits(withWater(k), tol); if (k === rings.length) nt[2] = lastBytes; return got; });
  if (w) { d.water = w.k; return done(2, { content: withWater(w.k), toleranceMm: tol, dropped: d, sections: w.got }); }
  if (nt.length < 3) nt.push(nt[1]!); // no ring to drop: the value before it
  d.water = rings.length;
  content = withWater(rings.length);

  // §6 step 3: SECONDARY roads, shortest first (RD19); primaries are never dropped
  const roads = content.roads ?? [];
  const secondary = roads.map((r, i) => [r, i] as const).filter(([r]) => r.cls === 2).map(([, i]) => i);
  const rOrder = dropOrder(secondary.map((i) => roadLength(roads[i]!.pts))).map((k) => secondary[k]!);
  const noWater = content;
  const withRoads = (k: number): C => { const gone = new Set(rOrder.slice(0, k)); return { ...noWater, roads: roads.filter((_, i) => !gone.has(i)) }; };
  const r = smallestFit(rOrder.length, (k) => { d.roads = k; const got = fits(withRoads(k), tol); if (k === rOrder.length) nt[3] = lastBytes; return got; });
  if (r) { d.roads = r.k; return done(3, { content: withRoads(r.k), toleranceMm: tol, dropped: d, sections: r.got }); }
  if (nt.length < 4) nt.push(nt[2]!); // no secondary to drop: the value before it
  if (ladder) { ladder.rung = -1; ladder.nt = nt; ladder.sections = layerBytes(last); }
  return null;
}

/**
 * The smallest k in [1, n] for which attempt(k) fits, by bisection -- or null when even k = n does not. attempt(0) is
 * known not to fit when this is called. It gives the same k as dropping one item at a time, because fitting is
 * MONOTONE in k: every drop shrinks the package (a ring or a road item costs at least 6 B, while a count gaining a digit
 * adds 1 B to PROVENANCE). The linear loop re-assembled the package after every drop, which on a dense square (A2.37:
 * KATL r 222, 14,904 drops) took about 27 s of CPU; this takes about log2(n) + 1 assemblies.
 */
function smallestFit<T>(n: number, attempt: (k: number) => T | null): { k: number; got: T } | null {
  if (n === 0) return null;
  const top = attempt(n);
  if (!top) return null;
  let lo = 0, hi = n, got = top; // attempt(lo) does not fit; attempt(hi) fits
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1, t = attempt(mid);
    if (t) { hi = mid; got = t; } else lo = mid;
  }
  return { k: hi, got };
}
