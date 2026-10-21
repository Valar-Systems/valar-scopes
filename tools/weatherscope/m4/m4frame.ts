// WXC1 v2, measurement 4: one hour's counts (docs/wxc1-v2.md section 4 and Addenda 1-4, frozen before this ran).
//   node --experimental-strip-types m4frame.ts <spikeDir> <airports.txt> <ICAO> <hsr.gz> <comp.gz> <pf.gz> <out.json>
//
// Decodes the hour's three MRMS files in memory with the spike's decoder pinned by Stage 2 (a5db678, mrms.ts) and
// writes COUNTS ONLY. m4.py grades them, in the frozen order, and stops where a step says stop.
//
//   - packing (R, E, D) and grid metadata of all three files       (A4.1's precondition; M4d)
//   - the PrecipFlag census, and the census under A4.1's scale sabotage (R + 2X)   (C-M4a)
//   - SeamlessHSR's and the composite's raw centre values           (C-M4b, against Stage 2's run2)
//   - over Weatherscope's 60 mi crop: pooled-IoU counts of PrecipFlag > 0 against raw SeamlessHSR >= 5 dBZ at
//     (0,0), the 8 offsets at +-1, the 8 at +-10 and the N-S window flip; for the real grid and for C-M4c's three
//     sabotages (full-grid N-S flip; the window read 10 cells east; read 1 cell east)   (C-M4c, A2.1-A2.2)
//   - per product: echo cells, echo with PrecipFlag 3, echo with PrecipFlag 0 or -3 (M4e, M4f); snow cells and
//     snow with echo >= 15 dBZ (M4g); code- and type-layer bytes at coarsen 1 and the v1 rung (M4h, A4.3)
//
// OFFSET CONVENTION: offset (dr, dc) reads PrecipFlag at (r + dr, c + dc) for the crop cell (r, c). "The window
// read 10 cells east" reads PrecipFlag at (r, c + 10), so the offset that undoes it is (0, -10): 10 cells west.
//
// The decoder is mrms.ts, which refuses any PNG but 16-bit grey (mrms.ts:108); A4.1's field-width reasoning rests
// on that file. Coordinates come from the vetted list and are never printed or written.
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

const [, , spike, airportsPath, icao, hsrGz, compGz, pfGz, outPath] = process.argv;
if (!outPath) { console.error("usage: m4frame.ts <spike> <airports> <ICAO> <hsr.gz> <comp.gz> <pf.gz> <out.json>"); process.exit(2); }
const { decodeMrms, codeOf } = await import(pathToFileURL(`${spike}/tools/wx/mrms.ts`).href);
const { cropBox, encodeCrop } = await import(pathToFileURL(`${spike}/tools/wx/crop.ts`).href);
// Addendum 5: PrecipFlag is 8 bits per value, which mrms.ts refuses; pngdecode.ts is its row loop for 8 or 16.
const { loadDecoder } = await import(new URL("./pngdecode.ts", import.meta.url).href);
const decodeAnyWidth = await loadDecoder(spike);

let lat = NaN, lon = NaN;
for (const line of readFileSync(airportsPath, "utf8").split(/\r?\n/)) {
  const f = line.trim().split(/\s+/); if (f[0] === icao) { lat = +f[1]; lon = +f[2]; }
}
if (!Number.isFinite(lat)) { console.error(`REFUSED: ${icao} is not on the vetted list`); process.exit(4); }

const PF_TABLE = new Set([-3, 0, 1, 3, 6, 7, 10, 91, 96]);   // NSSL UserTable_MRMS_PrecipFlags.csv
const KM = 60 * 1.609344;                                     // the 60 mi crop, as Stage 2's frame.ts
const MAX_ROWS = 3500;

function refuse(msg: string): never { console.error(`REFUSED: ${msg}`); process.exit(3); }

async function decodeFull(gz: string) {
  let f32: Float32Array | null = null, ni = 0, rows = 0;
  const g = await decodeMrms(new Uint8Array(readFileSync(gz)), (row: number, vals: Float32Array) => {
    if (!f32) { ni = vals.length; f32 = new Float32Array(ni * MAX_ROWS); }
    if (row >= MAX_ROWS) refuse(`${gz}: more than ${MAX_ROWS} rows`);
    f32!.set(vals, row * ni); rows++;
  });
  if (rows !== g.nj || g.ni !== ni) refuse(`${gz}: decoded ${rows} rows of ${ni}, header says ${g.nj} of ${g.ni}`);
  const meta = { ni: g.ni, nj: g.nj, la1: g.la1, lo1: g.lo1, la2: g.la2, lo2: g.lo2, di: g.di, dj: g.dj,
                 w2e: g.w2e, n2s: g.n2s, R: g.R, E: g.E, D: g.D, bits: g.bits, validTimeUtc: g.validTimeUtc };
  return { v: f32!.subarray(0, g.ni * g.nj), meta, g };
}

// nibble + LEB128 runs (docs/wxc1-v2.md 2.2 = v1's scheme): a run of 1-15 is one byte, a longer run is one byte
// plus its LEB128 length. Checked against the spike encoder's own output for every hour (codeBytesCheck below).
function runBytes(seq: ArrayLike<number>): number {
  let bytes = 0, cur = -1, len = 0;
  const flush = () => {
    if (!len) return;
    if (len <= 15) bytes += 1; else { bytes += 1; let x = len; do { bytes++; x >>>= 7; } while (x); }
  };
  for (let i = 0; i < seq.length; i++) { const x = seq[i]; if (x === cur) len++; else { flush(); cur = x; len = 1; } }
  flush();
  return bytes;
}

// ---- reflectivity: SeamlessHSR, then the composite (window values and full code grids, one at a time) --------
type Product = { meta: any; box: any; raw: Float32Array; code: Uint8Array; centreRaw: number; v1Rung: number;
                 codeBytesAtK1: number; encoderBytesAtK1: number; anchorMismatch: number };
async function product(gz: string): Promise<Product> {
  const { v, meta } = await decodeFull(gz);
  if (!meta.n2s || !meta.w2e) refuse(`${gz}: unexpected scanning`);
  // C-M4e (Addendum 5): the any-width decoder must reproduce mrms.ts on this 16-bit file, cell for cell.
  let anchorMismatch = 0, anchorRows = 0;
  await decodeAnyWidth(new Uint8Array(readFileSync(gz)), (row: number, vals: Float32Array) => {
    anchorRows++;
    for (let j = 0; j < vals.length; j++) if (vals[j] !== v[row * meta.ni + j]) anchorMismatch++;
  });
  if (anchorRows !== meta.nj) anchorMismatch += 1 + Math.abs(meta.nj - anchorRows) * meta.ni;
  const codes = new Uint8Array(meta.ni * meta.nj);
  for (let i = 0; i < codes.length; i++) codes[i] = codeOf(v[i]);
  const cg = { codes, ni: meta.ni, nj: meta.nj, la1: meta.la1, lo1: meta.lo1, di: meta.di, dj: meta.dj,
               frameEpoch: Math.floor(Date.parse(meta.validTimeUtc) / 1000) };
  const box = cropBox(cg, lat, lon, KM);
  if (box.r0 - 10 < 0 || box.c0 - 10 < 0 || box.r0 + box.rows + 10 > meta.nj || box.c0 + box.cols + 10 > meta.ni)
    refuse("the crop and its +-10 margin are not inside the grid");
  const raw = new Float32Array(box.rows * box.cols), code = new Uint8Array(box.rows * box.cols);
  for (let r = 0; r < box.rows; r++) for (let c = 0; c < box.cols; c++) {
    const i = (box.r0 + r) * meta.ni + box.c0 + c;
    raw[r * box.cols + c] = v[i]; code[r * box.cols + c] = codes[i];
  }
  const cr = Math.round((meta.la1 - lat) / meta.dj), cc = Math.round((lon - meta.lo1) / meta.di);
  const e1 = encodeCrop(cg, lat, lon, KM, Infinity);          // capBytes = Infinity: always coarsen 1
  const ev = encodeCrop(cg, lat, lon, KM);                     // the v1 rung, with the real cap
  return { meta, box, raw, code, centreRaw: v[cr * meta.ni + cc], v1Rung: ev.coarsen,
           codeBytesAtK1: runBytes(code), encoderBytesAtK1: e1.bytes.length - 32, anchorMismatch };
}

const hsr = await product(hsrGz);
const comp = await product(compGz);

// ---- PrecipFlag: the full grid stays in memory (the shifted and flipped reads leave the window) --------------
// Decoded with the any-width decoder (Addendum 5). The width sabotage is computed here, from the raw samples:
// each PAIR of 8-bit samples read as one 16-bit value, X' = 256 X1 + X2, the direction that exists for 8-bit data.
let pfBuf: Float32Array | null = null, pfRows = 0;
const pg = await decodeAnyWidth(new Uint8Array(readFileSync(pfGz)), (row: number, vals: Float32Array) => {
  if (!pfBuf) pfBuf = new Float32Array(vals.length * MAX_ROWS);
  if (row >= MAX_ROWS) refuse(`${pfGz}: more than ${MAX_ROWS} rows`);
  pfBuf!.set(vals, row * vals.length); pfRows++;
});
if (pfRows !== pg.nj) refuse(`${pfGz}: decoded ${pfRows} rows, header says ${pg.nj}`);
const pm = { ni: pg.ni, nj: pg.nj, la1: pg.la1, lo1: pg.lo1, la2: pg.la2, lo2: pg.lo2, di: pg.di, dj: pg.dj,
             w2e: pg.w2e, n2s: pg.n2s, R: pg.R, E: pg.E, D: pg.D, bits: pg.bits, validTimeUtc: pg.validTimeUtc };
const pf = pfBuf!.subarray(0, pg.ni * pg.nj);
const census: Record<string, number> = {}; let outOfTable = 0, nonInteger = 0;
const odd: number[] = [];
let sabOut = 0;
const div = Math.pow(10, pm.D), scale = Math.pow(2, pm.E), scale2 = Math.pow(2, pm.E + 1);
for (let i = 0; i < pf.length; i++) {
  const x = pf[i];
  if (!Number.isInteger(x)) nonInteger++;
  if (PF_TABLE.has(x)) census[String(x)] = (census[String(x)] ?? 0) + 1;
  else { outOfTable++; if (odd.length < 5 && !odd.includes(x)) odd.push(x); }
  const X = (x * div - pm.R) / scale;                            // A4.1's sabotage: the binary scale off by one
  if (!PF_TABLE.has((pm.R + X * scale2) / div)) sabOut++;
}
// Addendum 5's width sabotage, for 8-bit data: each pair of samples in a row read as one 16-bit sample.
let sabWidthOut: number | null = null;
if (pm.bits === 8) {
  sabWidthOut = 0;
  for (let r = 0; r < pm.nj; r++) for (let j = 0; j + 1 < pm.ni; j += 2) {
    const X1 = (pf[r * pm.ni + j] * div - pm.R) / scale, X2 = (pf[r * pm.ni + j + 1] * div - pm.R) / scale;
    if (!PF_TABLE.has((pm.R + (X1 * 256 + X2) * scale) / div)) sabWidthOut++;
  }
}

// ---- C-M4c: pooled-IoU counts, real and three sabotages -------------------------------------------------------
const B = hsr.box, ni = pm.ni, nj = pm.nj;
const VARIANTS: Record<string, (r: number, c: number) => number> = {
  real: (r, c) => pf[r * ni + c],
  sab_a_flip_grid: (r, c) => pf[(nj - 1 - r) * ni + c],
  sab_b_east10: (r, c) => pf[r * ni + c + 10],
  sab_c_east1: (r, c) => pf[r * ni + c + 1],
};
const OFFSETS: [string, number, number][] = [["0,0", 0, 0]];
for (const k of [1, 10]) for (const dr of [-k, 0, k]) for (const dc of [-k, 0, k]) if (dr || dc) OFFSETS.push([`${dr},${dc}`, dr, dc]);
const H = new Uint8Array(B.rows * B.cols);
for (let i = 0; i < H.length; i++) H[i] = hsr.raw[i] >= 5 ? 1 : 0;     // -99 and -999 are below 5
let hHas = [false, false];
for (let i = 0; i < H.length; i++) hHas[H[i]] = true;
const iou: Record<string, any> = {};
for (const [name, at] of Object.entries(VARIANTS)) {
  const per: Record<string, [number, number]> = {};
  let pHas = [false, false];
  const reads: [string, (r: number, c: number) => number][] = OFFSETS.map(([n, dr, dc]) =>
    [n, (r: number, c: number) => at(B.r0 + r + dr, B.c0 + c + dc)]);
  reads.push(["flipNS", (r: number, c: number) => at(B.r0 + (B.rows - 1 - r), B.c0 + c)]);
  let echo = 0, echoPfPos = 0;     // A(0,0) for this variant: SeamlessHSR echo cells (codes 1-13) with PrecipFlag > 0
  for (const [n, read] of reads) {
    let inter = 0, uni = 0;
    for (let r = 0; r < B.rows; r++) for (let c = 0; c < B.cols; c++) {
      const p = read(r, c) > 0 ? 1 : 0, h = H[r * B.cols + c];
      if (n === "0,0") {
        pHas[p] = true;
        const code = hsr.code[r * B.cols + c];
        if (code >= 1 && code <= 13) { echo++; if (p) echoPfPos++; }
      }
      if (p && h) inter++;
      if (p || h) uni++;
    }
    per[n] = [inter, uni];
  }
  iou[name] = { counts: per, boundary: pHas[0] && pHas[1] && hHas[0] && hHas[1], a00: [echoPfPos, echo] };
}

// ---- M4e-M4h per product, at (0,0) on the real grid -----------------------------------------------------------
function rows(p: Product) {
  let echo = 0, echoPfPos = 0, echoSnow = 0, echoNoType = 0, snow = 0, snowEcho = 0;
  const types: number[] = [];
  const pb = p.box;   // each product indexes its own box; m4.py stops at M4d unless both boxes are the same
  for (let r = 0; r < pb.rows; r++) for (let c = 0; c < pb.cols; c++) {
    const code = p.code[r * pb.cols + c], f = pf[(pb.r0 + r) * ni + pb.c0 + c];
    const isEcho = code >= 1 && code <= 13;
    if (f === 3) { snow++; if (isEcho) snowEcho++; }
    if (!isEcho) continue;
    echo++;
    if (f > 0) echoPfPos++;
    if (f === 3) echoSnow++;
    if (f === 0 || f === -3) echoNoType++;
    types.push(f === 3 ? 1 : 0);
  }
  const allOneType = types.every((t) => t === types[0]);
  return { echo, echoPfPos, echoSnow, echoNoType, snow, snowEcho, typeBytes: runBytes(types), allOneType,
           codeBytes: p.codeBytesAtK1, encoderCodeBytes: p.encoderBytesAtK1, v1Rung: p.v1Rung,
           anchorMismatch: p.anchorMismatch };
}
const sameBox = JSON.stringify(hsr.box) === JSON.stringify(comp.box);

writeFileSync(outPath, JSON.stringify({
  files: { hsr: hsrGz.split(/[\\/]/).pop(), comp: compGz.split(/[\\/]/).pop(), pf: pfGz.split(/[\\/]/).pop() },
  meta: { hsr: hsr.meta, comp: comp.meta, pf: pm },
  census: { counts: census, outOfTable, nonInteger, odd, sabScaleOutOfTable: sabOut, sabWidthOutOfTable: sabWidthOut },
  centre: { hsr: hsr.centreRaw, comp: comp.centreRaw },
  box: { rows: B.rows, cols: B.cols, sameBoxBothProducts: sameBox },
  iou,
  products: { hsr: rows(hsr), comp: rows(comp) },
}, null, 1));
console.log(`m4frame ${icao}: ${hsrGz.split(/[\\/]/).pop()} done`);
