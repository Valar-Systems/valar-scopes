// MD3a-d (docs/basemap-route.md A1.7): scripts/basemap-check.ts against a package built at HEAD over a store ingested
// from MI's synthetic shapefiles, through the ingest's own path (shapefile reader, featuresFrom, assignCells,
// writeManifest, regionsFor). Synthetic and labelled so: an example.invalid host, county 99001, state "ZZ", and the
// location-of-nothing origin MW uses. Exit 0 when all four rows are as frozen, 1 otherwise, 2 BLIND.
//
//   npx tsx scripts/basemap-md3.ts
import { execFileSync, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "../src/basemap/build";
import { crc32 } from "../src/basemap/format";
import { CURRENT_KEY, type Box, storeKeys } from "../src/basemap/store";
import { writeDbf, writeShp, zipShapefile } from "../test/basemap-shp";
import { encodeCoverage } from "./basemap/coverage";
import { PREDICATES, assignCells, bboxOf, encodeCell, encodeFeature, featuresFrom, reachOf, storeIdOf, writeManifest } from "./basemap/ingest-lib";
import { regionsFor } from "./basemap/regions";
import { readShapefile, shapefileFromZip } from "./basemap/shapefile";

const PROXY = join(dirname(fileURLToPath(import.meta.url)), "..");
const git = (...a: string[]) => execFileSync("git", a, { cwd: PROXY, encoding: "utf8" }).trim();
if (git("status", "--porcelain", "--", "src/basemap")) { console.log("MD3: BLIND -- src/basemap has uncommitted changes, so a package built now is not HEAD's code"); process.exit(2); }
const HEAD = git("rev-parse", "HEAD");
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const T = mkdtempSync(join(tmpdir(), "bm-md3-"));
const put = (dir: string, key: string, b: Uint8Array) => { const f = join(dir, ...key.split("/")); mkdirSync(dirname(f), { recursive: true }); writeFileSync(f, b); };

// the sources: two synthetic shapefiles, cached as the ingest caches them
const W = "https://example.invalid/AREAWATER/tl_2024_99001_areawater.zip", R = "https://example.invalid/PRISECROADS/tl_2024_99_prisecroads.zip";
const ring = (x: number, y: number, s: number) => [x, y, x, y + s, x + s, y + s, x + s, y, x, y]; // clockwise in (lon, lat)
const wzip = zipShapefile("w", writeShp([{ type: 5, parts: [ring(0.38, 0.36, 0.02)] }, { type: 5, parts: [ring(0.30, 0.40, 0.03)] }]),
  writeDbf([{ name: "AWATER", len: 14 }], [{ AWATER: "2000000" }, { AWATER: "3000000" }]));
const rzip = zipShapefile("r", writeShp([{ type: 3, parts: [[0.50, 0.35, 0.70, 0.36]] }, { type: 3, parts: [[0.55, 0.40, 0.65, 0.42, 0.75, 0.41]] }]),
  writeDbf([{ name: "MTFCC", len: 5 }, { name: "FULLNAME", len: 20 }], [{ MTFCC: "S1100", FULLNAME: "Synthetic Hwy" }, { MTFCC: "S1200", FULLNAME: "Synthetic Rd" }]));
for (const [u, z] of [[W, wzip], [R, rzip]] as const) put(join(T, "sources", "files", "example.invalid"), new URL(u).pathname.slice(1), z);

// the ingest's path: manifest, features, cells, regions, coverage, index, current
function storeFrom(files: Array<{ layer: string; url: string; sha256: string; bytes: number }>, dir: string) {
  const manifestText = writeManifest({ builder: HEAD, predicates: [...PREDICATES], files: files.map((f) => ({ ...f, vintage: "2024" })) });
  const order = files.map((f) => f.url).sort();
  const feats = [...featuresFrom("water", order.indexOf(W), readShapefile(...Object.values(shapefileFromZip(wzip)) as [Uint8Array, Uint8Array])),
    ...featuresFrom("roads", order.indexOf(R), readShapefile(...Object.values(shapefileFromZip(rzip)) as [Uint8Array, Uint8Array]))];
  const cells = assignCells(feats.map((f) => ({ bytes: encodeFeature(f), box: bboxOf(f.xy) })), 24 * 1024 * 1024);
  const box: Box = [0.0, 0.0, 1.0, 1.0];
  const regions = regionsFor({ "99001": box }, { "99": { box, usps: "ZZ" } }, files);
  const rows = new Map<number, Array<[number, number]>>([[37, [[50, 70]]]]);
  return storeIdOf(manifestText).then((id) => {
    const k = storeKeys(id);
    put(dir, k.manifest, new TextEncoder().encode(manifestText));
    for (const c of cells) put(dir, k.cell(c.key), encodeCell(c.features));
    put(dir, k.index, new TextEncoder().encode(JSON.stringify({ cells: cells.map((c) => ({ key: c.key, bytes: encodeCell(c.features).length, count: c.features.length, reach: reachOf(c) })), regions }) + "\n"));
    put(dir, k.coverage, encodeCoverage(rows));
    put(dir, CURRENT_KEY, new TextEncoder().encode(JSON.stringify({ storeId: id })));
    return id;
  });
}
const files = [{ layer: "water", url: W, sha256: sha(wzip), bytes: wzip.length }, { layer: "roads", url: R, sha256: sha(rzip), bytes: rzip.length }];
await storeFrom(files, join(T, "store"));
const reader = { get: async (k: string) => { try { return new Uint8Array(readFileSync(join(T, "store", ...k.split("/")))); } catch { return null; } } };
const built = await build(reader, { lat0Micro: 370_000, lon0Micro: 590_000, rKm: 97, epsMm: 100_584, mask: 5, levels: [] }, { commit: HEAD });
if (!built.ok) { console.log(`MD3: BLIND -- the fixture package did not build (${built.status})`); process.exit(2); }
writeFileSync(join(T, "a.bmpk"), built.bytes);

/** A copy of the package with `edit` applied and its CRC recomputed. */
function variant(name: string, edit: (b: Uint8Array) => void) {
  const b = built.ok ? built.bytes.slice() : new Uint8Array(0);
  edit(b);
  const dv = new DataView(b.buffer); dv.setUint32(28, 0, true); dv.setUint32(28, crc32(b), true);
  writeFileSync(join(T, name), b);
}
const dvA = new DataView(built.bytes.buffer, built.bytes.byteOffset, built.bytes.byteLength);
variant("b.bmpk", (b) => { const off = dvA.getUint32(32 + 8, true); b[off + 2] = b[off + 2]! ^ 0x01; }); // one byte inside WATER's body
variant("c.bmpk", (b) => { // the builder commit replaced by one that is not in the repository (the same length)
  const enc = new TextEncoder(), from = enc.encode(`blipscope-basemap ${HEAD}`), to = enc.encode(`blipscope-basemap ${"0".repeat(40)}`);
  for (let i = 0; i + from.length <= b.length; i++) if (from.every((v, k) => b[i + k] === v)) { b.set(to, i); return; }
  throw new Error("MD3c: the builder line was not found (BLIND)");
});
// MD3d: the store re-sealed over a manifest whose water file's sha256 is not the one the package pins
await storeFrom([{ ...files[0]!, sha256: "f".repeat(64) }, files[1]!], join(T, "store-d"));

const check = (pkg: string, store: string, withSources: boolean) => spawnSync("npx", ["tsx", "scripts/basemap-check.ts", join(T, pkg), "--store", join(T, store), ...(withSources ? ["--sources", join(T, "sources")] : [])], { cwd: PROXY, shell: true, encoding: "utf8" });
const rows: Array<[string, string, string, boolean, number]> = [
  ["MD3a", "a.bmpk", "store", true, 0], ["MD3b", "b.bmpk", "store", true, 1], ["MD3c", "c.bmpk", "store", true, 2], ["MD3d", "a.bmpk", "store-d", false, 1],
];
let off = 0;
for (const [row, pkg, store, src, want] of rows) {
  const r = check(pkg, store, src), line = (r.stdout ?? "").split("\n").find((l) => l.startsWith("check:")) ?? "(no verdict line)";
  const ok = r.status === want;
  if (!ok) off++;
  console.log(`${row}: exit ${r.status} (frozen ${want}) ${ok ? "as frozen" : "OFF-PREDICTION"} -- ${line}`);
}
if (process.argv.includes("--keep")) console.log(`kept: ${T}`); else rmSync(T, { recursive: true, force: true });
process.exit(off ? 1 : 0);
