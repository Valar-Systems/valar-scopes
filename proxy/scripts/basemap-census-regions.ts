// R-CENSUS (docs/basemap-deploy.md, the 2026-10-30 ruling's item 3): does every region of the SEALED store hold at
// least one file for each layer the builder reads from it? build() reads water, rivers, roads and peaks from the
// regions whose box meets the square (RD-P6), and throws when a requested one of them has no file there. regionsFor
// builds two kinds of region: a county ({water, rivers}) and a state ({roads, peaks}). Any region of another shape is
// NAMED (its index, its box, its files) and the census exits 1.
//
// It reads the sealed index through assembleStore, the local verified copy basemap-grade-store.ts uses, and prints the
// store id and the index object's sha256, so the reading names the bytes it read. Exit 0 every region as predicted;
// 1 a region named, or a count off the frozen 3,144 and 51; 2 BLIND (the store cannot be read).
//
//   npx tsx scripts/basemap-census-regions.ts --cache <ingest cache>
//   npx tsx scripts/basemap-census-regions.ts --cache <ingest cache> --control     (C-CENSUS)
//
// --control removes the rivers file(s) of the first county region from an in-memory copy of the index before the
// census runs, and says which county it planted: the census must then name exactly that county and exit 1.
import { createHash } from "node:crypto";
import { join } from "node:path";
import { type StoreIndex, storeKeys } from "../src/basemap/store";
import { assembleStore } from "./basemap/d1";

const arg = (n: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : undefined; };
const cache = arg("--cache"), control = process.argv.includes("--control");
if (!cache) { console.log("census: BLIND -- usage: --cache <ingest cache> [--control]"); process.exit(2); }
const cachePath = (c: string, url: string) => { const u = new URL(url); return join(c, "files", u.hostname, ...u.pathname.split("/").filter(Boolean)); };

let storeId: string, objects: Array<{ key: string; bytes: Uint8Array }>;
try { ({ storeId, objects } = assembleStore(cache, cachePath)); } catch (e) { console.log(`census: BLIND -- the store could not be assembled: ${(e as Error).message.slice(0, 160)}`); process.exit(2); }
const k = storeKeys(storeId), get = (key: string) => objects.find((o) => o.key === key)?.bytes;
const indexBytes = get(k.index), manifestBytes = get(k.manifest);
if (!indexBytes || !manifestBytes) { console.log("census: BLIND -- no index or manifest among the assembled objects"); process.exit(2); }
const index = JSON.parse(new TextDecoder().decode(indexBytes)) as StoreIndex;
const manifest = JSON.parse(new TextDecoder().decode(manifestBytes)) as { files: Array<{ url: string; layer: string }> };
console.log(`store ${storeId}; index.json sha256 ${createHash("sha256").update(indexBytes).digest("hex").slice(0, 12)}; ${index.regions.length} regions`);

const layerOf = new Map(manifest.files.map((f) => [f.url, f.layer]));
const base = (u: string) => u.split("/").pop() ?? u;
const regions = index.regions.map((r) => ({ box: r.box, files: [...r.files] })); // a copy: --control edits only this
if (control) {
  const i = regions.findIndex((r) => r.files.some((u) => layerOf.get(u) === "rivers"));
  if (i < 0) { console.log("census: BLIND -- the control found no county region to plant"); process.exit(2); }
  const geoid = /tl_2024_(\d{5})_/.exec(base(regions[i]!.files[0]!))?.[1];
  regions[i]!.files = regions[i]!.files.filter((u) => layerOf.get(u) !== "rivers");
  console.log(`CONTROL C-CENSUS: planted -- region ${i} (county ${geoid}) without its rivers file(s)`);
}

// A region's shape: the set of layers it holds, each with at least one file.
const COUNTY = ["rivers", "water"], STATE = ["peaks", "roads"];
let counties = 0, states = 0;
const named: string[] = [], geoids = new Set<string>(), fips = new Set<string>();
regions.forEach((r, i) => {
  const held = [...new Set(r.files.map((u) => layerOf.get(u) ?? "UNLISTED"))].sort();
  const name = (why: string) => named.push(`region ${i}: ${why}; box ${JSON.stringify(r.box)}; files ${JSON.stringify(r.files.map(base))}`);
  if (held.join() === COUNTY.join()) {
    const g = [...new Set(r.files.map((u) => /tl_2024_(\d{5})_(areawater|linearwater)\.zip$/.exec(base(u))?.[1] ?? "?"))];
    if (g.length !== 1 || g[0] === "?") return name(`a county region whose files do not name one county (${g.join(",")})`);
    if (geoids.has(g[0]!)) return name(`county ${g[0]} appears twice`);
    geoids.add(g[0]!); counties++;
  } else if (held.join() === STATE.join()) {
    const f = r.files.map((u) => /tl_2024_(\d{2})_prisecroads\.zip$/.exec(base(u))?.[1]).filter(Boolean);
    if (f.length !== 1) return name(`a state region with ${f.length} roads files`);
    if (fips.has(f[0]!)) return name(`state ${f[0]} appears twice`);
    fips.add(f[0]!); states++;
  } else {
    name(`holds {${held.join(", ")}}, neither a county's {water, rivers} nor a state's {roads, peaks}`);
  }
});

console.log(`county regions (>=1 water and >=1 rivers file, nothing else): ${counties}`);
console.log(`state regions (>=1 roads and >=1 peaks file, nothing else): ${states}`);
console.log(`regions of any other shape: ${named.length}`);
for (const n of named) console.log(`  NAMED ${n}`);
const asFrozen = named.length === 0 && counties === 3144 && states === 51;
console.log(`census: ${asFrozen ? "as frozen" : "OFF -- stop and report"}`);
process.exit(asFrozen ? 0 : 1);
