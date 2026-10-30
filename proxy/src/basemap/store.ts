// The store's shared formats (docs/basemap-route.md §2.3): its keys, the feature codec, the cell index, coverage,
// the tile-source bits and the Terrarium PNG decode. The ingest WRITES these (scripts/basemap/ingest-lib.ts
// re-exports the codec) and the builder READS them, so both sides use one definition. Pure: Web APIs only.

export type FeatureKind = "water" | "rivers" | "roads" | "peaks" | "places";

export interface Feature {
  kind: FeatureKind;
  file: number;    // index into the manifest's files (sorted by URL), RD2
  record: number;  // record index in that file
  part: number;    // part index in that record
  /** water: AWATER; rivers/roads: FULLNAME; roads: MTFCC; peaks: feature_id + name; places: GEOID, NAME, pop */
  attrs: Record<string, string | number>;
  xy: Float64Array; // [lon, lat, ...] as the source has them
}

export type Box = [minLon: number, minLat: number, maxLon: number, maxLat: number];

const te = new TextEncoder();
const KIND_CODE: Record<FeatureKind, number> = { water: 1, rivers: 2, roads: 3, peaks: 4, places: 5 };

/** One feature's bytes in a cell object: kind, source-order key, attributes, then f64 vertices. */
export function encodeFeature(f: Feature): Uint8Array {
  const texts: Uint8Array[] = [];
  const nums: number[] = [];
  const t = (s: string | number | undefined) => texts.push(te.encode(String(s ?? "")));
  if (f.kind === "water") nums.push(Number(f.attrs["AWATER"]));
  else if (f.kind === "rivers") t(f.attrs["FULLNAME"]);
  else if (f.kind === "roads") { t(f.attrs["MTFCC"]); t(f.attrs["FULLNAME"]); }
  else if (f.kind === "peaks") { nums.push(Number(f.attrs["feature_id"])); t(f.attrs["feature_name"]); }
  else { t(f.attrs["GEOID"]); t(f.attrs["NAME"]); nums.push(Number(f.attrs["pop"])); }
  const len = 1 + 2 + 4 + 2 + 1 + 1 + nums.length * 8 + texts.reduce((s, x) => s + 2 + x.length, 0) + 4 + f.xy.length * 8;
  const out = new Uint8Array(len);
  const dv = new DataView(out.buffer);
  let o = 0;
  dv.setUint8(o, KIND_CODE[f.kind]); o += 1;
  dv.setUint16(o, f.file, true); o += 2;
  dv.setUint32(o, f.record, true); o += 4;
  dv.setUint16(o, f.part, true); o += 2;
  dv.setUint8(o, nums.length); o += 1;
  dv.setUint8(o, texts.length); o += 1;
  for (const v of nums) { dv.setFloat64(o, v, true); o += 8; }
  for (const x of texts) { if (x.length > 0xffff) throw new Error("text over 65535 bytes"); dv.setUint16(o, x.length, true); o += 2; out.set(x, o); o += x.length; }
  dv.setUint32(o, f.xy.length / 2, true); o += 4;
  for (let i = 0; i < f.xy.length; i++) { dv.setFloat64(o, f.xy[i]!, true); o += 8; }
  return out;
}

const td = new TextDecoder("utf-8", { fatal: true, ignoreBOM: false });
const KIND_OF: Record<number, FeatureKind> = { 1: "water", 2: "rivers", 3: "roads", 4: "peaks", 5: "places" };

/** The inverse of encodeFeature, at `o`; returns the feature and where the next one starts. A record that
 *  runs past the buffer, or an unknown kind, is refused. */
export function decodeFeature(buf: Uint8Array, o: number): { f: Feature; end: number } {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const need = (n: number) => { if (o + n > buf.length) throw new Error(`feature runs past its buffer at ${o}`); };
  need(11);
  const kind = KIND_OF[dv.getUint8(o)];
  if (!kind) throw new Error(`unknown feature kind ${dv.getUint8(o)} at ${o}`);
  const file = dv.getUint16(o + 1, true), record = dv.getUint32(o + 3, true), part = dv.getUint16(o + 7, true);
  const nNums = dv.getUint8(o + 9), nTexts = dv.getUint8(o + 10);
  o += 11;
  const nums: number[] = [], texts: string[] = [];
  for (let i = 0; i < nNums; i++) { need(8); nums.push(dv.getFloat64(o, true)); o += 8; }
  for (let i = 0; i < nTexts; i++) { need(2); const n = dv.getUint16(o, true); o += 2; need(n); texts.push(td.decode(buf.subarray(o, o + n))); o += n; }
  need(4);
  const n = dv.getUint32(o, true); o += 4;
  need(n * 16);
  const xy = new Float64Array(n * 2);
  for (let i = 0; i < n * 2; i++) xy[i] = dv.getFloat64(o + 8 * i, true);
  o += n * 16;
  const attrs: Record<string, string | number> =
    kind === "water" ? { AWATER: nums[0]! } : kind === "rivers" ? { FULLNAME: texts[0]! }
    : kind === "roads" ? { MTFCC: texts[0]!, FULLNAME: texts[1]! }
    : kind === "peaks" ? { feature_id: nums[0]!, feature_name: texts[0]! }
    : { GEOID: texts[0]!, NAME: texts[1]!, pop: nums[0]! };
  return { f: { kind, file, record, part, attrs, xy }, end: o };
}

/** A cell object's bytes: "BMSC", version 1, the feature count, then the features (A1.7 MI5/MI6's cells). */
export function encodeCell(features: Uint8Array[]): Uint8Array {
  const n = features.reduce((a, f) => a + f.length, 12);
  const out = new Uint8Array(n), dv = new DataView(out.buffer);
  out.set([0x42, 0x4d, 0x53, 0x43], 0); dv.setUint32(4, 1, true); dv.setUint32(8, features.length, true);
  let o = 12;
  for (const f of features) { out.set(f, o); o += f.length; }
  return out;
}


// ------------------------------------------------------------------------------------------------ coverage

/** Whether the lattice point (h, c), in hundredths, is covered. */
export function covered(bin: Uint8Array, h: number, c: number): boolean {
  const dv = new DataView(bin.buffer, bin.byteOffset, bin.byteLength);
  if (dv.getUint32(0, false) !== 0x424d4356) throw new Error("not a coverage.bin");
  const nRows = dv.getUint32(8, true);
  let o = 12;
  for (let r = 0; r < nRows; r++) {
    const rh = dv.getInt32(o, true), n = dv.getUint32(o + 4, true);
    if (rh === h) {
      for (let k = 0; k < n; k++) { const a = dv.getInt32(o + 8 + 8 * k, true), b = dv.getInt32(o + 12 + 8 * k, true); if (c >= a && c <= b) return true; }
      return false;
    }
    if (rh > h) return false;
    o += 8 + 8 * n;
  }
  return false;
}

// ------------------------------------------------------------------------------------------------ keys and the index

/** §2.3's keys. `current` is written last, only after the store verifies. */
export const storeKeys = (id: string) => ({
  manifest: `bm/v1/${id}/manifest.json`,
  index: `bm/v1/${id}/index.json`,
  coverage: `bm/v1/${id}/coverage.bin`,
  terrainSources: `bm/v1/${id}/terrain-sources.bin`,
  cell: (key: string) => `bm/v1/${id}/cells/${key}.bin`,
  tile: (x: number, y: number) => `bm/v1/${id}/terrain/9/${x}/${y}.png`,
});
export const CURRENT_KEY = "bm/v1/current";

export interface CellIndexEntry { key: string; bytes: number; count: number; reach: Box }
/** RD-P6: a county's or state's box, and the files that are READ when it meets the square's latitude/longitude box. */
export interface Region { box: Box; files: string[] }
/** index.json. The regions travel here, not in an object of their own, so a vector build reads the five objects MW9
 *  counts (current, manifest, coverage, index, the cells). */
export interface StoreIndex { cells: CellIndexEntry[]; regions: Region[] }

/** The closed box test RD-P6 rules: [minLon, minLat, maxLon, maxLat] against the same. */
export const boxMeets = (a: Box, b: Box) => a[0] <= b[2] && a[2] >= b[0] && a[1] <= b[3] && a[3] >= b[1];

/** A cell object's features ("BMSC", version 1). A count that disagrees with the bytes is refused. */
export function decodeCell(b: Uint8Array): Feature[] {
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (b.length < 12 || dv.getUint32(0, false) !== 0x424d5343 || dv.getUint32(4, true) !== 1) throw new Error("not a BMSC v1 cell");
  const n = dv.getUint32(8, true), out: Feature[] = [];
  let o = 12;
  for (let i = 0; i < n; i++) { const r = decodeFeature(b, o); out.push(r.f); o = r.end; }
  if (o !== b.length) throw new Error(`cell holds ${b.length - o} bytes past its ${n} features`);
  return out;
}

// ------------------------------------------------------------------------------------------------ tile-source bits

/** terrain-sources.bin ("BMTS", version 1): each mirrored tile's source bits (SRTM 1, GMTED2010 2, ETOPO1 4, NRCAN 8). */
export function tileBitsOf(bin: Uint8Array): Map<string, number> {
  const dv = new DataView(bin.buffer, bin.byteOffset, bin.byteLength);
  if (bin.length < 12 || dv.getUint32(0, false) !== 0x424d5453 || dv.getUint32(4, true) !== 1) throw new Error("not a BMTS v1 file");
  const n = dv.getUint32(8, true);
  if (bin.length !== 12 + 5 * n) throw new Error("terrain-sources.bin length disagrees with its count");
  const out = new Map<string, number>();
  for (let i = 0; i < n; i++) out.set(`${dv.getUint16(12 + 5 * i, true)}/${dv.getUint16(14 + 5 * i, true)}`, dv.getUint8(16 + 5 * i));
  return out;
}

// ------------------------------------------------------------------------------------------------ PNG (Terrarium tiles)

const PNG_SIG = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

async function inflateZlib(data: Uint8Array): Promise<Uint8Array> {
  const stream = new Blob([data]).stream().pipeThrough(new DecompressionStream("deflate"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

/** A 256 x 256, 8-bit, non-interlaced RGB or RGBA PNG as 256 * 256 * 3 RGB bytes. Anything else is refused: a
 *  Terrarium tile is RGB, and a decoder that guessed at another form would hand the hillshade invented elevations. */
export async function decodeTerrariumPng(png: Uint8Array): Promise<Uint8Array> {
  if (png.length < 8 || PNG_SIG.some((v, i) => png[i] !== v)) throw new Error("not a PNG");
  const dv = new DataView(png.buffer, png.byteOffset, png.byteLength);
  let o = 8, w = 0, h = 0, ctype = -1;
  const idat: Uint8Array[] = [];
  for (;;) {
    if (o + 12 > png.length) throw new Error("PNG ends without IEND");
    const len = dv.getUint32(o, false), type = String.fromCharCode(png[o + 4]!, png[o + 5]!, png[o + 6]!, png[o + 7]!);
    if (o + 12 + len > png.length) throw new Error(`PNG chunk ${type} runs past the file`);
    const data = png.subarray(o + 8, o + 8 + len);
    if (type === "IHDR") {
      w = dv.getUint32(o + 8, false); h = dv.getUint32(o + 12, false); ctype = data[9]!;
      if (w !== 256 || h !== 256 || data[8] !== 8 || (ctype !== 2 && ctype !== 6) || data[10] !== 0 || data[11] !== 0 || data[12] !== 0) {
        throw new Error(`PNG is not a 256 x 256 8-bit RGB(A) non-interlaced tile (type ${ctype})`);
      }
    } else if (type === "IDAT") idat.push(data);
    else if (type === "IEND") break;
    o += 12 + len;
  }
  if (ctype < 0 || !idat.length) throw new Error("PNG has no IHDR or no IDAT");
  const z = new Uint8Array(idat.reduce((a, d) => a + d.length, 0));
  let p = 0;
  for (const d of idat) { z.set(d, p); p += d.length; }
  const raw = await inflateZlib(z);
  const bpp = ctype === 2 ? 3 : 4, stride = w * bpp;
  if (raw.length !== h * (stride + 1)) throw new Error(`PNG image data is ${raw.length} B, not ${h * (stride + 1)}`);
  const cur = new Uint8Array(stride), prev = new Uint8Array(stride), out = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)]!, row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let i = 0; i < stride; i++) {
      const a = i >= bpp ? cur[i - bpp]! : 0, b = prev[i]!, c = i >= bpp ? prev[i - bpp]! : 0;
      let v: number;
      switch (f) {
        case 0: v = 0; break;
        case 1: v = a; break;
        case 2: v = b; break;
        case 3: v = (a + b) >> 1; break;
        case 4: { const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c); v = pa <= pb && pa <= pc ? a : pb <= pc ? b : c; break; }
        default: throw new Error(`PNG filter type ${f}`);
      }
      cur[i] = (row[i]! + v) & 0xff;
    }
    for (let x = 0; x < w; x++) { const s = x * bpp, d = (y * w + x) * 3; out[d] = cur[s]!; out[d + 1] = cur[s + 1]!; out[d + 2] = cur[s + 2]!; }
    prev.set(cur);
  }
  return out;
}
