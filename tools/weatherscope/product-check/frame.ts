// Weatherscope Stage 2, step 2.0: decode ONE MRMS file in memory and keep only what the check measures.
//   node --experimental-strip-types frame.ts <spikeDir> <airports.txt> <file.grib2.gz> <outDir> <label> <ICAO[:crop][:sab]>...
// Writes <outDir>/<label>.json (validTime, sentinel census, the centre cell per airport) and, per airport
// asked, <outDir>/<ICAO>-<label>.wxc (the spike's encodeCrop, 60 mi) and -sab.wxc (the -5 dBZ sabotage).
// The 98 MB value grid never touches disk. Coordinates are read from the vetted list and never printed.
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { pathToFileURL } from "node:url";

const [, , spike, airportsPath, gzPath, outDir, label, ...specs] = process.argv;
if (!label || specs.length === 0) { console.error("usage: frame.ts <spike> <airports> <gz> <outDir> <label> <ICAO[:crop][:sab]>..."); process.exit(2); }
const { decodeMrms, codeOf } = await import(pathToFileURL(`${spike}/tools/wx/mrms.ts`).href);
const { encodeCrop } = await import(pathToFileURL(`${spike}/tools/wx/crop.ts`).href);

const pos = new Map<string, [number, number]>();
for (const line of readFileSync(airportsPath, "utf8").split(/\r?\n/)) {
  const f = line.trim().split(/\s+/); if (f.length >= 3) pos.set(f[0], [Number(f[1]), Number(f[2])]);
}

let f32: Float32Array | null = null, ni = 0, rowsSeen = 0;
const grid = await decodeMrms(new Uint8Array(readFileSync(gzPath)), (row: number, vals: Float32Array) => {
  if (!f32) { ni = vals.length; f32 = new Float32Array(ni * 3500); }
  f32!.set(vals, row * ni); rowsSeen++;
});
if (!grid.n2s || !grid.w2e || grid.ni !== ni || rowsSeen !== grid.nj) { console.error("REFUSED: unexpected grid scan or size"); process.exit(3); }
const nj = grid.nj, n = ni * nj, v = f32!.subarray(0, n);

// The anchor: every value is -999, -99, or a reflectivity of at least -30 dBZ.
const census = { nocov999: 0, noecho99: 0, below15: 0, echo: 0, odd: 0, oddMin: null as number | null, realMin: Infinity, realMax: -Infinity };
const codes = new Uint8Array(n), sab = new Uint8Array(n);
for (let i = 0; i < n; i++) {
  const x = v[i];
  if (x === -999) census.nocov999++;
  else if (x === -99) census.noecho99++;
  else if (x < -30) { census.odd++; census.oddMin = census.oddMin === null ? x : Math.min(census.oddMin, x); }
  else { if (x < 15) census.below15++; else census.echo++; if (x < census.realMin) census.realMin = x; if (x > census.realMax) census.realMax = x; }
  codes[i] = codeOf(x);
  sab[i] = (x === -999 || x === -99) ? codeOf(x) : codeOf(x - 5);     // the -5 dBZ sabotage (sentinels untouched)
}
const frameEpoch = Math.floor(Date.parse(grid.validTimeUtc) / 1000);
const g = { codes, ni, nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj, frameEpoch };
const km = 60 * 1.609344;
mkdirSync(outDir, { recursive: true });
const out: any = { validTimeUtc: grid.validTimeUtc, census, centres: {}, crops: {} };
for (const spec of specs) {
  const [icao, ...opts] = spec.split(":");
  const p = pos.get(icao); if (!p) { console.error(`REFUSED: ${icao} is not on the vetted list`); process.exit(4); }
  const [lat, lon] = p;
  const r = Math.round((grid.la1 - lat) / grid.dj), c = Math.round((lon - grid.lo1) / grid.di);
  out.centres[icao] = { raw: v[r * ni + c], code: codes[r * ni + c] };
  if (opts.includes("crop")) {
    const e = encodeCrop(g, lat, lon, km);
    writeFileSync(`${outDir}/${icao}-${label}.wxc`, e.bytes);
    out.crops[icao] = { bytes: e.bytes.length, coarsen: e.coarsen, flagged: e.flagged };
  }
  if (opts.includes("sab")) {
    const e = encodeCrop({ ...g, codes: sab }, lat, lon, km);
    writeFileSync(`${outDir}/${icao}-${label}-sab.wxc`, e.bytes);
    out.crops[icao + "-sab"] = { bytes: e.bytes.length, coarsen: e.coarsen, flagged: e.flagged };
  }
}
writeFileSync(`${outDir}/${label}.json`, JSON.stringify(out, null, 1));
console.log(`${label}: ${grid.validTimeUtc} census ok=${census.odd === 0} crops=${Object.keys(out.crops).length}`);
