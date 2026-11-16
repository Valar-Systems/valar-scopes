// SEA-STORE S-4 and S-5 (docs/basemap-dense-code.md, registered at 2891a937): the store's kind-6 features decoded with the store's own
// codec and grouped back into pieces (one record per piece: part 0 the outer, parts 1..n the islands), keyed by the 1-degree cell their
// outer lies in, in sea-ingest.mts's shape, for verify2.py (S-4). And S-5: each sea file's bounding box over its decoded pieces, against
// sea-regions.json. Counts to stdout.
//   (from <clone>/proxy) node --import tsx <scratchpad>/seastore/pieces.mts <cache dir> <out pieces.json> <clone proxy dir>
import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
const [C, outp, proxyDir] = process.argv.slice(2) as [string, string, string];
const { decodeCell } = await import(pathToFileURL(resolve(proxyDir, "src/basemap/store.ts")).href);
const store = existsSync(join(C, "dry-run", "store")) ? join(C, "dry-run", "store") : C; // a cache dir, or a bare store dir (the tool's own test)
const manifest = JSON.parse(readFileSync(join(store, "manifest.json"), "utf8")) as { files: Array<{ layer: string; url: string }> };
const walk = (d: string): string[] => readdirSync(d).flatMap((n) => { const p = join(d, n); return statSync(p).isDirectory() ? walk(p) : [p]; });
const recs = new Map<string, { file: number; record: number; src: string; parts: Map<number, number[][]> }>();
let seaFeatures = 0, cellsRead = 0;
for (const p of walk(join(store, "cells")).filter((x) => x.endsWith(".bin"))) {
  cellsRead++;
  for (const f of decodeCell(new Uint8Array(readFileSync(p)))) {
    if (f.kind !== "sea") continue;
    seaFeatures++;
    const k = `${f.file}/${f.record}`;
    const r = recs.get(k) ?? { file: f.file, record: f.record, src: String(f.attrs["src"]), parts: new Map() };
    if (r.parts.has(f.part)) throw new Error(`record ${k} part ${f.part} stored twice`);
    const ring: number[][] = []; for (let i = 0; i < f.xy.length; i += 2) ring.push([f.xy[i]!, f.xy[i + 1]!]);
    r.parts.set(f.part, ring); recs.set(k, r);
  }
}
const byCell: Record<string, Array<{ src: string; outer: number[][]; holes: number[][][] }>> = {};
const boxes = new Map<number, number[]>();
for (const r of [...recs.values()].sort((a, b) => a.record - b.record)) {
  const n = r.parts.size; for (let i = 0; i < n; i++) if (!r.parts.has(i)) throw new Error(`record ${r.file}/${r.record}: part ${i} missing`);
  const outer = r.parts.get(0)!;
  let lo = Infinity, la = Infinity, hi = -Infinity, ha = -Infinity;
  for (const [x, y] of outer) { lo = Math.min(lo, x!); la = Math.min(la, y!); hi = Math.max(hi, x!); ha = Math.max(ha, y!); }
  (byCell[`${Math.floor(la)}/${Math.floor(lo)}`] ??= []).push({ src: r.src, outer, holes: [...Array(n - 1).keys()].map((i) => r.parts.get(i + 1)!) });
  const b = boxes.get(r.file) ?? [Infinity, Infinity, -Infinity, -Infinity];
  for (const ring of r.parts.values()) for (const [x, y] of ring) { b[0] = Math.min(b[0]!, x!); b[1] = Math.min(b[1]!, y!); b[2] = Math.max(b[2]!, x!); b[3] = Math.max(b[3]!, y!); }
  boxes.set(r.file, b);
}
const sorted = Object.fromEntries(Object.keys(byCell).sort().map((k) => [k, byCell[k]!]));
writeFileSync(outp, JSON.stringify(sorted));
// S-5: the regions file against the decoded pieces' boxes, per sea file
const regions = (existsSync(join(store, "sea-regions.json")) ? JSON.parse(readFileSync(join(store, "sea-regions.json"), "utf8")) : []) as Array<{ box: number[]; files: string[] }>;
const s5 = [...boxes].map(([file, b]) => {
  const url = manifest.files[file]!.url, reg = regions.filter((r) => r.files.includes(url));
  return { file: url.split("/").pop(), layer: manifest.files[file]!.layer, regions: reg.length, equal: reg.length === 1 && JSON.stringify(reg[0]!.box) === JSON.stringify(b) };
});
console.log(JSON.stringify({ cellsRead, seaFeatures, pieces: recs.size, cellsWithSea: Object.keys(sorted).length, holes: [...recs.values()].reduce((a, r) => a + r.parts.size - 1, 0),
  S5: { regionsInFile: regions.length, perFile: s5 } }));
