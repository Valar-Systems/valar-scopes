// MG1 (docs/basemap-route.md A2.7, A2.8): my builder, over the sealed store, at bend.bmpk's own request, against
// bend.bmpk. Compared as ruled: the header byte for byte except totalBytes and the CRC; the directory byte for byte
// except PROVENANCE's length; sections 1-6 byte for byte; PROVENANCE line for line except the values of builder: and
// store:. Prints counts and section names, never a coordinate. Exit 0 equal, 1 different, 2 BLIND.
//
//   npx tsx scripts/basemap-golden.ts --cache <ingest cache>            MG1
//   npx tsx scripts/basemap-golden.ts --cache <c> --self                control: bend.bmpk against itself, 0 differences
//   npx tsx scripts/basemap-golden.ts --cache <c> --eps-factor 1.5      control S-GEPS: must FAIL on vertex counts
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { build } from "../src/basemap/build";
import { decode } from "../src/basemap/format";
import { CURRENT_KEY } from "../src/basemap/store";
import { assembleStore } from "./basemap/d1";

const argv = process.argv, arg = (n: string) => { const i = argv.indexOf(n); return i > 0 ? argv[i + 1] : undefined; };
const cache = arg("--cache"), self = argv.includes("--self"), factor = Number(arg("--eps-factor") ?? "1");
if (!cache) throw new Error("--cache <dir>");
const golden = new Uint8Array(readFileSync(join(process.cwd(), "..", "test/fixtures/wx/basemap/bend.bmpk")));
const g = decode(golden), gdv = new DataView(golden.buffer, golden.byteOffset, golden.byteLength);
const text = (b: Uint8Array) => new TextDecoder().decode((decode(b).sections.find((s) => s.type === 7) as { text: Uint8Array }).text).split("\n");
const gprov = text(golden);
const epsLine = gprov.find((l) => l.startsWith("epsilon.m: "));
if (!epsLine) { console.log("BLIND: bend.bmpk has no epsilon.m"); process.exit(2); }
const epsMm = Math.round(Number(epsLine.slice(11)) * 1000 * factor);
const types = new Set(g.sections.map((s) => s.type));
const mask = [1, 2, 3, 4, 5].reduce((m, t) => (types.has(t) ? m | (1 << (t - 1)) : m), 0) | (types.has(6) ? 32 : 0);
const levels = g.sections.filter((s) => s.type === 6).map((s) => ({ radiusMm: (s as { levelRadiusMm: number }).levelRadiusMm, n: (s as { n: number }).n }));

let mine: Uint8Array;
if (self) mine = golden.slice();
else {
  const cachePath = (c: string, url: string) => { const u = new URL(url); return join(c, "files", u.hostname, ...u.pathname.split("/").filter(Boolean)); };
  const { objects, current } = assembleStore(cache, cachePath);
  const map = new Map(objects.map((o) => [o.key, o.bytes]));
  map.set(CURRENT_KEY, current);
  const commit = execFileSync("git", ["rev-parse", "HEAD"], { encoding: "utf8" }).trim();
  const r = await build({ get: async (k) => map.get(k) ?? null }, { lat0Micro: gdv.getInt32(8, true), lon0Micro: gdv.getInt32(12, true), rKm: gdv.getUint32(16, true) / 1e6, epsMm, mask, levels }, { commit });
  if (!r.ok) { console.log(`BLIND: the build answered ${r.status} ${r.code}`); process.exit(2); }
  mine = r.bytes;
}
const mdv = new DataView(mine.buffer, mine.byteOffset, mine.byteLength);
const fails: string[] = [];

// the header: byte for byte except totalBytes (24..27) and the CRC (28..31)
const hdr = [...Array(24).keys()].filter((i) => golden[i] !== mine[i]);
if (hdr.length) fails.push(`header: ${hdr.length} byte(s) differ outside totalBytes and the CRC (first at ${hdr[0]})`);
// the directory: byte for byte except PROVENANCE's length
const n = gdv.getUint8(5);
if (mdv.getUint8(5) !== n) fails.push(`directory: ${mdv.getUint8(5)} sections against ${n}`);
else for (let i = 0; i < n; i++) {
  const e = 32 + 16 * i, isProv = gdv.getUint8(e) === 7;
  for (let k = 0; k < 16; k++) if ((!isProv || k < 12) && golden[e + k] !== mine[e + k]) { fails.push(`directory entry ${i} (type ${gdv.getUint8(e)}) differs at byte ${k}`); break; }
}
// sections 1-6, byte for byte, with a TERRAIN difference counted cell by cell (RD28's reading needs them)
const ms = decode(mine).sections;
const vcount = (s: unknown) => { const o = s as { rings?: unknown[][]; lines?: Array<{ pts: unknown[] }>; roads?: Array<{ pts: unknown[] }> }; return o.rings ? o.rings.reduce((a, r) => a + r.length, 0) : (o.lines ?? o.roads ?? []).reduce((a, l) => a + l.pts.length, 0); };
const vertexReport: string[] = [];
let terrainCells = 0, terrainTotal = 0;
for (let i = 0; i < n; i++) {
  const e = 32 + 16 * i, t = gdv.getUint8(e);
  if (t === 7) continue;
  const go = gdv.getUint32(e + 8, true), gl = gdv.getUint32(e + 12, true), mo = mdv.getUint32(e + 8, true), ml = mdv.getUint32(e + 12, true);
  const a = golden.subarray(go, go + gl), b = mine.subarray(mo, mo + ml);
  if (t <= 3) vertexReport.push(`type ${t}: ${vcount(g.sections[i])} vertices against ${vcount(ms[i])}`);
  if (t === 6) { terrainTotal += Math.max(0, gl - 8); for (let k = 8; k < Math.min(gl, ml); k++) if (a[k] !== b[k]) terrainCells++; }
  if (gl !== ml || a.some((v, k) => v !== b[k])) fails.push(`section ${i} (type ${t}): ${gl} B against ${ml} B, ${gl === ml ? `${a.reduce((c, v, k) => c + (v !== b[k] ? 1 : 0), 0)} byte(s) differ` : "lengths differ"}`);
}
// PROVENANCE: line for line, except the VALUES of builder: and store:
const mprov = text(mine);
if (mprov.length !== gprov.length) fails.push(`PROVENANCE: ${mprov.length} lines against ${gprov.length}`);
else gprov.forEach((l, k) => {
  const key = l.slice(0, l.indexOf(": ") + 1);
  if (key === "builder:" || key === "store:") { if (!mprov[k]!.startsWith(key)) fails.push(`PROVENANCE line ${k}: ${key} expected`); return; }
  if (l !== mprov[k]) fails.push(`PROVENANCE line ${k} (${key}) differs`);
});

console.log(`MG1${self ? " (control: bend.bmpk against itself)" : factor !== 1 ? ` (control S-GEPS: eps x ${factor})` : ""}: ${golden.length} B golden, ${mine.length} B mine`);
console.log(`  vertices: ${vertexReport.join("; ")}`);
console.log(`  terrain: ${terrainCells} of ${terrainTotal} cells differ (${terrainTotal ? ((100 * terrainCells) / terrainTotal).toFixed(4) : "0"} %)`);
for (const f of fails.slice(0, 12)) console.log(`  DIFF ${f}`);
console.log(fails.length ? `MG1: DIFFERENT (${fails.length})` : "MG1: EQUAL under the ruled masks");
process.exit(fails.length ? 1 : 0);
