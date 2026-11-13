// §6's budget (docs/basemap-package-format.md §6; §13's RD13, RD15, RD18-RD20; the route doc's MB rows). Pure.
//
// Over the cap, in order and never silently (Q-W4, docs/basemap-dense-code.md: A+B, then the owner's B, P1):
//   rung 0  as requested;            rung 1  ε × 2 for everything;          rung 2  water at ε × 8;
//   rung 3  + secondary roads dropped, shortest first;                      rung 4  + water rings dropped, smallest area first;
//   rung 5  rivers and roads at ε × 4 (water stays at ε × 8);               rungs 6, 7  rungs 3 and 4 again on rung 5's content.
// Primaries are never dropped. Every drop is counted, and the counts are written into PROVENANCE by the caller's `assemble`, so each
// measurement re-renders them (a count's digits are bytes). The water counts (collapsed, refused) travel with the water set drawn.
// Still over: null, which the builder answers by D2 (a smaller radius, build.ts) and finally as 422 too_large.
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
/** The content the ladder may drop from. `collapsed`, `refusedW` and `seaW` are the water set's own counts (the "(C)" rule): a rung
 *  that draws another water set carries that set's counts with its rings. */
export interface Droppable {
  water?: Pt[][]; roads?: Array<{ cls: 1 | 2; pts: Pt[] }>;
  collapsed?: Record<string, number | undefined>; refusedW?: number; seaW?: unknown;
}
export interface Fitted<C> { content: C; toleranceMm: number; dropped: Dropped; sections: Section[] }

/** The bytes each non-terrain section adds (its body, as nonTerrainBytes counts it), by layer. An absent layer adds 0. */
export interface LayerBytes { water: number; rivers: number; roads: number; peaks: number; places: number; provenance: number }

/**
 * Step 0 (docs/basemap-dense.md): what the ladder measured, for the builder's bm_build line. Counts only, and nothing new
 * is measured: every value is the nonTerrainBytes of an assembly the ladder already does.
 */
export interface Ladder {
  /** The rung that fitted, 0 to 7 (the ladder above); -1 none. */
  rung: number;
  /** The old ladder's per-rung bytes on a 422. The Q-W4 ladder leaves it empty, so bm_build carries no nt fields. */
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

  // rungs 2 to 7: water at ε × 8 (A+B); then secondary roads shortest first, then water smallest first; then the same at lines ε × 4 (P1)
  const dropSeq = (c0: C, tolH: number, rA: number, rB: number): Fitted<C> | null => {
    d.water = 0; d.roads = 0;
    const roads0 = c0.roads ?? [];
    const sec0 = roads0.map((r, i) => [r, i] as const).filter(([r]) => r.cls === 2).map(([, i]) => i);
    const order0 = dropOrder(sec0.map((i) => roadLength(roads0[i]!.pts))).map((k) => sec0[k]!);
    const noRoads = (k: number): C => { const gone = new Set(order0.slice(0, k)); return { ...c0, roads: roads0.filter((_, i) => !gone.has(i)) }; };
    const r0 = smallestFit(order0.length, (k) => { d.roads = k; return fits(noRoads(k), tolH); });
    if (r0) { d.roads = r0.k; return done(rA, { content: noRoads(r0.k), toleranceMm: tolH, dropped: d, sections: r0.got }); }
    d.roads = order0.length;
    const c1 = noRoads(order0.length), rings0 = c1.water ?? [];
    const w0 = dropOrder(rings0.map(ringArea2));
    const noWater0 = (k: number): C => { const gone = new Set(w0.slice(0, k)); return { ...c1, water: rings0.filter((_, i) => !gone.has(i)) }; };
    const w1 = smallestFit(rings0.length, (k) => { d.water = k; return fits(noWater0(k), tolH); });
    if (w1) { d.water = w1.k; return done(rB, { content: noWater0(w1.k), toleranceMm: tolH, dropped: d, sections: w1.got }); }
    return null;
  };
  const m8 = make(toleranceMm * 8), water8 = m8.water; // the drawn set's water counts travel with its rings ("(C)")
  const c2 = { ...content, water: water8, collapsed: { ...content.collapsed, water: m8.collapsed?.water, sea: m8.collapsed?.sea }, refusedW: m8.refusedW, seaW: m8.seaW } as C;
  s = fits(c2, tol);
  if (s) return done(2, { content: c2, toleranceMm: tol, dropped: d, sections: s });
  const tol4 = toleranceMm * 4;
  const c4 = (): C => { const m4 = make(tol4); return { ...m4, water: water8, collapsed: { ...m4.collapsed, water: m8.collapsed?.water, sea: m8.collapsed?.sea }, refusedW: m8.refusedW, seaW: m8.seaW } as C; };
  const at4 = (): Fitted<C> | null => {
    d.water = 0; d.roads = 0;
    const a = c4(), s4 = fits(a, tol4);
    if (s4) return done(5, { content: a, toleranceMm: tol4, dropped: d, sections: s4 });
    return dropSeq(a, tol4, 6, 7);
  };
  let got: Fitted<C> | null = dropSeq(c2, tol, 3, 4);
  if (!got) got = at4();
  if (got) return got;
  if (ladder) { ladder.rung = -1; ladder.nt = []; ladder.sections = layerBytes(last); }
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
