// Node wrapper for tools/wx/mrms.ts (A1). Decodes one MRMS .grib2.gz and writes, next to <outPrefix>:
//   <outPrefix>.f32    every cell's decoded value, float32 little-endian, file row order (7000 x 3500 for CONUS)
//   <outPrefix>.codes  every cell's wire code (uint8: 0, 1-13, 14, 15)
//   <outPrefix>.json   the grid header, timing, and 1,000 sampled cells with their lat/lon
//
//   node --experimental-strip-types tools/wx/decode-cli.ts <file.grib2.gz> <outPrefix> [--missing=<v>] [--sabotage-shift]
//
// --sabotage-shift: A1's sabotage -- every output row r holds decoded row r+1 (the last row repeats).
import { readFileSync, writeFileSync } from "node:fs";
import { decodeMrms, codeOf, cellLatLon } from "./mrms.ts";

const [, , inPath, outPrefix, ...flags] = process.argv;
if (!inPath || !outPrefix) { console.error("usage: decode-cli.ts <file.grib2.gz> <outPrefix> [--missing=<v>] [--sabotage-shift]"); process.exit(2); }
const shift = flags.includes("--sabotage-shift");
const mflag = flags.find((f) => f.startsWith("--missing="));
const missing = mflag ? Number(mflag.slice(10)) : undefined;

const gz = new Uint8Array(readFileSync(inPath));
const t0 = performance.now();
let f32: Float32Array | null = null, codes: Uint8Array | null = null, ni = 0;
const grid = await decodeMrms(gz, (row, vals) => {
  if (!f32) { ni = vals.length; f32 = new Float32Array(ni * 3500); codes = new Uint8Array(ni * 3500); }
  const target = shift ? row - 1 : row;          // sabotage: decoded row r lands in output row r-1
  if (target < 0) return;
  f32!.set(vals, target * ni);
  for (let j = 0; j < ni; j++) codes![target * ni + j] = codeOf(vals[j], missing);
});
if (shift) { // the last output row has nothing shifted into it: repeat the row above, as a shift would
  const last = (grid.nj - 1) * ni;
  f32!.copyWithin(last, last - ni, last); codes!.copyWithin(last, last - ni, last);
}
if (grid.nj !== 3500) { console.error(`grid has ${grid.nj} rows; this CLI allocates for CONUS (3500) -- refusing`); process.exit(2); }
const ms = performance.now() - t0;
writeFileSync(outPrefix + ".f32", Buffer.from(f32!.buffer));
writeFileSync(outPrefix + ".codes", Buffer.from(codes!.buffer));
// 1,000 sampled cells, deterministic (a fixed-stride walk), with their lat/lon from the decoder's header
const samples: { idx: number; row: number; col: number; lat: number; lon: number }[] = [];
const total = grid.ni * grid.nj;
for (let k = 0; k < 1000; k++) {
  const idx = Math.floor(((k * 2654435761) % 4294967296) / 4294967296 * total);
  const row = Math.floor(idx / grid.ni), col = idx % grid.ni;
  const [lat, lon] = cellLatLon(grid, row, col);
  samples.push({ idx, row, col, lat, lon });
}
writeFileSync(outPrefix + ".json", JSON.stringify({ grid, decodeMs: Math.round(ms), sabotageShift: shift, missing: missing ?? null, samples }, null, 1));
console.log(`decoded ${inPath.split(/[\\/]/).pop()}: ${grid.ni}x${grid.nj} valid ${grid.validTimeUtc} in ${Math.round(ms)} ms${shift ? " [SABOTAGE: rows shifted by one]" : ""}`);
