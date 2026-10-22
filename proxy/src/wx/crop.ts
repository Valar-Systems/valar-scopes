// The crop: the radar's own view box, encoded as one WXC1 v1 crop (docs/wx-server.md, "The crop").
//
// The ENCODER is the weather spike's (tools/wx/crop.ts at a5db678, A2/A4-graded, decoded byte-for-byte
// by include/WxCrop.h), changed in one way only: it reads cells through a `cellAt(r, c)` accessor
// instead of a flat array, so the same code runs over R2 tiles and over a flat test grid. The extra
// check in test/wx-crop.test.ts holds it to the spike's own output.
//
// The BOX is not the spike's (111.32 km per degree, an unclamped km radius). It is the view
// AircraftManager::ProjectAt draws -- +-radLat x +-radLon degrees, clamped -- plus padding, computed from
// the QUANTISED query so one ETag always means one crop. test/wx-crop.test.ts parses VIEW's numbers out
// of src/AircraftManager.cpp, so a change to the radar's projection fails the crop's tests.

import type { GridGeom } from "./grid";

export const HEADER_BYTES = 32;
export const CAP_BYTES = 32 * 1024;
export const PAD_CELLS = 2;

/** AircraftManager::ProjectAt's constants (KM_PER_DEGREE, MAX_DEGREES, MIN_DEGREES, the cos floor). */
export interface ViewConsts { kmPerDegree: number; maxDegrees: number; minDegrees: number; cosFloor: number }
export const VIEW: ViewConsts = { kmPerDegree: 111.0, maxDegrees: 2.0, minDegrees: 0.001, cosFloor: 0.01 };

export interface Query { lat: number; lon: number; rKm: number; lat2: string; lon2: string }

/** The device sends lat/lon at 2 dp (wireloc::Center) and r as whole km; the server works only from the
 *  quantised values, whatever a caller sends. */
export function quantise(lat: number, lon: number, r: number): Query {
  const q = (x: number) => Math.round(x * 100) / 100;
  const latQ = q(lat), lonQ = q(lon), rKm = Math.max(1, Math.round(r));
  return { lat: latQ, lon: lonQ, rKm, lat2: latQ.toFixed(2), lon2: lonQ.toFixed(2) };
}

/** ProjectAt's half-extents in degrees for a range of rKm at latitude lat. */
export function viewDegrees(lat: number, rKm: number, v: ViewConsts = VIEW): { radLat: number; radLon: number } {
  let cosLat = Math.cos(lat * Math.PI / 180);
  if (cosLat < v.cosFloor) cosLat = v.cosFloor;
  const clamp = (x: number) => Math.min(Math.max(x, v.minDegrees), v.maxDegrees);
  return { radLat: clamp(rKm / v.kmPerDegree), radLon: clamp(rKm / (v.kmPerDegree * cosLat)) };
}

export interface CellBox { r0: number; c0: number; rows: number; cols: number }

/** The cells whose AREAS cover the view box, rounded outward, plus `pad` cells on every side. May extend
 *  beyond the grid (those cells are 14). Rows run north->south, cols west->east (MRMS: n2s, w2e). */
export function cropCellBox(g: GridGeom, q: Query, pad = PAD_CELLS, v: ViewConsts = VIEW): CellBox {
  const { radLat, radLon } = viewDegrees(q.lat, q.rKm, v);
  const latN = q.lat + radLat, latS = q.lat - radLat, lonW = q.lon - radLon, lonE = q.lon + radLon;
  // cell r spans lat(r) +- dj/2 with lat(r) = la1 - r*dj; cell c spans lon(c) +- di/2 with lon(c) = lo1 + c*di
  const r0 = Math.floor((g.la1 - latN) / g.dj + 0.5) - pad;
  const r1 = Math.ceil((g.la1 - latS) / g.dj - 0.5) + pad;
  const c0 = Math.floor((lonW - g.lo1) / g.di + 0.5) - pad;
  const c1 = Math.ceil((lonE - g.lo1) / g.di - 0.5) + pad;
  return { r0, c0, rows: r1 - r0 + 1, cols: c1 - c0 + 1 };
}

export const boxMissesGrid = (g: GridGeom, b: CellBox) =>
  b.r0 + b.rows - 1 < 0 || b.r0 >= g.nj || b.c0 + b.cols - 1 < 0 || b.c0 >= g.ni;

export type CellAt = (r: number, c: number) => number;

export interface EncodedCrop { bytes: Uint8Array; coarsen: number; flagged: boolean; rows: number; cols: number; cells: Uint8Array }

/** Code of a k x k block: the strongest echo wins; else no coverage if ANY cell has none (never draw
 *  no-coverage as dry); else missing if any is missing; else 0. (The spike's blockCode.) */
function blockCode(cellAt: CellAt, r: number, c: number, k: number): number {
  let echo = 0, nocov = false, missing = false;
  for (let i = 0; i < k; i++) for (let j = 0; j < k; j++) {
    const v = cellAt(r + i, c + j);
    if (v >= 1 && v <= 13) { if (v > echo) echo = v; }
    else if (v === 14) nocov = true;
    else if (v === 15) missing = true;
  }
  return echo ? echo : nocov ? 14 : missing ? 15 : 0;
}

/** The spike's encodeAt, over an accessor. */
function encodeAt(g: GridGeom, epoch: number, box: CellBox, k: number, flagged: boolean, cellAt: CellAt): EncodedCrop {
  const rows = Math.ceil(box.rows / k), cols = Math.ceil(box.cols / k);
  const body: number[] = [];
  const cells = new Uint8Array(rows * cols);
  let cur = -1, run = 0, n = 0;
  const flush = () => {
    if (!run) return;
    if (run <= 15) body.push((cur << 4) | run);
    else {
      body.push(cur << 4);                       // low nibble 0: a long run, LEB128 length follows
      let len = run;
      do { let b = len & 0x7f; len >>>= 7; if (len) b |= 0x80; body.push(b); } while (len);
    }
    run = 0;
  };
  for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
    const rr = box.r0 + r * k, cc = box.c0 + c * k;
    const v = k === 1 ? cellAt(rr, cc) : blockCode(cellAt, rr, cc, k);
    cells[n++] = v;
    if (v === cur) run++;
    else { flush(); cur = v; run = 1; }
  }
  flush();
  const out = new Uint8Array(HEADER_BYTES + body.length);
  const dv = new DataView(out.buffer);
  out.set([0x57, 0x58, 0x43, 0x31], 0);                    // "WXC1"
  dv.setUint8(4, 1); dv.setUint8(5, k); dv.setUint8(6, flagged ? 1 : 0); dv.setUint8(7, 0);
  dv.setUint32(8, epoch, true);
  const latNW = g.la1 - (box.r0 + (k - 1) / 2) * g.dj, lonNW = g.lo1 + (box.c0 + (k - 1) / 2) * g.di;
  dv.setInt32(12, Math.round(latNW * 1e6), true); dv.setInt32(16, Math.round(lonNW * 1e6), true);
  dv.setUint32(20, Math.round(g.dj * k * 1e6), true); dv.setUint32(24, Math.round(g.di * k * 1e6), true);
  dv.setUint16(28, rows, true); dv.setUint16(30, cols, true);
  out.set(body, HEADER_BYTES);
  return { bytes: out, coarsen: k, flagged, rows, cols, cells };
}

/** Encode `box`; over capBytes it is coarsened 2x2, then 4x4, then 8x8, and flagged (the spike's rule).
 *  One that still does not fit comes back flagged and over the cap: the caller refuses it. */
export function encodeBox(g: GridGeom, epoch: number, box: CellBox, cellAt: CellAt, capBytes = CAP_BYTES): EncodedCrop {
  let e = encodeAt(g, epoch, box, 1, false, cellAt);
  for (const k of [2, 4, 8]) {
    if (e.bytes.length <= capBytes) break;
    e = encodeAt(g, epoch, box, k, true, cellAt);
  }
  return e;
}
