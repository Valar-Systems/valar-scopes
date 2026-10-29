// OPERA's 1 km LAEA pixels onto a 0.01 deg WXG1 grid (docs/wx-opuk.md; docs/uk-data.md §1.3), with the codes and the
// clutter mask. Inside each target cell, among the source pixels whose CENTRES fall in it, blockCode's priority: the
// strongest echo wins; else 14 (no coverage) if any pixel has none; else 0. A cell with no source centre inside takes
// the pixel containing its centre (nearest-neighbour alone would drop about one source row in ten: a 0.01 deg cell is
// 1.11 km tall against 1 km pixels). The source-to-cell map depends only on the two grids, so it is built once per
// isolate and reused (mapFor).

import type { GridGeom } from "./grid";
import { laea } from "./laea";
import { NODATA, type OperaGeo } from "./opera";
import { codeOfComposite } from "./products";

/** The clutter mask (docs/wx-opuk.md K15): one switch, on at 0.5 until C's reading on 2026-11-04 settles it. */
export const OPUK_QI_MASK = { on: true, threshold: 0.5 };

export interface MaskOpts { on: boolean; threshold: number; cmp?: "lt" | "le" }   // cmp "le": K15's sabotage only

/** One OPERA DBZH value (and its quality index) as a WXC1 code. */
export function operaCode(v: number, q: number, mask: MaskOpts = OPUK_QI_MASK): number {
  if (Number.isNaN(v)) return 0;                 // undetect: inside coverage, no echo
  if (v === NODATA) return 14;                   // no coverage
  const c = codeOfComposite(v);
  if (mask.on && c >= 1 && (mask.cmp === "le" ? q <= mask.threshold : q < mask.threshold)) return 0;
  return c;
}

export interface ResampleMap { cellOf: Int32Array; fill: Int32Array; rows: number; cols: number }

const cache = new Map<string, ResampleMap>();

/** The map for one source geometry and window onto one grid. cellOf[pixel] = target cell, or -1 outside the grid;
 *  fill[cell] = the window pixel containing the cell's centre for a cell no source centre falls in, -1 if that pixel
 *  lies outside the window, -2 for a cell that has source centres. */
export function buildMap(geo: OperaGeo, win: [number, number, number, number], g: GridGeom): ResampleMap {
  const P = laea(geo.proj);
  const [r0, r1, c0, c1] = win;
  const rows = r1 - r0 + 1, cols = c1 - c0 + 1;
  const cellOf = new Int32Array(rows * cols).fill(-1);
  const fill = new Int32Array(g.ni * g.nj).fill(-1);
  const north = g.la1 + g.dj / 2, west = g.lo1 - g.di / 2;
  for (let r = 0; r < rows; r++) {
    const y = geo.uly - (r0 + r + 0.5) * geo.ys;
    for (let c = 0; c < cols; c++) {
      const [lon, lat] = P.inverse(geo.ulx + (c0 + c + 0.5) * geo.xs, y);
      const tr = Math.floor((north - lat) / g.dj), tc = Math.floor((lon - west) / g.di);
      if (tr < 0 || tr >= g.nj || tc < 0 || tc >= g.ni) continue;
      const cell = tr * g.ni + tc;
      cellOf[r * cols + c] = cell;
      fill[cell] = -2;
    }
  }
  for (let tr = 0; tr < g.nj; tr++) {
    const lat = g.la1 - tr * g.dj;
    for (let tc = 0; tc < g.ni; tc++) {
      const cell = tr * g.ni + tc;
      if (fill[cell] === -2) continue;
      const [x, y] = P.forward(g.lo1 + tc * g.di, lat);
      const c = Math.floor((x - geo.ulx) / geo.xs) - c0, r = Math.floor((geo.uly - y) / geo.ys) - r0;
      fill[cell] = r >= 0 && r < rows && c >= 0 && c < cols ? r * cols + c : -1;
    }
  }
  return { cellOf, fill, rows, cols };
}

export function mapFor(geo: OperaGeo, win: [number, number, number, number], g: GridGeom): ResampleMap {
  const key = JSON.stringify([geo.proj, geo.ulx, geo.uly, geo.xs, geo.ys, win, g]);
  let m = cache.get(key);
  if (!m) { m = buildMap(geo, win, g); cache.set(key, m); }
  return m;
}

export interface ResampleOpts { mask?: MaskOpts; centreOnly?: boolean; coveredFixed?: number }   // the last two: sabotages

/** The grid's codes, row-major from the north-west cell, and the covered share (cells that are not 14, over all). */
export function resample(value: Float32Array, quality: Float32Array, m: ResampleMap, g: GridGeom,
                         o: ResampleOpts = {}): { codes: Uint8Array; covered: number } {
  const mask = o.mask ?? OPUK_QI_MASK;
  const n = g.ni * g.nj;
  const echo = new Uint8Array(n), nocov = new Uint8Array(n);
  if (!o.centreOnly) {
    for (let i = 0; i < m.cellOf.length; i++) {
      const cell = m.cellOf[i]!;
      if (cell < 0) continue;
      const code = operaCode(value[i]!, quality[i]!, mask);
      if (code >= 1 && code <= 13) { if (code > echo[cell]!) echo[cell] = code; }
      else if (code === 14) nocov[cell] = 1;
    }
  }
  const codes = new Uint8Array(n);
  let covered = 0;
  for (let cell = 0; cell < n; cell++) {
    let code: number;
    const f = m.fill[cell]!;
    if (o.centreOnly || f !== -2) {
      const px = o.centreOnly ? centrePixel(m, cell) : f;
      code = px < 0 ? 14 : operaCode(value[px]!, quality[px]!, mask);
    } else {
      code = echo[cell] ? echo[cell]! : nocov[cell] ? 14 : 0;
    }
    codes[cell] = code;
    if (code !== 14) covered++;
  }
  return { codes, covered: o.coveredFixed ?? covered / n };
}

/** K2's sabotage: the pixel containing each cell's centre, whatever lies inside the cell. Built lazily per map. */
const centreCache = new WeakMap<ResampleMap, Int32Array>();
export function prepareCentreOnly(geo: OperaGeo, win: [number, number, number, number], g: GridGeom, m: ResampleMap) {
  const P = laea(geo.proj);
  const [r0, , c0] = win;
  const out = new Int32Array(g.ni * g.nj);
  for (let tr = 0; tr < g.nj; tr++) for (let tc = 0; tc < g.ni; tc++) {
    const [x, y] = P.forward(g.lo1 + tc * g.di, g.la1 - tr * g.dj);
    const c = Math.floor((x - geo.ulx) / geo.xs) - c0, r = Math.floor((geo.uly - y) / geo.ys) - r0;
    out[tr * g.ni + tc] = r >= 0 && r < m.rows && c >= 0 && c < m.cols ? r * m.cols + c : -1;
  }
  centreCache.set(m, out);
}
function centrePixel(m: ResampleMap, cell: number): number {
  const a = centreCache.get(m);
  if (!a) throw new Error("prepareCentreOnly first");
  return a[cell]!;
}
