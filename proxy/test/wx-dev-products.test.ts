// Every OPERA product's window (W1, docs/wx-opera-code-stage.md §3, from the M stage's WO1-WO5) and the dev copy's
// products (DP2, DP3). The geometry is the real frame head's, so the pixel counts are the ones a build would read.

import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { type DevEnv, DEV_PRODUCTS, devCheck } from "../src/wx/ingest-dev-worker";
import { HEAD_BYTES, type OperaGeo, OperaRefused, type OverrunPx, parseHead, windowFor } from "../src/wx/opera";
import { OPUK_FIXTURE_GRID, PRODUCTS, type Product } from "../src/wx/products";
import { buildMap, resample } from "../src/wx/resample";
import dryBin from "./fixtures/wx/opera/20261029T0130.ranges.bin";
import dryExp from "./fixtures/wx/opera/20261029T0130.expected.json";

const geo: OperaGeo = parseHead(new Uint8Array(dryBin.slice(0, HEAD_BYTES)));
const byId = (id: string): Product => PRODUCTS.find((p) => p.id === id)!;
const OPERA = PRODUCTS.filter((p) => p.source === "opera");
const px = (w: [number, number, number, number]) => (w[1] - w[0] + 1) * (w[3] - w[2] + 1);
const insideRaster = (w: [number, number, number, number]) =>
  w[0] >= 0 && w[2] >= 0 && w[1] < geo.height && w[3] < geo.width;
const refused = (f: () => unknown) => {
  try { f(); return false; } catch (err) {
    if (err instanceof OperaRefused && String(err).includes("the window leaves the raster")) return true;
    throw err;
  }
};

describe("W1: every OPERA product's window, with its stated overrun", () => {
  const stated = OPERA.filter((p) => p.overrunPx);
  it("W1 all 10 inside the raster; each stated side reaches the raster's edge exactly; the other 5 need none", () => {
    const rows = OPERA.map((p) => {
      const w = windowFor(geo, p.grid!, undefined, { overrunPx: p.overrunPx });
      const o = p.overrunPx ?? {};
      const edges = (o.n === undefined || w[0] === 0) && (o.s === undefined || w[1] === geo.height - 1) &&
                    (o.w === undefined || w[2] === 0) && (o.e === undefined || w[3] === geo.width - 1);
      console.log(`W1 ${p.id}: window ${w.join(", ")} = ${px(w)} px, overrun ${JSON.stringify(o)}`);
      return { id: p.id, inside: insideRaster(w), edges };
    });
    expect(rows.filter((r) => !r.inside || !r.edges).map((r) => r.id)).toEqual([]);
    expect(stated.map((p) => p.id)).toEqual(["opis", "opib", "opmt", "opse", "opsn"]);
    const none = OPERA.filter((p) => !p.overrunPx);
    expect(none.filter((p) => refused(() => windowFor(geo, p.grid!))).map((p) => p.id)).toEqual([]);
  });
  it("W1 one pixel short of each stated overrun is refused", () => {
    const notRefused: string[] = [];
    for (const p of stated) {
      for (const side of Object.keys(p.overrunPx!) as (keyof OverrunPx)[]) {
        const short = { ...p.overrunPx!, [side]: p.overrunPx![side]! - 1 };
        if (!refused(() => windowFor(geo, p.grid!, undefined, { overrunPx: short }))) notRefused.push(`${p.id} ${side}`);
      }
    }
    expect(notRefused).toEqual([]);
  });
  it("W1s SABOTAGE: each of the 5 with its overrun removed is refused, and so is the fixture grid 12 deg north", () => {
    expect(stated.filter((p) => !refused(() => windowFor(geo, p.grid!))).map((p) => p.id)).toEqual([]);
    expect(refused(() => windowFor(geo, { ...OPUK_FIXTURE_GRID, la1: OPUK_FIXTURE_GRID.la1 + 12 }))).toBe(true);
  });
  it("SABOTAGE WO2s: an unbounded clamp -- not refused", () => {
    expect(refused(() => windowFor(geo, byId("opsn").grid!, undefined, { overrunPx: { n: 53 }, allowAny: true }))).toBe(false);
  });
  it("WO4: on opmt's clipped window, a cell past the raster has fill -1 and reads 14", () => {
    const p = byId("opmt"), g = p.grid!;
    const win = windowFor(geo, g, undefined, { overrunPx: p.overrunPx });
    const m = buildMap(geo, win, g);
    let past = 0;
    for (let c = 0; c < g.ni * g.nj; c++) if (m.fill[c] === -1) past++;
    const n = (win[1] - win[0] + 1) * (win[3] - win[2] + 1);
    const { codes } = resample(new Float32Array(n).fill(Number.NaN), new Float32Array(n), m, g);   // all undetect
    let notFourteen = 0;
    for (let c = 0; c < g.ni * g.nj; c++) if (m.fill[c] === -1 && codes[c] !== 14) notFourteen++;
    console.log(`WO4 opmt: cells with fill -1 ${past}; of those not 14: ${notFourteen}`);
    expect(past).toBeGreaterThan(0);
    expect(notFourteen).toBe(0);
  });
  it("WO5 (control): the fixture grid's window is unchanged -- the fixture's, 2,224,675 px", () => {
    const w = windowFor(geo, OPUK_FIXTURE_GRID);
    expect(w).toEqual(dryExp.window);
    expect(px(w)).toBe(2_224_675);
  });
});

describe("DP: the products, and the dev copy's", () => {
  const IDS = ["cref", "opuk", "opis", "opib", "opmt", "opwe", "opce", "opse", "opba", "opsw", "opsn", "shsr"];  // W4: shsr last
  const clean = (list: readonly Product[]) => JSON.stringify(list.map((p) => p.id)) === JSON.stringify(IDS);
  it("DP2: PRODUCTS is exactly the 12, in rule order, and DEV_PRODUCTS is empty", () => {
    expect(clean(PRODUCTS)).toBe(true);
    expect(DEV_PRODUCTS).toEqual([]);
  });
  it("SABOTAGE DP2s: a list with one more product fails the same check", () => {
    expect(clean([...PRODUCTS, { ...byId("opmt"), id: "opxx" }])).toBe(false);
  });
  it("DP3: an unknown MODE (and opux, now opuk) logs wx_dev_mode_error and touches no bucket; MODE opmt reaches its production row alone", async () => {
    for (const mode of ["nope", "opux"]) {
      const seen: string[] = [];
      let touched = 0;
      const WX = new Proxy(env.WX!, {
        get(t, prop) {
          const v = Reflect.get(t, prop);
          if (typeof v === "function") return (...a: unknown[]) => { touched++; return (v as (...x: unknown[]) => unknown).apply(t, a); };
          return v;
        },
      }) as R2Bucket;
      await devCheck({ WX, MODE: mode } as DevEnv, ":00", undefined, (l) => seen.push(l));
      expect(touched, mode).toBe(0);
      expect(JSON.parse(seen[0]!).evt, mode).toBe("wx_dev_mode_error");
    }

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
