// MRMS GRIB2 decoder for the weather spike (docs/wx-spike-2026-10.md). Worker-compatible: Web APIs only
// (DecompressionStream, typed arrays, DataView) -- no Node APIs in this file.
//
// Scope: exactly what MRMS CONUS products use, verified in A0 -- GRIB2 edition 2, grid template 3.0
// (regular lat/lon), data representation template 5.41 (PNG), 16-bit grey, no bitmap. Anything else is
// REFUSED with a reason rather than decoded on a guess.
//
// Streaming: the gzip file (~0.75 MB) is inflated whole; the PNG's zlib stream (~49 MB of rows for CONUS)
// is inflated as a stream and handed out ONE ROW AT A TIME, so peak memory is the file plus two rows plus
// whatever the caller keeps (the 4-bit grid is ~12.3 MB).

export interface MrmsGrid {
  ni: number; nj: number;           // columns, rows
  la1: number; lo1: number;         // first grid point (deg; lon normalised to -180..180)
  la2: number; lo2: number;         // last grid point
  di: number; dj: number;           // increments (deg)
  w2e: boolean; n2s: boolean;       // scanning: west->east, north->south
  R: number; E: number; D: number;  // value = (R + X * 2^E) / 10^D
  bits: number;
  validTimeUtc: string;             // ISO, from section 1 (reference time)
}

export class MrmsRefused extends Error {}

const u32 = (b: Uint8Array, o: number) => ((b[o] << 24) >>> 0) + (b[o + 1] << 16) + (b[o + 2] << 8) + b[o + 3];
const u16 = (b: Uint8Array, o: number) => (b[o] << 8) | b[o + 1];
// GRIB2 signed integers are sign-and-magnitude, not two's complement.
const s32 = (b: Uint8Array, o: number) => { const v = u32(b, o); return (v & 0x80000000) ? -(v & 0x7fffffff) : v; };
const s16 = (b: Uint8Array, o: number) => { const v = u16(b, o); return (v & 0x8000) ? -(v & 0x7fff) : v; };

async function inflateAll(data: Uint8Array, format: "gzip" | "deflate"): Promise<Uint8Array> {
  const out = new Response(new Blob([data]).stream().pipeThrough(new DecompressionStream(format)));
  return new Uint8Array(await out.arrayBuffer());
}

/** Parse the GRIB2 sections we need. Returns the grid and the PNG payload of section 7. */
export function parseGrib2(g: Uint8Array): { grid: MrmsGrid; png: Uint8Array } {
  if (String.fromCharCode(g[0], g[1], g[2], g[3]) !== "GRIB") throw new MrmsRefused("not a GRIB message");
  if (g[7] !== 2) throw new MrmsRefused(`GRIB edition ${g[7]}, want 2`);
  let o = 16;
  let grid: Partial<MrmsGrid> = {};
  let png: Uint8Array | null = null;
  let tmpl5 = -1, tmpl3 = -1, bitmap = -1;
  while (o < g.length - 4) {
    if (String.fromCharCode(g[o], g[o + 1], g[o + 2], g[o + 3]) === "7777") break;
    const len = u32(g, o), sec = g[o + 4];
    if (len < 5 || o + len > g.length) throw new MrmsRefused(`bad section ${sec} length ${len} at ${o}`);
    if (sec === 1) {
      const y = u16(g, o + 12), mo = g[o + 14], d = g[o + 15], h = g[o + 16], mi = g[o + 17], se = g[o + 18];
      const p2 = (n: number) => String(n).padStart(2, "0");
      grid.validTimeUtc = `${y}-${p2(mo)}-${p2(d)}T${p2(h)}:${p2(mi)}:${p2(se)}Z`;
    } else if (sec === 3) {
      tmpl3 = u16(g, o + 12);
      if (tmpl3 !== 0) throw new MrmsRefused(`grid template 3.${tmpl3}, want 3.0`);
      grid.ni = u32(g, o + 30); grid.nj = u32(g, o + 34);
      // basic angle 0 / subdivisions 0 or all-ones -> units of 1e-6 degree
      const ba = u32(g, o + 38), sub = u32(g, o + 42);
      const unit = (ba === 0 || ba === 0xffffffff) ? 1e-6 : ba / sub;
      const lon = (v: number) => { let x = v * unit; if (x > 180) x -= 360; return x; };
      grid.la1 = s32(g, o + 46) * unit; grid.lo1 = lon(u32(g, o + 50));
      grid.la2 = s32(g, o + 55) * unit; grid.lo2 = lon(u32(g, o + 59));
      grid.di = u32(g, o + 63) * unit; grid.dj = u32(g, o + 67) * unit;
      const scan = g[o + 71];
      grid.w2e = (scan & 0x80) === 0; grid.n2s = (scan & 0x40) === 0;
      if (scan & 0x20) throw new MrmsRefused("j-consecutive scanning not supported");
    } else if (sec === 5) {
      tmpl5 = u16(g, o + 9);
      if (tmpl5 !== 41) throw new MrmsRefused(`data template 5.${tmpl5}, want 5.41 (PNG)`);
      grid.R = new DataView(g.buffer, g.byteOffset + o + 11, 4).getFloat32(0, false);
      grid.E = s16(g, o + 15); grid.D = s16(g, o + 17); grid.bits = g[o + 19];
    } else if (sec === 6) {
      bitmap = g[o + 5];
      if (bitmap !== 255) throw new MrmsRefused(`bitmap indicator ${bitmap}; only 'no bitmap' (255) supported`);
    } else if (sec === 7) {
      png = g.subarray(o + 5, o + len);
    }
    o += len;
  }
  if (!png || tmpl3 !== 0 || tmpl5 !== 41) throw new MrmsRefused("missing section 3, 5 or 7");
  return { grid: grid as MrmsGrid, png };
}

/**
 * Decode an MRMS .grib2.gz. Calls onRow(rowIndex, values) for each row in file order (row 0 = the first
 * grid row, north edge for n2s). `values` is reused between calls: copy what you keep.
 */
export async function decodeMrms(gz: Uint8Array, onRow: (row: number, values: Float32Array) => void): Promise<MrmsGrid> {
  const g = await inflateAll(gz, "gzip");
  const { grid, png } = parseGrib2(g);
  if (grid.bits !== 16) throw new MrmsRefused(`bits per value ${grid.bits}, want 16`);
  // PNG: signature, then chunks; concatenate IDAT payloads (a zlib stream).
  const sig = [137, 80, 78, 71, 13, 10, 26, 10];
  for (let i = 0; i < 8; i++) if (png[i] !== sig[i]) throw new MrmsRefused("section 7 is not a PNG");
  let p = 8, w = 0, h = 0, depth = 0, ctype = 0;
  const idat: Uint8Array[] = [];
  while (p < png.length) {
    const clen = u32(png, p), ctype4 = String.fromCharCode(png[p + 4], png[p + 5], png[p + 6], png[p + 7]);
    const data = png.subarray(p + 8, p + 8 + clen);
    if (ctype4 === "IHDR") { w = u32(data, 0); h = u32(data, 4); depth = data[8]; ctype = data[9]; if (data[12] !== 0) throw new MrmsRefused("interlaced PNG"); }
    else if (ctype4 === "IDAT") idat.push(data);
    else if (ctype4 === "IEND") break;
    p += 12 + clen;
  }
  if (w !== grid.ni || h !== grid.nj) throw new MrmsRefused(`PNG ${w}x${h} does not match grid ${grid.ni}x${grid.nj}`);
  if (depth !== 16 || ctype !== 0) throw new MrmsRefused(`PNG depth ${depth} colour type ${ctype}, want 16-bit grey`);

  const scale = Math.pow(2, grid.E), div = Math.pow(10, grid.D), R = grid.R;
  const rowBytes = w * 2, bpp = 2;
  let prev = new Uint8Array(rowBytes), cur = new Uint8Array(rowBytes);
  const vals = new Float32Array(w);
  let fill = -1;            // -1 = expecting the filter byte; else bytes filled into cur
  let filter = 0, row = 0;
  const finishRow = () => {
    // unfilter in place
    for (let i = 0; i < rowBytes; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let x = cur[i];
      switch (filter) {
        case 0: break;
        case 1: x += a; break;
        case 2: x += b; break;
        case 3: x += (a + b) >> 1; break;
        case 4: { const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
                  x += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
        default: throw new MrmsRefused(`PNG filter ${filter} on row ${row}`);
      }
      cur[i] = x & 0xff;
    }
    for (let j = 0; j < w; j++) vals[j] = (R + ((cur[2 * j] << 8) | cur[2 * j + 1]) * scale) / div;
    onRow(row, vals);
    row++;
    const t = prev; prev = cur; cur = t;
  };
  const zlib = new Blob(idat).stream().pipeThrough(new DecompressionStream("deflate"));
  const reader = zlib.getReader();
  for (;;) {
    const { value, done } = await reader.read();
    if (done) break;
    let k = 0;
    while (k < value.length && row < h) {
      if (fill < 0) { filter = value[k++]; fill = 0; continue; }
      const take = Math.min(rowBytes - fill, value.length - k);
      cur.set(value.subarray(k, k + take), fill);
      fill += take; k += take;
      if (fill === rowBytes) { finishRow(); fill = -1; }
    }
  }
  if (row !== h) throw new MrmsRefused(`PNG ended after ${row} of ${h} rows`);
  return grid;
}

/** The ruled wire-format code for one value (MergedReflectivityQCComposite semantics; see the write-up). */
export function codeOf(v: number, missing?: number): number {
  if (v === -999) return 14;                                  // no radar coverage
  if (missing !== undefined && v === missing) return 15;      // genuine missing, only where a product has one
  if (v < 15) return 0;                                       // -99 (no echo), negatives, under 15 dBZ
  const c = 1 + Math.floor((v - 15) / 5);
  return c > 13 ? 13 : c;
}

/** Cell centre of (row, col) for a decoded grid. */
export function cellLatLon(grid: MrmsGrid, row: number, col: number): [number, number] {
  const lat = grid.n2s ? grid.la1 - row * grid.dj : grid.la1 + row * grid.dj;
  let lon = grid.w2e ? grid.lo1 + col * grid.di : grid.lo1 - col * grid.di;
  if (lon > 180) lon -= 360;
  return [lat, lon];
}
