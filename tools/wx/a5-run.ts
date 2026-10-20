// A5: coverage. Decode one frame with the ruled mapping and the two sabotage mappings, crop each test point
// through the real encoder, and report each crop's code shares and its centre cell's code. Coordinates are
// passed in, never printed.
//   node --experimental-strip-types tools/wx/a5-run.ts <frame.grib2.gz>
import { readFileSync } from "node:fs";
import { decodeMrms } from "./mrms.ts";
import { encodeCrop, decodeCrop, type CodeGrid } from "./crop.ts";

const RULED = (v: number) => v === -999 ? 14 : v < 15 ? 0 : Math.min(13, 1 + Math.floor((v - 15) / 5));
const SAB_NOCOV_DRY = (v: number) => v < 15 ? 0 : Math.min(13, 1 + Math.floor((v - 15) / 5));            // -999 -> dry
const SAB_NOECHO_MISSING = (v: number) => v === -999 ? 14 : v === -99 ? 15 : RULED(v);                     // -99 -> 15
const POINTS: [string, number, number, number][] = [
  ["mid-Pacific (outside grid)", 30.0, -145.0, 40], ["London LHR (outside grid)", 51.47, -0.45, 40],
  ["Atlantic patch (inside grid)", 30.0, -65.0, 40], ["Vancouver YVR", 49.19, -123.18, 40],
  ["Toronto YYZ", 43.68, -79.63, 40], ["Montreal YUL", 45.47, -73.74, 40], ["Iowa quiet box", 42.0, -93.5, 100],
];
const gz = new Uint8Array(readFileSync(process.argv[2]));
for (const [label, map] of [["RULED", RULED], ["SABOTAGE -999 as dry", SAB_NOCOV_DRY], ["SABOTAGE -99 as 15", SAB_NOECHO_MISSING]] as const) {
  const codes = new Uint8Array(7000 * 3500);
  const grid = await decodeMrms(gz, (row, vals) => { for (let j = 0; j < vals.length; j++) codes[row * vals.length + j] = map(vals[j]); });
  const g: CodeGrid = { codes, ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj, frameEpoch: 0 };
  console.log(`== mapping: ${label} (frame ${grid.validTimeUtc})`);
  for (const [name, lat, lon, r] of POINTS) {
    const c = decodeCrop(encodeCrop(g, lat, lon, r).bytes);
    const n = c.codes.length, cnt = new Map<number, number>();
    for (const x of c.codes) cnt.set(x, (cnt.get(x) ?? 0) + 1);
    const share = (k: number) => (100 * (cnt.get(k) ?? 0) / n).toFixed(1);
    const centre = c.codes[Math.floor(c.rows / 2) * c.cols + Math.floor(c.cols / 2)];
    let echo = 0; for (let k = 1; k <= 13; k++) echo += cnt.get(k) ?? 0;
    console.log(`  ${name.padEnd(30)} centre=${String(centre).padStart(2)}  0:${share(0).padStart(5)}%  echo:${(100 * echo / n).toFixed(1).padStart(5)}%  14:${share(14).padStart(5)}%  15:${share(15).padStart(5)}%`);
  }
}
