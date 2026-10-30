// A minimal ESRI shapefile + dBASE reader for the basemap ingest. PURE (Uint8Array in, records out), so the
// workers pool can test it. It reads exactly what the sources use -- null, PolyLine (3), Polygon (5) and Point
// (1) -- and REFUSES any other shape type rather than skipping it: a skipped record is a feature silently gone.
//
// Coordinates are read with DataView.getFloat64, so every vertex is the source's own IEEE double, bit for bit
// (A1.7 MI3). Nothing here rounds, projects or simplifies.
import { unzipSync } from "fflate";

export interface ShpRecord {
  /** 0-based record number in the .shp, i.e. RD2's record index. */
  index: number;
  /** Each part's vertices as [x0, y0, x1, y1, ...] (x = lon, y = lat). Empty for a null shape. */
  parts: Float64Array[];
  attrs: Record<string, string>;
}

const td = new TextDecoder("utf-8", { fatal: true, ignoreBOM: false });

/** The .shp, .dbf (and the .cpg, if any) of the first shapefile in a zip; refuses a zip with none or two. */
export function shapefileFromZip(zip: Uint8Array): { shp: Uint8Array; dbf: Uint8Array } {
  const files = unzipSync(zip, { filter: (f) => /\.(shp|dbf)$/i.test(f.name) });
  const names = Object.keys(files);
  const shp = names.filter((n) => /\.shp$/i.test(n)), dbf = names.filter((n) => /\.dbf$/i.test(n));
  if (shp.length !== 1 || dbf.length !== 1) throw new Error(`zip holds ${shp.length} .shp and ${dbf.length} .dbf, not one each`);
  return { shp: files[shp[0]!]!, dbf: files[dbf[0]!]! };
}

/** dBASE III records as strings (trimmed of the right-padding dBASE adds), in file order. */
export function readDbf(b: Uint8Array): Array<Record<string, string>> {
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const n = dv.getUint32(4, true), headerLen = dv.getUint16(8, true), recLen = dv.getUint16(10, true);
  const fields: Array<{ name: string; len: number }> = [];
  for (let o = 32; o < headerLen - 1 && b[o] !== 0x0d; o += 32) {
    const raw = b.subarray(o, o + 11);
    const end = raw.indexOf(0);
    fields.push({ name: td.decode(raw.subarray(0, end < 0 ? 11 : end)), len: b[o + 16]! });
  }
  const out: Array<Record<string, string>> = [];
  for (let i = 0; i < n; i++) {
    let o = headerLen + i * recLen;
    if (o + recLen > b.length) throw new Error(`dbf truncated at record ${i} of ${n}`);
    o += 1; // the deletion flag
    const rec: Record<string, string> = {};
    for (const f of fields) { rec[f.name] = td.decode(b.subarray(o, o + f.len)).replace(/[ \u0000]+$/, "").replace(/^ +/, ""); o += f.len; }
    out.push(rec);
  }
  return out;
}

/** Every record of a .shp, with its .dbf attributes joined by position. Refuses a count mismatch. */
export function readShapefile(shp: Uint8Array, dbf: Uint8Array): ShpRecord[] {
  const attrs = readDbf(dbf);
  const dv = new DataView(shp.buffer, shp.byteOffset, shp.byteLength);
  if (dv.getInt32(0, false) !== 9994) throw new Error("not a shapefile (file code)");
  const fileLen = dv.getInt32(24, false) * 2;
  if (fileLen !== shp.byteLength) throw new Error(`shp length ${shp.byteLength} != header ${fileLen}`);
  const out: ShpRecord[] = [];
  let o = 100;
  while (o < fileLen) {
    const contentLen = dv.getInt32(o + 4, false) * 2;
    const c = o + 8;
    const type = dv.getInt32(c, true);
    const index = out.length;
    let parts: Float64Array[] = [];
    if (type === 0) parts = [];
    else if (type === 1) parts = [new Float64Array([dv.getFloat64(c + 4, true), dv.getFloat64(c + 12, true)])];
    else if (type === 3 || type === 5) {
      const nParts = dv.getInt32(c + 36, true), nPoints = dv.getInt32(c + 40, true);
      const starts: number[] = [];
      for (let p = 0; p < nParts; p++) starts.push(dv.getInt32(c + 44 + 4 * p, true));
      const pts = c + 44 + 4 * nParts;
      for (let p = 0; p < nParts; p++) {
        const a = starts[p]!, z = p + 1 < nParts ? starts[p + 1]! : nPoints;
        const xy = new Float64Array((z - a) * 2);
        for (let k = a; k < z; k++) { xy[(k - a) * 2] = dv.getFloat64(pts + 16 * k, true); xy[(k - a) * 2 + 1] = dv.getFloat64(pts + 16 * k + 8, true); }
        parts.push(xy);
      }
    } else throw new Error(`shape type ${type} at record ${index}: not read, refused`);
    if (index >= attrs.length) throw new Error(`more shapes than dbf records (${attrs.length})`);
    out.push({ index, parts, attrs: attrs[index]! });
    o = c + contentLen;
  }
  if (out.length !== attrs.length) throw new Error(`${out.length} shapes but ${attrs.length} dbf records`);
  return out;
}

/** Twice the signed shoelace area of an [x, y, ...] ring. A shapefile's outer rings are clockwise (negative
 *  here, with y up) and its holes counter-clockwise (positive) -- the ESRI convention RD3 relies on. */
export function signedArea2(xy: Float64Array): number {
  let s = 0;
  const n = xy.length / 2;
  for (let i = 0, j = n - 1; i < n; j = i++) s += xy[j * 2]! * xy[i * 2 + 1]! - xy[i * 2]! * xy[j * 2 + 1]!;
  return s;
}
