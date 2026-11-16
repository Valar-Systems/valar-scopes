// SCRATCH (map-server): step8_26.pred Amendment 5d: tbstore26.mts 5bc1ec970941 with ONLY its homes (the six S4 centres) and its commit stamp (#696's head) changed.
// SCRATCH (map-server): step8_26.pred Amendment 5c, THE STORE ADAPTER, a new instrument. READ-ONLY on every store; it writes only the
// packages and reports under <out>, which must not exist.
// It builds the 16 packages with the dry-clone's builder (e81e794908a6, the code PR's head) over a store held in memory as a map
// {key -> bytes}, keyed exactly as R2 keys them (store.ts storeKeys and CURRENT_KEY). get(key) returns the bytes, or null for a key not
// in the map, as an R2 miss does. ONE store type and ONE build loop serve both sources, so the control tests the code that serves tree B:
//   dir   <store dir>   an R2-mirror directory (sea-obj): every file under it keyed bm/v1/<storeId>/<its path>, and its `current`
//                       file at CURRENT_KEY; storeId read from that `current`.
//   cache <cache dir>   tree B: d1.ts's own assembleStore(cache, cachePath) objects, which are what D1 would upload, plus its `current` at
//                       CURRENT_KEY. cachePath is ingest-basemap.ts:68-71, transcribed (importing that file runs its main()), and
//                       assembleStore checks every tile's sha256 through it, so a wrong transcription throws.
// Each build is cp-run.mts's exactly: the same 16 homes, the client's query (r=162, eps=201168, the five layers, the three levels),
// commit 40 zeros. Sequential, one process.
// Per home: writes <out>/<HOME>-<tag>.bmpk and <out>/<HOME>-<tag>.report.json (the builder's report object), and prints one line
//   "RESULT " + JSON: cp-run.mts's fields, plus gets and misses (this store's own count of keys asked for and absent).
// Last line: "SUMMARY " + JSON {source, storeId, objects, homes, ok, shas}. Counts and 12-hex shas only.
//   (from dry-clone/proxy) node --import tsx ../../mirror/tbstore26.mts <dir|cache> <path> <out dir> <tag>
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join, relative, sep } from "node:path";
import { build } from "../dry-clone/proxy/src/basemap/build.ts";
import { decode } from "../dry-clone/proxy/src/basemap/format.ts";
import { nonTerrainBytes } from "../dry-clone/proxy/src/basemap/budget.ts";
import { parseQuery } from "../dry-clone/proxy/src/basemap/query.ts";
import { CURRENT_KEY } from "../dry-clone/proxy/src/basemap/store.ts";
import { assembleStore } from "../dry-clone/proxy/scripts/basemap/d1.ts";

const [mode, src, out, tag] = process.argv.slice(2) as [string, string, string, string];
if (!["dir", "cache"].includes(mode) || !src || !out || !/^[a-z0-9-]+$/.test(tag ?? "")) { console.log(`RESULT ${JSON.stringify({ error: "usage" })}`); process.exit(2); }
if (existsSync(out)) { console.log(`RESULT ${JSON.stringify({ error: "out exists" })}`); process.exit(2); }
const HOMES: Record<string, [string, string]> = {   // 5d: seam25.pred S4-1's six centres, as s4b_pkg.mts fe1ae0ffeedb writes them
  "38021": ["45.94", "-98.17"], "39167": ["39.59", "-81.68"], "44007": ["41.76", "-71.38"],
  "56029": ["44.74", "-108.59"], "17005": ["38.74", "-89.50"], "19123": ["41.27", "-92.41"],
};

const map = new Map<string, Uint8Array>();
let storeId: string;
if (mode === "dir") {
  const cur = readFileSync(join(src, "current"));
  storeId = (JSON.parse(cur.toString("utf8")) as { storeId: string }).storeId;
  map.set(CURRENT_KEY, new Uint8Array(cur));
  const walk = (d: string): string[] => readdirSync(d).flatMap((n) => { const p = join(d, n); return statSync(p).isDirectory() ? walk(p) : [p]; });
  for (const p of walk(src)) {
    const rel = relative(src, p).split(sep).join("/");
    if (rel === "current") continue;
    map.set(`bm/v1/${storeId}/${rel}`, new Uint8Array(readFileSync(p)));
  }
} else {
  const cachePath = (cache: string, url: string) => { const u = new URL(url); return join(cache, "files", u.hostname, ...u.pathname.split("/").filter(Boolean)); };
  const a = assembleStore(src, cachePath);
  storeId = a.storeId;
  for (const o of a.objects) { if (map.has(o.key)) throw new Error("assembleStore gave one key twice"); map.set(o.key, o.bytes); }
  map.set(CURRENT_KEY, a.current);
}
let gets = 0, misses = 0;
const store = { async get(key: string) { gets++; const b = map.get(key); if (!b) misses++; return b ?? null; } };

mkdirSync(out, { recursive: true });
const shas: Record<string, string> = {};
let ok = 0;
for (const [home, [lat, lon]] of Object.entries(HOMES)) {
  gets = 0; misses = 0;
  const p = parseQuery(new URL(`https://x/api/v1/blipscope/basemap?lat=${lat}&lon=${lon}&r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242`));
  if (!p.ok) { console.log(`RESULT ${JSON.stringify({ home, error: p.code })}`); continue; }
  const r = await build(store, { lat0Micro: p.q.lat0Micro, lon0Micro: p.q.lon0Micro, rKm: p.q.rKm, epsMm: p.q.epsMm, mask: p.q.mask, levels: p.q.levels }, { commit: "e81e794908a63d05fe4b9469bab6a928ff595a48" });
  if (!r.ok) { console.log(`RESULT ${JSON.stringify({ home, ok: false, status: r.status, code: r.code, gets, misses })}`); continue; }
  writeFileSync(join(out, `${home}-${tag}.bmpk`), r.bytes);
  writeFileSync(join(out, `${home}-${tag}.report.json`), JSON.stringify(r.report) + "\n");
  const pkg = decode(r.bytes) as any, s = r.report.sea!;
  const sha = createHash("sha256").update(r.bytes).digest("hex").slice(0, 12);
  shas[home] = sha; ok++;
  console.log(`RESULT ${JSON.stringify({ home, ok: true, sha, rung: r.ladder.rung, radiusKm: pkg.radiusMm / 1e6, ntb: nonTerrainBytes(pkg.sections), radii: r.report.radii,
    refusedBuilt: r.report.refusedBuilt, refused: r.report.refused, gets, misses,
    sea: { drawn: s.drawn, refused: s.refused, collapsed: s.collapsed, outside: s.outside, dropped: s.dropped, lost: s.lost, islandsDropped: s.islandsDropped,
      islandsSmall: s.islandsSmall, bridges: s.bridges, bytes: s.bytes, refused5: s.refused5, repairs: s.repairs, repairVertices: s.repairVertices } })}`);
}
console.log(`SUMMARY ${JSON.stringify({ source: mode, storeId, objects: map.size, homes: Object.keys(HOMES).length, ok, shas })}`);
process.exit(ok === Object.keys(HOMES).length ? 0 : 1);
