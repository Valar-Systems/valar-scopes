// SCRATCH Q-W4 part 2 (the 06:35:32Z ruling, KB8): EDGE-LOCKED simplification, for the sea path only. locked[i] marks a vertex on its
// own cell's boundary (the ingest's cut: a meridian or a parallel at a whole degree). The anchors are index 0 (Douglas-Peucker's own
// anchor, as simplifyRingTP's) and every locked vertex; DP runs on each chain between consecutive anchors, its two ends kept; then
// simplifyRingTP's own topology loop, unchanged, which only ever adds vertices. With no locked vertex this is simplifyRingTP exactly:
// one chain, 0 .. n, the same dpKeep over the same path.
// KB4 (B') (registered at cf2f65300e89): the loop's state is returned too (simplifyRingTPLockedState), so the sea path's step 4a can
// restore vertices on the SAME path by the SAME rule (restoreNext, the one implementation of DP's next choice); simplifyRingTPLocked
// is that state's rings, byte for byte as before.
export interface TPState { path: Pt[]; m: Pt[]; n: number; rMm: number; keep: Set<number>; q: Pt[]; at: number[] }
/** DP's next choice on quantised edge s (at: each quantised vertex's path index): the unkept original vertex farthest from the edge's
 *  chord, the first maximum winning. 1 if one was restored, else 0. Used by the topology loop and by step 4a. */
function restoreNext(st: TPState, at: number[], s: number): number {
  const a = at[s]!, b = s + 1 < at.length ? at[s + 1]! : st.n;
  let best = -1, k = -1;
  for (let x = a + 1; x < b; x++) { if (st.keep.has(x)) continue; const d = segDist(st.m[x]!, st.m[a]!, st.m[b]!); if (d > best) { best = d; k = x; } }
  if (k >= 0) { st.keep.add(k); return 1; }
  return 0;
}
/** The edge-locked simplify, with its state. `count` false leaves waterFixStats alone (step 4a recomputes a state step 3 already counted). */
export function simplifyRingTPLockedState(ringKm: Pt[], epsM: number, rMm: number, locked: boolean[], count = true): TPState {
  if (ringKm.length < 3) return { path: ringKm.slice(), m: [], n: ringKm.length, rMm, keep: new Set(), q: dedupe(ringKm.map((p) => quantise(p, rMm)), true), at: [] };
  const path = [...ringKm, ringKm[0]!], m = toM(path), n = ringKm.length;
  const anchors = [...new Set([0, ...locked.flatMap((v, i) => (v && i < n ? [i] : []))])].sort((a, b) => a - b);
  const keep = new Set<number>(anchors);
  anchors.forEach((a, j) => {
    const b = j + 1 < anchors.length ? anchors[j + 1]! : n;
    for (const k of dpKeep(m.slice(a, b + 1), epsM)) if (a + k < n) keep.add(a + k);
  });
  const st: TPState = { path, m, n, rMm, keep, q: [], at: [] };
  let first = true;
  for (;;) {
    const kept = [...keep].sort((a, b) => a - b), { q, at } = quantKept(path, kept, rMm), pairs = properPairs(q);
    st.q = q; st.at = at;
    if (!pairs.length) return st;
    if (first) { if (count) waterFixStats.tpRings++; first = false; }
    let added = 0;
    for (const s of new Set(pairs.flat())) added += restoreNext(st, at, s);
    if (!added) { if (count) waterFixStats.tpLeft++; return st; }
    if (count) waterFixStats.tpAdded += added;
  }
}
export function simplifyRingTPLocked(ringKm: Pt[], epsM: number, rMm: number, locked: boolean[]): Pt[] {
  return simplifyRingTPLockedState(ringKm, epsM, rMm, locked).q;
}
/** waterQ4 with the lock: the same backstop, the same refusal marker. */
export function waterQ4Locked(ringKm: Pt[], epsM: number, rMm: number, locked: boolean[]): Pt[] {
  const q = simplifyRingTPLocked(ringKm, epsM, rMm, locked);
  if (Q4W.backstop && properPairs(q).length) { q4Stats.refused++; return Q4P.prov ? REFUSED : []; } // SCRATCH Q-W4 (C)
  return q;
}
// ---- KB4 (B'), step 4a (registered at cf2f65300e89): the repair, between step 3 (simplify) and step 4 (bridge).
const xo = (a: Pt, b: Pt, c: Pt) => { const v = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); return v > 0 ? 1 : v < 0 ? -1 : 0; };
/** Every edge of two DIFFERENT rings in a PROPER crossing (step 5's sense: a touch, a shared vertex or a collinear overlap is not one),
 *  by ring index; exact on the quantised integers. The bounding-box test only skips pairs that cannot cross. */
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
/** The frozen limit: vertices restored per piece. */
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
