// A6: freshness. Poll the live MRMS folder every 30 s for DURATION seconds; for each new frame, download,
// decode and crop (3 public airports from the A2 list, 80 km), and log one JSON line:
//   { key, frameT, lastModT, seenT, readyT, lagS = readyT - frameT }
// The skip-every-other sabotage is computed from this log afterwards (a skipped frame's crop is the next
// processed frame's), so the sabotage and the measurement see the same frames.
//
//   node --experimental-strip-types tools/wx/a6-poll.ts <durationS> <out.jsonl>
import { readFileSync, appendFileSync, writeFileSync } from "node:fs";
import { decodeMrms, codeOf } from "./mrms.ts";
import { encodeCrop, type CodeGrid } from "./crop.ts";

const BUCKET = "https://noaa-mrms-pds.s3.amazonaws.com";
const PROD = "CONUS/MergedReflectivityQCComposite_00.50";
const [, , durS, outPath] = process.argv;
const air = readFileSync(new URL("./airports-a2.txt", import.meta.url), "utf8").trim().split(/\r?\n/).slice(0, 50)
  .filter((_, i) => i % 17 === 0).map((l) => { const [id, lat, lon] = l.trim().split(/\s+/); return { id, lat: +lat, lon: +lon }; });
const frameTime = (k: string) => { const m = k.match(/_(\d{8})-(\d{6})\.grib2/)!; return Date.parse(`${m[1].slice(0,4)}-${m[1].slice(4,6)}-${m[1].slice(6,8)}T${m[2].slice(0,2)}:${m[2].slice(2,4)}:${m[2].slice(4,6)}Z`); };
writeFileSync(outPath, "");
const seen = new Set<string>();
const codes = new Uint8Array(7000 * 3500);
const end = Date.now() + Number(durS) * 1000;
let first = true;
while (Date.now() < end) {
  const day = new Date().toISOString().slice(0, 10).replace(/-/g, "");
  const x = await (await fetch(`${BUCKET}/?list-type=2&prefix=${PROD}/${day}/&start-after=${encodeURIComponent(`${PROD}/${day}/MRMS_MergedReflectivityQCComposite_00.50_${day}-${new Date(Date.now() - 20 * 60000).toISOString().slice(11, 19).replace(/:/g, "")}.grib2.gz`)}`)).text();
  const items = [...x.matchAll(/<Key>([^<]+)<\/Key><LastModified>([^<]+)<\/LastModified>/g)].map((m) => ({ key: m[1], lastMod: Date.parse(m[2]) }));
  for (const it of items) {
    if (seen.has(it.key)) continue;
    seen.add(it.key);
    if (first) continue;                     // frames already present at start are not freshness samples
    const seenT = Date.now();
    const gz = new Uint8Array(await (await fetch(`${BUCKET}/${it.key}`)).arrayBuffer());
    const grid = await decodeMrms(gz, (row, vals) => { for (let j = 0; j < vals.length; j++) codes[row * vals.length + j] = codeOf(vals[j]); });
    const g: CodeGrid = { codes, ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj, frameEpoch: Math.floor(frameTime(it.key) / 1000) };
    for (const a of air) encodeCrop(g, a.lat, a.lon, 80);
    const readyT = Date.now(), frameT = frameTime(it.key);
    appendFileSync(outPath, JSON.stringify({ key: it.key.split("/").pop(), frameT, lastModT: it.lastMod, seenT, readyT, lagS: (readyT - frameT) / 1000 }) + "\n");
  }
  first = false;
  await new Promise((r) => setTimeout(r, 30000));
}
console.log("A6 poll done");
