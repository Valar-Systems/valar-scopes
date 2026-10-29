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
  const goneW = new Set<number>();
  for (const i of dropOrder(rings.map(ringArea2))) {
    goneW.add(i);
    d.water++;
    content = { ...content, water: rings.filter((_, k) => !goneW.has(k)) };
    s = fits(content, tol);
    if (s) return { content, toleranceMm: tol, dropped: d, sections: s };
  }

  // §6 step 3: SECONDARY roads, shortest first (RD19); primaries are never dropped
  const roads = content.roads ?? [];
  const secondary = roads.map((r, i) => [r, i] as const).filter(([r]) => r.cls === 2).map(([, i]) => i);
  const goneR = new Set<number>();
  for (const k of dropOrder(secondary.map((i) => roadLength(roads[i]!.pts)))) {
    goneR.add(secondary[k]!);
    d.roads++;
    content = { ...content, roads: roads.filter((_, i) => !goneR.has(i)) };
    s = fits(content, tol);
    if (s) return { content, toleranceMm: tol, dropped: d, sections: s };
  }
  return null;
}
