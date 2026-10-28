// v1 coverage (docs/basemap-route.md §2.6, BQ4 ruled): a two-decimal lattice point is covered when it lies
// inside a tl_2024_us_state polygon of the 50 states + DC. PURE.
//
// Computed EXACTLY for every lattice point, by scanline at each point's own double: latitude h/100 and
// longitude k/100, the values crop.ts's quantise hands the route. So the request-time test is a lookup, and
// there is no tolerance anywhere for a quantised origin to fall through.
//
// Stored as, per latitude row h (hundredths), the covered longitude intervals [k0, k1] (inclusive, in
// hundredths). The even-odd rule is applied per state; states do not overlap, so their union is the union of
// their intervals. A point exactly on a ring edge follows the half-open crossing rule (y1 > y) != (y2 > y).

export interface Ring { xy: Float64Array } // [lon, lat, ...], closed or not

/** The covered lattice of a set of states (each a list of rings). Rows outside every state are absent. */
export function coverageRows(states: Ring[][]): Map<number, Array<[number, number]>> {
  const rows = new Map<number, Array<[number, number]>>();
  for (const rings of states) {
    // bucket this state's edges by the lattice rows they cross
    const byRow = new Map<number, number[]>(); // row -> [x1, y1, x2, y2, ...]
    for (const { xy } of rings) {
      const n = xy.length / 2;
      for (let i = 0; i < n; i++) {
        const j = (i + 1) % n;
        const x1 = xy[i * 2]!, y1 = xy[i * 2 + 1]!, x2 = xy[j * 2]!, y2 = xy[j * 2 + 1]!;
        if (y1 === y2) continue; // a horizontal edge never crosses a row under the half-open rule
        const lo = Math.ceil(Math.min(y1, y2) * 100) - 1, hi = Math.floor(Math.max(y1, y2) * 100) + 1;
        for (let h = lo; h <= hi; h++) (byRow.get(h) ?? byRow.set(h, []).get(h)!).push(x1, y1, x2, y2);
      }
    }
    for (const [h, e] of byRow) {
      const y = h / 100;
      const xs: number[] = [];
      for (let k = 0; k < e.length; k += 4) {
        const x1 = e[k]!, y1 = e[k + 1]!, x2 = e[k + 2]!, y2 = e[k + 3]!;
        if ((y1 > y) !== (y2 > y)) xs.push(x1 + (y - y1) * (x2 - x1) / (y2 - y1));
      }
      // Every ring is closed implicitly ((i + 1) % n above), so under the half-open rule each ring crosses a
      // row an even number of times. No "odd crossings" guard: it could never fire, and would only look like one.
      xs.sort((a, b) => a - b);
      const out = rows.get(h) ?? [];
      for (let k = 0; k < xs.length; k += 2) {
        // lattice longitudes c/100 with xa <= c/100 < xb, decided on the doubles themselves
        const xa = xs[k]!, xb = xs[k + 1]!;
        let c0 = Math.ceil(xa * 100); while ((c0 - 1) / 100 >= xa) c0--; while (c0 / 100 < xa) c0++;
        let c1 = Math.ceil(xb * 100) - 1; while ((c1 + 1) / 100 < xb) c1++; while (c1 / 100 >= xb) c1--;
        if (c1 >= c0) out.push([c0, c1]);
      }
      if (out.length) rows.set(h, out);
    }
  }
  for (const [h, iv] of rows) rows.set(h, mergeIntervals(iv));
  return rows;
}

export function mergeIntervals(iv: Array<[number, number]>): Array<[number, number]> {
  const s = [...iv].sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const out: Array<[number, number]> = [];
  for (const [a, b] of s) {
    const last = out[out.length - 1];
    if (last && a <= last[1] + 1) last[1] = Math.max(last[1], b); else out.push([a, b]);
  }
  return out;
}

/** coverage.bin: "BMCV", u32 version 1, u32 row count; per row i32 h, u32 n, then n x (i32 c0, i32 c1). Rows ascending. */
export function encodeCoverage(rows: Map<number, Array<[number, number]>>): Uint8Array {
  const hs = [...rows.keys()].sort((a, b) => a - b);
  const size = 12 + hs.reduce((a, h) => a + 8 + rows.get(h)!.length * 8, 0);
  const out = new Uint8Array(size), dv = new DataView(out.buffer);
  out.set([0x42, 0x4d, 0x43, 0x56]); dv.setUint32(4, 1, true); dv.setUint32(8, hs.length, true);
  let o = 12;
  for (const h of hs) {
    const iv = rows.get(h)!;
    dv.setInt32(o, h, true); dv.setUint32(o + 4, iv.length, true); o += 8;
    for (const [a, b] of iv) { dv.setInt32(o, a, true); dv.setInt32(o + 4, b, true); o += 8; }
  }
  return out;
}

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
