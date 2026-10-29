// 3.5b's Bend radar day and window, by the rule frozen in docs/weatherscope-stage3.md A10.4, with MAIN's decode
// (proxy/src/wx/mrms.ts) and crop path (proxy/src/wx/crop.ts), loaded from a checkout given on the command line and
// never copied or changed, and the DEVICE's own per-frame measures (frame_stats, built from WxRender.h + WxLogic.h).
//
//   npx tsx pick_bend.mts <proxy-dir> <cache-dir> <bend.bmpk> <frame_stats exe>
//
// The rule (A10.4), as frozen:
//   data   MRMS MultiSensor_QPE_24H_Pass2_00.00 at 12:00Z, every day 2024-05-01 .. 2024-09-30
//          (no product for those dates: RadarOnly_QPE_24H_00.00, by the same rule)
//   score  the number of cells within 15 mi of the centre with >= 2.5 mm
//   cands  the 5 highest scores, ties to the earlier day
//   window on each candidate in score order, over the 24 h its QPE covers: the FIRST 15-frame window in time order,
//          SeamlessHSR, every second object, where (a) every frame has an echo within 30 mi, (b) at least one has an
//          echo within 15 mi, (c) every frame has >= 50 % of its on-disc map pixels dry at 60 mi, (d) every frame
//          has 0 snow-flagged cells within 60 mi by PrecipFlag_00.00 at its own time (A12.9: added after the
//          2024-05-02 05:52-06:48Z window, chosen without it, held 19,427 snow cells; precipflag.py reads it)
//   stop   no window on all 5 days: stop and report; no criterion relaxed without a ruling
//
// The centre is bend.bmpk's own origin (its header), the site feed's point at 2 dp. It is printed nowhere; distances
// from it are. Readings this tool makes are marked READING.
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { gunzipSync } from "node:zlib";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const [proxy, cache, pkgPath, statsExe] = process.argv.slice(2).map((a) => resolve(a)) as [string, string, string, string];
if (!statsExe) { console.error("usage: pick_bend.mts <proxy-dir> <cache-dir> <bend.bmpk> <frame_stats exe>"); process.exit(2); }
const { decodeMrms, codeOf, parseGrib2 } = await import(pathToFileURL(join(proxy, "src", "wx", "mrms.ts")).href);
const { quantise, cropCellBox, encodeBox } = await import(pathToFileURL(join(proxy, "src", "wx", "crop.ts")).href);
const commit = execFileSync("git", ["-C", proxy, "rev-parse", "HEAD"]).toString().trim();
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const BUCKET = "https://noaa-mrms-pds.s3.amazonaws.com";
const KM_PER_MI = 1.609344, KM_PER_DEG = 111.0;

const pkg = readFileSync(pkgPath);
const centre = { lat: pkg.readInt32LE(8) / 1e6, lon: pkg.readInt32LE(12) / 1e6 };   // never printed
mkdirSync(cache, { recursive: true });

async function get(key: string): Promise<{ bytes: Uint8Array; sha256: string } | null> {
  const path = join(cache, key.replace(/\//g, "__"));
  if (existsSync(path)) { const b = new Uint8Array(readFileSync(path)); return { bytes: b, sha256: sha(b) }; }
  const r = await fetch(`${BUCKET}/${key}`);
  if (r.status === 404) return null;
  if (r.status !== 200) throw new Error(`${key}: HTTP ${r.status}`);
  const b = new Uint8Array(await r.arrayBuffer());
  if (b[0] !== 0x1f || b[1] !== 0x8b) throw new Error(`${key}: not gzip`);
  writeFileSync(path, b);
  return { bytes: b, sha256: sha(b) };
}

async function list(prefix: string): Promise<string[]> {
  const keys: string[] = [];
  let token: string | null = null;
  for (;;) {
    const url = `${BUCKET}/?list-type=2&prefix=${prefix}&max-keys=1000` + (token ? `&continuation-token=${encodeURIComponent(token)}` : "");
    const body = await (await fetch(url)).text();
    keys.push(...[...body.matchAll(/<Key>([^<]+)<\/Key>/g)].map((m) => m[1]!));
    const t = /<NextContinuationToken>([^<]+)<\/NextContinuationToken>/.exec(body);
    if (!t) return keys;
    token = t[1]!;
  }
}

// ---------------------------------------------------------------------------------------------------- the score
// READING: "within 15 mi of the centre" is WxLogic's PlanarKm from the centre to the cell centre (111 km a degree,
// cos of the centre's latitude), <= 15 mi; a cell's value is the decoded millimetres, >= 2.5.
async function score(product: string, day: string): Promise<{ key: string; sha256: string; score: number } | null> {
  const key = `CONUS/${product}/${day}/MRMS_${product}_${day}-120000.grib2.gz`;
  const f = await get(key);
  if (!f) return null;
  const { grid } = parseGrib2(gunzipSync(f.bytes));
  if (!grid.w2e) throw new Error(`${key} is not west->east`);
  const kx = KM_PER_DEG * Math.cos(centre.lat * (Math.PI / 180)), lim = 15 * KM_PER_MI;
  let n = 0;
  await decodeMrms(f.bytes, (row: number, vals: Float32Array) => {
    const lat = grid.n2s ? grid.la1 - row * grid.dj : grid.la1 + row * grid.dj;
    const dy = (lat - centre.lat) * KM_PER_DEG;
    if (Math.abs(dy) > lim) return;
    for (let c = 0; c < vals.length; c++) {
      const dx = (grid.lo1 + c * grid.di - centre.lon) * kx;
      if (dx * dx + dy * dy <= lim * lim && (vals[c] as number) >= 2.5) n++;
    }
  });
  return { key, sha256: f.sha256, score: n };
}

const days: string[] = [];
for (let t = Date.UTC(2024, 4, 1); t <= Date.UTC(2024, 8, 30); t += 86400e3) days.push(new Date(t).toISOString().slice(0, 10).replace(/-/g, ""));

let product = "MultiSensor_QPE_24H_Pass2_00.00";
let scores: { day: string; key: string; sha256: string; score: number }[] = [];
const missing: string[] = [];
for (const d of days) {
  const s = await score(product, d);
  if (s) scores.push({ day: d, ...s }); else missing.push(d);
}
console.log(`scored ${scores.length} of ${days.length} days with ${product}; missing ${missing.length}${missing.length ? ": " + missing.join(" ") : ""}`);
if (scores.length === 0) {
  product = "RadarOnly_QPE_24H_00.00";
  console.log(`no ${"MultiSensor"} product at all: the escape hatch, ${product}`);
  for (const d of days) { const s = await score(product, d); if (s) scores.push({ day: d, ...s }); else missing.push(d); }
}
const ranked = [...scores].sort((a, b) => b.score - a.score || (a.day < b.day ? -1 : 1));
const cands = ranked.slice(0, 5);
console.log("top 10 by score: " + ranked.slice(0, 10).map((s) => `${s.day} ${s.score}`).join(", "));
console.log("candidates: " + cands.map((s) => `${s.day} (${s.score})`).join(", "));

// ---------------------------------------------------------------------------------------------- the window
const q = quantise(centre.lat, centre.lon, 60 * KM_PER_MI);
type Stat = { key: string; sha256: string; valid: string; nearestMi: number; onDisc: number; dry: number; snow?: number | null };
const READER = join(dirname(fileURLToPath(import.meta.url)), "precipflag.py");
let missingFlags = 0;
function snowOf(s: Stat): number | null {   // (d): the frame's own time; null when the object is missing
  if (s.snow !== undefined) return s.snow;
  const stamp = /_(\d{8}-\d{6})\.grib2\.gz$/.exec(s.key)![1]!;
  const out = execFileSync("python", ["-I", READER, pkgPath, join(cache, "pf"), "--snow", stamp]).toString().trim();
  s.snow = out === "missing" ? null : Number(/^snow (\d+)$/.exec(out)![1]);
  if (s.snow === null) missingFlags++;
  writeFileSync(statsPath, JSON.stringify([...statCache.values()]));
  return s.snow;
}
const statCache = new Map<string, Stat>();
const statsPath = join(cache, "frame-stats.json");
if (existsSync(statsPath)) for (const s of JSON.parse(readFileSync(statsPath, "utf8")) as Stat[]) statCache.set(s.key, s);

async function stat(key: string, valid: string): Promise<Stat> {
  const hit = statCache.get(key);
  if (hit) return hit;
  const f = (await get(key))!;
  const rowCodes: Uint8Array[] = [];
  const grid = await decodeMrms(f.bytes, (row: number, vals: Float32Array) => {
    const c = new Uint8Array(vals.length);
    for (let j = 0; j < vals.length; j++) c[j] = codeOf(vals[j] as number);   // SeamlessHSR has no missing value
    rowCodes[row] = c;
  });
  if (!grid.n2s || !grid.w2e) throw new Error(`${key} is not north->south, west->east`);
  const g = { ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj };
  const box = cropCellBox(g, q);
  const cellAt = (r: number, c: number) => (r < 0 || r >= g.nj || c < 0 || c >= g.ni ? 14 : rowCodes[r]![c]!);
  const epoch = Math.floor(Date.parse(grid.validTimeUtc) / 1000);
  const e = encodeBox(g, epoch, box, cellAt);
  const tmp = join(cache, "frame.wxc");
  writeFileSync(tmp, e.bytes);
  const [near, on, dry] = execFileSync(statsExe, [tmp]).toString().trim().split(/\s+/).map(Number) as [number, number, number];
  const s: Stat = { key, sha256: f.sha256, valid, nearestMi: near, onDisc: on, dry };
  statCache.set(key, s);
  writeFileSync(statsPath, JSON.stringify([...statCache.values()]));
  return s;
}

const NAME = /_(\d{8})-(\d{6})\.grib2\.gz$/;
const validOf = (k: string) => { const m = NAME.exec(k)!; return `${m[1]!.slice(0, 4)}-${m[1]!.slice(4, 6)}-${m[1]!.slice(6)}T${m[2]!.slice(0, 2)}:${m[2]!.slice(2, 4)}:${m[2]!.slice(4)}Z`; };
let chosen: { day: string; frames: Stat[] } | null = null;
const tried: Record<string, unknown>[] = [];
for (const c of cands) {
  const end = Date.UTC(+c.day.slice(0, 4), +c.day.slice(4, 6) - 1, +c.day.slice(6), 12);
  const prev = new Date(end - 86400e3).toISOString().slice(0, 10).replace(/-/g, "");
  const keys = [...await list(`CONUS/SeamlessHSR_00.00/${prev}/`), ...await list(`CONUS/SeamlessHSR_00.00/${c.day}/`)]
    .filter((k) => NAME.test(k))
    .filter((k) => { const t = Date.parse(validOf(k)); return t > end - 86400e3 && t <= end; })   // READING: (D-1 12Z, D 12Z]
    .sort((a, b) => (validOf(a) < validOf(b) ? -1 : 1));
  let found = -1, evaluated = 0;
  for (let i = 0; i + 28 < keys.length && found < 0; i++) {
    let ok = true, near15 = false;
    for (let k = 0; k < 15 && ok; k++) {
      const s = await stat(keys[i + 2 * k]!, validOf(keys[i + 2 * k]!));
      evaluated = statCache.size;
      const a = s.nearestMi >= 0 && s.nearestMi <= 30;
      const cc = s.dry * 2 >= s.onDisc;   // >= 50 %, in integers
      if (s.nearestMi >= 0 && s.nearestMi <= 15) near15 = true;
      ok = a && cc && snowOf(s) === 0;   // (d) last: it costs a decode, and order cannot change the result
    }
    if (ok && near15) found = i;
  }
  tried.push({ day: c.day, objects: keys.length, firstWindowStart: found >= 0 ? validOf(keys[found]!) : null });
  console.log(`candidate ${c.day}: ${keys.length} objects over its 24 h; ${found >= 0 ? `window from ${validOf(keys[found]!)}` : "no window qualifies"} (${evaluated} frames measured so far)`);
  if (found >= 0) {
    chosen = { day: c.day, frames: Array.from({ length: 15 }, (_, k) => statCache.get(keys[found + 2 * k]!)!) };
    break;
  }
}

const record = {
  what: "Weatherscope 3.5b's Bend radar day and window, by docs/weatherscope-stage3.md A10.4's rule",
  decoder: `main's proxy/src/wx/mrms.ts and crop.ts at ${commit}; per-frame measures by frame_stats (WxRender.h, WxLogic.h)`,
  criterionD: "0 snow-flagged cells within 60 mi in every frame (A12.9); reason: 2024-05-02 05:52-06:48Z, chosen without it, held 1,077-1,574 per frame, 19,427 in all",
  missingPrecipFlag: missingFlags,
  product, missingDays: missing, scores: scores.map(({ day, score, key, sha256 }) => ({ day, score, key, sha256 })),
  candidates: cands.map((s) => s.day), tried,
  chosen: chosen && { day: chosen.day, frames: chosen.frames.map(({ key, sha256, valid, nearestMi, dry, onDisc, snow }) => ({ key, sha256, valid, nearestMi, dryPct: +(100 * dry / onDisc).toFixed(2), snow })) },
};
writeFileSync(join(cache, "selection.json"), JSON.stringify(record, null, 1) + "\n");
if (!chosen) { console.log("STOP (A10.4): no window qualifies on any of the 5 candidate days; no criterion relaxed without a ruling"); process.exit(3); }
console.log(`CHOSEN: ${chosen.day}, the window ${chosen.frames[0]!.valid} .. ${chosen.frames[14]!.valid}`);
for (const f of chosen.frames) console.log(`  ${f.key.split("/").pop()}  sha256 ${f.sha256.slice(0, 12)}  nearest ${f.nearestMi.toFixed(1)} mi  dry ${(100 * f.dry / f.onDisc).toFixed(1)} %  snow ${f.snow}`);
console.log(`PrecipFlag objects missing among the frames tested: ${missingFlags}`);
