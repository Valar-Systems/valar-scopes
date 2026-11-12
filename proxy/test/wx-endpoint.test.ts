// WS6 (docs/wx-server.md): the /wx endpoint, case by case -- status, body shape and R2 reads (counted
// through a wrapped binding) -- plus /healthz's wx field.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { cropCellBox, quantise } from "../src/wx/crop";
import { FrameBuilder, frameMeta, tilesFor } from "../src/wx/grid";
import type { LatestRecord } from "../src/wx/handler";
import { type Product, PRODUCTS, cropEtag, frameKey, latestKey } from "../src/wx/products";
import H3_FIXTURE from "./fixtures/wx/healthz-h3.json";
import { apiRequest, call } from "./helpers";

const cref = PRODUCTS[0]!;
const GRID = { ni: 600, nj: 400, la1: 40.0, lo1: -100.0, di: 0.01, dj: 0.01 };   // 2 x 1 tiles of 512
// Centres are public airports cleared by the spike's forbidden.py: KMCI here, PHNL for "off the grid".
const Q = "lat=39.30&lon=-94.71&r=40";   // KMCI, well inside GRID

async function store(epoch: number) {
  const m = frameMeta("cref", epoch, GRID);
  const fb = new FrameBuilder(m);
  const row = new Uint8Array(GRID.ni);
  for (let r = 0; r < GRID.nj; r++) { for (let c = 0; c < GRID.ni; c++) row[c] = (r * 7 + c * 3) % 14; fb.setRow(r, row); }
  await env.WX!.put(frameKey(cref, epoch), fb.bytes);
  const rec: LatestRecord = { product: "cref", epoch, key: frameKey(cref, epoch), ingestedAt: Date.now(), ...GRID, tile: m.tile };
  await env.WX!.put(latestKey(cref), JSON.stringify(rec));
}

/** env.WX with every read (get / head / list) counted. */
function counting() {
  let reads = 0;
  const WX = new Proxy(env.WX!, {
    get(t, prop) {
      const v = Reflect.get(t, prop);
      if (prop === "get" || prop === "head" || prop === "list") return (...a: unknown[]) => { reads++; return (v as (...x: unknown[]) => unknown).apply(t, a); };
      return typeof v === "function" ? v.bind(t) : v;
    },
  }) as R2Bucket;
  return { WX, reads: () => reads };
}

/** A `latest` record alone for any product: all /healthz reads (docs/wx-healthz-products.md, H2 and H3). */
async function storeLatest(p: Product, epoch: number) {
  const rec: LatestRecord = { product: p.id, epoch, key: frameKey(p, epoch), ingestedAt: Date.now(), ...GRID,
                              tile: frameMeta(p.id, epoch, GRID).tile };
  await env.WX!.put(latestKey(p), JSON.stringify(rec));
}
const byId = (id: string): Product => PRODUCTS.find((p) => p.id === id)!;
const healthWx = async () =>
  ((await (await call(new Request("https://proxy.test/healthz"))).json()) as { wx: Record<string, Record<string, unknown>> }).wx;

const wx = (q = Q, h: Record<string, string> = {}) => apiRequest(`/api/v1/blipscope/wx/cref?${q}`, h);
const fresh = () => Math.floor(Date.now() / 1000) - 90;

describe("WS6: the /wx endpoint", () => {
  it("no WX binding: 503 not_configured", async () => {
    const res = await call(wx(), { WX: undefined });
    expect(res.status).toBe(503);
    expect(await res.json()).toMatchObject({ error: "not_configured" });
  });

  it("unknown product: 404 not_found, 0 R2 reads", async () => {
    const c = counting();
    const res = await call(apiRequest(`/api/v1/blipscope/wx/nope?${Q}`), { WX: c.WX });
    expect(res.status).toBe(404);
    expect(c.reads()).toBe(0);
  });

  it("bad lat / lon / r: 400 with the blips codes, 0 R2 reads", async () => {
    for (const [q, code] of [["lat=91&lon=0&r=10", "bad_lat"], ["lat=0&lon=181&r=10", "bad_lon"], ["lat=0&lon=0&r=0", "bad_r"], ["lat=x&lon=0&r=1", "bad_lat"]] as const) {
      const c = counting();
      const res = await call(wx(q), { WX: c.WX });
      expect(res.status, q).toBe(400);
      expect(await res.json()).toMatchObject({ error: code });
      expect(c.reads(), q).toBe(0);
    }
  });

  it("unauthenticated: 401, before any R2 read", async () => {
    const c = counting();
    const res = await call(new Request(`https://proxy.test/api/v1/blipscope/wx/cref?${Q}`), { WX: c.WX });
    expect(res.status).toBe(401);
    expect(c.reads()).toBe(0);
  });

  it("no latest: 503 wx_stale, 1 read", async () => {
    const c = counting();
    const res = await call(wx(), { WX: c.WX });
    expect(res.status).toBe(503);
    expect(await res.json()).toMatchObject({ error: "wx_stale" });
    expect(c.reads()).toBe(1);
  });

  it("latest older than 600 s: 503 wx_stale, 1 read", async () => {
    await store(Math.floor(Date.now() / 1000) - 601);
    const c = counting();
    const res = await call(wx(), { WX: c.WX });
    expect(res.status).toBe(503);
    expect(c.reads()).toBe(1);
  });

  it("fresh: 200 octet-stream WXC1 <= 32,768 bytes with ETag and X-Wx-Frame, 1 + at most 4 reads", async () => {
    const e = fresh();
    await store(e);
    const c = counting();
    const res = await call(wx(), { WX: c.WX });
    const body = new Uint8Array(await res.arrayBuffer());
    const q = quantise(39.3, -94.71, 40), m = frameMeta("cref", e, GRID), b = cropCellBox(m, q);
    const tiles = tilesFor(m, b.r0, b.r0 + b.rows - 1, b.c0, b.c0 + b.cols - 1).length;
    console.log(`WS6 fresh: ${res.status}, ${body.length} bytes, reads ${c.reads()} (tiles ${tiles})`);
    expect(res.status).toBe(200);
    expect(res.headers.get("Content-Type")).toBe("application/octet-stream");
    expect(String.fromCharCode(...body.subarray(0, 4))).toBe("WXC1");
    expect(body.length).toBeLessThanOrEqual(32768);
    expect(res.headers.get("ETag")).toBe(`"wx1-cref-${e}-39.30_-94.71_40"`);
    expect(res.headers.get("X-Wx-Frame")).toBe(String(e));
    expect(c.reads()).toBe(1 + tiles);
    expect(tiles).toBeLessThanOrEqual(4);
  });

  it("If-None-Match equal to the current ETag: 304, no body, 1 read", async () => {
    const e = fresh();
    await store(e);
    const c = counting();
    const res = await call(wx(Q, { "If-None-Match": `"wx1-cref-${e}-39.30_-94.71_40"` }), { WX: c.WX });
    expect(res.status).toBe(304);
    expect((await res.arrayBuffer()).byteLength).toBe(0);
    expect(c.reads()).toBe(1);
  });

  it("a stale ETag (an older frame) gets a 200, never a 304", async () => {
    const e = fresh();
    await store(e);
    const res = await call(wx(Q, { "If-None-Match": `"wx1-cref-${e - 120}-39.30_-94.71_40"` }));
    expect(res.status).toBe(200);
    await res.arrayBuffer();
  });

  it("the box misses the grid (PHNL): 200, all 14, 1 read", async () => {
    await store(fresh());
    const c = counting();
    const res = await call(wx("lat=21.32&lon=-157.92&r=80"), { WX: c.WX });
    const body = new Uint8Array(await res.arrayBuffer());
    expect(res.status).toBe(200);
    expect(c.reads()).toBe(1);
    // one long run of code 14 covering every cell: 0xE0 then the LEB128 length
    expect(body[32]).toBe(0xe0);
  });

  it("the ETag carries the product: two products, same frame and query, different ETags", () => {
    const other: Product = { ...cref, id: "test2" };
    const a = cropEtag(cref, 1, "39.30", "-94.71", 40), b = cropEtag(other, 1, "39.30", "-94.71", 40);
    expect(a).not.toBe(b);
  });

  it("SABOTAGE: an ETag without the product must collide", () => {
    const noProduct = (_p: Product, e: number, la: string, lo: string, r: number) => `"wx1-${e}-${la}_${lo}_${r}"`;
    const other: Product = { ...cref, id: "test2" };
    expect(noProduct(cref, 1, "39.30", "-94.71", 40)).toBe(noProduct(other, 1, "39.30", "-94.71", 40));
  });
});

describe("/healthz's wx field", () => {
  it("not_configured without the binding", async () => {
    const res = await call(new Request("https://proxy.test/healthz"), { WX: undefined });
    expect((await res.json() as { wx: unknown }).wx).toBe("not_configured");
  });
  // docs/wx-healthz-products.md: EVERY product, routed or not, so a deployed-but-unrouted ingest is never invisible.
  it("H1 every product in PRODUCTS' order, routed from ROUTED; no frame -> null age and null stale; no commit key, no array", async () => {
    const w = await healthWx();
    expect(Object.keys(w)).toEqual(PRODUCTS.map((p) => p.id));
    expect(Object.keys(w)).toHaveLength(11);
    // Literals, production's ROUTED today: a product joining ROUTED updates this line in its own PR.
    expect(Object.entries(w).filter(([, e]) => e.routed === true).map(([id]) => id)).toEqual(["cref", "opuk"]);
    for (const p of PRODUCTS) {
      const e = w[p.id]!;
      expect(e.frameAgeS).toBeNull();
      expect(e.stale).toBeNull();            // no frame is not a stale frame: never a made-up verdict
      expect(typeof e.routed).toBe("boolean");
      if (p.source === "opera") expect(e.coveredShare).toBeNull(); else expect("coveredShare" in e).toBe(false);
    }
    // confirm-deploy.sh:74 and smoke-prod.sh:264 take EVERY "commit":"..." in the body, and smoke-prod.sh:300 cuts
    // "upstreams":[...] greedily: nothing inside wx may carry either.
    const text = JSON.stringify(w);
    expect(text).not.toMatch(/"commit"/);
    expect(text).not.toContain("[");
  });
  it("H2 a deployed but unrouted product (opmt) with a frame: routed false, a real age, not stale", async () => {
    await storeLatest(byId("opmt"), Math.floor(Date.now() / 1000) - 90);
    const w = await healthWx();
    expect(w.opmt!.routed).toBe(false);
    expect(w.opmt!.stale).toBe(false);
    expect(w.opmt!.frameAgeS).toBeGreaterThanOrEqual(90);
    expect(w.opmt!.frameAgeS).toBeLessThan(120);
  });
  it("H3 a planted stale product reads stale, on both sides of the boundary, for both kinds of product", async () => {
    expect([byId("opse").maxAgeS, byId("opba").maxAgeS, cref.maxAgeS]).toEqual([1800, 1800, 600]);
    const now = Math.floor(Date.now() / 1000);
    await storeLatest(byId("opse"), now - (1800 + 60));
    await storeLatest(byId("opba"), now - (1800 - 60));
    await storeLatest(cref, now - (600 + 60));
    const w = await healthWx();
    expect(w.opse!.stale).toBe(true);
    expect(w.opba!.stale).toBe(false);
    expect(w.cref!.stale).toBe(true);
    // The state smoke's selftest (H6) prints its WARN lines from: the committed fixture's routed and stale, product by
    // product, are this body's -- so the WARNs there come from what this plant produced, not from a hand-written body.
    const proj = (x: Record<string, Record<string, unknown>>) =>
      Object.fromEntries(Object.entries(x).map(([id, e]) => [id, { routed: e.routed, stale: e.stale }]));
    expect(proj(w)).toEqual(proj(H3_FIXTURE.wx as Record<string, Record<string, unknown>>));
  });
  it("the newest frame's age when there is one", async () => {
    await store(Math.floor(Date.now() / 1000) - 90);
    const res = await call(new Request("https://proxy.test/healthz"));
    const age = ((await res.json()) as { wx: { cref: { frameAgeS: number } } }).wx.cref.frameAgeS;
    expect(age).toBeGreaterThanOrEqual(90);
    expect(age).toBeLessThan(120);
  });
});
