// The sea ingest (docs/basemap-dense-code.md, part 2 and PARITY step 2): the pinned zips and the store's own coverage to the per-cell SEA
// PIECES the store keeps as kind 6. A port of the PARITY-proven sea-ingest.mts (45500edf4529), sections 1-3, statement for statement, with
// its run-time options removed: the code PR's cells are a function of the manifest's files only (QW4-ID (b)), so nothing here reads an
// environment variable or a flag. The extent comes from the coverage the same ingest computes (I3, confirmed after 04:2xZ), never a file.
//   1. the extent: every 1-degree cell within 250 km of the coverage;
//   2. the sources: the ocean's polygons; the lakes of at least 1.5 km2 not wholly inside the US (design (a), uscontain.ts);
//   3. the cut (rectclip.ts) per extent cell, and the island rule: a hole is kept when its area is at least 1.5 km2.
import { type Pt, type Rect, clipPolygon, holeKm2, inRing } from "./rectclip";
import { container, whollyInside } from "./uscontain";

const LIM = 250, MIN_LAKE = 1.5, MIN_ISLAND = 1.5, R = 6371.0088;
const rad = (d: number) => (d * Math.PI) / 180;

// ---- 1. the extent (sea-extent.py's definition, as sea-dryrun.py section 1)
/** The coverage's row segments, [lat, lon0, lon1] in degrees, from its BMCV bytes. */
export function coverageSegments(cov: Uint8Array): Array<[number, number, number]> {
  const dv = new DataView(cov.buffer, cov.byteOffset, cov.byteLength);
  if (String.fromCharCode(...cov.subarray(0, 4)) !== "BMCV") throw new Error("not a coverage.bin");
  const nRows = dv.getUint32(8, true); const segs: Array<[number, number, number]> = []; let o = 12;
  for (let r = 0; r < nRows; r++) { const h = dv.getInt32(o, true), n = dv.getUint32(o + 4, true); o += 8; for (let k = 0; k < n; k++) segs.push([h / 100.0, dv.getInt32(o + 8 * k, true) / 100.0, dv.getInt32(o + 8 * k + 4, true) / 100.0]); o += 8 * n; }
  return segs;
}
const hav = (la1: number, lo1: number, la2: number, lo2: number) => { const p1 = rad(la1), p2 = rad(la2), dl = rad(lo2 - lo1); const x = Math.sin((p2 - p1) / 2) ** 2 + Math.cos(p1) * Math.cos(p2) * Math.sin(dl / 2) ** 2; return 2 * R * Math.asin(Math.min(1.0, Math.sqrt(x))); };
const pymod = (a: number, m: number) => ((a % m) + m) % m;
const wrap = (lon: number) => pymod(lon + 180.0, 360.0) - 180.0;
const lonGap = (a: number, b: number, c0: number) => { let best = 360.0; for (const s of [-360.0, 0.0, 360.0]) { const lo = c0 + s, hi = c0 + 1 + s; best = Math.min(best, b >= lo && a <= hi ? 0.0 : Math.min(Math.abs(lo - b), Math.abs(a - hi))); } return best; };
/** The extent's cells, "lat0/lon0" (1-degree, south-west corner): every cell within LIM km of a coverage segment. */
export function seaExtent(segs: Array<[number, number, number]>): Set<string> {
  const cells = new Set<string>();
  for (const [lat, a, b] of segs) {
    const dlat = LIM / 111.0 + 0.01;
    for (let la0 = Math.floor(lat - dlat); la0 <= Math.floor(lat + dlat); la0++) {
      const latc = Math.min(Math.max(lat, la0), la0 + 1), cosm = Math.max(Math.cos(rad(Math.max(Math.abs(lat), Math.abs(latc)) + 0.5)), 0.01);
      const dlon = LIM / (111.0 * cosm) + 0.01;
      for (let lo0 = Math.floor(a - dlon); lo0 <= Math.floor(b + dlon); lo0++) {
        const key = `${la0}/${Math.trunc(wrap(lo0))}`;
        if (cells.has(key)) continue;
        if (hav(lat, 0.0, latc, lonGap(a, b, lo0)) <= LIM) cells.add(key);
      }
    }
  }
  return cells;
}

// ---- 2. the sources
export interface Poly { outer: Pt[]; holes: Pt[][] }
export const ringsOf = (parts: Float64Array[]): Pt[][] => parts.map((xy) => { const r: Pt[] = []; for (let i = 0; i < xy.length; i += 2) r.push([xy[i]!, xy[i + 1]!]); return r; });
const sa = (r: Pt[]) => { let s = 0; for (let k = 0; k < r.length - 1; k++) s += r[k]![0] * r[k + 1]![1] - r[k + 1]![0] * r[k]![1]; return s; }; // the dry run's sa, on the closed ring
/** Outers (clockwise) and holes; each hole to the outer that holds its first vertex. `notInOne` counts holes held by no outer or by two. */
export function polygonize(rings: Pt[][], stats?: { notInOne: number }): Poly[] {
  const outers = rings.filter((r) => sa(r) < 0), holes = rings.filter((r) => sa(r) >= 0);
  const out: Poly[] = outers.map((r) => ({ outer: r, holes: [] }));
  for (const h of holes) { const hits = out.filter((p) => inRing(h[0]!, p.outer)); if (hits.length !== 1 && stats) stats.notInOne++; if (hits[0]) hits[0].holes.push(h); }
  return out;
}
const ringArea = (r: Pt[]) => Math.abs(sa(r)) / 2;
const ringCy = (r: Pt[]) => { let a2 = 0, cy = 0; for (let k = 0; k < r.length - 1; k++) { const cr = r[k]![0] * r[k + 1]![1] - r[k + 1]![0] * r[k]![1]; a2 += cr; cy += (r[k]![1] + r[k + 1]![1]) * cr; } return cy / (3 * a2); };
/** shapely's km2 on the union: area, and the area-weighted centroid latitude, holes subtracted. */
export function km2(ps: Poly[]): number {
  let A = 0, M = 0;
  for (const p of ps) { const ao = ringArea(p.outer); A += ao; M += ao * ringCy(p.outer); for (const h of p.holes) { const ah = ringArea(h); A -= ah; M -= ah * ringCy(h); } }
  return A * 111.0 * 111.0 * Math.max(Math.cos(rad(M / A)), 0.01);
}
export interface ShpRecord { index: number; parts: Float64Array[]; attrs: Record<string, string> }
export interface SeaSources { ocean: Poly[]; lakes: Array<{ name: string; index: number; ps: Poly[] }>; stats: { lakesRead: number; inside: number; small: number; notInOne: number } }
/** The ocean's polygons, and the lakes kept: at least 1.5 km2, and not wholly inside the US (its admin-0 record's parts). */
export function seaSources(oceanRecs: ShpRecord[], lakeRecs: ShpRecord[], adminRecs: ShpRecord[]): SeaSources {
  const st = { notInOne: 0 };
  const ocean = polygonize(ringsOf(oceanRecs[0]!.parts), st);
  const usRec = adminRecs.find((r) => r.attrs["ADM0_A3"] === "USA");
  if (!usRec) throw new Error("no USA record");
  const USC = container(polygonize(ringsOf(usRec.parts), st).map((p) => p.outer)); // the US has no holes (read from the reference)
  const lakes: SeaSources["lakes"] = []; let inside = 0, small = 0;
  for (const rec of lakeRecs) {
    const ps = polygonize(ringsOf(rec.parts), st); if (!ps.length) continue;
    if (km2(ps) < MIN_LAKE) { small++; continue; }
    if (whollyInside(USC, ps.map((p) => p.outer))) { inside++; continue; }
    lakes.push({ name: rec.attrs["name"] || rec.attrs["name_en"] || "lake", index: rec.index, ps });
  }
  return { ocean, lakes, stats: { lakesRead: lakeRecs.length, inside, small, notInOne: st.notInOne } };
}

// ---- 3. the cut (step 1's clip) and the island rule
export interface SeaPiece { src: string; outer: Pt[]; holes: Pt[][] }
/** Per extent cell (sorted), every source polygon's pieces in that cell, closed rings; holes under 1.5 km2 dropped. */
export function seaPieces(cells: Set<string>, s: SeaSources): { byCell: Record<string, SeaPiece[]>; stats: { pieces: number; kept: number; dropped: number; verts: number } } {
  const srcs = [...s.ocean.map((p) => ({ src: "ocean", p })), ...s.lakes.flatMap((l) => l.ps.map((p) => ({ src: l.name, p })))];
  const bb = srcs.map(({ p }) => { let a = Infinity, b = Infinity, c = -Infinity, e = -Infinity; for (const [x, y] of p.outer) { if (x < a) a = x; if (x > c) c = x; if (y < b) b = y; if (y > e) e = y; } return [a, b, c, e]; });
  const out: Record<string, SeaPiece[]> = {}; let pieces = 0, kept = 0, dropped = 0, verts = 0;
  for (const key of [...cells].sort()) {
    const [la0, lo0] = key.split("/").map(Number) as [number, number]; const Rr: Rect = { x0: lo0, y0: la0, x1: lo0 + 1, y1: la0 + 1 };
    srcs.forEach(({ src, p }, i) => {
      const [a, b, c, e] = bb[i]!; if (c! <= Rr.x0 || a! >= Rr.x1 || e! <= Rr.y0 || b! >= Rr.y1) return;
      for (const pc of clipPolygon(p.outer, p.holes, Rr)) {
        const hs = pc.holes.filter((h) => holeKm2(h) >= MIN_ISLAND); kept += hs.length; dropped += pc.holes.length - hs.length;
        const close = (r: Pt[]): Pt[] => [...r, r[0]!];
        (out[key] ??= []).push({ src, outer: close(pc.outer), holes: hs.map(close) }); pieces++; verts += pc.outer.length + 1 + hs.reduce((n, h) => n + h.length + 1, 0);
      }
    });
  }
  return { byCell: out, stats: { pieces, kept, dropped, verts } };
}
