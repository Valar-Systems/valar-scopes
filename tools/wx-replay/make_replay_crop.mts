// The replay's crop (docs/wx-device.md, "The replay route", RC1): ONE SeamlessHSR frame of #496's Bend storm, encoded
// at r = 161 km (E2:08's request at 100 mi) around Bend's public point, by MAIN's own decode and crop path -- the
// server's proxy/src/wx/mrms.ts (decodeMrms, codeOf, cellLatLon) and proxy/src/wx/crop.ts (quantise, cropCellBox,
// encodeBox), loaded from the checkout given on the command line and never copied or changed.
//
// A third path beside tools/weatherscope/loop/make_loop.mts and make_bend_loop.mts. What make_bend_loop.mts
// establishes, this one does too:
//   - the encoder equals origin/main's (git diff --quiet) and its commit is on origin/main, or it refuses;
//   - every input hashes to a record this tool did not write: #496's COMMITTED bend-loop MANIFEST.json, read with
//     `git show origin/main:` rather than from the working tree;
//   - the grid is north->south, west->east, and its own valid time is the record's;
//   - cells off the grid are 14 (no coverage);
//   - the centre is the committed bend.bmpk's origin, read from its header and printed nowhere. Nothing this tool
//     writes or prints holds a coordinate; at the end it checks that of its own output and its sidecar, in memory.
//
// The frame is chosen by a rule fixed before any frame is decoded (the write-up's): of the 15, the one with the most
// echo cells (codes 1-13) within 50 km of the point, ties to the earliest; none within 50 km anywhere is a stop.
//
//   node make_replay_crop.mts <repo-dir> <cache-dir> <out-dir>
//
// <cache-dir> holds the archive downloads (fetched once from the public bucket, then sha-checked every run). The crop
// and its sidecar go to <out-dir> as REPLAY-<utc>-cref.wxc1 and REPLAY-<utc>.json. Neither is ever committed.
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const args = process.argv.slice(2);
if (args.length !== 3) {
  console.error("usage: make_replay_crop.mts <repo-dir> <cache-dir> <out-dir>");
  process.exit(2);
}
const [repo, cache, outDir] = args.map((a) => resolve(a)) as [string, string, string];
const proxy = join(repo, "proxy");
const said: string[] = [];
const say = (s: string) => { said.push(s); console.log(s); };
const stop = (s: string): never => { console.error(`STOP: ${s}`); process.exit(1); };

// The encoder: main's, checked rather than assumed.
const git = (...a: string[]) => execFileSync("git", ["-C", repo, ...a]).toString();
const commit = git("log", "-1", "--format=%H", "--", "proxy/src/wx").trim();
try { execFileSync("git", ["-C", repo, "diff", "--quiet", "origin/main", "--", "proxy/src/wx"]); }
catch { stop("proxy/src/wx/ here differs from origin/main's"); }
try { execFileSync("git", ["-C", repo, "merge-base", "--is-ancestor", commit, "origin/main"]); }
catch { stop(`the encoder's commit ${commit.slice(0, 12)} is not on origin/main`); }
const { decodeMrms, codeOf, cellLatLon } = await import(pathToFileURL(join(proxy, "src", "wx", "mrms.ts")).href);
const { quantise, cropCellBox, encodeBox } = await import(pathToFileURL(join(proxy, "src", "wx", "crop.ts")).href);
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const iso = (d: Date) => d.toISOString().replace(/\.\d{3}Z$/, "Z");   // no fraction: it would look like a coordinate

// The records, as committed on main.
const manifest = JSON.parse(git("show", "origin/main:test/fixtures/wx/bend-loop/MANIFEST.json"));
const pkg = execFileSync("git", ["-C", repo, "show", "origin/main:test/fixtures/wx/basemap/bend.bmpk"]);
const centre = { lat: pkg.readInt32LE(8) / 1e6, lon: pkg.readInt32LE(12) / 1e6 };   // never printed
type Frame = { noaaKey: string; gribSha256: string; frameEpoch: number };
const frames = manifest.frames as Frame[];
if (!Array.isArray(frames) || frames.length !== 15) stop(`#496's manifest lists ${frames?.length} frames, not 15`);

const R_KM = 161;
const NEAR_KM = 50;
const q = quantise(centre.lat, centre.lon, R_KM);
const BUCKET = "https://noaa-mrms-pds.s3.amazonaws.com";
mkdirSync(cache, { recursive: true });

function km(lat1: number, lon1: number, lat2: number, lon2: number): number {
  const rad = Math.PI / 180, dLat = (lat2 - lat1) * rad, dLon = (lon2 - lon1) * rad;
  const a = Math.sin(dLat / 2) ** 2 + Math.cos(lat1 * rad) * Math.cos(lat2 * rad) * Math.sin(dLon / 2) ** 2;
  return 6371 * 2 * Math.asin(Math.sqrt(a));
}

async function input(f: Frame): Promise<Uint8Array> {
  const file = join(cache, f.noaaKey.replace(/\//g, "__"));
  if (!existsSync(file)) {
    const r = await fetch(`${BUCKET}/${f.noaaKey}`);
    if (!r.ok) stop(`the archive answered ${r.status} for ${f.noaaKey.split("/").pop()}`);
    writeFileSync(file, new Uint8Array(await r.arrayBuffer()));
  }
  const gz = new Uint8Array(readFileSync(file));
  if (sha(gz) !== f.gribSha256) stop(`${f.noaaKey.split("/").pop()} does not hash to #496's gribSha256`);
  return gz;
}

async function decode(f: Frame) {
  const gz = await input(f);
  const rowCodes: Uint8Array[] = [];
  const grid = await decodeMrms(gz, (row: number, vals: Float32Array) => {
    const c = new Uint8Array(vals.length);
    for (let j = 0; j < vals.length; j++) c[j] = codeOf(vals[j] as number);   // SeamlessHSR has no missing value
    rowCodes[row] = c;
  });
  if (!grid.n2s || !grid.w2e) stop(`${f.noaaKey.split("/").pop()} is not north->south, west->east`);
  const epoch = Math.floor(Date.parse(grid.validTimeUtc) / 1000);
  if (epoch !== f.frameEpoch) stop(`${f.noaaKey.split("/").pop()}'s own valid time is not the record's`);
  const g = { ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj };
  const box = cropCellBox(g, q);
  const cellAt = (r: number, c: number) => (r < 0 || r >= g.nj || c < 0 || c >= g.ni ? 14 : rowCodes[r]![c]!);
  let near = 0, inCrop = 0;
  for (let r = box.r0; r < box.r0 + box.rows; r++)
    for (let c = box.c0; c < box.c0 + box.cols; c++) {
      const code = cellAt(r, c);
      if (code < 1 || code > 13) continue;
      inCrop++;
      const [la, lo] = cellLatLon(grid, r, c);
      if (km(q.lat, q.lon, la, lo) <= NEAR_KM) near++;
    }
  return { g, epoch, box, cellAt, near, inCrop };
}

// The rule: most echo within 50 km, ties to the earliest (the manifest's order is time order; checked).
for (let i = 1; i < frames.length; i++)
  if (frames[i]!.frameEpoch <= frames[i - 1]!.frameEpoch) stop("#496's frames are not in time order");
let best = -1, bestNear = -1;
const counts: Array<{ near: number; inCrop: number }> = [];
for (let i = 0; i < frames.length; i++) {
  const d = await decode(frames[i]!);
  counts.push({ near: d.near, inCrop: d.inCrop });
  say(`  ${frames[i]!.noaaKey.split("/").pop()}: echo cells within ${NEAR_KM} km ${d.near}, in the crop ${d.inCrop}`);
  if (d.near > bestNear) { best = i; bestNear = d.near; }
}
if (bestNear <= 0) stop(`no frame has echo within ${NEAR_KM} km of the point`);

const f = frames[best]!;
const d = await decode(f);
const e = encodeBox(d.g, d.epoch, d.box, d.cellAt);
const made = new Date();
const stamp = iso(made).replace(/[-:]/g, "");
mkdirSync(outDir, { recursive: true });
const cropPath = join(outDir, `REPLAY-${stamp}-cref.wxc1`);
writeFileSync(cropPath, e.bytes);
const sidecar = {
  replay: true,
  what: "the wx replay's crop (docs/wx-device.md, RC1): one archive frame, encoded by main's own crop path, for the bench only; never committed or uploaded",
  source: { archive: "the public NOAA MRMS archive (noaa-mrms-pds), SeamlessHSR_00.00", key: f.noaaKey, date: "2024-06-02" },
  frameValidUtc: iso(new Date(f.frameEpoch * 1000)),
  frameEpoch: f.frameEpoch,
  encoder: `main's proxy/src/wx/mrms.ts and proxy/src/wx/crop.ts at ${commit}`,
  gribSha256: f.gribSha256,
  gribShaRecord: "#496's committed test/fixtures/wx/bend-loop/MANIFEST.json",
  cropSha256: sha(e.bytes),
  bytes: e.bytes.length, coarsen: e.coarsen, rows: e.rows, cols: e.cols, rKm: q.rKm,
  point: "Bend: the site feed's public point at 2 dp (the committed test/fixtures/wx/basemap/bend.bmpk's origin)",
  echo: { withinKm: NEAR_KM, near: bestNear, inCrop: counts[best]!.inCrop },
  selection: `of #496's 15 frames, the most echo cells (codes 1-13) within ${NEAR_KM} km of the point, ties to the earliest`,
  made: iso(made),
};
const sjson = JSON.stringify(sidecar, null, 1) + "\n";
writeFileSync(join(outDir, `REPLAY-${stamp}.json`), sjson);
say(`chose ${f.noaaKey.split("/").pop()}: ${e.bytes.length} B, coarsen ${e.coarsen}, ${e.rows} x ${e.cols}, ` +
    `crop sha256 ${sha(e.bytes).slice(0, 12)}; encoder at ${commit.slice(0, 7)}`);
say(`wrote REPLAY-${stamp}-cref.wxc1 and REPLAY-${stamp}.json`);

// RC1's coordinate scan, of this tool's own output and its sidecar, in memory: the point's own values in every form
// a print could take, and anything coordinate-shaped. Booleans only.
const text = said.join("\n") + "\n" + sjson;
const forms = [q.lat2, q.lon2, centre.lat.toFixed(2), centre.lon.toFixed(2), centre.lat.toFixed(1), centre.lon.toFixed(1),
               String(Math.round(centre.lat * 1e6)), String(Math.round(centre.lon * 1e6))];
const exact = forms.some((s) => text.includes(s));
// Microdegrees are 8-9 digits; so is every archive file's yyyymmdd. A date is counted and set aside, never printed;
// the point's own microdegrees are in `forms` above, whatever their shape.
const date = /^20\d{2}(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])$/;
const long = [...text.matchAll(/(?:^|[^0-9A-Za-z])(-?\d{8,9})(?=[^0-9A-Za-z]|$)/g)].map((m) => m[1]!);
const dates = long.filter((s) => date.test(s)).length;
const shaped = /-?\b\d{1,3}\.\d{2,}\b/.test(text) || long.length > dates;
console.log(`RC1 scan: the point's values in the output or sidecar: ${exact ? "YES" : "no"}; ` +
            `a coordinate-shaped number: ${shaped ? "YES" : "no"} (8-9 digit dates set aside: ${dates})`);
process.exit(exact || shaped ? 1 : 0);
