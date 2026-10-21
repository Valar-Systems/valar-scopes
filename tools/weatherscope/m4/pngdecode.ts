// MRMS GRIB2 (template 5.41, PNG) decode for 8 OR 16 bits per value. Measurement 4 only (docs/wxc1-v2.md,
// Addendum 5).
//
// WHY THIS EXISTS: PrecipFlag_00.00 is packed at 8 bits per value (GRIB section 5 and the PNG IHDR agree, read
// from two files on different days), and the spike's mrms.ts, pinned by Stage 2, refuses anything but 16
// (mrms.ts:93, :108). This is that decoder's row loop with one generalisation, bytes per sample = bits / 8, and
// nothing else changed. It reuses mrms.ts's own parseGrib2 for the GRIB sections.
//
// ITS ANCHOR (C-M4e): on every 16-bit SeamlessHSR and composite file of the run, it must reproduce mrms.ts's
// values exactly, cell for cell, so the shared path is proven on files Stage 2 already trusted. The 8-bit branch
// differs only in the sample width; C-M4a's census and its two sabotages, and C-M4c, cover it.
import { pathToFileURL } from "node:url";

export async function loadDecoder(spike: string) {
  const { parseGrib2, MrmsRefused } = await import(pathToFileURL(`${spike}/tools/wx/mrms.ts`).href);
  const u32 = (b: Uint8Array, o: number) => ((b[o] << 24) >>> 0) + (b[o + 1] << 16) + (b[o + 2] << 8) + b[o + 3];

  async function inflateAll(data: Uint8Array, format: "gzip" | "deflate"): Promise<Uint8Array> {
    const out = new Response(new Blob([data]).stream().pipeThrough(new DecompressionStream(format)));
    return new Uint8Array(await out.arrayBuffer());
  }

  // The bit depth is the CALLER'S EXPECTATION for its product, refused on mismatch, never "8 or 16" for any file
  // (agreed with blipscope-5c for the Worker's product table: SeamlessHSR and the composite 16, PrecipFlag 8).
  // A shared "accept either" would let a reflectivity file arriving at 8 bits decode silently, at a different
  // quantisation; mrms.ts's 16-bit refusal is what catches that today.
  // onRow(row, values, samples): samples are the raw unpacked integers X.
  return async function decodeAnyWidth(gz: Uint8Array, expectBits: 8 | 16,
      onRow: (row: number, values: Float32Array, samples: Uint32Array) => void): Promise<any> {
    if (expectBits !== 8 && expectBits !== 16) throw new MrmsRefused(`expected bits ${expectBits}: only 8 or 16 exist here`);
    const g = await inflateAll(gz, "gzip");
    const { grid, png } = parseGrib2(g);
    if (grid.bits !== expectBits) throw new MrmsRefused(`bits per value ${grid.bits}, this product expects ${expectBits}`);
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
    if (depth !== grid.bits || ctype !== 0) throw new MrmsRefused(`PNG depth ${depth} colour type ${ctype}, want ${grid.bits}-bit grey`);

    const scale = Math.pow(2, grid.E), div = Math.pow(10, grid.D), R = grid.R;
    const bpp = grid.bits / 8, rowBytes = w * bpp;     // the one generalisation
    let prev = new Uint8Array(rowBytes), cur = new Uint8Array(rowBytes);
    const vals = new Float32Array(w), xs = new Uint32Array(w);
    let fill = -1, filter = 0, row = 0;
    const finishRow = () => {
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
      for (let j = 0; j < w; j++) {
        const X = bpp === 2 ? ((cur[2 * j] << 8) | cur[2 * j + 1]) : cur[j];
        xs[j] = X;
        vals[j] = (R + X * scale) / div;
      }
      onRow(row, vals, xs);
      row++;
      const t = prev; prev = cur; cur = t;
    };
    const reader = new Blob(idat).stream().pipeThrough(new DecompressionStream("deflate")).getReader();
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
  };
}
