// 3.5b's Bend loop (docs/weatherscope-stage3.md A10.4, A12.10, A12.12): the 15 crops of the window A12.10 chose, made by
// MAIN's shared code -- the server's own decode (proxy/src/wx/mrms.ts: decodeMrms, codeOf) and crop path
// (proxy/src/wx/crop.ts: quantise, cropCellBox, encodeBox), loaded from a full checkout given on the command line and
// never copied or changed. A second path beside make_loop.mts (Dallas); A12.12 lists what that first path establishes,
// and this one does each of them.
//
//   npx tsx make_bend_loop.mts <proxy-dir> <cache-dir> <bend.bmpk> <out-dir> [--check]
//
// <cache-dir> holds pick_bend.mts's downloads and its selection.json (A12.10's record); each file's sha256 is checked
// against that record before use. The centre is bend.bmpk's own origin (its header), printed nowhere and stored only
// inside each crop's header, as every WXC1 crop stores its NW cell. The manifest holds no coordinate. --check
// regenerates in memory and exits 1 if any committed byte differs (D1, determinism).
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const args = process.argv.slice(2).filter((a) => a !== "--check");
const check = process.argv.includes("--check");
if (args.length !== 4) {
  console.error("usage: make_bend_loop.mts <proxy-dir> <cache-dir> <bend.bmpk> <out-dir> [--check]");
  process.exit(2);
}
const [proxy, cache, pkgPath, outDir] = args.map((a) => resolve(a)) as [string, string, string, string];
const { decodeMrms, codeOf } = await import(pathToFileURL(join(proxy, "src", "wx", "mrms.ts")).href);
const { quantise, encodeBox, cropCellBox } = await import(pathToFileURL(join(proxy, "src", "wx", "crop.ts")).href);
// The encoder's commit: the last commit to touch src/wx/ (on main, so it survives this branch's squash), and the
// encoder must equal origin/main's exactly, or the tool refuses: "main's decode and crop path" is checked, not assumed.
const commit = execFileSync("git", ["-C", proxy, "log", "-1", "--format=%H", "--", "src/wx"]).toString().trim();
try { execFileSync("git", ["-C", proxy, "diff", "--quiet", "origin/main", "--", "src/wx"]); }
catch { console.error("STOP: proxy/src/wx/ here differs from origin/main's"); process.exit(1); }
execFileSync("git", ["-C", proxy, "merge-base", "--is-ancestor", commit, "origin/main"]);   // throws if not on main
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

const pkg = readFileSync(pkgPath);
const centre = { lat: pkg.readInt32LE(8) / 1e6, lon: pkg.readInt32LE(12) / 1e6 };   // never printed
const RADIUS_KM = 60 * 1.609344;

type Frame = { key: string; sha256: string; valid: string };
const sel = JSON.parse(readFileSync(join(cache, "selection.json"), "utf8"));
if (!sel.chosen || sel.chosen.day !== "20240603") { console.error("STOP: selection.json is not A12.10's record (chosen 20240603)"); process.exit(1); }
const frames = sel.chosen.frames as Frame[];
if (frames.length !== 15) { console.error(`STOP: the record lists ${frames.length} frames, not 15`); process.exit(1); }

const q = quantise(centre.lat, centre.lon, RADIUS_KM);
const entries: Record<string, unknown>[] = [];
let differs = 0;
if (!check) mkdirSync(outDir, { recursive: true });

for (const f of frames) {
  const file = join(cache, f.key.replace(/\//g, "__"));
  const gz = new Uint8Array(readFileSync(file));
  if (sha(gz) !== f.sha256) { console.error(`STOP: ${f.key} does not hash to the record's sha256`); process.exit(1); }
  const rowCodes: Uint8Array[] = [];
  const grid = await decodeMrms(gz, (row: number, vals: Float32Array) => {
    const c = new Uint8Array(vals.length);
    for (let j = 0; j < vals.length; j++) c[j] = codeOf(vals[j] as number);   // no missing value: SeamlessHSR has none
    rowCodes[row] = c;
  });
  if (!grid.n2s || !grid.w2e) { console.error(`STOP: ${f.key} is not north->south, west->east`); process.exit(1); }
  const epoch = Math.floor(Date.parse(grid.validTimeUtc) / 1000);
  if (epoch !== Math.floor(Date.parse(f.valid) / 1000)) {
    console.error(`STOP: ${f.key}'s own valid time ${grid.validTimeUtc} is not its name's ${f.valid}`);
    process.exit(1);
  }
  const g = { ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj };
  const box = cropCellBox(g, q);
  const cellAt = (r: number, c: number) => (r < 0 || r >= g.nj || c < 0 || c >= g.ni ? 14 : rowCodes[r]![c]!);
  const e = encodeBox(g, epoch, box, cellAt);
  const name = f.key.split("/").pop()!.replace(/^MRMS_SeamlessHSR_00\.00_/, "shsr-").replace(/\.grib2\.gz$/, ".wxc");
  const path = join(outDir, name);
  if (check) {
    const same = existsSync(path) && Buffer.compare(readFileSync(path), Buffer.from(e.bytes)) === 0;
    if (!same) differs++;
    console.log(`  ${name}: ${same ? "identical" : "DIFFERS"}`);
  } else {
    writeFileSync(path, e.bytes);
    console.log(`  ${name}: ${e.bytes.length} B, coarsen ${e.coarsen}, ${e.rows} x ${e.cols}, sha256 ${sha(e.bytes).slice(0, 12)}`);
  }
  entries.push({
    name, noaaKey: f.key, gribSha256: f.sha256, frameEpoch: epoch, bytes: e.bytes.length, sha256: sha(e.bytes),
    coarsen: e.coarsen, flagged: e.flagged, rows: e.rows, cols: e.cols, codesSha256: sha(e.cells),
  });
}

const manifest = {
  what: "Weatherscope 3.5b's Bend loop: 15 SeamlessHSR crops, 2024-06-02 19:10-20:06Z, Bend, 60 mi (A10.4, A12.10)",
  encoder: `main's proxy/src/wx/mrms.ts (decodeMrms, codeOf) and proxy/src/wx/crop.ts (quantise, cropCellBox, encodeBox) at ${commit}`,
  centre: "bend.bmpk's origin (the site feed's point at 2 dp), read from its header; stored only in each crop's header",
  radiusKm: q.rKm,
  selection: "pick_bend.mts by A10.4's rule with A12.9's criterion (d); A12.10's record",
  codesSha256: "the ENCODER's own cells (EncodedCrop.cells), so the device decoder is graded against an independent answer",
  frames: entries,
};
const mjson = JSON.stringify(manifest, null, 1) + "\n";
const mpath = join(outDir, "MANIFEST.json");
if (check) {
  const same = existsSync(mpath) && readFileSync(mpath, "utf8").replace(/\r\n/g, "\n") === mjson;
  if (!same) differs++;
  console.log(`  MANIFEST.json: ${same ? "identical" : "DIFFERS"}`);
  console.log(differs ? `D1: ${differs} file(s) differ` : "D1: every crop and the manifest regenerate byte-identical");
  process.exit(differs ? 1 : 0);
}
writeFileSync(mpath, mjson);
const total = entries.reduce((s, e) => s + (e.bytes as number), 0);
console.log(`wrote 15 crops (${total} B in all) and MANIFEST.json; encoder at ${commit.slice(0, 7)}`);
