// The per-product age limit, the server's side (docs/wx-age-limit.md, A1-A7): X-Wx-Max-Age states the SERVED product's
// device limit on its 200s and 304s; a product that states none sends none; errors send none. Centres are public
// airports, named by code (test/fixtures/wx-airports-uk.txt; KMCI as test/wx-endpoint.test.ts has it).
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { FrameBuilder, type GridGeom, frameMeta } from "../src/wx/grid";
import { type LatestRecord, handleWx } from "../src/wx/handler";
import { OPUK_FIXTURE_GRID, PRODUCTS, type Product, frameKey, latestKey } from "../src/wx/products";
import airportsUk from "./fixtures/wx-airports-uk.txt";
import { apiRequest, call } from "./helpers";

const cref = PRODUCTS.find((p) => p.id === "cref")!;
const opuk = PRODUCTS.find((p) => p.id === "opuk")!;
const SMALL: GridGeom = { ni: 600, nj: 400, la1: 40.0, lo1: -100.0, di: 0.01, dj: 0.01 };   // cref's test grid: KMCI inside
const SLOW = 120_000;

const AP = new Map(airportsUk.split(/\r?\n/).filter((l) => l.trim() && !l.startsWith("#")).map((l) => {
  const [id, lat, lon] = l.trim().split(/\s+/);
  return [id!, { lat: Number(lat), lon: Number(lon) }] as const;
}));
AP.set("KMCI", { lat: 39.30, lon: -94.71 });
const q = (id: string) => { const a = AP.get(id)!; return `lat=${a.lat.toFixed(2)}&lon=${a.lon.toFixed(2)}&r=161`; };
const wxAt = (product: string, id: string, h: Record<string, string> = {}) =>
  call(apiRequest(`/api/v1/blipscope/wx/${product}?${q(id)}`, h));
const now = () => Math.floor(Date.now() / 1000);

async function storeFrame(p: Product, g: GridGeom, epoch: number) {
  const m = frameMeta(p.id, epoch, g);
  const fb = new FrameBuilder(m);
  const row = new Uint8Array(g.ni);
  for (let r = 0; r < g.nj; r++) { for (let c = 0; c < g.ni; c++) row[c] = (r * 7 + c * 3) % 14; fb.setRow(r, row); }
  await env.WX!.put(frameKey(p, epoch), fb.bytes);
  const rec: LatestRecord = { product: p.id, epoch, key: frameKey(p, epoch), ingestedAt: Date.now(), ...g, tile: m.tile };
  await env.WX!.put(latestKey(p), JSON.stringify(rec));
}
async function both(epoch = now() - 120) {
  await storeFrame(cref, SMALL, epoch);
  await storeFrame(opuk, OPUK_FIXTURE_GRID, epoch);
}
const age = (r: Response) => r.headers.get("X-Wx-Max-Age");

describe("the per-product age limit: X-Wx-Max-Age", () => {
  it("A7: opuk states 720, cref states nothing, and 720 lies in the device's [600, 900]", () => {
    expect(opuk.deviceMaxAgeS).toBe(720);
    expect(cref.deviceMaxAgeS).toBeUndefined();
    expect(opuk.deviceMaxAgeS! >= 600 && opuk.deviceMaxAgeS! <= 900).toBe(true);
  });

  it("A1: /wx/cref at LHR (served opuk): 720 on the 200 and on its 304", async () => {
    await both();
    const a = await wxAt("cref", "LHR");
    expect([a.status, age(a)]).toEqual([200, "720"]);
    const b = await wxAt("cref", "LHR", { "If-None-Match": a.headers.get("ETag")! });
    expect([b.status, age(b)]).toEqual([304, "720"]);
  }, SLOW);

  it("A2: /wx/cref at KMCI (served cref): none on the 200 or the 304", async () => {
    await both();
    const a = await wxAt("cref", "KMCI");
    expect([a.status, age(a)]).toEqual([200, null]);
    const b = await wxAt("cref", "KMCI", { "If-None-Match": a.headers.get("ETag")! });
    expect([b.status, age(b)]).toEqual([304, null]);
  }, SLOW);

  it("A3: /wx/cref at CDG and PHNL (cref's all-14 crops): none", async () => {
    await both();
    for (const id of ["CDG", "PHNL"]) {
      const r = await wxAt("cref", id);
      expect([r.status, r.headers.get("ETag")?.includes("-cref-"), age(r)], id).toEqual([200, true, null]);
    }
  }, SLOW);

  it("A4: /wx/opuk at LHR: 720", async () => {
    await both();
    const r = await wxAt("opuk", "LHR");
    expect([r.status, age(r)]).toEqual([200, "720"]);
  }, SLOW);

  it("A5: errors carry none -- 503 wx_stale, 400 bad_lat, 404 not_found", async () => {
    await storeFrame(opuk, OPUK_FIXTURE_GRID, now() - 1801);
    const stale = await wxAt("cref", "LHR");
    expect([stale.status, age(stale)]).toEqual([503, null]);
    const bad = await call(apiRequest("/api/v1/blipscope/wx/cref?lat=91&lon=0&r=10"));
    expect([bad.status, age(bad)]).toEqual([400, null]);
    const nf = await call(apiRequest(`/api/v1/blipscope/wx/nope?${q("LHR")}`));
    expect([nf.status, age(nf)]).toEqual([404, null]);
  }, SLOW);

  it("A6 SABOTAGE: the header taken from the REQUESTED product -- LHR's 200 carries none", async () => {
    await both();
    const r = await handleWx(new Request(`https://proxy.test/api/v1/blipscope/wx/cref?${q("LHR")}`), env, "cref", {}, true);
    expect(r.status).toBe(200);
    expect(r.headers.get("ETag"), "CONTROL: it was served from opuk").toContain("-opuk-");
    expect(age(r)).toBeNull();
  }, SLOW);
});
