// R-SWEEP (docs/basemap-deploy.md, the second 2026-10-30 ruling's item 3): at the route's smallest accepted r (1 km,
// quantise's max(1, round(r))), does every origin build() accepts have a square that meets a county region (water,
// rivers) AND a state region (roads, peaks)? build() throws when a requested layer has no file among the regions its
// square meets; R-CENSUS showed every region carries its layers, and this asks the remaining question: does every
// accepted square MEET a region of each kind. Squares nest as r grows, so r = 1 is the worst case.
//
// Every term is the builder's own: the origins are exactly the cells coverage.bin lists (build() refuses any other,
// covered(cov, h, c) on the origin's 0.01° cell); the square is squareBox(makeFrame(h*1e4, c*1e4, 1e6, eps), 1e6);
// the test is boxMeets. It reads the sealed store through assembleStore (the local verified copy) and prints the store
// id and the sha256 of index.json and coverage.bin. Exit 0 both counts 0; 1 any square named; 2 BLIND.
//
//   npx tsx scripts/basemap-sweep-small-r.ts --cache <ingest cache> [--out <file>]
//   npx tsx scripts/basemap-sweep-small-r.ts --cache <ingest cache> --control      (C-SWEEP: Texas's state region removed)
import { createHash } from "node:crypto";
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { squareBox } from "../src/basemap/build";
import { makeFrame } from "../src/basemap/frame";
import { type Box, type StoreIndex, boxMeets, covered, storeKeys } from "../src/basemap/store";
import { assembleStore } from "./basemap/d1";

const arg = (n: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : undefined; };
const cache = arg("--cache"), out = arg("--out"), control = process.argv.includes("--control");
const blind = (why: string): never => { console.log(`sweep: BLIND -- ${why}`); process.exit(2); };
if (!cache) blind("usage: --cache <ingest cache> [--out <file>] [--control]");
const cachePath = (c: string, url: string) => { const u = new URL(url); return join(c, "files", u.hostname, ...u.pathname.split("/").filter(Boolean)); };

let storeId = "", objects: Array<{ key: string; bytes: Uint8Array }> = [];
try { ({ storeId, objects } = assembleStore(cache!, cachePath)); } catch (e) { blind(`the store could not be assembled: ${(e as Error).message.slice(0, 160)}`); }
const k = storeKeys(storeId), get = (key: string) => objects.find((o) => o.key === key)?.bytes;
const indexBytes = get(k.index), manifestBytes = get(k.manifest), cov = get(k.coverage);
if (!indexBytes || !manifestBytes || !cov) blind("no index, manifest or coverage among the assembled objects");
const sha12 = (b: Uint8Array) => createHash("sha256").update(b).digest("hex").slice(0, 12);
console.log(`store ${storeId}; index.json sha256 ${sha12(indexBytes!)}; coverage.bin sha256 ${sha12(cov!)}`);

// ---- the regions, by kind (as R-CENSUS read them) ----------------------------------------------------------------
const index = JSON.parse(new TextDecoder().decode(indexBytes)) as StoreIndex;
const manifest = JSON.parse(new TextDecoder().decode(manifestBytes)) as { files: Array<{ url: string; layer: string }> };
const layerOf = new Map(manifest.files.map((f) => [f.url, f.layer]));
const countyBoxes: Box[] = [], stateBoxes: Array<{ box: Box; fips: string }> = [];
for (const r of index.regions) {
  const held = [...new Set(r.files.map((u) => layerOf.get(u)))].sort().join();
  if (held === "rivers,water") countyBoxes.push(r.box);
  else if (held === "peaks,roads") stateBoxes.push({ box: r.box, fips: /tl_2024_(\d{2})_prisecroads\.zip$/.exec(r.files.find((u) => layerOf.get(u) === "roads") ?? "")?.[1] ?? "?" });
  else blind(`a region of another shape ({${held}}): R-CENSUS no longer holds`);
}
let texas: Box | null = null;
if (control) {
  const i = stateBoxes.findIndex((s) => s.fips === "48");
  if (i < 0) blind("the control found no Texas state region");
  texas = stateBoxes[i]!.box;
  stateBoxes.splice(i, 1);
  console.log("CONTROL C-SWEEP: planted -- Texas's state region (FIPS 48) removed from an in-memory copy");
}
console.log(`regions: ${countyBoxes.length} county, ${stateBoxes.length} state`);

// A 1-degree bucket index over the boxes, so each origin tests only the boxes near it. A box is filed in every bucket
// it spans; a square looks in every bucket IT spans, so no meeting pair can be missed.
const bucketsOf = (b: Box) => { const out: string[] = []; for (let x = Math.floor(b[0]); x <= Math.floor(b[2]); x++) for (let y = Math.floor(b[1]); y <= Math.floor(b[3]); y++) out.push(`${x},${y}`); return out; };
const indexOf = (boxes: Box[]) => { const m = new Map<string, Box[]>(); for (const b of boxes) for (const key of bucketsOf(b)) { const l = m.get(key); if (l) l.push(b); else m.set(key, [b]); } return m; };
const countyIdx = indexOf(countyBoxes), stateIdx = indexOf(stateBoxes.map((s) => s.box));
const meetsAny = (idx: Map<string, Box[]>, sq: Box) => bucketsOf(sq).some((key) => (idx.get(key) ?? []).some((b) => boxMeets(b, sq)));

// ---- the covered origins: coverage.bin in the layout covered() reads ---------------------------------------------
const dv = new DataView(cov!.buffer, cov!.byteOffset, cov!.byteLength);
if (dv.getUint32(0, false) !== 0x424d4356) blind("coverage.bin has no BMCV magic");
const nRows = dv.getUint32(8, true);
const rows: Array<[number, Array<[number, number]>]> = [];
let o = 12, expected = 0;
for (let r = 0; r < nRows; r++) {
  const h = dv.getInt32(o, true), n = dv.getUint32(o + 4, true), ranges: Array<[number, number]> = [];
  for (let j = 0; j < n; j++) { const a = dv.getInt32(o + 8 + 8 * j, true), b = dv.getInt32(o + 12 + 8 * j, true); ranges.push([a, b]); expected += b - a + 1; }
  rows.push([h, ranges]); o += 8 + 8 * n;
}
// The parse must read coverage as covered() does: every range's ends are covered, and the cells just outside are not.
let spot = 0;
for (const [h, ranges] of rows) for (const [a, b] of ranges) {
  if (!covered(cov!, h, a) || !covered(cov!, h, b)) blind(`the parse disagrees with covered() inside row ${h}`);
  if (covered(cov!, h, a - 1) && !ranges.some(([x, y]) => a - 1 >= x && a - 1 <= y)) blind(`the parse disagrees with covered() at a range's start, row ${h}`);
  spot++;
}

// ---- the sweep -----------------------------------------------------------------------------------------------------
const R_MM = 1_000_000;
let visited = 0;
const noCounty: string[] = [], noState: string[] = [];
let stateMissOutsideTexas = 0;
for (const [h, ranges] of rows) for (const [a, b] of ranges) for (let c = a; c <= b; c++) {
  visited++;
  const sq = squareBox(makeFrame(h * 10_000, c * 10_000, R_MM, 100_000), R_MM);
  const cell = `${(h / 100).toFixed(2)},${(c / 100).toFixed(2)}`;
  if (!meetsAny(countyIdx, sq)) noCounty.push(cell);
  if (!meetsAny(stateIdx, sq)) { noState.push(cell); if (texas && !boxMeets(texas, sq)) stateMissOutsideTexas++; }
}

console.log(`covered origins visited: ${visited}; coverage.bin's own sum: ${expected}; ranges spot-checked against covered(): ${spot}`);
if (visited !== expected) blind("the origins visited are not the coverage file's own count");
console.log(`r = 1 km squares meeting NO county region (water, rivers): ${noCounty.length}`);
console.log(`r = 1 km squares meeting NO state region (roads, peaks): ${noState.length}`);
if (control) console.log(`  of those, squares not meeting Texas's box: ${stateMissOutsideTexas}`);
const named = [...noCounty.map((x) => `no county region: ${x}`), ...noState.map((x) => `no state region: ${x}`)];
if (out) { writeFileSync(out, named.join("\n") + (named.length ? "\n" : "")); console.log(`named cells written to ${out}`); }
for (const n of named.slice(0, 40)) console.log(`  NAMED ${n}`);
if (named.length > 40) console.log(`  ... ${named.length - 40} more (all in --out)`);
const zero = noCounty.length === 0 && noState.length === 0;
console.log(`sweep: ${zero ? "both counts 0 -- the gap closes" : "squares named -- stop"}`);
process.exit(zero ? 0 : 1);
