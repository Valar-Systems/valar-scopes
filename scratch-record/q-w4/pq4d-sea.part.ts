// SCRATCH, Q-W4 part 2: the builder's SEA PATH, behind Q4_SEA=1 (docs/basemap-dense-code.md, #652; #616's design, the 04:12:32Z ruling).
// A sea feature (kind 6) is one PIECE: part 0 its outer ring, parts 1..n its islands, stored at ingest as a boolean rectangle clip of the
// ocean or a design-(a) lake to a 1-degree cell. Per record, in #616's order:
//   1. clip the outer and each hole to the square (the water layer's clipRing); an outer clipped to nothing is OUTSIDE (not lost);
//   2. drop the holes now under 1.5 km2 (islandsSmall);
//   3. simplify and quantise each ring through the water path's own function (tp and the backstop under Q4_TP=1);
//      an outer refused is REFUSED, under 3 vertices COLLAPSED; a hole refused or collapsed is dropped (islandsDropped);
//   4. BRIDGE: each hole, largest first, joins the ring through the nearest (hole vertex, ring vertex) pair whose segment properly crosses
//      no edge of the ring, of that hole, or of any hole not yet bridged. The splice repeats the two vertices, so each bridge adds 2
//      vertices and has zero width under FillRing's rule. A hole no pair can reach is dropped (islandsDropped);
//   4a. KB4 (B') (registered at cf2f65300e89), between 3 and 4: when two DIFFERENT rings cross properly, every crossing edge (with
//      another ring, or with its own) gets back DP's next choice, round after round, until nothing crosses (frame.ts repairRings). A
//      round restoring nothing, or more than SEA_REPAIR_LIMIT (64) vertices restored, STOPS THE BUILD (SeaRepairStop: 500). Counted
//      on the counts (repairs, repairVertices, one repairLog line per piece). Plant "limit1" sets the limit to 1 (S-KB-REPAIRLIMIT);
//   5. the backstop on the bridged ring: a proper crossing left refuses the piece (REFUSED; counted apart as refused5, for (C)).
// PLANTS (Q4_SEAPLANT): "nobridge" sends the islands as their own rings (S-KB-NOBRIDGE); "cross" takes the nearest pair with no crossing
// test and skips step 5, so KB2 can see it (S-KB-CROSS); "order" bridges before simplifying (S-KB-ORDER); "seam" moves every
// odd-numbered piece by one quantum in x (S-KB-SEAM). Each piece's FATE is kept by its source key, for KB7-c.
// EDGE LOCK (the 06:35:32Z ruling, KB8): step 3 keeps every vertex on the piece's own cell boundary. A part's vertex whose lon or lat is a
// whole degree is LOCKED where the part is made (layers.ts, from its lon/lat); the square clip moves no surviving vertex, so a lock is
// carried through it by the vertex's exact frame point; under Q4_TP=1, step 3 is then waterQ4Locked (frame.ts), DP on the chains between
// locked vertices. Plant "edgeunlock" (S-KB-EDGEUNLOCK) takes the unlocked path, run 3's.
import { type Frame, type Pt, K, clipRing, crossRingEdges, dedupe, properPairs, quantise, repairRings, simplifyRing, simplifyRingTPLockedState, Q4W, REFUSED, SEA_REPAIR_LIMIT, waterQ4, waterQ4Locked } from "./frame";

export const Q4S = { on: process.env.Q4_SEA === "1", plant: process.env.Q4_SEAPLANT ?? "" };
export interface SeaPart { file: number; record: number; part: number; km: Pt[]; lock?: boolean[] }
export type SeaFate = "drawn" | "refused" | "collapsed" | "outside";
export interface SeaOut { file: number; record: number; part: number; ring: Pt[] | null; fate: SeaFate; plantedRings?: Pt[][]; q?: { outer: Pt[]; holes: Pt[][] } } // q: the quantised outer and islands BEFORE bridging, for KB1's reference
export interface SeaCounts { refused: number; collapsed: number; islandsDropped: number; islandsSmall: number; bridges: number; outside: number; drawn: number;
  refused5: number; repairs: number; repairVertices: number; repairLog: string[] } // KB4 (B') and (C)
export const newSeaCounts = (): SeaCounts => ({ refused: 0, collapsed: 0, islandsDropped: 0, islandsSmall: 0, bridges: 0, outside: 0, drawn: 0, refused5: 0, repairs: 0, repairVertices: 0, repairLog: [] });
/** KB4 (B'): a repair that cannot finish stops the build (the builder answers 500 with this code), never a partial package.
 *  "sea_repair_state" is the builder's own guard: step 4a's recomputed state must be step 3's rings. */
export class SeaRepairStop extends Error { constructor(readonly code: "sea_repair_failed" | "sea_repair_limit" | "sea_repair_state", msg: string) { super(msg); } }
const sameRing = (a: Pt[], b: Pt[]) => a.length === b.length && a.every((p, i) => p[0] === b[i]![0] && p[1] === b[i]![1]);
/** An island's point for the log: the area centroid of its clipped ring (km, frame), as lat,lon to 3 decimals. */
function pointOf(r: Pt[], f: Frame): string {
  let a2 = 0, cx = 0, cy = 0;
  for (let i = 0; i < r.length; i++) { const [x0, y0] = r[i]!, [x1, y1] = r[(i + 1) % r.length]!, c = x0 * y1 - x1 * y0; a2 += c; cx += (x0 + x1) * c; cy += (y0 + y1) * c; }
  const x = cx / (3 * a2), y = cy / (3 * a2);
  return `${(f.lat0 - y / K).toFixed(3)},${(f.lon0 + x / (K * f.c0)).toFixed(3)}`;
}

const ISLAND_MIN_KM2 = 1.5;
const shoelace = (r: Pt[]) => { let s = 0; for (let i = 0; i < r.length; i++) { const [x0, y0] = r[i]!, [x1, y1] = r[(i + 1) % r.length]!; s += x0 * y1 - x1 * y0; } return s / 2; };
const orient = (a: Pt, b: Pt, c: Pt) => Math.sign((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
const crosses = (a: Pt, b: Pt, c: Pt, d: Pt) => orient(a, b, c) * orient(a, b, d) < 0 && orient(c, d, a) * orient(c, d, b) < 0;
function clear(a: Pt, b: Pt, rings: Pt[][]): boolean {
  for (const r of rings) for (let i = 0; i < r.length; i++) if (crosses(a, b, r[i]!, r[(i + 1) % r.length]!)) return false;
  return true;
}
/** Bridges `holes` into `outer`, largest first, through the nearest visible pair; returns the ring, the bridges made and the holes dropped. */
export function bridge(outer: Pt[], holes: Pt[][], test = true): { ring: Pt[]; bridges: number; dropped: number } {
  let ring = outer.slice(), bridges = 0, dropped = 0;
  const order = holes.map((h, i) => [i, Math.abs(shoelace(h))] as const).sort((a, b) => b[1] - a[1] || a[0] - b[0]).map(([i]) => i);
  const left = new Set(order);
  for (const hi of order) {
    left.delete(hi);
    const h = holes[hi]!, others = [...left].map((k) => holes[k]!);
    const cand: Array<[number, number, number]> = [];
    for (let i = 0; i < h.length; i++) for (let j = 0; j < ring.length; j++) { const dx = h[i]![0] - ring[j]![0], dy = h[i]![1] - ring[j]![1]; cand.push([dx * dx + dy * dy, i, j]); }
    cand.sort((a, b) => a[0] - b[0] || a[1] - b[1] || a[2] - b[2]);
    let made = false;
    for (const [, i, j] of cand) {
      if (test && !clear(h[i]!, ring[j]!, [ring, h, ...others])) continue;
      ring = [...ring.slice(0, j + 1), ...h.slice(i), ...h.slice(0, i), h[i]!, ring[j]!, ...ring.slice(j + 1)];
      made = true; bridges++; break;
    }
    if (!made) dropped++;
  }
  return { ring, bridges, dropped };
}
/** The water path's own simplify-and-quantise (tp and the backstop under Q4_TP=1). */
const simp = (r: Pt[], f: Frame): Pt[] => (Q4W.tp ? waterQ4(r, f.epsM, f.rMm) : dedupe(simplifyRing(r, f.epsM).map((p) => quantise(p, f.rMm)), true));
const km2 = (r: Pt[]) => Math.abs(shoelace(r));
/** The package's own byte cost of one ring (format.ts: varint count, then zigzag varint points and deltas). */
export function ringBytes(r: Pt[]): number {
  const vl = (v: number) => { let n = 1; while (v >= 0x80) { v = Math.floor(v / 0x80); n++; } return n; };
  const zz = (v: number) => vl(((v << 1) ^ (v >> 31)) >>> 0);
  let b = vl(r.length), px = 0, py = 0;
  r.forEach(([x, y], i) => { b += i === 0 ? zz(x) + zz(y) : zz(x - px) + zz(y - py); px = x; py = y; });
  return b;
}
export function seaRings(parts: SeaPart[], f: Frame, n: SeaCounts): SeaOut[] {
  const byRec = new Map<string, SeaPart[]>();
  for (const p of parts) { const k = `${p.file}/${p.record}`; (byRec.get(k) ?? byRec.set(k, []).get(k)!).push(p); }
  const out: SeaOut[] = [];
  for (const ps of [...byRec.values()].sort((a, b) => a[0]!.file - b[0]!.file || a[0]!.record - b[0]!.record)) {
    ps.sort((a, b) => a.part - b.part);
    const o0 = ps[0]!;
    if (o0.part !== 0) continue;
    const key = { file: o0.file, record: o0.record, part: 0 };
    const lockKeys = new Set<string>(); // KB8: the locked vertices' exact frame points, over every part of the record
    for (const p of ps) (p.lock ?? []).forEach((v, i) => { if (v) lockKeys.add(`${p.km[i]![0]},${p.km[i]![1]}`); });
    const simpL = (r: Pt[]): Pt[] => (Q4S.plant === "edgeunlock" || !Q4W.tp ? simp(r, f) : waterQ4Locked(r, f.epsM, f.rMm, r.map(([x, y]) => lockKeys.has(`${x},${y}`))));
    const oc = clipRing(o0.km, f.rKm);
    if (!oc.length) { n.outside++; out.push({ ...key, ring: null, fate: "outside" }); continue; }
    const hc = ps.slice(1).map((p) => clipRing(p.km, f.rKm)).filter((h) => h.length >= 3);
    const holesKm = hc.filter((h) => km2(h) >= ISLAND_MIN_KM2);
    n.islandsSmall += hc.length - holesKm.length;
    let ring: Pt[];
    let qkeep: { outer: Pt[]; holes: Pt[][] } | undefined;
    if (Q4S.plant === "order") {
      const b = bridge(oc, holesKm, true); n.bridges += b.bridges; n.islandsDropped += b.dropped;
      ring = simp(b.ring, f);
      if (ring === REFUSED) { n.refused++; out.push({ ...key, ring: null, fate: "refused" }); continue; }
      if (ring.length < 3) { n.collapsed++; out.push({ ...key, ring: null, fate: "collapsed" }); continue; }
    } else {
      let oq = simpL(oc);
      if (oq === REFUSED) { n.refused++; out.push({ ...key, ring: null, fate: "refused" }); continue; }
      if (oq.length < 3) { n.collapsed++; out.push({ ...key, ring: null, fate: "collapsed" }); continue; }
      const hs = holesKm.map((h) => ({ km: h, q: simpL(h) })).filter((x) => x.q !== REFUSED && x.q.length >= 3);
      let hq = hs.map((x) => x.q);
      n.islandsDropped += holesKm.length - hq.length;
      if (hq.length && crossRingEdges([oq, ...hq]).size) { // KB4 (B'), step 4a: two different rings cross after step 3
        const lockOf = (r: Pt[]) => (Q4S.plant === "edgeunlock" || !Q4W.tp ? r.map(() => false) : r.map(([x, y]) => lockKeys.has(`${x},${y}`)));
        const st = [oc, ...hs.map((x) => x.km)].map((r) => simplifyRingTPLockedState(r, f.epsM, f.rMm, lockOf(r), false));
        const id = `${key.file}/${key.record}`, q3 = [oq, ...hq];
        if (st.some((s, i) => !sameRing(s.q, q3[i]!))) throw new SeaRepairStop("sea_repair_state", `${id}: step 4a's state is not step 3's rings`);
        const R = repairRings(st, Q4S.plant === "limit1" ? 1 : SEA_REPAIR_LIMIT);
        const line = `sea_repair ${id} rings ${R.crossed.join(",")} restored ${R.restored} rounds ${R.rounds} island ${R.crossed.filter((i) => i > 0).map((i) => pointOf(hs[i - 1]!.km, f)).join(" ")}`;
        if (R.stop) throw new SeaRepairStop(R.stop, line);
        oq = R.q[0]!; hq = R.q.slice(1);
        n.repairs++; n.repairVertices += R.restored; n.repairLog.push(line);
      }
      qkeep = { outer: oq, holes: hq };
      if (Q4S.plant === "nobridge") { n.drawn++; out.push({ ...key, ring: oq, fate: "drawn", plantedRings: hq, q: qkeep }); continue; }
      const b = bridge(oq, hq, Q4S.plant !== "cross"); n.bridges += b.bridges; n.islandsDropped += b.dropped;
      ring = b.ring;
    }
    if (Q4S.plant === "seam" && key.record % 2 === 1) ring = ring.map(([x, y]) => [x + 1, y] as Pt);
    if (Q4S.plant !== "cross" && Q4W.backstop && properPairs(ring).length) { n.refused++; n.refused5++; out.push({ ...key, ring: null, fate: "refused" }); continue; }
    n.drawn++; out.push({ ...key, ring, fate: "drawn", q: qkeep });
  }
  return out;
}
