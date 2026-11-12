// W4's X9, the look (docs/weatherscope-stage4.md S4.12.4): the stored loop's 15 SeamlessHSR frames, re-encoded THROUGH
// THE shsr ROW (proxy/src/wx/products.ts: productById("shsr").codeOf) by the server's own decode and crop path, must give
// the committed MANIFEST's crop sha256 exactly. One reading before the PR, not in CI.
//
//   npx tsx x9_shsr.mts <proxy-dir> <grib-dir> <kdfw-60mi.wxc> <loop-fixture-dir>
//
// <grib-dir> is fetch_loop.py's download (outside every repo) with its fetched.json. Each GRIB is first checked against
// the MANIFEST's gribSha256 for its key: an archive copy that no longer matches is the INSTRUMENT, not a reading (exit 2).
// The centre is read from the Stage 1 crop's header exactly as make_loop.mts reads it, and is printed nowhere.
// Exit 0 all 15 identical; 1 any crop differs; 2 the instrument (a GRIB that does not match, a missing frame, usage).
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const args = process.argv.slice(2);
if (args.length !== 4) {
  console.error("usage: x9_shsr.mts <proxy-dir> <grib-dir> <kdfw-60mi.wxc> <loop-fixture-dir>");
  process.exit(2);
}
const [proxy, gribDir, kdfwPath, loopDir] = args.map((a) => resolve(a)) as [string, string, string, string];
const { decodeMrms } = await import(pathToFileURL(join(proxy, "src", "wx", "mrms.ts")).href);
const { quantise, cropCellBox, encodeBox } = await import(pathToFileURL(join(proxy, "src", "wx", "crop.ts")).href);
const { productById } = await import(pathToFileURL(join(proxy, "src", "wx", "products.ts")).href);
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

const row = productById("shsr");
if (!row || row.source !== "mrms") { console.error("INSTRUMENT: no MRMS row shsr in this proxy's products.ts"); process.exit(2); }

// KDFW's centre, as make_loop.mts reads it (the NW cell centre plus (n-1)/2 cells).
const k = readFileSync(kdfwPath);
const dv = new DataView(k.buffer, k.byteOffset, k.byteLength);
const latNw = dv.getInt32(12, true) / 1e6, lonNw = dv.getInt32(16, true) / 1e6;
const cellLat = dv.getUint32(20, true) / 1e6, cellLon = dv.getUint32(24, true) / 1e6;
const rows = dv.getUint16(28, true), cols = dv.getUint16(30, true);
const centre = { lat: latNw - ((rows - 1) / 2) * cellLat, lon: lonNw + ((cols - 1) / 2) * cellLon };
const q = quantise(centre.lat, centre.lon, 60 * 1.609344);

type Frame = { name: string; noaaKey: string; gribSha256: string; frameEpoch: number; sha256: string };
const manifest = JSON.parse(readFileSync(join(loopDir, "MANIFEST.json"), "utf8")) as { frames: Frame[] };
type Fetched = { key: string; valid: string; file: string; sha256: string };
const fetched = JSON.parse(readFileSync(join(gribDir, "fetched.json"), "utf8")) as Fetched[];
if (manifest.frames.length !== 15) { console.error(`INSTRUMENT: the manifest lists ${manifest.frames.length} frames`); process.exit(2); }

let same = 0;
for (const m of manifest.frames) {
  const f = fetched.find((x) => x.key === m.noaaKey);
  if (!f) { console.error(`INSTRUMENT: ${m.name}: its key was not fetched`); process.exit(2); }
  const gz = new Uint8Array(readFileSync(join(gribDir, f.file)));
  if (sha(gz) !== m.gribSha256) { console.error(`INSTRUMENT: ${m.name}: the archive copy no longer matches the manifest's gribSha256`); process.exit(2); }
  const rowCodes: Uint8Array[] = [];
  const grid = await decodeMrms(gz, (r: number, vals: Float32Array) => {
    const c = new Uint8Array(vals.length);
    for (let j = 0; j < vals.length; j++) c[j] = row.codeOf(vals[j] as number);   // THROUGH THE ROW
    rowCodes[r] = c;
  });
  const epoch = Math.floor(Date.parse(grid.validTimeUtc) / 1000);
  if (epoch !== m.frameEpoch) { console.error(`INSTRUMENT: ${m.name}: valid time ${epoch}, the manifest's ${m.frameEpoch}`); process.exit(2); }
  const g = { ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj };
  const box = cropCellBox(g, q);
  const cellAt = (r: number, c: number) => (r < 0 || r >= g.nj || c < 0 || c >= g.ni ? 14 : rowCodes[r]![c]!);
  const e = encodeBox(g, epoch, box, cellAt);
  const ok = sha(e.bytes) === m.sha256;
  if (ok) same++;
  console.log(`  ${m.name}: grib matches; crop ${ok ? "identical" : "DIFFERS"} (sha256 ${sha(e.bytes).slice(0, 12)})`);
}
console.log(`X9: ${same} of 15 crops identical through the shsr row`);
process.exit(same === 15 ? 0 : 1);
