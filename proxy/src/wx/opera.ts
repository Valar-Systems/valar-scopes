// The OPERA CIRRUS DBZH cloud-optimised GeoTIFF reader (docs/wx-opuk.md, K1). Range reads only: the first 8 KB for
// IFD 0 and its tile index, then one range per window row of tiles (each row's tiles lie 8 bytes apart: GDAL's
// leader and trailer). Accepts exactly the layout OPERA publishes and refuses anything else: classic little-endian
// TIFF, float32 x 2 samples (value, quality index), chunky, deflate, no predictor, LAEA on WGS84. The projection is
// read from the frame's own GeoKeys and its tiepoint and pixel scale.

import type { GridGeom } from "./grid";
import { type Laea, type LaeaParams, laea, WGS84 } from "./laea";

export const HEAD_BYTES = 8192;
export const NODATA = -9999000;

export type RangeFetch = (start: number, end: number) => Promise<Uint8Array>;

export class OperaRefused extends Error {}

export interface OperaGeo {
  width: number; height: number; tileW: number; tileH: number; tilesX: number;
  offsets: number[]; counts: number[];
  ulx: number; uly: number; xs: number; ys: number;   // the raster's (0,0) corner and pixel size, metres
  proj: LaeaParams;
}

function u16(b: Uint8Array, o: number) { return b[o]! | (b[o + 1]! << 8); }
function u32(b: Uint8Array, o: number) { return (b[o]! | (b[o + 1]! << 8) | (b[o + 2]! << 16) | (b[o + 3]! << 24)) >>> 0; }
function f64(b: Uint8Array, o: number) { return new DataView(b.buffer, b.byteOffset + o, 8).getFloat64(0, true); }

/** IFD 0 of a classic little-endian TIFF, from its first bytes. Throws OperaRefused on anything unexpected. */
export function parseHead(head: Uint8Array): OperaGeo {
  if (head.length < 8 || head[0] !== 0x49 || head[1] !== 0x49 || u16(head, 2) !== 42) {
    throw new OperaRefused("not a classic little-endian TIFF");
  }
  const ifd = u32(head, 4);
  if (ifd + 2 > head.length) throw new OperaRefused("IFD 0 past the header read");
  const n = u16(head, ifd);
  const SIZE: Record<number, number> = { 1: 1, 2: 1, 3: 2, 4: 4, 12: 8, 16: 8 };
  const tags = new Map<number, number[]>();
  for (let i = 0; i < n; i++) {
    const e = ifd + 2 + 12 * i;
    if (e + 12 > head.length) throw new OperaRefused("IFD 0 past the header read");
    const tag = u16(head, e), type = u16(head, e + 2), cnt = u32(head, e + 4);
    const sz = SIZE[type];
    if (!sz || type === 16) { if (type === 16) throw new OperaRefused("BigTIFF fields"); continue; }
    const bytes = sz * cnt;
    const at = bytes <= 4 ? e + 8 : u32(head, e + 8);
    if (at + bytes > head.length) throw new OperaRefused(`tag ${tag} past the header read`);
    const vals: number[] = [];
    for (let k = 0; k < cnt; k++) {
      const o = at + k * sz;
      vals.push(type === 3 ? u16(head, o) : type === 4 ? u32(head, o) : type === 12 ? f64(head, o) : head[o]!);
    }
    tags.set(tag, vals);
  }
  const one = (t: number) => { const v = tags.get(t); if (!v || !v.length) throw new OperaRefused(`missing tag ${t}`); return v[0]!; };
  const need = (cond: boolean, why: string) => { if (!cond) throw new OperaRefused(why); };
  need(one(259) === 8, "compression is not deflate (8)");
  need(one(277) === 2, "samples per pixel is not 2");
  need((tags.get(258) ?? []).every((b) => b === 32), "bits per sample is not 32");
  need((tags.get(339) ?? []).every((s) => s === 3), "sample format is not IEEE float");
  need(!tags.has(284) || one(284) === 1, "planar configuration is not chunky");
  need(!tags.has(317) || one(317) === 1, "a predictor is set");
  const offsets = tags.get(324), counts = tags.get(325);
  need(!!offsets && !!counts && offsets.length === counts.length, "no tile index");
  const scale = tags.get(33550), tie = tags.get(33922), gk = tags.get(34735), gd = tags.get(34736);
  need(!!scale && !!tie && !!gk && !!gd, "no georeference");
  need(tie![0] === 0 && tie![1] === 0, "the tiepoint is not at raster (0, 0)");
  // GeoKeys: [version, rev, minor, count, then (id, location, count, value) x count]
  const keys = new Map<number, number>();
  for (let i = 0; i < gk![3]!; i++) {
    const [id, loc, , val] = gk!.slice(4 + 4 * i, 8 + 4 * i) as [number, number, number, number];
    keys.set(id, loc === 0 ? val : loc === 34736 ? gd![val]! : NaN);
  }
  need(keys.get(3075) === 10, "the projection is not LAEA (ProjCoordTrans 10)");
  need(keys.get(2056) === 7030, "the ellipsoid is not WGS84 (7030)");
  need(keys.get(3076) === 9001, "the linear unit is not the metre");
  const proj: LaeaParams = { lat0: keys.get(3088)!, lon0: keys.get(3089)!, x0: keys.get(3082)!, y0: keys.get(3083)!, ...WGS84 };
  need([proj.lat0, proj.lon0, proj.x0, proj.y0].every(Number.isFinite), "the projection's centre or false origin is missing");
  const width = one(256), height = one(257), tileW = one(322), tileH = one(323);
  return { width, height, tileW, tileH, tilesX: Math.ceil(width / tileW), offsets: offsets!, counts: counts!,
           ulx: tie![3]!, uly: tie![4]!, xs: scale![0]!, ys: scale![1]!, proj };
}

/** The source pixels a grid needs: its cell-boundary rectangle's four edges, sampled every cell, projected, floored.
 *  The same rule as tools/wx/opera_fixture.py. */
export function windowFor(geo: OperaGeo, g: GridGeom, P: Laea = laea(geo.proj)): [number, number, number, number] {
  const north = g.la1 + g.dj / 2, south = g.la1 - (g.nj - 1) * g.dj - g.dj / 2;
  const west = g.lo1 - g.di / 2, east = g.lo1 + (g.ni - 1) * g.di + g.di / 2;
  const nlat = Math.round((north - south) / g.dj), nlon = Math.round((east - west) / g.di);
  let rmin = Infinity, rmax = -Infinity, cmin = Infinity, cmax = -Infinity;
  const take = (lon: number, lat: number) => {
    const [x, y] = P.forward(lon, lat);
    const c = (x - geo.ulx) / geo.xs, r = (geo.uly - y) / geo.ys;
    if (r < rmin) rmin = r; if (r > rmax) rmax = r; if (c < cmin) cmin = c; if (c > cmax) cmax = c;
  };
  for (let i = 0; i <= nlon; i++) { take(west + ((east - west) * i) / nlon, north); take(west + ((east - west) * i) / nlon, south); }
  for (let j = 0; j <= nlat; j++) { take(west, south + ((north - south) * j) / nlat); take(east, south + ((north - south) * j) / nlat); }
  const w: [number, number, number, number] = [Math.floor(rmin), Math.floor(rmax), Math.floor(cmin), Math.floor(cmax)];
  if (w[0] < 0 || w[2] < 0 || w[1] >= geo.height || w[3] >= geo.width) throw new OperaRefused("the window leaves the raster");
  return w;
}

async function inflate(b: Uint8Array): Promise<Uint8Array> {
  const out = new Response(new Blob([b]).stream().pipeThrough(new DecompressionStream("deflate")));
  return new Uint8Array(await out.arrayBuffer());
}

export interface OperaWindow { geo: OperaGeo; window: [number, number, number, number]; rows: number; cols: number;
                               value: Float32Array; quality: Float32Array; }

/** Read a grid's window of one frame: the head, then one range per window row of tiles. Row-major, north first. */
export async function readWindow(fetchRange: RangeFetch, g: GridGeom, band = 0): Promise<OperaWindow> {
  const head = await fetchRange(0, HEAD_BYTES - 1);
  const geo = parseHead(head);
  const win = windowFor(geo, g);
  const [r0, r1, c0, c1] = win;
  const rows = r1 - r0 + 1, cols = c1 - c0 + 1;
  const value = new Float32Array(rows * cols), quality = new Float32Array(rows * cols);
  const { tileW: tw, tileH: th, tilesX: tx } = geo;
  for (let ty = Math.floor(r0 / th); ty <= Math.floor(r1 / th); ty++) {
    const first = ty * tx + Math.floor(c0 / tw), last = ty * tx + Math.floor(c1 / tw);
    const lo = geo.offsets[first]!, hi = geo.offsets[last]! + geo.counts[last]! - 1;
    const row = await fetchRange(lo, hi);
    if (row.length !== hi - lo + 1) throw new OperaRefused(`short range: ${row.length} of ${hi - lo + 1}`);
    for (let t = first; t <= last; t++) {
      const raw = await inflate(row.subarray(geo.offsets[t]! - lo, geo.offsets[t]! - lo + geo.counts[t]!));
      if (raw.length !== tw * th * 8) throw new OperaRefused(`tile ${t}: ${raw.length} B inflated, not ${tw * th * 8}`);
      const f = new Float32Array(raw.buffer, raw.byteOffset, tw * th * 2);
      const txi = t - ty * tx;
      const rs = Math.max(r0, ty * th), re = Math.min(r1, ty * th + th - 1);
      const cs = Math.max(c0, txi * tw), ce = Math.min(c1, txi * tw + tw - 1);
      for (let r = rs; r <= re; r++) {
        const lr = r - ty * th, wr = (r - r0) * cols;
        for (let c = cs; c <= ce; c++) {
          const k = (lr * tw + (c - txi * tw)) * 2;
          value[wr + c - c0] = f[k + band]!;
          quality[wr + c - c0] = f[k + 1 - band]!;
        }
      }
    }
  }
  return { geo, window: win, rows, cols, value, quality };
}
