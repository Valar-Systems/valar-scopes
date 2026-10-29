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

/**
 * §6. `make(tolMm)` builds the layers at a tolerance; `assemble(content, dropped, tolMm)` writes every section of the
 * package, PROVENANCE included. The content's `water` and `roads` are the items §6 may drop.
 */
export function fitBudget<C extends Droppable>(
  toleranceMm: number, capBytes: number, make: (tolMm: number) => C, assemble: (c: C, d: Dropped, tolMm: number) => Section[],
): Fitted<C> | null {
  const d: Dropped = { water: 0, roads: 0 };
  const fits = (c: C, tol: number) => { const s = assemble(c, d, tol); return nonTerrainBytes(s) <= capBytes ? s : null; };

  let content = make(toleranceMm);
  let s = fits(content, toleranceMm);
  if (s) return { content, toleranceMm, dropped: d, sections: s };

  const tol = toleranceMm * EPS_STEP; // §6 step 1: once (RD13: the header holds the ε used)
  content = make(tol);
  s = fits(content, tol);
  if (s) return { content, toleranceMm: tol, dropped: d, sections: s };

  // §6 step 2: water rings, smallest area first (RD18), the survivors kept in source order
  const rings = content.water ?? [];
  const wOrder = dropOrder(rings.map(ringArea2));
  const base = content;
  const withWater = (k: number): C => { const gone = new Set(wOrder.slice(0, k)); return { ...base, water: rings.filter((_, i) => !gone.has(i)) }; };
  const w = smallestFit(rings.length, (k) => { d.water = k; return fits(withWater(k), tol); });
  if (w) { d.water = w.k; return { content: withWater(w.k), toleranceMm: tol, dropped: d, sections: w.got }; }
  d.water = rings.length;
  content = withWater(rings.length);

  // §6 step 3: SECONDARY roads, shortest first (RD19); primaries are never dropped
  const roads = content.roads ?? [];
  const secondary = roads.map((r, i) => [r, i] as const).filter(([r]) => r.cls === 2).map(([, i]) => i);
  const rOrder = dropOrder(secondary.map((i) => roadLength(roads[i]!.pts))).map((k) => secondary[k]!);
  const noWater = content;
  const withRoads = (k: number): C => { const gone = new Set(rOrder.slice(0, k)); return { ...noWater, roads: roads.filter((_, i) => !gone.has(i)) }; };
  const r = smallestFit(rOrder.length, (k) => { d.roads = k; return fits(withRoads(k), tol); });
  if (r) { d.roads = r.k; return { content: withRoads(r.k), toleranceMm: tol, dropped: d, sections: r.got }; }
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
