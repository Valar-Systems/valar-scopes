// A tiny synthetic MRMS-shaped GRIB2 (.grib2.gz) for the ingest tests: exactly the subset mrms.ts accepts
// (edition 2, grid template 3.0, data template 5.41 PNG 16-bit grey, no bitmap). No MRMS file is committed,
// so the ingest is driven through the real decoder with one of these. The section offsets are the ones
// parseGrib2 reads; PNG chunk CRCs are written as 0 (the decoder does not read them).

export interface SynthGrib { ni: number; nj: number; la1: number; lo1: number; di: number; dj: number; values: number[] }

const u32 = (v: number) => [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
const u16 = (v: number) => [(v >>> 8) & 255, v & 255];
const s32 = (v: number) => u32(v < 0 ? (0x80000000 | -v) >>> 0 : v);   // GRIB2 sign-and-magnitude

async function compress(data: Uint8Array, format: "gzip" | "deflate"): Promise<Uint8Array> {
  const out = new Response(new Blob([data]).stream().pipeThrough(new CompressionStream(format)));
  return new Uint8Array(await out.arrayBuffer());
}

function section(n: number, body: number[]): number[] { return [...u32(5 + body.length), n, ...body]; }

/** values: row-major from the NW cell, in dBZ; -999 = no coverage, -99 = no echo (R = -999, E = 0, D = 0,
 *  so a stored X is value + 999). */
export async function synthGribGz(g: SynthGrib): Promise<Uint8Array> {
  const rows: number[] = [];
  for (let r = 0; r < g.nj; r++) {
    rows.push(0);                                              // PNG filter: none
    for (let c = 0; c < g.ni; c++) rows.push(...u16(Math.round(g.values[r * g.ni + c]! + 999)));
  }
  const idat = await compress(new Uint8Array(rows), "deflate");
  const chunk = (t: string, d: number[] | Uint8Array) => [...u32(d.length), ...[...t].map((ch) => ch.charCodeAt(0)), ...d, 0, 0, 0, 0];
  const png = [137, 80, 78, 71, 13, 10, 26, 10,
    ...chunk("IHDR", [...u32(g.ni), ...u32(g.nj), 16, 0, 0, 0, 0]), ...chunk("IDAT", idat), ...chunk("IEND", [])];
  const deg = (x: number) => Math.round(x * 1e6);
  const lon360 = (x: number) => deg(x < 0 ? x + 360 : x);
  const s1 = section(1, [0, 7, 0, 0, 2, 1, 1, ...u16(2026), 10, 20, 20, 10, 36, 0, 0]);       // ref time at o+12..o+18
  const s3body = new Array(72 - 5).fill(0);
  const put = (o: number, b: number[]) => b.forEach((v, i) => { s3body[o - 5 + i] = v; });
  put(12, u16(0)); put(30, u32(g.ni)); put(34, u32(g.nj)); put(38, u32(0)); put(42, u32(0xffffffff));
  put(46, s32(deg(g.la1))); put(50, u32(lon360(g.lo1)));
  put(55, s32(deg(g.la1 - (g.nj - 1) * g.dj))); put(59, u32(lon360(g.lo1 + (g.ni - 1) * g.di)));
  put(63, u32(deg(g.di))); put(67, u32(deg(g.dj))); put(71, [0]);                            // scan: w->e, n->s
  const s3 = section(3, s3body);
  const R = new Uint8Array(new Float32Array([-999]).buffer).reverse();                       // big-endian float32
  const s5 = section(5, [...u32(g.ni * g.nj), ...u16(41), ...R, ...u16(0), ...u16(0), 16, 0]);
  const s6 = section(6, [255]);
  const s7 = section(7, png);
  const body = [...s1, ...s3, ...s5, ...s6, ...s7, 0x37, 0x37, 0x37, 0x37];
  const total = 16 + body.length;
  const s0 = [0x47, 0x52, 0x49, 0x42, 0, 0, 209, 2, 0, 0, 0, 0, ...u32(total)];
  return compress(new Uint8Array([...s0, ...body]), "gzip");
}
