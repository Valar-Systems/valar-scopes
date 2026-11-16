// SCRATCH (map-server): step8_26.pred Amendment 5d.5's ELIGIBILITY (the orchestrator's uniform rule). READ-ONLY; writes mirror/elig26.json
// (absent before). Counts only, never a coordinate.
// For each of the six AFTER packages and each rung (10, 25, 50 mi), the SEAM/BREAK finder's own window is rebuilt exactly as
// s4_render2.cpp builds it:
//   - L (mirror/s4-lines/<g>.txt) projected with RadarView at the package centre: px = C + (lon - homeLon) / rLon * C,
//     py = C - (lat - homeLat) / rLat * C, rLat = km / 111, rLon = km / (111 cos lat), C = 120, size 240;
//   - sampled every 0.5 px per segment (steps = ceil(len / 0.5)), skipping samples off the 240 px frame or off the disc (WxLayer.h's
//     OffDisc);
//   - the window = every pixel floor(sample + o * normal), o = -3 .. 3, the pixels SEAM and BREAK read.
// A package vertex lands at px = C + x_km * C / km, py = C + y_km * C / km (frame.ts's frame, x east and y south, at the
// package's own scale). That is the same linear map, because the view is centred on the package centre.
// ELIGIBLE: a WATER ring (S4-3) or RIVER polyline (S4-3b) with at least one vertex in the window. Sections are taken by one(), which
// throws unless the type occurs once (rule (c)).
// Prints, per centre and rung: the samples (the grader checks them against s4_render2's own SEAM samples), the eligible water rings
// and the eligible river polylines.
//   (from dry-clone/proxy) node --import tsx ../../mirror/elig26.mts
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { decode } from "../dry-clone/proxy/src/basemap/format.ts";
import { QMAX } from "../dry-clone/proxy/src/basemap/frame.ts";

const S = new URL("../", import.meta.url);
const OUT = new URL("mirror/elig26.json", S);
if (existsSync(OUT)) { console.log("refused: mirror/elig26.json exists"); process.exit(2); }
const G = ["38021", "39167", "44007", "56029", "17005", "19123"], MI = [10, 25, 50], kMi = 1.609344, SZ = 240, C = SZ / 2;
const offDisc = (x: number, y: number) => { const dx = 2 * x + 1 - SZ, dy = 2 * y + 1 - SZ; return dx * dx + dy * dy > SZ * SZ; };
const one = (p: any, t: number) => { const s = p.sections.filter((x: any) => x.type === t); if (s.length !== 1) throw new Error(`type ${t} occurs ${s.length} times`); return s[0]; };
const res: Record<string, Record<string, { samples: number; water: number; rivers: number; waterTotal: number; riversTotal: number }>> = {};
for (const g of G) {
  const pkg = decode(new Uint8Array(readFileSync(new URL(`s4-pkg-after/${g}-after.bmpk`, S)))) as any;
  const lat = pkg.lat0Micro / 1e6, lon = pkg.lon0Micro / 1e6;
  const segs = readFileSync(new URL(`mirror/s4-lines/${g}.txt`, S), "utf8").trim().split("\n").map((l) => l.trim().split(/\s+/).map(Number));
  const rings = one(pkg, 1).rings as number[][][], lines = (one(pkg, 2).lines as Array<{ pts: number[][] }>).map((l) => l.pts);
  res[g] = {};
  for (const mi of MI) {
    const km = mi * kMi, cosLat = Math.max(Math.cos((lat * Math.PI) / 180), 0.01);
    const rLat = Math.min(Math.max(km / 111.0, 0.001), 2.0), rLon = Math.min(Math.max(km / (111.0 * cosLat), 0.001), 2.0);
    const px = (la: number, lo: number): [number, number] => [C + ((lo - lon) / rLon) * C, C - ((la - lat) / rLat) * C];
    const win = new Set<number>();
    let samples = 0;
    for (const [la1, lo1, la2, lo2] of segs) {
      const [x1, y1] = px(la1!, lo1!), [x2, y2] = px(la2!, lo2!);
      const dx = x2 - x1, dy = y2 - y1, len = Math.sqrt(dx * dx + dy * dy);
      if (!(len > 0)) continue;
      const nx = -dy / len, ny = dx / len, steps = Math.ceil(len / 0.5);
      for (let k = 0; k <= steps; ++k) {
        const sx = x1 + (dx * k) / steps, sy = y1 + (dy * k) / steps, si = Math.floor(sx), sj = Math.floor(sy);
        if (si < 0 || sj < 0 || si >= SZ || sj >= SZ || offDisc(si, sj)) continue;
        ++samples;
        for (let o = -3; o <= 3; ++o) { const i = Math.floor(sx + o * nx), j = Math.floor(sy + o * ny); if (i >= 0 && j >= 0 && i < SZ && j < SZ) win.add(j * SZ + i); }
      }
    }
    const scale = (pkg.radiusMm / 1e6 / QMAX) * (C / km);
    const hit = (pts: number[][]) => pts.some(([qx, qy]) => { const i = Math.floor(C + qx! * scale), j = Math.floor(C + qy! * scale); return i >= 0 && j >= 0 && i < SZ && j < SZ && win.has(j * SZ + i); });
    res[g][String(mi)] = { samples, water: rings.filter(hit).length, rivers: lines.filter(hit).length, waterTotal: rings.length, riversTotal: lines.length };
    console.log(`${g} ${mi} mi: samples ${samples}; eligible water rings ${res[g][String(mi)]!.water} of ${rings.length}; eligible river polylines ${res[g][String(mi)]!.rivers} of ${lines.length}`);
  }
}
writeFileSync(OUT, JSON.stringify(res) + "\n");
const ctl = res["44007"]!["10"]!.water >= 1 && res["19123"]!["10"]!.rivers >= 1;
console.log(`CONTROL CENTRES: 44007 water at 10 mi ${res["44007"]!["10"]!.water}; 19123 rivers at 10 mi ${res["19123"]!["10"]!.rivers}; ${ctl ? "both >= 1" : "OUTSIDE: a control centre counts 0"}`);
process.exit(ctl ? 0 : 1);
