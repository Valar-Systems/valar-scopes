// The builder's SEA PATH (docs/basemap-dense-code.md, part 2; docs/basemap-sea-mask.md, #616). A sea feature (store kind 6) is one PIECE:
// part 0 its outer ring, parts 1..n its islands, stored at ingest as a boolean rectangle clip of the Natural Earth ocean, or of a design-(a)
// lake, to a 1-degree cell. Per record, in #616's order:
//   1. clip the outer and each hole to the square (the water layer's clipRing); an outer clipped to nothing is OUTSIDE (not lost);
//   2. drop the holes now under 1.5 km2 (islandsSmall);
//   3. simplify and quantise each ring through the water path's own tp and backstop, EDGE-LOCKED (KB8): every vertex on the piece's own
//      cell boundary is kept. An outer refused is REFUSED, under 3 vertices COLLAPSED; a hole refused or collapsed is dropped
//      (islandsDropped);
//   4a. REPAIR (KB4 (B')), between 3 and 4: when two DIFFERENT rings cross properly, every crossing edge (with another ring, or with its own)
//      gets back DP's next choice, round after round, until nothing crosses (frame.ts repairRings). A round restoring nothing, or more
//      than SEA_REPAIR_LIMIT (64) vertices restored, STOPS THE BUILD (SeaRepairStop: the builder answers 500). Counted on the counts
//      (repairs, repairVertices, one repairLog line per piece);
//   4. BRIDGE: each hole, largest first, joins the ring through the nearest (hole vertex, ring vertex) pair whose segment properly crosses
//      no edge of the ring, of that hole, or of any hole not yet bridged. The splice repeats the two vertices, so each bridge adds 2
//      vertices and has zero width under FillRing's rule. A hole no pair can reach is dropped (islandsDropped);
//   5. the backstop on the bridged ring: a proper crossing left refuses the piece (REFUSED; counted apart as refused5).
// Each piece's FATE is kept by its source key (KB7-c). The checks' sabotages (S-KB-*) are edits a driver plants in a copy of this file.
// EDGE LOCK: a part's vertex whose lon or lat is a whole degree is LOCKED where the part is made (layers.ts, from its lon/lat); the square
// clip moves no surviving vertex, so a lock is carried through it by the vertex's exact frame point.
import { type Frame, type Pt, K, REFUSED, SEA_REPAIR_LIMIT, clipRing, crossRingEdges, properPairs, repairRings, simplifyRingTPLockedState, waterQ4Locked } from "./frame";

export interface SeaPart { file: number; record: number; part: number; km: Pt[]; lock?: boolean[] }
export type SeaFate = "drawn" | "refused" | "collapsed" | "outside";
/** One piece's outcome. `q`: the quantised outer and islands BEFORE bridging, for KB1's reference. */
export interface SeaOut { file: number; record: number; part: number; ring: Pt[] | null; fate: SeaFate; q?: { outer: Pt[]; holes: Pt[][] } }
export interface SeaCounts { refused: number; collapsed: number; islandsDropped: number; islandsSmall: number; bridges: number; outside: number; drawn: number;
  refused5: number; refused3: number; repairs: number; repairVertices: number;
  /** one per repaired piece: COUNTS only, for the production log line (no record key, no point) */
  repairCounts: RepairCounts[];
  /** one per repaired piece: the full line, with the record key and the island's point -- the harness's only, never logged */
  repairLog: string[] }
/** A repair's counts: the rings involved, the vertices restored, the rounds. */
export interface RepairCounts { rings: number; restored: number; rounds: number } // refused3: step 3's backstop refusals, outer and islands (refusedBuilt)
export const newSeaCounts = (): SeaCounts => ({ refused: 0, collapsed: 0, islandsDropped: 0, islandsSmall: 0, bridges: 0, outside: 0, drawn: 0, refused5: 0, refused3: 0, repairs: 0, repairVertices: 0, repairCounts: [], repairLog: [] });
/** A repair that cannot finish stops the build (the builder answers 500 with this code), never a partial package. "sea_repair_state" is
 *  the builder's own guard: the repair's recomputed state must be step 3's rings. */
/** `msg` is the full line (record key, island point): the harness's only. `counts` is what production may log. */
export class SeaRepairStop extends Error { constructor(readonly code: "sea_repair_failed" | "sea_repair_limit" | "sea_repair_state", msg: string, readonly counts: RepairCounts | null = null) { super(msg); } }
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
export function bridge(outer: Pt[], holes: Pt[][]): { ring: Pt[]; bridges: number; dropped: number } {
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
      if (!clear(h[i]!, ring[j]!, [ring, h, ...others])) continue;
      ring = [...ring.slice(0, j + 1), ...h.slice(i), ...h.slice(0, i), h[i]!, ring[j]!, ...ring.slice(j + 1)];
      made = true; bridges++; break;
    }
    if (!made) dropped++;
  }
  return { ring, bridges, dropped };
}
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
    const lockKeys = new Set<string>(); // the locked vertices' exact frame points, over every part of the record
    for (const p of ps) (p.lock ?? []).forEach((v, i) => { if (v) lockKeys.add(`${p.km[i]![0]},${p.km[i]![1]}`); });
    const lockOf = (r: Pt[]) => r.map(([x, y]) => lockKeys.has(`${x},${y}`));
    const simpL = (r: Pt[]): Pt[] => waterQ4Locked(r, f.epsM, f.rMm, lockOf(r));
    const oc = clipRing(o0.km, f.rKm);
    if (!oc.length) { n.outside++; out.push({ ...key, ring: null, fate: "outside" }); continue; }
    const hc = ps.slice(1).map((p) => clipRing(p.km, f.rKm)).filter((h) => h.length >= 3);
    const holesKm = hc.filter((h) => km2(h) >= ISLAND_MIN_KM2);
    n.islandsSmall += hc.length - holesKm.length;
    let oq = simpL(oc);
    if (oq === REFUSED) { n.refused++; n.refused3++; out.push({ ...key, ring: null, fate: "refused" }); continue; }
    if (oq.length < 3) { n.collapsed++; out.push({ ...key, ring: null, fate: "collapsed" }); continue; }
    const hq0 = holesKm.map((h) => ({ km: h, q: simpL(h) }));
    n.refused3 += hq0.filter((x) => x.q === REFUSED).length;
    const hs = hq0.filter((x) => x.q !== REFUSED && x.q.length >= 3);
    let hq = hs.map((x) => x.q);
    n.islandsDropped += holesKm.length - hq.length;
    if (hq.length && crossRingEdges([oq, ...hq]).size) { // step 4a: two different rings cross after step 3
      const st = [oc, ...hs.map((x) => x.km)].map((r) => simplifyRingTPLockedState(r, f.epsM, f.rMm, lockOf(r)));
      const id = `${key.file}/${key.record}`, q3 = [oq, ...hq];
      if (st.some((s, i) => !sameRing(s.q, q3[i]!))) throw new SeaRepairStop("sea_repair_state", `${id}: step 4a's state is not step 3's rings`);
      const R = repairRings(st, SEA_REPAIR_LIMIT);
      const line = `sea_repair ${id} rings ${R.crossed.join(",")} restored ${R.restored} rounds ${R.rounds} island ${R.crossed.filter((i) => i > 0).map((i) => pointOf(hs[i - 1]!.km, f)).join(" ")}`;
      const counts: RepairCounts = { rings: R.crossed.length, restored: R.restored, rounds: R.rounds };
      if (R.stop) throw new SeaRepairStop(R.stop, line, counts);
      oq = R.q[0]!; hq = R.q.slice(1);
      n.repairs++; n.repairVertices += R.restored; n.repairCounts.push(counts); n.repairLog.push(line);
    }
    const qkeep = { outer: oq, holes: hq };
    const b = bridge(oq, hq); n.bridges += b.bridges; n.islandsDropped += b.dropped;
    const ring = b.ring;
    if (properPairs(ring).length) { n.refused++; n.refused5++; out.push({ ...key, ring: null, fate: "refused" }); continue; }
    n.drawn++; out.push({ ...key, ring, fate: "drawn", q: qkeep });
  }
  return out;
}
