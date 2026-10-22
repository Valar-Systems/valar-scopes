// A deterministic synthetic CONUS code grid for the weather tests (docs/wx-server.md, WS1-WS7). No MRMS
// file is committed (the spike's rule), so the tests need a grid with every feature a real frame has:
// dry areas (0), echoes of every strength (1-13) in blobs, a no-coverage region (14), and a missing patch
// (15; the composite does not use it, the encoder and the device decoder must still handle it).

import type { GridGeom } from "../src/wx/grid";

/** MRMS CONUS, as A0 read it: 7000 x 3500 at 0.01 deg, NW cell centre 54.995 N 129.995 W. */
export const CONUS: GridGeom = { ni: 7000, nj: 3500, la1: 54.995, lo1: -129.995, di: 0.01, dj: 0.01 };

function lcg(seed: number) {
  let s = seed >>> 0;
  return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
}

/** One code per cell, row-major from the NW cell. Same seed, same grid, on every machine. */
export function synthCodes(g: GridGeom = CONUS, seed = 20261020): Uint8Array {
  const codes = new Uint8Array(g.ni * g.nj);
  const rnd = lcg(seed);
  // storms: 400 blobs, each a few hundred km across at most, peak 6-13, falling off with distance
  for (let b = 0; b < 400; b++) {
    const cr = Math.floor(rnd() * g.nj), cc = Math.floor(rnd() * g.ni);
    const rad = 8 + Math.floor(rnd() * 70), peak = 6 + Math.floor(rnd() * 8);
    for (let r = Math.max(0, cr - rad); r < Math.min(g.nj, cr + rad); r++)
      for (let c = Math.max(0, cc - rad); c < Math.min(g.ni, cc + rad); c++) {
        const d = Math.hypot(r - cr, c - cc) / rad;
        if (d >= 1) continue;
        const v = Math.max(1, Math.round(peak * (1 - d) + (rnd() - 0.5) * 2));
        const i = r * g.ni + c;
        if (v > codes[i]!) codes[i] = Math.min(13, v);
      }
  }
  // no coverage: the Pacific west of 125.5 W, and a band in the far north-east
  for (let r = 0; r < g.nj; r++) {
    for (let c = 0; c < 450; c++) codes[r * g.ni + c] = 14;
    if (r < 300) for (let c = 6200; c < g.ni; c++) codes[r * g.ni + c] = 14;
  }
  // missing: one small patch in the middle of the country
  for (let r = 1700; r < 1740; r++) for (let c = 3400; c < 3460; c++) codes[r * g.ni + c] = 15;
  return codes;
}

export const flatCellAt = (g: GridGeom, codes: Uint8Array) => (r: number, c: number) =>
  (r < 0 || r >= g.nj || c < 0 || c >= g.ni) ? 14 : codes[r * g.ni + c]!;
