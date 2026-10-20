// Crop encoder for the weather spike: wire format v1 (docs/wx-spike-2026-10.md). Worker-compatible.
//
// A device receives GRID CELLS (lat/lon), never screen pixels, and maps them through the same projection
// as the planes. A crop is the box of cells around a location, cut from one decoded frame.
//
// Header, little-endian, 32 bytes:
//   0  "WXC1"          magic + format version
//   4  u8  version (1)  5 u8 coarsen (1,2,4,8)  6 u8 flags (bit0: coarsened to fit the cap)  7 u8 reserved
//   8  u32 frame time (UTC epoch s)
//  12  i32 lat of the NW cell centre (microdegrees)   16 i32 lon of the NW cell centre (microdegrees)
//  20  u32 cell height (microdegrees)                 24 u32 cell width (microdegrees)
//  28  u16 rows   30 u16 cols
// Body: run-length bytes, row-major from the NW cell: (code << 4) | (run - 1), run 1..16, runs continue
// across rows. Codes: 0 = no echo / under 15 dBZ inside coverage; 1-13 = 15-75 dBZ in 5 dBZ steps;
// 14 = no radar coverage; 15 = missing data. Cells outside the source grid are 14.

export interface CodeGrid {
  codes: Uint8Array;     // one code per cell, row-major, row 0 = north edge, col 0 = west edge
  ni: number; nj: number;
  la1: number; lo1: number;  // centre of cell (0,0), degrees
  di: number; dj: number;    // degrees
  frameEpoch: number;
}

export const HEADER_BYTES = 32;
export const CAP_BYTES = 32 * 1024;
const KM_PER_DEG = 111.32;

export interface CropBox { r0: number; c0: number; rows: number; cols: number }

/** The cell box covering a square of +-radiusKm around (lat, lon). May extend beyond the grid. */
export function cropBox(g: CodeGrid, lat: number, lon: number, radiusKm: number): CropBox {
  const hh = radiusKm / KM_PER_DEG;
  const hw = radiusKm / (KM_PER_DEG * Math.cos(lat * Math.PI / 180));
  const r0 = Math.round((g.la1 - (lat + hh)) / g.dj), r1 = Math.round((g.la1 - (lat - hh)) / g.dj);
  const c0 = Math.round(((lon - hw) - g.lo1) / g.di), c1 = Math.round(((lon + hw) - g.lo1) / g.di);
  return { r0, c0, rows: r1 - r0 + 1, cols: c1 - c0 + 1 };
}

const cellAt = (g: CodeGrid, r: number, c: number) =>
  (r < 0 || r >= g.nj || c < 0 || c >= g.ni) ? 14 : g.codes[r * g.ni + c];

/** Code of a k x k block: the strongest echo wins; else no coverage if ANY cell has none (never draw
 *  no-coverage as dry); else missing if any is missing; else 0. */
function blockCode(g: CodeGrid, r: number, c: number, k: number): number {
  let echo = 0, nocov = false, missing = false;
  for (let i = 0; i < k; i++) for (let j = 0; j < k; j++) {
    const v = cellAt(g, r + i, c + j);
    if (v >= 1 && v <= 13) { if (v > echo) echo = v; }
    else if (v === 14) nocov = true;
    else if (v === 15) missing = true;
  }
  return echo ? echo : nocov ? 14 : missing ? 15 : 0;
}

export interface EncodedCrop { bytes: Uint8Array; coarsen: number; flagged: boolean; rows: number; cols: number }

function encodeAt(g: CodeGrid, box: CropBox, k: number, flagged: boolean, originShiftCells = 0): EncodedCrop {
  const rows = Math.ceil(box.rows / k), cols = Math.ceil(box.cols / k);
  const body: number[] = [];
  let cur = -1, run = 0;
  const flush = () => { if (run) body.push((cur << 4) | (run - 1)); run = 0; };
  for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
    const rr = box.r0 + originShiftCells + r * k, cc = box.c0 + originShiftCells + c * k;
    const v = k === 1 ? cellAt(g, rr, cc) : blockCode(g, rr, cc, k);
    if (v === cur && run < 16) run++;
    else { flush(); cur = v; run = 1; }
  }
  flush();
  const out = new Uint8Array(HEADER_BYTES + body.length);
  const dv = new DataView(out.buffer);
  out.set([0x57, 0x58, 0x43, 0x31], 0);                    // "WXC1"
  dv.setUint8(4, 1); dv.setUint8(5, k); dv.setUint8(6, flagged ? 1 : 0); dv.setUint8(7, 0);
  dv.setUint32(8, g.frameEpoch, true);
  // NW cell centre of the crop (for k > 1, the centre of the first block)
  const latNW = g.la1 - (box.r0 + (k - 1) / 2) * g.dj, lonNW = g.lo1 + (box.c0 + (k - 1) / 2) * g.di;
  dv.setInt32(12, Math.round(latNW * 1e6), true); dv.setInt32(16, Math.round(lonNW * 1e6), true);
  dv.setUint32(20, Math.round(g.dj * k * 1e6), true); dv.setUint32(24, Math.round(g.di * k * 1e6), true);
  dv.setUint16(28, rows, true); dv.setUint16(30, cols, true);
  out.set(body, HEADER_BYTES);
  return { bytes: out, coarsen: k, flagged, rows, cols };
}

/**
 * Encode the crop around (lat, lon). With the cap on (the default), a crop over capBytes is coarsened 2x2,
 * then 4x4, then 8x8, and flagged; one that still does not fit is returned flagged and the caller refuses
 * it. capBytes = Infinity is the sabotage (no cap). `originShiftCells` exists only for A3's sabotage.
 */
export function encodeCrop(g: CodeGrid, lat: number, lon: number, radiusKm: number,
                           capBytes = CAP_BYTES, originShiftCells = 0): EncodedCrop {
  const box = cropBox(g, lat, lon, radiusKm);
  let e = encodeAt(g, box, 1, false, originShiftCells);
  for (const k of [2, 4, 8]) {
    if (e.bytes.length <= capBytes) break;
    e = encodeAt(g, box, k, true, originShiftCells);
  }
  return e;
}

/** Decode a crop back to codes (the device's job; used by the checks). */
export function decodeCrop(b: Uint8Array): { coarsen: number; flagged: boolean; frameEpoch: number; latNW: number; lonNW: number;
                                             cellLat: number; cellLon: number; rows: number; cols: number; codes: Uint8Array } {
  if (b[0] !== 0x57 || b[1] !== 0x58 || b[2] !== 0x43 || b[3] !== 0x31) throw new Error("not a WXC1 crop");
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const rows = dv.getUint16(28, true), cols = dv.getUint16(30, true);
  const codes = new Uint8Array(rows * cols);
  let p = 0;
  for (let i = HEADER_BYTES; i < b.length; i++) {
    const code = b[i] >> 4, run = (b[i] & 15) + 1;
    codes.fill(code, p, p + run); p += run;
  }
  if (p !== rows * cols) throw new Error(`crop body decodes to ${p} cells, header says ${rows * cols}`);
  return { coarsen: dv.getUint8(5), flagged: (dv.getUint8(6) & 1) === 1, frameEpoch: dv.getUint32(8, true),
           latNW: dv.getInt32(12, true) / 1e6, lonNW: dv.getInt32(16, true) / 1e6,
           cellLat: dv.getUint32(20, true) / 1e6, cellLon: dv.getUint32(24, true) / 1e6, rows, cols, codes };
}
