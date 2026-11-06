// Terrain (docs/basemap-package-format.md §4, §5.4, §5.6; §13's RD10, RD16, RD21, RD27, RD28). Pure, double throughout.
//
// The hillshade and the 4 x 4 mean are computed per pixel, by shadeAt and meanAt, and only for the 16 pixels each
// level sample reads. The full-array forms (hillshade, boxMean4) are the same functions over a whole grid, so the MT
// rows test the code the builder runs. A whole mosaic's arrays would cost ~100 MB at the largest level; this costs
// nothing but CPU. Each pixel's arithmetic is the reference's, aligned as bendmock.py aligns it.

const PI = Math.PI;
const ALT = 45 * PI / 180, AZ = 315 * PI / 180; // RD16

/** §5.6: Terrarium's elevation in metres, R*256 + G + B/256 - 32768, in that order, in double. */
export const decodeTerrarium = (r: number, g: number, b: number) => r * 256 + g + b / 256 - 32768;

/** RD10: a summit's elevation, the z9 pixel's Terrarium value then floor(v + 0.5). */
export const elevationM = (r: number, g: number, b: number) => Math.floor(decodeTerrarium(r, g, b) + 0.5);

/** RD21: a point's global z9 position, before flooring. */
export const z9X = (lon: number) => ((lon + 180) / 360) * 2 ** 17;
export const z9Y = (lat: number) => ((1 - Math.log(Math.tan(PI / 4 + (lat * PI / 180) / 2)) / PI) / 2) * 2 ** 17;
/** RD21: the global z9 pixel containing a point (truncation, never rounding). */
export const z9Pixel = (lat: number, lon: number): [number, number] => [Math.floor(z9X(lon)), Math.floor(z9Y(lat))];

/** §5.6: metres per z9 pixel at the origin's latitude. */
export const mppOf = (lat0: number) => 156543.03 * Math.cos(lat0 * PI / 180) / 2 ** 9;

/** §5.6 at one pixel of a w x h grid: central differences inside, one-sided at the grid's edge (numpy's gradient),
 *  slope with the 2x exaggeration, aspect, the 315/45 light, clipped to [0, 1]. RD28: the library's maths. */
export function shadeAt(dem: (x: number, y: number) => number, w: number, h: number, x: number, y: number, mpp: number): number {
  const gx = x === 0 ? dem(1, y) - dem(0, y) : x === w - 1 ? dem(w - 1, y) - dem(w - 2, y) : (dem(x + 1, y) - dem(x - 1, y)) / 2;
  const gy = y === 0 ? dem(x, 1) - dem(x, 0) : y === h - 1 ? dem(x, h - 1) - dem(x, h - 2) : (dem(x, y + 1) - dem(x, y - 1)) / 2;
  const slope = Math.atan(2.0 * Math.hypot(gx, gy) / mpp);
  const aspect = Math.atan2(-gx, gy);
  const s = Math.sin(ALT) * Math.cos(slope) + Math.cos(ALT) * Math.sin(slope) * Math.cos(AZ - aspect);
  return s < 0 ? 0 : s > 1 ? 1 : s;
}

/** §5.6, RD27: the 4 x 4 mean at (x, y) over a grid edge-padded by 2, i.e. the padded pixels [x, x+3] x [y, y+3], which
 *  are the grid's [x-2, x+1] x [y-2, y+1] clamped to its edge. Summed row by row, left to right, from 0.0, then / 16. */
export function meanAt(s: (x: number, y: number) => number, w: number, h: number, x: number, y: number): number {
  let t = 0.0;
  for (let a = 0; a < 4; a++) {
    for (let b = 0; b < 4; b++) {
      const px = x - 2 + b, py = y - 2 + a;
      const cx = px < 0 ? 0 : px > w - 1 ? w - 1 : px, cy = py < 0 ? 0 : py > h - 1 ? h - 1 : py;
      t += s(cx, cy);
    }
  }
  return t / 16;
}

/** §5.6 over a whole w x h DEM (row-major). */
export function hillshade(dem: Float64Array, w: number, h: number, lat0: number): Float64Array {
  const mpp = mppOf(lat0), at = (x: number, y: number) => dem[y * w + x]!;
  const out = new Float64Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) out[y * w + x] = shadeAt(at, w, h, x, y, mpp);
  return out;
}

/** §5.6's 4 x 4 mean over a whole w x h field (row-major). */
export function boxMean4(s: Float64Array, w: number, h: number): Float64Array {
  const at = (x: number, y: number) => s[y * w + x]!;
  const out = new Float64Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) out[y * w + x] = meanAt(at, w, h, x, y);
  return out;
}

/** §5.6: the stored byte. */
export const shadeByte = (v: number) => Math.floor(v * 255 + 0.5);

// ------------------------------------------------------------------------------------------------ levels (§4)

const K = 111.0;
interface Origin { lat0: number; lon0: number; c0: number }

/** §4, RD21: the z9 pixel containing level pixel (i, j)'s centre (row 0 north), in the reference builder's order of
 *  operations: the pixel size in mm first, then km. */
export function levelPixelToZ9(f: Origin, levelMm: number, n: number, i: number, j: number): [number, number] {
  const pxMm = 2 * levelMm / n;
  const sKm = ((j + 0.5) - n / 2) * pxMm / 1e6;
  const lat = f.lat0 - sKm / K;
  const eKm = ((i + 0.5) - n / 2) * pxMm / 1e6;
  const lon = f.lon0 + eKm / (K * f.c0);
  return z9Pixel(lat, lon);
}

/** §5.6: every z9 tile meeting the level's square, plus a one-tile margin on every side: [tx0, ty0, tx1, ty1]. */
export function levelTiles(f: Origin, levelMm: number): [number, number, number, number] {
  const r = levelMm / 1e6;
  const [x0, y0] = z9Pixel(f.lat0 + r / K, f.lon0 - r / (K * f.c0));
  const [x1, y1] = z9Pixel(f.lat0 - r / K, f.lon0 + r / (K * f.c0));
  return [Math.floor(x0 / 256) - 1, Math.floor(y0 / 256) - 1, Math.floor(x1 / 256) + 1, Math.floor(y1 / 256) + 1];
}

/** A full rectangle of z9 tiles, [tx0, tx1] x [ty0, ty1], as Terrarium RGB (256 x 256 x 3 each). */
export interface Mosaic { tx0: number; ty0: number; tx1: number; ty1: number; rgb(X: number, Y: number): [number, number, number] }

/** A mosaic over decoded tiles; every tile of the rectangle must be present (a hole would be sampled as nothing). */
export function mosaicOf(tiles: Map<string, Uint8Array>, tx0: number, ty0: number, tx1: number, ty1: number): Mosaic {
  const tw = tx1 - tx0 + 1, grid: Uint8Array[] = [];
  for (let y = ty0; y <= ty1; y++) for (let x = tx0; x <= tx1; x++) {
    const t = tiles.get(`${x}/${y}`);
    if (!t || t.length !== 256 * 256 * 3) throw new Error(`terrain tile ${x}/${y} missing or not 256 x 256 RGB`);
    grid.push(t);
  }
  return {
    tx0, ty0, tx1, ty1,
    rgb(X, Y) {
      const tx = (X >> 8) - tx0, ty = (Y >> 8) - ty0;
      if (tx < 0 || ty < 0 || tx >= tw || Y >> 8 > ty1) throw new Error("z9 pixel outside the mosaic");
      const t = grid[ty * tw + tx]!, o = ((Y & 255) * 256 + (X & 255)) * 3;
      return [t[o]!, t[o + 1]!, t[o + 2]!];
    },
  };
}

/** RD10: a summit's elevation from the unsmoothed z9 pixel containing it. */
export function summitElevation(m: Pick<Mosaic, "rgb">, lat: number, lon: number): number {
  const [X, Y] = z9Pixel(lat, lon);
  return elevationM(...m.rgb(X, Y));
}

/** §4, §5.6: one level's N x N shade bytes, row 0 north, each pixel the smoothed shade of the z9 pixel containing its
 *  centre, with the MOSAIC's edges as the grid's. Each sample's 16 shades are computed on demand: the same shadeAt
 *  and meanAt as the full-grid forms, with no region array (a 250 km level at 60 N would need ~85 MB of one). */
export function terrainLevel(f: Origin, m: Mosaic, levelMm: number, n: number): Uint8Array {
  const W = (m.tx1 - m.tx0 + 1) * 256, H = (m.ty1 - m.ty0 + 1) * 256, ox = m.tx0 * 256, oy = m.ty0 * 256;
  const dem = (x: number, y: number) => decodeTerrarium(...m.rgb(x + ox, y + oy));
  const mpp = mppOf(f.lat0);
  const shade = (x: number, y: number) => shadeAt(dem, W, H, x, y, mpp);
  const out = new Uint8Array(n * n);
  for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
    const [X, Y] = levelPixelToZ9(f, levelMm, n, i, j);
    const lx = X - ox, ly = Y - oy;
    if (lx < 0 || ly < 0 || lx >= W || ly >= H) throw new Error("a level pixel falls outside its mosaic");
    out[j * n + i] = shadeByte(meanAt(shade, W, H, lx, ly));
  }
  return out;
}
