// WXG1: one stored weather frame, tiled so a crop needs at most 4 range reads (docs/wx-server.md).
//
// Layout, little-endian:
//   0  "WXG1"   4 u8 version (1)   5 u8 product id length   6..13 product id, ASCII, zero-padded
//  14  u16 tile size (cells per side)   16 u32 frame epoch (UTC s)   20 u32 ni (cols)   24 u32 nj (rows)
//  28  f64 la1   36 f64 lo1   44 f64 di   52 f64 dj   (degrees; cell (0,0) is the NW corner's centre,
//                                                      rows run north->south, cols west->east)
//  60  u16 tilesX   62 u16 tilesY
//  64  the tiles, row-major (ty, then tx). A tile is tile x tile cells, 4 bits each, two per byte, cell
//      index (lr * tile + lc), even index in the LOW nibble (the spike's packing). Edge tiles are padded
//      with 14 (no coverage), so every tile has the same size and its offset needs no index.

export const WXG1_HEADER_BYTES = 64;
export const TILE = 512;
export const NO_COVERAGE = 14;

export interface GridGeom {
  ni: number; nj: number;      // cols, rows
  la1: number; lo1: number;    // centre of cell (0,0), degrees
  di: number; dj: number;      // cell width / height, degrees
}

export interface FrameMeta extends GridGeom {
  productId: string;
  epoch: number;
  tile: number;
  tilesX: number;
  tilesY: number;
}

export const tileBytes = (tile: number) => (tile * tile) >> 1;
export const tileOffset = (m: Pick<FrameMeta, "tile" | "tilesX">, ty: number, tx: number) =>
  WXG1_HEADER_BYTES + (ty * m.tilesX + tx) * tileBytes(m.tile);

export function frameMeta(productId: string, epoch: number, g: GridGeom, tile = TILE): FrameMeta {
  if (!/^[a-z0-9]{2,8}$/.test(productId)) throw new Error(`bad product id ${productId}`);
  if (tile % 2 !== 0) throw new Error("tile size must be even");
  return { productId, epoch, ...g, tile, tilesX: Math.ceil(g.ni / tile), tilesY: Math.ceil(g.nj / tile) };
}

export function objectBytes(m: FrameMeta): number {
  return WXG1_HEADER_BYTES + m.tilesX * m.tilesY * tileBytes(m.tile);
}

/** Builds one WXG1 object row by row, straight into the tiled layout (never a flat copy of the grid). */
export class FrameBuilder {
  readonly meta: FrameMeta;
  readonly bytes: Uint8Array;

  constructor(meta: FrameMeta) {
    this.meta = meta;
    this.bytes = new Uint8Array(objectBytes(meta));
    this.bytes.fill((NO_COVERAGE << 4) | NO_COVERAGE, WXG1_HEADER_BYTES);
    writeHeader(this.bytes, meta);
  }

  /** Row `r` of the grid (north edge = 0), one code 0-15 per column. */
  setRow(r: number, codes: ArrayLike<number>): void {
    const m = this.meta, t = m.tile, b = this.bytes;
    if (r < 0 || r >= m.nj) throw new Error(`row ${r} outside 0..${m.nj - 1}`);
    if (codes.length !== m.ni) throw new Error(`row has ${codes.length} cells, grid has ${m.ni}`);
    const ty = Math.floor(r / t), lr = r - ty * t;
    for (let c = 0; c < m.ni; c++) {
      const tx = Math.floor(c / t), lc = c - tx * t;
      const idx = lr * t + lc;
      const at = tileOffset(m, ty, tx) + (idx >> 1);
      const v = codes[c] & 15;
      b[at] = (idx & 1) ? ((b[at] & 0x0f) | (v << 4)) : ((b[at] & 0xf0) | v);
    }
  }
}

function writeHeader(b: Uint8Array, m: FrameMeta): void {
  const dv = new DataView(b.buffer, b.byteOffset, WXG1_HEADER_BYTES);
  b.set([0x57, 0x58, 0x47, 0x31], 0);                       // "WXG1"
  dv.setUint8(4, 1);
  dv.setUint8(5, m.productId.length);
  for (let i = 0; i < 8; i++) dv.setUint8(6 + i, i < m.productId.length ? m.productId.charCodeAt(i) : 0);
  dv.setUint16(14, m.tile, true);
  dv.setUint32(16, m.epoch, true);
  dv.setUint32(20, m.ni, true);
  dv.setUint32(24, m.nj, true);
  dv.setFloat64(28, m.la1, true);
  dv.setFloat64(36, m.lo1, true);
  dv.setFloat64(44, m.di, true);
  dv.setFloat64(52, m.dj, true);
  dv.setUint16(60, m.tilesX, true);
  dv.setUint16(62, m.tilesY, true);
}

/** Parse a WXG1 header. Refuses (throws) anything that is not exactly what writeHeader writes. */
export function readHeader(h: Uint8Array): FrameMeta {
  if (h.length < WXG1_HEADER_BYTES) throw new Error("WXG1 header too short");
  if (h[0] !== 0x57 || h[1] !== 0x58 || h[2] !== 0x47 || h[3] !== 0x31) throw new Error("not a WXG1 object");
  const dv = new DataView(h.buffer, h.byteOffset, WXG1_HEADER_BYTES);
  if (dv.getUint8(4) !== 1) throw new Error(`WXG1 version ${dv.getUint8(4)}`);
  const n = dv.getUint8(5);
  let productId = "";
  for (let i = 0; i < n; i++) productId += String.fromCharCode(dv.getUint8(6 + i));
  const m = frameMeta(productId, dv.getUint32(16, true), {
    ni: dv.getUint32(20, true), nj: dv.getUint32(24, true),
    la1: dv.getFloat64(28, true), lo1: dv.getFloat64(36, true),
    di: dv.getFloat64(44, true), dj: dv.getFloat64(52, true),
  }, dv.getUint16(14, true));
  if (m.tilesX !== dv.getUint16(60, true) || m.tilesY !== dv.getUint16(62, true)) throw new Error("WXG1 tile counts disagree");
  return m;
}

/** Code of cell (r, c) inside tile (ty, tx)'s bytes; the caller has already picked the right tile. */
export function cellInTile(tile: Uint8Array, t: number, lr: number, lc: number): number {
  const idx = lr * t + lc;
  const v = tile[idx >> 1];
  return (idx & 1) ? (v >> 4) : (v & 15);
}

/** The tiles a cell box touches, clipped to the grid. Empty when the box misses the grid entirely. */
export function tilesFor(m: FrameMeta, r0: number, r1: number, c0: number, c1: number): Array<[number, number]> {
  const rr0 = Math.max(r0, 0), rr1 = Math.min(r1, m.nj - 1), cc0 = Math.max(c0, 0), cc1 = Math.min(c1, m.ni - 1);
  if (rr0 > rr1 || cc0 > cc1) return [];
  const out: Array<[number, number]> = [];
  for (let ty = Math.floor(rr0 / m.tile); ty <= Math.floor(rr1 / m.tile); ty++)
    for (let tx = Math.floor(cc0 / m.tile); tx <= Math.floor(cc1 / m.tile); tx++) out.push([ty, tx]);
  return out;
}

/** A cell reader over a set of fetched tiles; 14 outside the grid. Throws on a tile it was not given,
 *  so a crop can never silently read a tile that tilesFor did not ask for. */
export function tiledCellAt(m: FrameMeta, tiles: Map<string, Uint8Array>): (r: number, c: number) => number {
  const t = m.tile;
  return (r: number, c: number) => {
    if (r < 0 || r >= m.nj || c < 0 || c >= m.ni) return NO_COVERAGE;
    const ty = Math.floor(r / t), tx = Math.floor(c / t);
    const tile = tiles.get(`${ty}-${tx}`);
    if (!tile) throw new Error(`tile ${ty}-${tx} was not fetched`);
    return cellInTile(tile, t, r - ty * t, c - tx * t);
  };
}
