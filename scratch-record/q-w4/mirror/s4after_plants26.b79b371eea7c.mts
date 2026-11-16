// SCRATCH (map-server): step8_26.pred Amendment 5d, seam25.pred's two planted controls, each on a COPY of an AFTER package. READ-ONLY
// on the originals and the cache; writes only s4-pkg-after/44007-plant.bmpk and s4-pkg-after/17005-plant.bmpk (absent before).
//   S4-3's (seam25.pred :40-43): every WATER ring of 44007-after.bmpk whose centroid lies inside 44007's 2024 county polygon is shifted
//     500 m east.
//   S4-3b's (seam25.pred :70-74): every RIVER polyline of 17005-after.bmpk with a vertex inside 17005's 2024 county polygon is shifted
//     500 m along L's direction at the centre (L = mirror/s4-lines/17005.txt; the segment whose midpoint is nearest the package's
//     centre).
// Coordinates are the package's own: frame.ts's makeFrame and toFrameKm, at quantise's scale (q = km * 1e6 / radiusMm * 32767). The
// polygon is GEOID's from tl_2024_us_county.zip in the cache (the dry-clone's shapefile reader; the file's sha256 checked against tree
// B's manifest), projected the same way. 500 m is round(500000 * 32767 / radiusMm) q, as s4_plant.mts; shifted values are clamped to
// +-32767. Centroid: the shoelace centroid, or the vertex mean for a zero-area ring. Inside: even-odd over all the polygon's parts.
// Proofs, printed before each write: encode(decode(original)) equals the original bytes; the copy's sha256 differs; decoding both,
// exactly the shifted rings or polylines differ, and every other section is equal. Prints counts only, never a coordinate.
//   (from dry-clone/proxy) node --import tsx ../../mirror/s4after_plants26.mts
import { createHash } from "node:crypto";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { decode, encode } from "../dry-clone/proxy/src/basemap/format.ts";
import { makeFrame, toFrameKm, QMAX } from "../dry-clone/proxy/src/basemap/frame.ts";
import { readShapefile, shapefileFromZip } from "../dry-clone/proxy/scripts/basemap/shapefile.ts";

const S = fileURLToPath(new URL("../", import.meta.url));
const C = join(S, "..", "bm-cache-qw4");
const OUT = join(S, "s4-pkg-after");
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
type P = [number, number];

const man = JSON.parse(readFileSync(join(C, "dry-run", "store", "manifest.json"), "utf8")).files as Array<{ url: string; layer: string; sha256: string }>;
const cf = man.find((f) => f.layer === "counties")!;
const u = new URL(cf.url);
const zip = new Uint8Array(readFileSync(join(C, "files", u.hostname, ...u.pathname.split("/").filter(Boolean))));
if (sha(zip) !== cf.sha256) { console.log("INSTRUMENT: the county file does not match tree B's manifest"); process.exit(3); }
const { shp, dbf } = shapefileFromZip(zip);
const counties = readShapefile(shp, dbf);
const polyOf = (geoid: string) => { const r = counties.filter((x) => x.attrs["GEOID"] === geoid); if (r.length !== 1) throw new Error(`${r.length} records for ${geoid}`); return r[0]!.parts; };

function frameOf(pkg: any) {
  const f = makeFrame(pkg.lat0Micro, pkg.lon0Micro, pkg.radiusMm, pkg.toleranceMm);
  const q = (lon: number, lat: number): P => { const k = toFrameKm(f, lon, lat); return [k[0] * 1e6 / pkg.radiusMm * QMAX, k[1] * 1e6 / pkg.radiusMm * QMAX]; };
  return { f, q };
}
const ringsQ = (parts: Float64Array[], q: (lon: number, lat: number) => P) => parts.map((xy) => { const r: P[] = []; for (let i = 0; i < xy.length; i += 2) r.push(q(xy[i]!, xy[i + 1]!)); return r; });
function inside(pt: P, rings: P[][]) {
  let c = false;
  for (const r of rings) for (let i = 0, j = r.length - 1; i < r.length; j = i++) {
    const [xi, yi] = r[i]!, [xj, yj] = r[j]!;
    if ((yi > pt[1]) !== (yj > pt[1]) && pt[0] < ((xj - xi) * (pt[1] - yi)) / (yj - yi) + xi) c = !c;
  }
  return c;
}
function centroid(r: P[]): P {
  let a = 0, cx = 0, cy = 0;
  for (let i = 0; i < r.length; i++) { const [x0, y0] = r[i]!, [x1, y1] = r[(i + 1) % r.length]!; const cr = x0 * y1 - x1 * y0; a += cr; cx += (x0 + x1) * cr; cy += (y0 + y1) * cr; }
  if (Math.abs(a) < 1e-9) return [r.reduce((s, p) => s + p[0], 0) / r.length, r.reduce((s, p) => s + p[1], 0) / r.length];
  return [cx / (3 * a), cy / (3 * a)];
}
const clamp = (v: number) => Math.max(-QMAX, Math.min(QMAX, v));
// 5d.2, rule (c): a section is taken by type ONLY when that type occurs exactly once; repeated types are paired by index.
const one = (p: any, t: number) => { const s = p.sections.filter((x: any) => x.type === t); if (s.length !== 1) throw new Error(`type ${t} occurs ${s.length} times`); return s[0]; };
const plain = (v: unknown): unknown => (ArrayBuffer.isView(v) ? Array.from(v as unknown as ArrayLike<number>) : Array.isArray(v) ? v.map(plain)
  : v && typeof v === "object" ? Object.fromEntries(Object.entries(v as object).map(([k, x]) => [k, plain(x)])) : v);

function plant(name: string, geoid: string, kind: 1 | 2, dir: (fr: ReturnType<typeof frameOf>, pkg: any) => P) {
  const src = join(OUT, `${geoid}-after.bmpk`), dst = join(OUT, `${geoid}-plant.bmpk`);
  if (existsSync(dst)) { console.log(`${name}: refused: ${geoid}-plant.bmpk exists`); process.exit(2); }
  const orig = new Uint8Array(readFileSync(src));
  const pkg = decode(orig) as any;
  const round = sha(encode(decode(orig) as any)) === sha(orig);
  const fr = frameOf(pkg), poly = ringsQ(polyOf(geoid), fr.q);
  const dq = Math.round(500000 * QMAX / pkg.radiusMm), [ux, uy] = dir(fr, pkg);
  const sx = Math.round(dq * ux), sy = Math.round(dq * uy);
  const sec = one(pkg, kind);
  let moved = 0;
  if (kind === 1) {
    sec.rings = sec.rings.map((r: P[]) => { if (!inside(centroid(r), poly)) return r; moved++; return r.map(([x, y]) => [clamp(x + sx), clamp(y + sy)] as P); });
  } else {
    sec.lines = sec.lines.map((l: { name: number; pts: P[] }) => { if (!l.pts.some((p) => inside(p, poly))) return l; moved++; return { ...l, pts: l.pts.map(([x, y]) => [clamp(x + sx), clamp(y + sy)] as P) }; });
  }
  const out = encode(pkg);
  const after = decode(out) as any, before = decode(orig) as any;
  const typesEqual = JSON.stringify(before.sections.map((s: any) => s.type)) === JSON.stringify(after.sections.map((s: any) => s.type));
  const others = typesEqual && before.sections.every((s: any, i: number) => s.type === kind || JSON.stringify(plain(s)) === JSON.stringify(plain(after.sections[i])));
  const a = (one(before, kind)[kind === 1 ? "rings" : "lines"]) as any[], b = (one(after, kind)[kind === 1 ? "rings" : "lines"]) as any[];
  const differing = a.filter((x, i) => JSON.stringify(plain(x)) !== JSON.stringify(plain(b[i]))).length;
  const ok = round && moved > 0 && sha(out) !== sha(orig) && others && differing === moved && a.length === b.length;
  console.log(`${name}: round trip ${round}; ${kind === 1 ? "water rings" : "river polylines"} shifted ${moved} of ${a.length}; shift ${dq} q (500 m); `
    + `copy sha ${sha(out).slice(0, 12)} differs ${sha(out) !== sha(orig)}; differing ${differing}; other sections equal ${others}; ${ok ? "APPLIED" : "NOT APPLIED"}`);
  if (!ok) process.exit(1);
  writeFileSync(dst, out);
}

plant("S4-3 CONTROL (44007 water, 500 m east)", "44007", 1, () => [1, 0]);
plant("S4-3b CONTROL (17005 rivers, 500 m along L)", "17005", 2, (fr, pkg) => {
  const segs = readFileSync(join(S, "mirror", "s4-lines", "17005.txt"), "utf8").trim().split("\n").map((l) => l.trim().split(/\s+/).map(Number));
  const c0 = fr.q(pkg.lon0Micro / 1e6, pkg.lat0Micro / 1e6);
  let best = Infinity, d: P = [1, 0];
  for (const [la1, lo1, la2, lo2] of segs) {
    const p1 = fr.q(lo1!, la1!), p2 = fr.q(lo2!, la2!), m: P = [(p1[0] + p2[0]) / 2 - c0[0], (p1[1] + p2[1]) / 2 - c0[1]], dd = m[0] * m[0] + m[1] * m[1];
    const len = Math.hypot(p2[0] - p1[0], p2[1] - p1[1]);
    if (dd < best && len > 0) { best = dd; d = [(p2[0] - p1[0]) / len, (p2[1] - p1[1]) / len]; }
  }
  return d;
});
