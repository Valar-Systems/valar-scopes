// The region table and dispatch by region (docs/wx-opera-code-stage.md §3 and §7): G1's header, D1, D2, D3, D5, A1, F2,
// R1 and R2, with the in-test sabotages D2s, D3s-b and R1s. The points are fixtures chosen by an independent
// implementation (tools/wx/eu_borders.py, shapely), never by the generator: country label points and border crossings,
// public geography. Each row logs one `RESULT <row> <json>` line, which proxy/scripts/sabotage-wx-region.py reads when it
// plants a regenerated table (D1s, D3s-a, D5s, F2s) or an edited row (A1s).
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { quantise } from "../src/wx/crop";
import { FrameBuilder, type GridGeom, frameMeta } from "../src/wx/grid";
import { type LatestRecord, handleWx } from "../src/wx/handler";
import { OPUK_LAND, PRODUCTS, type Product, ROUTED, frameKey, latestKey } from "../src/wx/products";
import { type DispatchOpts, decodeRegion, regionProductId, regionTable, servedProduct } from "../src/wx/region";
import { REGION_HEADER } from "../src/wx/region.generated";
import points from "./fixtures/wx/region-points.json";
import { apiRequest, call } from "./helpers";

const NE_SHA = "239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255";
const SLOW = 180_000;
const cref = PRODUCTS.find((p) => p.id === "cref")!;
const OPERA = PRODUCTS.filter((p) => p.source === "opera");
const ALL: DispatchOpts = { routed: new Set(PRODUCTS.map((p) => p.id)) };
const result = (row: string, v: unknown) => console.log(`RESULT ${row} ${JSON.stringify(v)}`);
const now = () => Math.floor(Date.now() / 1000);

/** As the server reads a query: quantise() at r 161, then dispatch. */
const served = (lat: number, lon: number, o: DispatchOpts = ALL) => {
  const q = quantise(lat, lon, 161);
  return servedProduct(cref, q.lat, q.lon, o).id;
};
/** As the device sends it (include/WireLocation.h): Round2, half away from zero, then "%.2f". */
const round2 = (v: number) => { const r = Math.sign(v) * Math.round(Math.abs(v) * 100) / 100; return r === 0 ? 0 : r; };
const deviceQuery = (lat: number, lon: number) => `lat=${round2(lat).toFixed(2)}&lon=${round2(lon).toFixed(2)}&r=161`;

/** A small synthetic frame at a product's grid corner, and its `latest`: the handler reads the frame's own geometry. */
async function storeFrame(p: Product, epoch: number) {
  const g: GridGeom = p.grid ? { la1: p.grid.la1, lo1: p.grid.lo1, ni: 64, nj: 64, di: 0.01, dj: 0.01 }
                             : { la1: 40.0, lo1: -100.0, ni: 64, nj: 64, di: 0.01, dj: 0.01 };
  const m = frameMeta(p.id, epoch, g);
  const fb = new FrameBuilder(m);
  const row = new Uint8Array(g.ni);
  for (let r = 0; r < g.nj; r++) { for (let c = 0; c < g.ni; c++) row[c] = (r * 7 + c * 3) % 14; fb.setRow(r, row); }
  await env.WX!.put(frameKey(p, epoch), fb.bytes);
  const rec: LatestRecord = { product: p.id, epoch, key: frameKey(p, epoch), ingestedAt: Date.now(), ...g, tile: m.tile };
  await env.WX!.put(latestKey(p), JSON.stringify(rec));
}
const wxDirect = (product: string, query: string, o: DispatchOpts = ALL, h: Record<string, string> = {}) =>
  handleWx(new Request(`https://proxy.test/api/v1/blipscope/wx/${product}?${query}`, { headers: h }), env, product, o);
const etagProduct = (r: Response) => /^"wx1-([a-z0-9]+)-/.exec(r.headers.get("ETag") ?? "")?.[1] ?? null;
/** One long run of code 14 covering every cell: 0xE0 then the LEB128 length (as test/wx-endpoint.test.ts). */
const allNoCoverage = (b: Uint8Array) => b[32] === 0xe0;

describe("G1: the generated table, against products.ts and its own header", () => {
  it("G1 the header names the pinned file, the 40, the UK box and the rules in PRODUCTS' order; every run is valid", () => {
    const h = REGION_HEADER;
    expect(h.sabotage).toBeNull();
    expect(h.sha256).toBe(NE_SHA);
    expect([...h.countries].sort()).toEqual(points.d1.map((e) => e.name).sort());
    expect(h.ukBox).toEqual({ latS: OPUK_LAND.latS, latN: OPUK_LAND.latN, lonW: OPUK_LAND.lonW, lonE: OPUK_LAND.lonE });
    expect(h.rules).toEqual(OPERA.map((p) => ({ id: p.id, latS: p.rule!.latS, latN: p.rule!.latN, lonW: p.rule!.lonW, lonE: p.rule!.lonE })));
    const t = decodeRegion();
    let bad = 0, points_ = 0;
    for (let i = 0; i < t.rows; i++) {
      for (let j = t.rowIndex[i]!; j < t.rowIndex[i + 1]!; j++) {
        const a = t.runs[j * 3]!, b = t.runs[j * 3 + 1]!, k = t.runs[j * 3 + 2]!;
        if (a > b || k >= t.ids.length || (j > t.rowIndex[i]! && a <= t.runs[(j - 1) * 3 + 1]!)) bad++;
        points_ += b - a + 1;
      }
    }
    result("G1", { rows: t.rows, runs: h.runs, points: points_, bad });
    expect([bad, points_]).toEqual([0, h.points]);
  });
});

describe("D1: the 40 countries' label points", () => {
  it("D1 each label point, quantised, is served by the fixture's product (closed edges: Lithuania opce, Q-EDGE)", () => {
    const bad = points.d1.filter((e) => served(e.lat, e.lon) !== e.product).map((e) => e.name);
    result("D1", bad);
    expect(points.d1).toHaveLength(40);
    expect(bad).toEqual([]);
  });
});

describe("D2: the 15 left-out countries' label points", () => {
  it("D2 all 15 get cref: a 200 with the all-14 crop, cref's ETag, no X-Wx-Max-Age", async () => {
    await storeFrame(cref, now() - 120);
    const bad: string[] = [];
    for (const e of points.d2) {
      if (served(e.lat, e.lon) !== "cref") { bad.push(e.name); continue; }
      const res = await wxDirect("cref", deviceQuery(e.lat, e.lon));
      const body = new Uint8Array(await res.arrayBuffer());
      if (res.status !== 200 || etagProduct(res) !== "cref" || !allNoCoverage(body) || res.headers.get("X-Wx-Max-Age") !== null) bad.push(e.name);
    }
    result("D2", bad);
    expect(points.d2).toHaveLength(15);
    expect(bad).toEqual([]);
  }, SLOW);
  it("D2s SABOTAGE: dispatch by grid instead of by region -- exactly 8 left-out label points leave cref", () => {
    const red = points.d2.filter((e) => served(e.lat, e.lon, { ...ALL, byWindow: true }) !== "cref").map((e) => e.name);
    result("D2s", red);
    expect(red.sort()).toEqual(["Albania", "Belarus", "Bulgaria", "Faroe Islands", "Italy", "Kosovo", "North Macedonia", "Ukraine"]);
  });
});

describe("D3: the 15 named crossings with an excluded country", () => {
  it("D3 the served side gets its product; the excluded side gets cref's all-14", async () => {
    await storeFrame(cref, now() - 120);
    const bad: string[] = [];
    for (const e of points.d3) {
      if (served(e.served.lat, e.served.lon) !== e.served.product) bad.push(`${e.crossing}: served side`);
      if (served(e.excluded.lat, e.excluded.lon) !== "cref") { bad.push(`${e.crossing}: excluded side`); continue; }
      const res = await wxDirect("cref", deviceQuery(e.excluded.lat, e.excluded.lon));
      const body = new Uint8Array(await res.arrayBuffer());
      if (res.status !== 200 || etagProduct(res) !== "cref" || !allNoCoverage(body)) bad.push(`${e.crossing}: excluded side's answer`);
    }
    result("D3", bad);
    expect(points.d3).toHaveLength(15);
    expect(bad).toEqual([]);
  }, SLOW);
  it("D3s-b SABOTAGE: dispatch by grid -- all 15 excluded sides leave cref", () => {
    const red = points.d3.filter((e) => served(e.excluded.lat, e.excluded.lon, { ...ALL, byWindow: true }) !== "cref").map((e) => e.crossing);
    result("D3s-b", red.length);
    expect(red).toHaveLength(15);
  });
});

describe("D5: today's UK land box (Q-UKBOX)", () => {
  it("D5 every lattice point in the box is served by opuk, under production's ROUTED", () => {
    let n = 0, notOpuk = 0;
    for (let r = Math.round(OPUK_LAND.latS * 100); r <= Math.round(OPUK_LAND.latN * 100); r++) {
      for (let c = Math.round(OPUK_LAND.lonW * 100); c <= Math.round(OPUK_LAND.lonE * 100); c++) {
        n++;
        if (servedProduct(cref, r / 100, c / 100).id !== "opuk") notOpuk++;
      }
    }
    result("D5", { points: n, notOpuk });
    expect([n, notOpuk]).toEqual([1_167_661, 0]);
  });
});

describe("A1: every OPERA product's age limits", () => {
  it("A1 maxAgeS 1,800 and deviceMaxAgeS 720 on every row; X-Wx-Max-Age 720 on its 200 and its 304", async () => {
    const bad: string[] = [];
    for (const p of OPERA) {
      if (p.maxAgeS !== 1800 || p.deviceMaxAgeS !== 720) { bad.push(`${p.id}: row`); continue; }
      await storeFrame(p, now() - 120);
      const g = p.grid!, q = `lat=${(g.la1 - g.nj * g.dj / 2).toFixed(2)}&lon=${(g.lo1 + g.ni * g.di / 2).toFixed(2)}&r=161`;
      const a = await wxDirect(p.id, q);
      const b = await wxDirect(p.id, q, ALL, { "If-None-Match": a.headers.get("ETag") ?? "" });
      if (a.status !== 200 || a.headers.get("X-Wx-Max-Age") !== "720" || b.status !== 304 || b.headers.get("X-Wx-Max-Age") !== "720") bad.push(p.id);
    }
    result("A1", bad);
    expect(OPERA).toHaveLength(10);
    expect(bad).toEqual([]);
  }, SLOW);
});

describe("F2: the server side, as the device asks", () => {
  it("F2 /wx/cref at the 40 label points, with the device's rounding: the fixture's product, X-Wx-Max-Age 720", async () => {
    const e0 = now() - 120;
    for (const p of PRODUCTS) await storeFrame(p, e0);
    const bad: string[] = [];
    for (const e of points.d1) {
      const res = await wxDirect("cref", deviceQuery(e.lat, e.lon));
      await res.arrayBuffer();
      if (res.status !== 200 || etagProduct(res) !== e.product || res.headers.get("X-Wx-Max-Age") !== "720") bad.push(e.name);
    }
    result("F2", bad);
    expect(bad).toEqual([]);
  }, SLOW);
});

describe("R1, R2: production's ROUTED (Q-ROUTED (a), ruled 2026-11-08 ~02:33Z)", () => {
  const isles = points.d1.filter((e) => e.product === "opuk");
  it("R1 through the Worker: the 5 isles label points get opuk with 720; the 35 others get cref's all-14 with none", async () => {
    expect([...ROUTED].sort()).toEqual(["cref", "opuk"]);
    const e0 = now() - 120;
    await storeFrame(cref, e0);
    await storeFrame(PRODUCTS.find((p) => p.id === "opuk")!, e0);
    const bad: string[] = [];
    for (const e of points.d1) {
      const res = await call(apiRequest(`/api/v1/blipscope/wx/cref?${deviceQuery(e.lat, e.lon)}`));
      const body = new Uint8Array(await res.arrayBuffer());
      const ok = e.product === "opuk"
        ? res.status === 200 && etagProduct(res) === "opuk" && res.headers.get("X-Wx-Max-Age") === "720"
        : res.status === 200 && etagProduct(res) === "cref" && allNoCoverage(body) && res.headers.get("X-Wx-Max-Age") === null;
      if (!ok) bad.push(e.name);
    }
    result("R1", { isles: isles.map((e) => e.name).sort(), bad });
    expect(isles.map((e) => e.name).sort()).toEqual(["Guernsey", "Ireland", "Isle of Man", "Jersey", "United Kingdom"]);
    expect(bad).toEqual([]);
  }, SLOW);
  it("R1s SABOTAGE: every product routed, with only cref's and opuk's frames stored -- the 35 get 503 wx_stale", async () => {
    const e0 = now() - 120;
    await storeFrame(cref, e0);
    await storeFrame(PRODUCTS.find((p) => p.id === "opuk")!, e0);
    let stale = 0;
    for (const e of points.d1) {
      const res = await wxDirect("cref", deviceQuery(e.lat, e.lon));
      const body = await res.text();
      if (res.status === 503 && body.includes("wx_stale")) stale++;
    }
    result("R1s", stale);
    expect(stale).toBe(35);
  }, SLOW);
  it("R2 (printed): the lattice points the next proxy deploy moves to opuk -- opuk's points outside today's box", () => {
    const t = regionTable(), k = t.ids.indexOf("opuk");
    const [bS, bN, bW, bE] = [OPUK_LAND.latS, OPUK_LAND.latN, OPUK_LAND.lonW, OPUK_LAND.lonE].map((v) => Math.round(v * 100));
    let outside = 0, all = 0;
    for (let i = 0; i < t.rows; i++) {
      const r = t.row0 - i;
      for (let j = t.rowIndex[i]!; j < t.rowIndex[i + 1]!; j++) {
        if (t.runs[j * 3 + 2] !== k) continue;
        const a = t.runs[j * 3]! + t.col0, b = t.runs[j * 3 + 1]! + t.col0;
        all += b - a + 1;
        const inBox = r >= bS! && r <= bN! ? Math.max(0, Math.min(b, bE!) - Math.max(a, bW!) + 1) : 0;
        outside += b - a + 1 - inBox;
      }
    }
    result("R2", { opuk: all, outsideTheBox: outside });
    const uk = points.d1.find((e) => e.name === "United Kingdom")!, q = quantise(uk.lat, uk.lon, 161);
    expect(regionProductId(q.lat, q.lon)).toBe("opuk");   // CONTROL: the lookup this count walks answers opuk at all
    expect(outside).toBe(48_721);
  });
});
