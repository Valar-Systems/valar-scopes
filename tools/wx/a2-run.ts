// A2: crop sizes. Every MergedReflectivityQCComposite frame in [start, end) UTC, 50 public airports
// (tools/wx/airports-a2.txt), radius 40 / 80 / 160 km. Frames are downloaded, decoded in memory and dropped --
// nothing is kept on disk. Writes one JSON line per frame (sizes per airport and radius, capped and uncapped).
//
//   node --experimental-strip-types tools/wx/a2-run.ts <startISO> <endISO> <out.jsonl>
//   node --experimental-strip-types tools/wx/a2-run.ts --speckle <out.json>      (the sabotage frame)
import { readFileSync, appendFileSync, writeFileSync } from "node:fs";
import { decodeMrms, codeOf } from "./mrms.ts";
import { encodeCrop, decodeCrop, CAP_BYTES, type CodeGrid } from "./crop.ts";

const BUCKET = "https://noaa-mrms-pds.s3.amazonaws.com";
const PROD = "CONUS/MergedReflectivityQCComposite_00.50";
const RADII = [40, 80, 160];
const airports = readFileSync(new URL("./airports-a2.txt", import.meta.url), "utf8").trim().split(/\r?\n/)
  .map((l) => { const [id, lat, lon] = l.trim().split(/\s+/); return { id, lat: Number(lat), lon: Number(lon) }; });

async function listKeys(day: string): Promise<string[]> {
  const keys: string[] = []; let after = "";
  for (;;) {
    const u = `${BUCKET}/?list-type=2&prefix=${PROD}/${day}/${after ? `&start-after=${encodeURIComponent(after)}` : ""}`;
    const x = await (await fetch(u)).text();
    const ks = [...x.matchAll(/<Key>([^<]+)<\/Key>/g)].map((m) => m[1]);
    keys.push(...ks);
    if (!/<IsTruncated>true/.test(x) || ks.length === 0) break;
    after = ks[ks.length - 1];
  }
  return keys;
}
const frameTime = (k: string) => { const m = k.match(/_(\d{8})-(\d{6})\.grib2/)!; return Date.parse(`${m[1].slice(0,4)}-${m[1].slice(4,6)}-${m[1].slice(6,8)}T${m[2].slice(0,2)}:${m[2].slice(2,4)}:${m[2].slice(4,6)}Z`); };

// Round trip: decode(encode(crop)) must equal exactly the cells the encoder was given, bit for bit.
function roundTrip(e: { bytes: Uint8Array; cells: Uint8Array }): number {
  try {
    const d = decodeCrop(e.bytes).codes;
    if (d.length !== e.cells.length) return 0;
    for (let i = 0; i < d.length; i++) if (d[i] !== e.cells[i]) return 0;
    return 1;
  } catch { return 0; }
}

function measure(g: CodeGrid) {
  // `${id}:${r}` -> [capped bytes, coarsen, flagged, uncapped bytes, round-trip ok, SABOTAGE round-trip ok]
  const out: Record<string, number[]> = {};
  for (const a of airports) for (const r of RADII) {
    const c = encodeCrop(g, a.lat, a.lon, r);
    const u = encodeCrop(g, a.lat, a.lon, r, Infinity);
    const s = encodeCrop(g, a.lat, a.lon, r, CAP_BYTES, 0, /*longRunOffByOne=*/true);
    out[`${a.id}:${r}`] = [c.bytes.length, c.coarsen, c.flagged ? 1 : 0, u.bytes.length, roundTrip(c), roundTrip(s)];
  }
  return out;
}

if (process.argv[2] === "--speckle") {
  // SABOTAGE frame: alternating classes in every cell, the worst case for run-length coding.
  const ni = 7000, nj = 3500, codes = new Uint8Array(ni * nj);
  for (let r = 0; r < nj; r++) for (let c = 0; c < ni; c++) codes[r * ni + c] = ((r + c) & 1) ? 7 : 0;
  const g: CodeGrid = { codes, ni, nj, la1: 54.995, lo1: -129.995, di: 0.01, dj: 0.01, frameEpoch: 0 };
  writeFileSync(process.argv[3], JSON.stringify(measure(g)));
  console.log("speckle frame measured");
  process.exit(0);
}

const [, , startIso, endIso, outPath] = process.argv;
const t0 = Date.parse(startIso), t1 = Date.parse(endIso);
const days = new Set<string>();
for (let t = t0; t < t1 + 86400000; t += 86400000) days.add(new Date(t).toISOString().slice(0, 10).replace(/-/g, ""));
const keys = (await Promise.all([...days].map(listKeys))).flat().filter((k) => { const t = frameTime(k); return t >= t0 && t < t1; }).sort();
console.log(`${keys.length} frames in [${startIso}, ${endIso})`);
writeFileSync(outPath, "");
const codes = new Uint8Array(7000 * 3500);
let n = 0;
for (const k of keys) {
  const gz = new Uint8Array(await (await fetch(`${BUCKET}/${k}`)).arrayBuffer());
  const grid = await decodeMrms(gz, (row, vals) => { for (let j = 0; j < vals.length; j++) codes[row * vals.length + j] = codeOf(vals[j]); });
  const g: CodeGrid = { codes, ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj, frameEpoch: Math.floor(frameTime(k) / 1000) };
  let echo = 0; for (let i = 0; i < codes.length; i++) if (codes[i] >= 1 && codes[i] <= 13) echo++;
  appendFileSync(outPath, JSON.stringify({ key: k.split("/").pop(), gzBytes: gz.length, echoCells: echo, crops: measure(g) }) + "\n");
  if (++n % 25 === 0) console.log(`  ${n}/${keys.length}`);
}
console.log("done");
