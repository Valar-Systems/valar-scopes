// SCRATCH (map-server): step8_26.pred Amendment 5d.6, S4-3b's CUT-AND-OFFSET control, on a COPY of 19123-after.bmpk. READ-ONLY on the
// original and the cache; writes only s4-pkg-after/19123-cut.bmpk (absent before).
// The registered whole-polyline shift could not make a break: 4 of 19123's 7 river polylines with a vertex inside the county CROSS the
// county line as single joined lines. So each CROSSING polyline is CUT at its intersection X with 19123's 2024 county polygon, and ONLY
// its inside pieces (X included) are offset 500 m ALONG L. The outside piece then ends exactly at the line, and the inside piece starts
// 500 m along it. Wholly-inside and wholly-outside polylines are unchanged.
// Coordinates are the package's own, as s4after_plants26.mts: frame.ts's makeFrame and toFrameKm at quantise's scale. The polygon comes
// from tree B's sha-checked tl_2024_us_county.zip. L's direction is that of the segment of mirror/s4-lines/19123.txt whose midpoint is
// nearest the centre. 500 m is round(500000 * 32767 / radiusMm) q. X is the crossing segment's first intersection with the polygon's
// edges, rounded to whole q. Every value is clamped to +-32767. A cut line keeps its name index.
// Proofs, printed before the write:
//   - encode(decode(original)) equals the original;
//   - the copy's sha differs;
//   - the river section's line count rises by EXACTLY the number of cuts;
//   - the non-crossing lines, in order, are unchanged;
//   - the type sequence is equal, and every other section is equal BY INDEX.
// Sections are taken by one() (rule (c)). Prints counts only.
//   (from dry-clone/proxy) node --import tsx ../../mirror/s4after_cut26.mts
import { createHash } from "node:crypto";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { decode, encode } from "../dry-clone/proxy/src/basemap/format.ts";
import { makeFrame, toFrameKm, QMAX } from "../dry-clone/proxy/src/basemap/frame.ts";
import { readShapefile, shapefileFromZip } from "../dry-clone/proxy/scripts/basemap/shapefile.ts";

const S = fileURLToPath(new URL("../", import.meta.url));
const C = join(S, "..", "bm-cache-qw4");
const SRC = join(S, "s4-pkg-after", "19123-after.bmpk"), DST = join(S, "s4-pkg-after", "19123-cut.bmpk");
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
type P = [number, number];
const one = (p: any, t: number) => { const s = p.sections.filter((x: any) => x.type === t); if (s.length !== 1) throw new Error(`type ${t} occurs ${s.length} times`); return s[0]; };
const plain = (v: unknown): unknown => (ArrayBuffer.isView(v) ? Array.from(v as unknown as ArrayLike<number>) : Array.isArray(v) ? v.map(plain)
  : v && typeof v === "object" ? Object.fromEntries(Object.entries(v as object).map(([k, x]) => [k, plain(x)])) : v);
const clamp = (v: number) => Math.max(-QMAX, Math.min(QMAX, Math.round(v)));
if (existsSync(DST)) { console.log("refused: 19123-cut.bmpk exists"); process.exit(2); }

const man = JSON.parse(readFileSync(join(C, "dry-run", "store", "manifest.json"), "utf8")).files as Array<{ url: string; layer: string; sha256: string }>;
const cf = man.find((f) => f.layer === "counties")!; const u = new URL(cf.url);
const zip = new Uint8Array(readFileSync(join(C, "files", u.hostname, ...u.pathname.split("/").filter(Boolean))));
if (sha(zip) !== cf.sha256) { console.log("INSTRUMENT: the county file does not match tree B's manifest"); process.exit(3); }
const { shp, dbf } = shapefileFromZip(zip);
const rec = readShapefile(shp, dbf).filter((x) => x.attrs["GEOID"] === "19123");
if (rec.length !== 1) { console.log(`INSTRUMENT: ${rec.length} records for 19123`); process.exit(3); }

const orig = new Uint8Array(readFileSync(SRC));
const round = sha(encode(decode(orig) as any)) === sha(orig);
const pkg = decode(orig) as any;
const f = makeFrame(pkg.lat0Micro, pkg.lon0Micro, pkg.radiusMm, pkg.toleranceMm);
const q = (lon: number, lat: number): P => { const k = toFrameKm(f, lon, lat); return [k[0] * 1e6 / pkg.radiusMm * QMAX, k[1] * 1e6 / pkg.radiusMm * QMAX]; };
const poly: P[][] = rec[0]!.parts.map((xy) => { const r: P[] = []; for (let i = 0; i < xy.length; i += 2) r.push(q(xy[i]!, xy[i + 1]!)); return r; });
const inside = (pt: P) => { let c = false; for (const r of poly) for (let i = 0, j = r.length - 1; i < r.length; j = i++) { const [xi, yi] = r[i]!, [xj, yj] = r[j]!; if ((yi > pt[1]) !== (yj > pt[1]) && pt[0] < ((xj - xi) * (pt[1] - yi)) / (yj - yi) + xi) c = !c; } return c; };
// the first intersection of segment a->b with any polygon edge, as the parameter t in (0, 1]
function cross(a: P, b: P): P {
  let best = Infinity;
  for (const r of poly) for (let i = 0; i < r.length; i++) {
    const c = r[i]!, d = r[(i + 1) % r.length]!;
    const den = (b[0] - a[0]) * (d[1] - c[1]) - (b[1] - a[1]) * (d[0] - c[0]);
    if (den === 0) continue;
    const t = ((c[0] - a[0]) * (d[1] - c[1]) - (c[1] - a[1]) * (d[0] - c[0])) / den;
    const s = ((c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0])) / den;
    if (t >= 0 && t <= 1 && s >= 0 && s <= 1 && t < best) best = t;
  }
  if (!Number.isFinite(best)) throw new Error("a crossing segment meets no polygon edge");
  return [clamp(a[0] + (b[0] - a[0]) * best), clamp(a[1] + (b[1] - a[1]) * best)];
}
// L's direction at the centre
const segs = readFileSync(join(S, "mirror", "s4-lines", "19123.txt"), "utf8").trim().split("\n").map((l) => l.trim().split(/\s+/).map(Number));
const c0 = q(pkg.lon0Micro / 1e6, pkg.lat0Micro / 1e6);
let bestD = Infinity, dir: P = [1, 0];
for (const [la1, lo1, la2, lo2] of segs) {
  const p1 = q(lo1!, la1!), p2 = q(lo2!, la2!), m: P = [(p1[0] + p2[0]) / 2 - c0[0], (p1[1] + p2[1]) / 2 - c0[1]], dd = m[0] * m[0] + m[1] * m[1], len = Math.hypot(p2[0] - p1[0], p2[1] - p1[1]);
  if (dd < bestD && len > 0) { bestD = dd; dir = [(p2[0] - p1[0]) / len, (p2[1] - p1[1]) / len]; }
}
const dq = Math.round(500000 * QMAX / pkg.radiusMm), off: P = [Math.round(dq * dir[0]), Math.round(dq * dir[1])];
const riv = one(pkg, 2);
const before = riv.lines as Array<{ name: number; pts: P[] }>;
const out: Array<{ name: number; pts: P[] }> = [];
let crossing = 0, cuts = 0;
const keptOrder: string[] = [];
for (const l of before) {
  const ins = l.pts.map(inside);
  if (!(ins.some(Boolean) && ins.some((x) => !x))) { out.push(l); keptOrder.push(JSON.stringify(plain(l))); continue; }
  crossing++;
  let run: P[] = [l.pts[0]!], runIn = ins[0]!;
  const flush = () => { out.push({ name: l.name, pts: runIn ? run.map(([x, y]) => [clamp(x + off[0]), clamp(y + off[1])] as P) : run }); };
  for (let i = 1; i < l.pts.length; i++) {
    if (ins[i] !== runIn) { const X = cross(l.pts[i - 1]!, l.pts[i]!); run.push(X); flush(); cuts++; run = [X, l.pts[i]!]; runIn = ins[i]!; }
    else run.push(l.pts[i]!);
  }
  flush();
}
riv.lines = out;
const copy = encode(pkg);
const a = decode(orig) as any, b = decode(copy) as any;
const typesEqual = JSON.stringify(a.sections.map((s: any) => s.type)) === JSON.stringify(b.sections.map((s: any) => s.type));
const others = typesEqual && a.sections.every((s: any, i: number) => s.type === 2 || JSON.stringify(plain(s)) === JSON.stringify(plain(b.sections[i])));
const after = one(b, 2).lines as Array<{ name: number; pts: P[] }>;
const countOk = after.length === before.length + cuts;
// the non-crossing lines, in order: present unchanged (each kept line appears, in the same relative order, in the copy)
let k = 0; for (const l of after) { if (k < keptOrder.length && JSON.stringify(plain(l)) === keptOrder[k]) k++; }
const keptOk = k === keptOrder.length;
const ok = round && crossing > 0 && cuts > 0 && sha(copy) !== sha(orig) && countOk && keptOk && others;
console.log(`S4-3b CUT CONTROL (19123): round trip ${round}; crossing polylines ${crossing}; cuts ${cuts}; lines ${before.length} -> ${after.length} (+cuts ${countOk}); `
  + `non-crossing lines unchanged in order ${keptOk} (${keptOrder.length}); offset ${dq} q (500 m) along L; copy sha ${sha(copy).slice(0, 12)} differs ${sha(copy) !== sha(orig)}; `
  + `other sections equal by index ${others}; ${ok ? "APPLIED" : "NOT APPLIED"}`);
if (!ok) process.exit(1);
writeFileSync(DST, copy);
