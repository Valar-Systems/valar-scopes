// Test-only: writes tiny ESRI shapefiles and dBASE tables in memory, so the ingest's reader is exercised on
// real file structure (big-endian record headers, part tables, dBASE padding) rather than on hand-made objects.
import { zipSync } from "fflate";

export type ShapeIn = { type: 1 | 3 | 5; parts: number[][] /* [[x0,y0,x1,y1,...], ...] */ };

export function writeShp(shapes: ShapeIn[]): Uint8Array {
  const recs = shapes.map((s) => {
    const n = s.parts.reduce((a, p) => a + p.length / 2, 0);
    const len = s.type === 1 ? 20 : 44 + 4 * s.parts.length + 16 * n;
    const b = new Uint8Array(len), dv = new DataView(b.buffer);
    dv.setInt32(0, s.type, true);
    if (s.type === 1) { dv.setFloat64(4, s.parts[0]![0]!, true); dv.setFloat64(12, s.parts[0]![1]!, true); return b; }
    const xs = s.parts.flatMap((p) => p.filter((_, i) => i % 2 === 0)), ys = s.parts.flatMap((p) => p.filter((_, i) => i % 2 === 1));
    [Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys)].forEach((v, i) => dv.setFloat64(4 + 8 * i, v, true));
    dv.setInt32(36, s.parts.length, true); dv.setInt32(40, n, true);
    let start = 0, o = 44 + 4 * s.parts.length;
    s.parts.forEach((p, i) => { dv.setInt32(44 + 4 * i, start, true); start += p.length / 2; });
    for (const p of s.parts) for (let k = 0; k < p.length; k += 2) { dv.setFloat64(o, p[k]!, true); dv.setFloat64(o + 8, p[k + 1]!, true); o += 16; }
    return b;
  });
  const total = 100 + recs.reduce((a, r) => a + 8 + r.length, 0);
  const out = new Uint8Array(total), dv = new DataView(out.buffer);
  dv.setInt32(0, 9994, false); dv.setInt32(24, total / 2, false); dv.setInt32(28, 1000, true); dv.setInt32(32, shapes[0]?.type ?? 0, true);
  let o = 100;
  recs.forEach((r, i) => { dv.setInt32(o, i + 1, false); dv.setInt32(o + 4, r.length / 2, false); out.set(r, o + 8); o += 8 + r.length; });
  return out;
}

export function writeDbf(fields: Array<{ name: string; len: number }>, rows: Array<Record<string, string>>): Uint8Array {
  const te = new TextEncoder();
  const headerLen = 32 + 32 * fields.length + 1, recLen = 1 + fields.reduce((a, f) => a + f.len, 0);
  const out = new Uint8Array(headerLen + recLen * rows.length + 1), dv = new DataView(out.buffer);
  out[0] = 0x03; dv.setUint32(4, rows.length, true); dv.setUint16(8, headerLen, true); dv.setUint16(10, recLen, true);
  fields.forEach((f, i) => { out.set(te.encode(f.name), 32 + 32 * i); out[32 + 32 * i + 11] = 0x43; out[32 + 32 * i + 16] = f.len; });
  out[headerLen - 1] = 0x0d;
  rows.forEach((r, k) => {
    let o = headerLen + k * recLen; out[o++] = 0x20;
    for (const f of fields) { const v = te.encode((r[f.name] ?? "").padEnd(f.len, " ")); out.set(v.subarray(0, f.len), o); o += f.len; }
  });
  out[out.length - 1] = 0x1a;
  return out;
}

export const zipShapefile = (base: string, shp: Uint8Array, dbf: Uint8Array) =>
  zipSync({ [`${base}.shp`]: shp, [`${base}.dbf`]: dbf, [`${base}.prj`]: new TextEncoder().encode("GEOGCS[]") });
