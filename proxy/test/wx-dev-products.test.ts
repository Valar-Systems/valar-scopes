// The M stage (docs/wx-opera-dev-measure.md): the stated overrun past the raster (WO1-WO5) and the dev-only product
// rows (DP1-DP3). The geometry is the real frame head's, so the pixel counts are the ones a build would read.

import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { type DevEnv, DEV_PRODUCTS, devCheck } from "../src/wx/ingest-dev-worker";
import { frameMeta, objectBytes } from "../src/wx/grid";
import { HEAD_BYTES, type OperaGeo, OperaRefused, parseHead, windowFor } from "../src/wx/opera";
import { OPUK_GRID, PRODUCTS, type Product } from "../src/wx/products";
import { buildMap, resample } from "../src/wx/resample";
import dryBin from "./fixtures/wx/opera/20261029T0130.ranges.bin";
import dryExp from "./fixtures/wx/opera/20261029T0130.expected.json";

const geo: OperaGeo = parseHead(new Uint8Array(dryBin.slice(0, HEAD_BYTES)));
const dev = (id: string): Product => DEV_PRODUCTS.find((p) => p.id === id)!;
const px = (w: [number, number, number, number]) => (w[1] - w[0] + 1) * (w[3] - w[2] + 1);
const insideRaster = (w: [number, number, number, number]) =>
  w[0] >= 0 && w[2] >= 0 && w[1] < geo.height && w[3] < geo.width;
const refused = (f: () => unknown) => {
  try { f(); return false; } catch (err) {
    if (err instanceof OperaRefused && String(err).includes("the window leaves the raster")) return true;
    throw err;
  }
};

describe("WO: the stated overrun past the raster", () => {
  it("WO1: opwe and opux inside; opsn clips exactly 54 px north, opmt exactly 43 px south", () => {
    const rows = ["opwe", "opsn", "opux", "opmt"].map((id) => {
      const p = dev(id);
      const w = windowFor(geo, p.grid!, undefined, { overrunPx: p.overrunPx });
      console.log(`WO1 ${id}: window ${w.join(", ")} = ${px(w)} px, overrun ${JSON.stringify(p.overrunPx ?? {})}`);
      return { id, inside: insideRaster(w), r0: w[0], r1: w[1] };
    });
    expect(rows.every((r) => r.inside)).toBe(true);
    expect(rows[1]!.r0).toBe(0);                             // opsn's north edge is the raster's
    expect(rows[3]!.r1).toBe(geo.height - 1);                // opmt's south edge is the raster's
    expect(rows[0]!.r0).toBeGreaterThan(0);                  // opwe stops short of both
    expect(rows[0]!.r1).toBeLessThan(geo.height - 1);
  });
  it("WO2: one pixel short of the stated overrun is refused", () => {
    expect(refused(() => windowFor(geo, dev("opsn").grid!, undefined, { overrunPx: { n: 53 } }))).toBe(true);
    expect(refused(() => windowFor(geo, dev("opmt").grid!, undefined, { overrunPx: { s: 42 } }))).toBe(true);
  });
  it("SABOTAGE WO2s: an unbounded clamp -- not refused", () => {
    expect(refused(() => windowFor(geo, dev("opsn").grid!, undefined, { overrunPx: { n: 53 }, allowAny: true }))).toBe(false);
  });
  it("WO3: with no overrun stated, opsn, opmt and OPUK_GRID 12 deg north are all refused", () => {
    expect(refused(() => windowFor(geo, dev("opsn").grid!))).toBe(true);
    expect(refused(() => windowFor(geo, dev("opmt").grid!))).toBe(true);
    expect(refused(() => windowFor(geo, { ...OPUK_GRID, la1: OPUK_GRID.la1 + 12 }))).toBe(true);
  });
  it("WO4: on opmt's clipped window, a cell past the raster has fill -1 and reads 14", () => {
    const p = dev("opmt"), g = p.grid!;
    const win = windowFor(geo, g, undefined, { overrunPx: p.overrunPx });
    const m = buildMap(geo, win, g);
    let past = 0;
    for (let c = 0; c < g.ni * g.nj; c++) if (m.fill[c] === -1) past++;
    const n = (win[1] - win[0] + 1) * (win[3] - win[2] + 1);
    const { codes } = resample(new Float32Array(n).fill(Number.NaN), new Float32Array(n), m, g);   // all undetect
    let notFourteen = 0;
    for (let c = 0; c < g.ni * g.nj; c++) if (m.fill[c] === -1 && codes[c] !== 14) notFourteen++;
    const lastRowCovered = codes.subarray((g.nj - 1) * g.ni).filter((v) => v !== 14).length;
    console.log(`WO4 opmt: cells with fill -1 ${past}; of those not 14: ${notFourteen}; last row not 14: ${lastRowCovered}`);
    expect(past).toBeGreaterThan(0);
    expect(notFourteen).toBe(0);
  });
  it("WO5 (control): OPUK_GRID's window is unchanged -- the fixture's, 2,224,675 px", () => {
    const w = windowFor(geo, OPUK_GRID);
    expect(w).toEqual(dryExp.window);
    expect(px(w)).toBe(2_224_675);
  });
});

describe("DP: the dev-only products", () => {
  it("DP1: the rows equal section 3 of the plan, as whole values", () => {
    const got = DEV_PRODUCTS.map((p) => {
      const g = p.grid!, m = frameMeta(p.id, 0, g);
      return [p.id, g.la1, g.lo1, g.ni, g.nj, g.di, g.dj, g.ni * g.nj, m.tilesX * m.tilesY, objectBytes(m), p.deviceMaxAgeS, p.maxAgeS];
    });
    console.log(`DP1: ${JSON.stringify(got)}`);
    expect(got).toEqual([
      ["opwe", 55.72, -7.15, 1968, 1638, 0.01, 0.01, 3_223_584, 16, 2_097_216, 720, 1800],
      ["opsn", 73.19, 5.16, 2832, 1222, 0.01, 0.01, 3_460_704, 18, 2_359_360, 720, 1800],
      ["opux", 62.92, -15.71, 1954, 1585, 0.01, 0.01, 3_097_090, 16, 2_097_216, 720, 1800],
      ["opmt", 38.1, 12.16, 444, 433, 0.01, 0.01, 192_252, 1, 131_136, 720, 1800],
    ]);
    expect(DEV_PRODUCTS.every((p) => p.landBox === undefined && p.source === "opera")).toBe(true);
  });
  const clean = (list: readonly Product[]) =>
    JSON.stringify(list.map((p) => p.id)) === JSON.stringify(["cref", "opuk"]) &&
    !list.some((p) => DEV_PRODUCTS.some((d) => d.id === p.id));
  it("DP2: production's list is exactly cref and opuk", () => { expect(clean(PRODUCTS)).toBe(true); });
  it("SABOTAGE DP2s: a list with opwe added fails the same check", () => {
    expect(clean([...PRODUCTS, dev("opwe")])).toBe(false);
  });
  it("DP3: an unknown MODE logs wx_dev_mode_error and touches no bucket; a dev MODE reaches that product alone", async () => {
    const seen: string[] = [];
    let touched = 0;
    const WX = new Proxy(env.WX!, {
      get(t, prop) {
        const v = Reflect.get(t, prop);
        if (typeof v === "function") return (...a: unknown[]) => { touched++; return (v as (...x: unknown[]) => unknown).apply(t, a); };
        return v;
      },
    }) as R2Bucket;
    await devCheck({ WX, MODE: "nope" } as DevEnv, ":00", undefined, (l) => seen.push(l));
    expect(touched).toBe(0);
    expect(JSON.parse(seen[0]!).evt).toBe("wx_dev_mode_error");

    const asked: string[] = [];
    const lines: string[] = [];
    const doFetch = async (u: string) => { asked.push(u); return new Response("<ListBucketResult></ListBucketResult>"); };
    await devCheck({ WX: env.WX!, MODE: "opmt" } as DevEnv, ":00", doFetch, (l) => lines.push(l));
    const products = lines.map((l) => JSON.parse(l).product);
    console.log(`DP3: MODE opmt -> lines for ${JSON.stringify(products)}, ${asked.length} request(s)`);
    expect(products.length).toBeGreaterThan(0);
    expect(new Set(products)).toEqual(new Set(["opmt"]));
  });
});
