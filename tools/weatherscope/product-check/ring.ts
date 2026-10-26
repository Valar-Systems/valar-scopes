// Weatherscope Stage 2, (e): the snow ring (docs/weatherscope-stage2.md, addendum 6; frozen before this ran).
//   node --experimental-strip-types ring.ts <spikeDir> <airports.txt> <ICAO> <file.grib2.gz>...
// For each MRMS file: the census at the -40 floor, and the MAXIMUM raw value over the grid cells whose centre lies
// within 15 mi of the airport by the device's flat projection (111.0 km per degree; longitude scaled by the cosine
// of the airport's latitude). -99 and -999 are sentinels, not values. One JSON line per file; never a coordinate.
import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
const [, , spike, airportsPath, icao, ...files] = process.argv;
const { decodeMrms } = await import(pathToFileURL(`${spike}/tools/wx/mrms.ts`).href);
let lat = NaN, lon = NaN;
for (const line of readFileSync(airportsPath, "utf8").split(/\r?\n/)) { const f = line.trim().split(/\s+/); if (f[0] === icao) { lat = +f[1]; lon = +f[2]; } }
if (!Number.isFinite(lat)) { console.error(`REFUSED: ${icao} is not on the vetted list`); process.exit(4); }
const R_KM = 15 * 1.609344, KM_PER_DEG = 111.0, COS = Math.cos(lat * Math.PI / 180);
for (const gzPath of files) {
  let f32: Float32Array | null = null, ni = 0;
  const g = await decodeMrms(new Uint8Array(readFileSync(gzPath)), (row: number, vals: Float32Array) => {
    if (!f32) { ni = vals.length; f32 = new Float32Array(ni * 3500); } f32!.set(vals, row * ni); });
  const v = f32!.subarray(0, ni * g.nj);
  let odd = 0;
  for (let i = 0; i < v.length; i++) { const x = v[i]; if (x !== -999 && x !== -99 && x < -40) odd++; }
  // Only rows/cols that can be within the ring are visited; every cell's own centre is tested.
  const dLat = R_KM / KM_PER_DEG, dLon = R_KM / (KM_PER_DEG * COS);
  const r0 = Math.max(0, Math.floor((g.la1 - (lat + dLat)) / g.dj) - 1), r1 = Math.min(g.nj - 1, Math.ceil((g.la1 - (lat - dLat)) / g.dj) + 1);
  const c0 = Math.max(0, Math.floor((lon - dLon - g.lo1) / g.di) - 1), c1 = Math.min(ni - 1, Math.ceil((lon + dLon - g.lo1) / g.di) + 1);
  let cells = 0, max: number | null = null, sent = 0;
  for (let r = r0; r <= r1; r++) for (let c = c0; c <= c1; c++) {
    const cl = g.la1 - r * g.dj, cn = g.lo1 + c * g.di;
    const dy = (cl - lat) * KM_PER_DEG, dx = (cn - lon) * KM_PER_DEG * COS;
    if (dx * dx + dy * dy > R_KM * R_KM) continue;
    cells++;
    const x = v[r * ni + c];
    if (x === -999 || x === -99) { sent++; continue; }
    if (max === null || x > max) max = x;
  }
  console.log(JSON.stringify({ file: gzPath.split(/[\/]/).pop(), validTimeUtc: g.validTimeUtc, census_ok: odd === 0, ringCells: cells, sentinels: sent, max }));
}
