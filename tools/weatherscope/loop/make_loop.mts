// 3.3c (docs/weatherscope-stage3.md A8.10): the loop's 15 crops, made by MAIN's shared code -- the server's own
// decode (proxy/src/wx/mrms.ts: decodeMrms, codeOf) and crop path (proxy/src/wx/crop.ts: quantise, cropCellBox,
// encodeBox), loaded from a full checkout given on the command line and never copied or changed.
//
//   npx tsx make_loop.mts <proxy-dir> <grib-dir> <kdfw-60mi.wxc> <out-dir> [--check]
//
// <grib-dir> is fetch_loop.py's download (outside every repo) with its fetched.json; each file's sha256 is checked
// before use. The centre is KDFW's, read from the Stage 1 crop's own header (forbidden.py cleared it); it is printed
// nowhere and stored only inside each crop's header, as every WXC1 crop stores its NW cell. The manifest holds no
// coordinate. --check regenerates in memory and exits 1 if any committed byte differs (D1, determinism).
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const args = process.argv.slice(2).filter((a) => a !== "--check");
const check = process.argv.includes("--check");
if (args.length !== 4) {
  console.error("usage: make_loop.mts <proxy-dir> <grib-dir> <kdfw-60mi.wxc> <out-dir> [--check]");
  process.exit(2);
}
const [proxy, gribDir, kdfwPath, outDir] = args.map((a) => resolve(a)) as [string, string, string, string];
const { decodeMrms, codeOf } = await import(pathToFileURL(join(proxy, "src", "wx", "mrms.ts")).href);
const { quantise, cropCellBox, encodeBox } = await import(pathToFileURL(join(proxy, "src", "wx", "crop.ts")).href);
const commit = execFileSync("git", ["-C", proxy, "rev-parse", "HEAD"]).toString().trim();
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

// KDFW's centre from the Stage 1 crop's header: the NW cell centre plus (n-1)/2 cells, as WxRender.h computes it.
const k = readFileSync(kdfwPath);
const dv = new DataView(k.buffer, k.byteOffset, k.byteLength);
const latNw = dv.getInt32(12, true) / 1e6, lonNw = dv.getInt32(16, true) / 1e6;
const cellLat = dv.getUint32(20, true) / 1e6, cellLon = dv.getUint32(24, true) / 1e6;
const rows = dv.getUint16(28, true), cols = dv.getUint16(30, true);
const centre = { lat: latNw - ((rows - 1) / 2) * cellLat, lon: lonNw + ((cols - 1) / 2) * cellLon };
const RADIUS_KM = 60 * 1.609344;

type Fetched = { key: string; valid: string; file: string; bytes: number; sha256: string };
const fetched = JSON.parse(readFileSync(join(gribDir, "fetched.json"), "utf8")) as Fetched[];
if (fetched.length !== 15) { console.error(`STOP: fetched.json lists ${fetched.length} frames, not 15`); process.exit(1); }

const q = quantise(centre.lat, centre.lon, RADIUS_KM);
const entries: Record<string, unknown>[] = [];
let differs = 0;
if (!check) mkdirSync(outDir, { recursive: true });

for (const f of fetched) {
  const gz = new Uint8Array(readFileSync(join(gribDir, f.file)));
  if (sha(gz) !== f.sha256) { console.error(`STOP: ${f.file} does not hash to fetched.json's sha256`); process.exit(1); }
  const rowCodes: Uint8Array[] = [];
  const grid = await decodeMrms(gz, (row: number, vals: Float32Array) => {
    const c = new Uint8Array(vals.length);
    for (let j = 0; j < vals.length; j++) c[j] = codeOf(vals[j] as number);   // no missing value: SeamlessHSR has none
    rowCodes[row] = c;
  });
  if (!grid.n2s || !grid.w2e) { console.error(`STOP: ${f.file} is not north->south, west->east`); process.exit(1); }
  const epoch = Math.floor(Date.parse(grid.validTimeUtc) / 1000);
  if (epoch !== Math.floor(Date.parse(f.valid) / 1000)) {
    console.error(`STOP: ${f.file}'s own valid time ${grid.validTimeUtc} is not its name's ${f.valid}`);
    process.exit(1);
  }
  const g = { ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj };
  const box = cropCellBox(g, q);
  const cellAt = (r: number, c: number) => (r < 0 || r >= g.nj || c < 0 || c >= g.ni ? 14 : rowCodes[r]![c]!);
  const e = encodeBox(g, epoch, box, cellAt);
  const name = f.file.replace(/^MRMS_SeamlessHSR_00\.00_/, "shsr-").replace(/\.grib2\.gz$/, ".wxc");
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
  what: "Weatherscope 3.3c's loop: 15 SeamlessHSR crops, 2024-05-21, the hour to 22:00Z, KDFW, 60 mi (A8.10)",
  encoder: `main's proxy/src/wx/mrms.ts (decodeMrms, codeOf) and proxy/src/wx/crop.ts (quantise, cropCellBox, encodeBox) at ${commit}`,
  centre: "KDFW, read from test/fixtures/wx/kdfw-60mi.wxc's header (forbidden.py-cleared in Stage 1); stored only in each crop's header",
  radiusKm: q.rKm,
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
