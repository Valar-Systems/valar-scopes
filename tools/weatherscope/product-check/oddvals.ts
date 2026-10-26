// Characterise the values that tripped 2.0's sentinel anchor (below -30 dBZ, not -999/-99), without grading
// anything: their distinct values and counts, the low tail just above -30 for comparison, and how many fall
// inside each 60 mi crop box. Prints counts only, never a coordinate.
//   node --experimental-strip-types oddvals.ts <spikeDir> <airports.txt> <file.grib2.gz> <ICAO>
import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
const [, , spike, airportsPath, gzPath, icao] = process.argv;
const { decodeMrms } = await import(pathToFileURL(`${spike}/tools/wx/mrms.ts`).href);
const { cropBox } = await import(pathToFileURL(`${spike}/tools/wx/crop.ts`).href);
let f32: Float32Array | null = null, ni = 0;
const grid = await decodeMrms(new Uint8Array(readFileSync(gzPath)), (row: number, vals: Float32Array) => {
  if (!f32) { ni = vals.length; f32 = new Float32Array(ni * 3500); } f32!.set(vals, row * ni); });
const n = ni * grid.nj, v = f32!.subarray(0, n);
const hist = new Map<number, number>();
for (let i = 0; i < n; i++) { const x = v[i]; if (x !== -999 && x !== -99 && x < -27) hist.set(x, (hist.get(x) ?? 0) + 1); }
const rows = [...hist.entries()].sort((a, b) => a[0] - b[0]);
console.log("values below -27 dBZ (sentinels excluded): " + rows.map(([x, c]) => `${x}:${c}`).join("  "));
let lat = 0, lon = 0;
for (const line of readFileSync(airportsPath, "utf8").split(/\r?\n/)) { const f = line.trim().split(/\s+/); if (f[0] === icao) { lat = +f[1]; lon = +f[2]; } }
const g = { codes: new Uint8Array(0), ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj, frameEpoch: 0 };
const box = cropBox(g as any, lat, lon, 60 * 1.609344);
let inBox = 0;
for (let r = box.r0; r < box.r0 + box.rows; r++) for (let c = box.c0; c < box.c0 + box.cols; c++) {
  if (r < 0 || r >= grid.nj || c < 0 || c >= ni) continue;
  const x = v[r * ni + c]; if (x !== -999 && x !== -99 && x < -30) inBox++; }
console.log(`below -30 dBZ inside ${icao}'s 60 mi crop box: ${inBox}`);
