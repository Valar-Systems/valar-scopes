// MW and MP (docs/basemap-route.md A1.7): the endpoint, through the proxy's own dispatch, auth and alias, with the
// builder Worker's real entry over MW's synthetic store behind counting bindings. Each test is named by its case id.
// MW's store: origin (0.37, 0.59), the location of nothing; one water ring and one primary road in one cell.
import { describe, expect, it } from "vitest";
import { build } from "../src/basemap/build";
import { crc32, decode, validate } from "../src/basemap/format";
import { BASEMAP_REV, resetBasemapIsolate } from "../src/basemap/handler";
import builder from "../src/basemap/worker";
import { type SynthDef, COMMIT, lonLatOf, writeSynthStore } from "./basemap-synth";
import { apiRequest, call } from "./helpers";

const QW = "lat=0.37&lon=0.59&r=97&eps=100584&layers=water,roads";
const ETAG = /^"bm1-[0-9]+-[0-9a-f]{12}-[0-9a-f]{24}"$/;

async function mwStore(opts: { current?: boolean; badManifest?: boolean } = {}) {
  const W = "https://example.invalid/water/w.zip", R = "https://example.invalid/roads/r.zip";
  const ll = (pts: Array<[number, number]>) => pts.map(([e, s]) => lonLatOf(0.37, 0.59, e, s));
  const covered: Array<[number, number]> = [];
  for (let h = 30; h <= 40; h++) for (let c = 50; c <= 60; c++) covered.push([h, c]);
  const def: SynthDef = {
    files: [{ layer: "water", url: W, sha256: "3".repeat(64), vintage: "2024" }, { layer: "roads", url: R, sha256: "4".repeat(64), vintage: "2024" }],
    features: [
      { kind: "water", url: W, record: 0, part: 0, attrs: { AWATER: 2_000_000 }, lonlat: ll([[1, 1], [3, 1], [3, 3], [1, 3], [1, 1]]) },
      { kind: "roads", url: R, record: 0, part: 0, attrs: { MTFCC: "S1100", FULLNAME: "Main Hwy" }, lonlat: ll([[-5, -2], [5, -2]]) },
    ],
    regions: [{ box: [0.3, 0.3, 0.9, 0.45], files: [W] }, { box: [0.3, 0.3, 0.9, 0.45], files: [R] }],
    covered,
  };
  const s = await writeSynthStore(def);
  if (opts.current === false) s.store.objects.delete("bm/v1/current");
  if (opts.badManifest) s.store.objects.set(`bm/v1/${s.storeId}/manifest.json`, new TextEncoder().encode("{\"files\":[]}\n"));
  return s;
}

/** The builder Worker's real entry over a store, with its calls, its reads and any write counted (MP2). */
let allWrites = 0, shims = 0; // MP2: every write through every builder shim the MW rows create
function builderOver(objects: Map<string, Uint8Array>, buildOverride?: (q: unknown) => Promise<Response>) {
  const n = { calls: 0, reads: 0, writes: 0 };
  shims++;
  const STORE = {
    async get(k: string) { n.reads++; const b = objects.get(k); return b ? { arrayBuffer: async () => b.slice().buffer } : null; },
    async put() { n.writes++; allWrites++; return null; }, async delete() { n.writes++; allWrites++; },
  } as unknown as R2Bucket;
  const BASEMAP = {
    async fetch(input: RequestInfo, init?: RequestInit) {
      n.calls++;
      const req = new Request(input as string, init);
      if (buildOverride) return buildOverride(await req.json());
      return builder.fetch(req, { STORE, BUILD_COMMIT: COMMIT });
    },
  } as unknown as Fetcher;
  return { BASEMAP, n };
}

const get = (path: string, h: Record<string, string> = {}) => apiRequest(path, h);
const logs: string[] = [];
const etags: string[] = [];
function capture<T>(f: () => Promise<T>): Promise<T> {
  const orig = { log: console.log, info: console.info, warn: console.warn, error: console.error };
  for (const k of Object.keys(orig) as Array<keyof typeof orig>) console[k] = (...a: unknown[]) => { logs.push(a.map(String).join(" ")); };
  return f().finally(() => Object.assign(console, orig));
}
async function mw(path: string, b: ReturnType<typeof builderOver>, h: Record<string, string> = {}) {
  const res = await capture(() => call(get(path, h), { BASEMAP: b.BASEMAP }));
  const e = res.headers.get("etag"); if (e) etags.push(e);
  return res;
}

describe("MW, the endpoint", () => {
  it("MW1: no BASEMAP binding is 503 not_configured", async () => {
    resetBasemapIsolate();
    const res = await call(get(`/api/v1/blipscope/basemap?${QW}`), { BASEMAP: undefined });
    expect([res.status, (await res.json() as { error: string }).error]).toEqual([503, "not_configured"]);
  });

  it("MW2-MW7: a bad lat, lon, r, eps, layers or levels is 400, with no builder call and no read", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const bad: Array<[string, string]> = [["lat=91", "bad_lat"], ["lon=181", "bad_lon"], ["r=0", "bad_r"], ["eps=999", "bad_eps"], ["layers=lakes", "bad_layers"], ["layers=water,terrain", "bad_levels"]];
    for (const [kv, code] of bad) {
      const u = new URLSearchParams(QW); const [k, v] = kv.split("="); u.set(k!, v!);
      const res = await mw(`/api/v1/blipscope/basemap?${u.toString().replace(/%2C/g, ",")}`, b);
      expect([res.status, (await res.json() as { error: string }).error], kv).toEqual([400, code]);
    }
    expect(b.n).toMatchObject({ calls: 0, reads: 0 });
  });

  it("MW8: no device key is 401, with no builder call", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const res = await capture(() => call(new Request(`https://proxy.test/api/v1/blipscope/basemap?${QW}`), { BASEMAP: b.BASEMAP }));
    expect(res.status).toBe(401);
    expect(b.n).toMatchObject({ calls: 0, reads: 0 });
  });

  /** MW9's request on a fresh isolate: the package, one builder call, five reads. */
  async function mw9() {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const res = await mw(`/api/v1/blipscope/basemap?${QW}`, b);
    return { s, b, res, bytes: new Uint8Array(await res.arrayBuffer()) };
  }

  it("MW9: 200, the package, ETag, no-cache; 1 builder call, 5 reads", async () => {
    const { b, res, bytes, s } = await mw9();
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("application/octet-stream");
    expect(res.headers.get("cache-control")).toBe("no-cache");
    expect(res.headers.get("etag")).toMatch(ETAG);
    expect(res.headers.get("etag")).toContain(`bm1-${BASEMAP_REV}-${s.storeId}-`);
    expect(validate(bytes).status).toBe("kOk");
    expect(b.n).toMatchObject({ calls: 1, reads: 5 });
  });

  it("MW10: If-None-Match with MW9's ETag is 304, no body, with no builder call and no read", async () => {
    const first = await mw9();
    const before = { ...first.b.n };
    const res = await mw(`/api/v1/blipscope/basemap?${QW}`, first.b, { "If-None-Match": first.res.headers.get("etag")! });
    expect(res.status).toBe(304);
    expect((await res.arrayBuffer()).byteLength).toBe(0);
    expect({ calls: first.b.n.calls - before.calls, reads: first.b.n.reads - before.reads }).toEqual({ calls: 0, reads: 0 });
  });

  it("MW11: the same request again is MW9's bytes from the Cache API, with no builder call and no read", async () => {
    const first = await mw9();
    const before = { ...first.b.n };
    const res = await mw(`/api/v1/blipscope/basemap?${QW}`, first.b);
    expect(res.status).toBe(200);
    expect(new Uint8Array(await res.arrayBuffer())).toEqual(first.bytes);
    expect({ calls: first.b.n.calls - before.calls, reads: first.b.n.reads - before.reads }).toEqual({ calls: 0, reads: 0 });
  });

  it("MW12: outside coverage is 404 no_coverage after 3 reads", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const res = await mw(`/api/v1/blipscope/basemap?${QW.replace("lat=0.37", "lat=10.00")}`, b);
    expect([res.status, (await res.json() as { error: string }).error]).toEqual([404, "no_coverage"]);
    expect(b.n).toMatchObject({ calls: 1, reads: 3 });
  });

  it("MW13: a test builder with capBytes 100 gives 422 too_large after 5 reads", async () => {
    resetBasemapIsolate();
    const s = await mwStore();
    let reads = 0;
    const counted = { get: async (k: string) => { reads++; return s.store.objects.get(k) ?? null; } };
    const b = builderOver(s.store.objects, async (q) => {
      const r = await build(counted, q as Parameters<typeof build>[1], { commit: COMMIT, capBytes: 100 });
      return r.ok ? new Response(r.bytes, { headers: { "x-basemap-store": r.storeId } }) : new Response(JSON.stringify({ error: r.code }), { status: r.status });
    });
    const res = await mw(`/api/v1/blipscope/basemap?${QW}`, b);
    expect([res.status, (await res.json() as { error: string }).error]).toEqual([422, "too_large"]);
    expect([b.n.calls, reads]).toEqual([1, 5]);
  });

  it("MW14: a store with no current is 503 basemap_unavailable after 1 read", async () => {
    resetBasemapIsolate();
    const s = await mwStore({ current: false }), b = builderOver(s.store.objects);
    const res = await mw(`/api/v1/blipscope/basemap?${QW}`, b);
    expect([res.status, (await res.json() as { error: string }).error]).toEqual([503, "basemap_unavailable"]);
    expect(b.n).toMatchObject({ calls: 1, reads: 1 });
  });

  it("MW15: a builder answering an invalid package is 500 builder_invalid, never the bytes", async () => {
    resetBasemapIsolate();
    const s = await mwStore();
    const good = await build(s.store, { lat0Micro: 370_000, lon0Micro: 590_000, rKm: 97, epsMm: 100_584, mask: 5, levels: [] }, { commit: COMMIT });
    if (!good.ok) throw new Error("MW15's fixture did not build (BLIND)");
    const bad = good.bytes.slice(), dv = new DataView(bad.buffer);
    // the ROADS section's first item's class byte, set to 3, then the CRC recomputed
    const n = dv.getUint8(5);
    let off = -1;
    for (let i = 0; i < n; i++) if (dv.getUint8(32 + 16 * i) === 3) off = dv.getUint32(32 + 16 * i + 8, true);
    if (off < 0) throw new Error("MW15's fixture has no ROADS section (BLIND)");
    bad[off] = 3;
    dv.setUint32(28, 0, true); dv.setUint32(28, crc32(bad), true);
    expect(validate(bad).status).not.toBe("kOk"); // CONTROL: the patch really makes it invalid
    const b = builderOver(s.store.objects, async () => new Response(bad, { headers: { "x-basemap-store": s.storeId } }));
    const res = await mw(`/api/v1/blipscope/basemap?${QW}`, b);
    expect(res.status).toBe(500);
    expect(res.headers.get("content-type")).toContain("application/json");
    expect((await res.json() as { error: string }).error).toBe("builder_invalid");
  });

  it("MW16: the deprecated /v1/basemap alias answers MW9's bytes and ETag", async () => {
    const first = await mw9();
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const res = await mw(`/v1/basemap?${QW}`, b);
    expect(res.status).toBe(200);
    expect(res.headers.get("etag")).toBe(first.res.headers.get("etag"));
    expect(new Uint8Array(await res.arrayBuffer())).toEqual(first.bytes);
  });

  it("MW17: a manifest that does not hash to current's store id is 503 basemap_unavailable after 2 reads", async () => {
    resetBasemapIsolate();
    const s = await mwStore({ badManifest: true }), b = builderOver(s.store.objects);
    const res = await mw(`/api/v1/blipscope/basemap?${QW}`, b);
    expect([res.status, (await res.json() as { error: string }).error]).toEqual([503, "basemap_unavailable"]);
    expect(b.n).toMatchObject({ calls: 1, reads: 2 });
  });
});

describe("MP, privacy (over every MW row above)", () => {
  it("MP1: no log line holds the origin", () => {
    expect(logs.filter((l) => l.includes("0.37") || l.includes("0.59"))).toEqual([]);
  });
  it("MP2: no store write across every MW row (the builder's only binding is the store)", () => {
    expect(shims).toBeGreaterThan(10); // CONTROL: the MW rows really ran through the shims
    expect(allWrites).toBe(0);
  });
  it("MP3: no ETag and no cache key holds the origin", () => {
    expect(etags.length).toBeGreaterThan(3); // CONTROL: the rows above produced ETags to look at
    expect(etags.filter((e) => /0\.37|0\.59/.test(e))).toEqual([]);
    for (const e of etags) expect(`https://basemap-cache.internal/${e.replace(/"/g, "")}`).not.toMatch(/0\.37|0\.59/);
  });
});
