// MW and MP (docs/basemap-route.md A1.7): the endpoint, through the proxy's own dispatch, auth and alias, with the
// builder Worker's real entry over MW's synthetic store behind counting bindings. Each test is named by its case id.
// MW's store: origin (0.37, 0.59), the location of nothing; one water ring and one primary road in one cell.
import { env as cf } from "cloudflare:test";
import { describe, expect, it, vi } from "vitest";
import { build } from "../src/basemap/build";
import { crc32, decode, validate } from "../src/basemap/format";
import { BASEMAP_REV, SYNC_WAIT_MS, basemapDigest, basemapHealth, basemapTiming, cacheKey, errKey, lockKey, pkgKey, resetBasemapIsolate } from "../src/basemap/handler";
import { etagOf, parseQuery } from "../src/basemap/query";
import builder, { makeBuilder } from "../src/basemap/worker";
import { type SynthDef, COMMIT, lonLatOf, mbStore, writeSynthStore } from "./basemap-synth";
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
      return makeBuilder(COMMIT).fetch(req, { STORE });
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
    // a valid commit, so this row tests validate() alone: without it the commit check (MW19) would answer 500 first
    const b = builderOver(s.store.objects, async () => new Response(bad, { headers: { "x-basemap-store": s.storeId, "x-basemap-commit": COMMIT } }));
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

  it("MW18: an unstamped bundle refuses every build as builder_unstamped, with no store read", async () => {
    const s = await mwStore();
    let reads = 0;
    // Writes count into MP2's audit, as builderOver's stub's do: this row runs the builder too, so MP2's "no store
    // write across every MW row" must see its runs (basemap-deploy.md: S-PWRITE found this stub outside the audit).
    shims++;
    const STORE = {
      async get(k: string) { reads++; const v = s.store.objects.get(k); return v ? { arrayBuffer: async () => v.slice().buffer } : null; },
      async put() { allWrites++; return null; }, async delete() { allWrites++; },
    } as unknown as R2Bucket;
    const at = { lat0Micro: 370_000, lon0Micro: 590_000, epsMm: 100_584 };
    const levels = [{ radiusMm: 24_743_664, n: 246 }, { radiusMm: 49_487_328, n: 246 }, { radiusMm: 98_974_656, n: 246 }];
    // §5.4's four shapes, asking only for the layers MW's store holds (water, roads; terrain on the terrain shape). As
    // first written they asked for every layer, and the control threw on rivers before the refusal ran (basemap-deploy.md).
    const shapes = [{ ...at, rKm: 97, mask: 37, levels }, { ...at, rKm: 97, mask: 5, levels: [] }, { ...at, rKm: 161, mask: 5, levels: [] }, { ...at, rKm: 222, mask: 5, levels: [] }];
    const post = (q: object) => new Request("https://basemap.internal/build", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(q) });
    // CONTROL: the same bundle code, stamped, gets past the stamp on each shape (it reads the store), so the refusal
    // below is the stamp's and nothing else's
    for (const q of shapes) {
      const before = reads;
      await makeBuilder(COMMIT).fetch(post(q), { STORE });
      expect(reads, JSON.stringify(q.rKm)).toBeGreaterThan(before);
    }
    reads = 0;
    // the default export is the bundle as built, under this build's own define ("UNSTAMPED", wrangler.toml)
    for (const q of shapes) {
      const res = await builder.fetch(post(q), { STORE });
      expect([res.status, (await res.json() as { error: string }).error]).toEqual([503, "builder_unstamped"]);
    }
    expect(reads).toBe(0);
  });

  it("MW19: a builder 200 without a 40-hex x-basemap-commit is 500 builder_invalid, and nothing is cached", async () => {
    resetBasemapIsolate();
    const s = await mwStore();
    const good = await build(s.store, { lat0Micro: 370_000, lon0Micro: 590_000, rKm: 97, epsMm: 100_584, mask: 5, levels: [] }, { commit: COMMIT });
    if (!good.ok) throw new Error("MW19's fixture did not build (BLIND)");
    expect(validate(good.bytes).status).toBe("kOk"); // CONTROL: the package itself is valid, so only the commit is wrong
    for (const commit of [null, "UNSTAMPED", COMMIT.slice(0, 7)]) {
      const headers: Record<string, string> = { "x-basemap-store": s.storeId };
      if (commit !== null) headers["x-basemap-commit"] = commit;
      const b = builderOver(s.store.objects, async () => new Response(good.bytes, { headers }));
      const res = await mw(`/api/v1/blipscope/basemap?${QW}`, b);
      expect([res.status, (await res.json() as { error: string }).error], String(commit)).toEqual([500, "builder_invalid"]);
      // nothing learned from the refused answer: no last build. (Its `commit` field is MH2's to test, so this row
      // stays about the commit check alone.)
      expect((basemapHealth({ BASEMAP: b.BASEMAP }) as { store: string | null }).store, String(commit)).toBeNull();
    }
  });
});

// MK (docs/basemap-branch-log.md): the branch each request took, as `bm` on the proxy's own request line. Each row reads
// the ONE line of the request it is named for; setup requests are checked for their status only, never their label.
const LABELS = ["304-known", "hit", "known-miss", "unknown-store"];
type ReqLine = { evt: string; ep: string; status: number; bm?: string };
const reqLinesFrom = (from: number): ReqLine[] =>
  logs.slice(from).filter((l) => l.startsWith('{"evt":"req"')).map((l) => JSON.parse(l) as ReqLine);
/** One request through capture, and its line: exactly one, or the row is an instrument failure, never a pass. */
async function mkCall(f: () => Promise<Response>) {
  const from = logs.length;
  const res = await capture(f);
  const lines = reqLinesFrom(from);
  expect(lines.length, "request lines for one request").toBe(1);
  return { res, line: lines[0]! };
}
const QW98 = QW.replace("r=97", "r=98"); // rKm is round(r): another query, so another ETag and no cached copy

describe("MK, the branch on the proxy's request line", () => {
  /** MW9's setup, as a setup: a fresh isolate builds MW's query once (its label is not read). */
  async function mw9() {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    return { b, res: await mw(`/api/v1/blipscope/basemap?${QW}`, b) };
  }

  it("MK1: a fresh isolate's request is unknown-store, with one builder call", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const { res, line } = await mkCall(() => call(get(`/api/v1/blipscope/basemap?${QW}`), { BASEMAP: b.BASEMAP }));
    expect([res.status, line.bm, b.n.calls]).toEqual([200, "unknown-store", 1]);
  });

  it("MK2: If-None-Match with the ETag the isolate knows is 304-known, with no further builder call", async () => {
    const first = await mw9();
    expect(first.res.status).toBe(200);
    const { res, line } = await mkCall(() => call(get(`/api/v1/blipscope/basemap?${QW}`, { "If-None-Match": first.res.headers.get("etag")! }), { BASEMAP: first.b.BASEMAP }));
    expect([res.status, line.bm, first.b.n.calls]).toEqual([304, "304-known", 1]);
  });

  it("MK3: the same request again is a hit, with no further builder call", async () => {
    const first = await mw9();
    expect(first.res.status).toBe(200);
    const { res, line } = await mkCall(() => call(get(`/api/v1/blipscope/basemap?${QW}`), { BASEMAP: first.b.BASEMAP }));
    expect([res.status, line.bm, first.b.n.calls]).toEqual([200, "hit", 1]);
  });

  it("MK4: a known store with no cached copy is known-miss, with one further builder call", async () => {
    const first = await mw9();
    expect(first.res.status).toBe(200);
    const { res, line } = await mkCall(() => call(get(`/api/v1/blipscope/basemap?${QW98}`), { BASEMAP: first.b.BASEMAP }));
    expect([res.status, line.bm, first.b.n.calls]).toEqual([200, "known-miss", 2]);
  });

  it("MK5: a 401, a 400 and a 503 carry no bm", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const cases: Array<[number, () => Promise<Response>]> = [
      [401, () => call(new Request(`https://proxy.test/api/v1/blipscope/basemap?${QW}`), { BASEMAP: b.BASEMAP })],
      [400, () => call(get(`/api/v1/blipscope/basemap?${QW.replace("lat=0.37", "lat=91")}`), { BASEMAP: b.BASEMAP })],
      [503, () => call(get(`/api/v1/blipscope/basemap?${QW}`), { BASEMAP: undefined })],
    ];
    for (const [status, f] of cases) {
      const { res, line } = await mkCall(f);
      expect([res.status, line.status], String(status)).toEqual([status, status]); // CONTROL: this request's own line
      expect("bm" in line, String(status)).toBe(false);
    }
  });

  it("MK6: every /basemap request line in this file carries one of the four labels or none, and each label was seen", () => {
    const lines = reqLinesFrom(0).filter((o) => o.ep.endsWith("/basemap"));
    const seen = new Map(LABELS.map((l) => [l, 0]));
    for (const o of lines) {
      if (!("bm" in o)) continue;
      expect(LABELS, JSON.stringify(o.bm)).toContain(o.bm);
      seen.set(o.bm!, (seen.get(o.bm!) ?? 0) + 1);
    }
    for (const [l, n] of seen) expect(n, `${l} seen`).toBeGreaterThan(0); // CONTROL: the scan saw every label
  });

  it("MK7: the label goes on the request line only, never into the dataset point", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const writeDataPoint = vi.fn();
    const { res, line } = await mkCall(() => call(get(`/api/v1/blipscope/basemap?${QW}`), { BASEMAP: b.BASEMAP, METRICS: { writeDataPoint } as unknown as AnalyticsEngineDataset }));
    expect(res.status).toBe(200);
    expect(LABELS).toContain(line.bm); // CONTROL: there was a label to leak
    const points = writeDataPoint.mock.calls.map((c) => c[0] as { blobs: string[] });
    expect(points.length).toBe(1);
    expect(points[0]!.blobs.length).toBe(6);
    for (const l of LABELS) expect(points[0]!.blobs).not.toContain(l);
  });
});

// BB (docs/basemap-build-reads.md): the builder's one line per build, about its store reads, in counts and
// milliseconds only. Each of BB1 and BB2 reads the ONE line its own request produced.
type BuildLine = { evt: string; status: number; reads: number; bytes: number; readMs: number; maxReadMs: number; wallMs: number };
const BUILD_KEYS = ["evt", "status", "reads", "bytes", "readMs", "maxReadMs", "wallMs"];
const buildLinesFrom = (from: number): BuildLine[] =>
  logs.slice(from).filter((l) => l.startsWith('{"evt":"bm_build"')).map((l) => JSON.parse(l) as BuildLine);

describe("BB, the builder's read line", () => {
  it("BB1: a 200's one line counts exactly the reads the store saw", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const from = logs.length;
    const res = await mw(`/api/v1/blipscope/basemap?${QW}`, b);
    expect(res.status).toBe(200);
    const lines = buildLinesFrom(from);
    expect(lines.length, "build lines for one build").toBe(1);
    const l = lines[0]!;
    expect([l.status, l.reads, b.n.reads]).toEqual([200, 5, 5]);
    expect(l.bytes).toBeGreaterThan(0); // CONTROL: it measured something
    for (const k of BUILD_KEYS.slice(1)) expect(Number.isInteger((l as unknown as Record<string, number>)[k]), k).toBe(true);
    expect(l.maxReadMs).toBeLessThanOrEqual(l.readMs);
  });

  it("BB2: no_coverage writes its line too, with 3 reads", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const from = logs.length;
    const res = await mw(`/api/v1/blipscope/basemap?${QW.replace("lat=0.37", "lat=10.00")}`, b);
    expect(res.status).toBe(404);
    const lines = buildLinesFrom(from);
    expect(lines.length, "build lines for one build").toBe(1);
    expect([lines[0]!.status, lines[0]!.reads]).toEqual([404, 3]);
  });

  it("BB3: every build line holds exactly its seven fields, integers only, and nothing of the query", () => {
    const lines = logs.filter((l) => l.startsWith('{"evt":"bm_build"'));
    expect(lines.length).toBeGreaterThanOrEqual(3); // CONTROL: there were lines to check
    for (const raw of lines) {
      const o = JSON.parse(raw) as Record<string, unknown>;
      expect(Object.keys(o), raw).toEqual(BUILD_KEYS);
      for (const k of BUILD_KEYS.slice(1)) expect(Number.isInteger(o[k]), `${k} in ${raw}`).toBe(true);
      expect(raw.includes("0.37") || raw.includes("0.59"), raw).toBe(false);
      expect(Object.values(o).some((v) => v === 370_000 || v === 590_000), raw).toBe(false);
    }
  });
});

// MS (docs/basemap-a1.md): A1, "202, retry" with stored packages, and O1, the store id from KV. Each row uses its own
// radius, so its keys are its own; the bucket and KV are the test environment's (in memory). MX1: C1's parallel reads.
const pkgBucket = () => (cf as unknown as { BASEMAP_PKG: R2Bucket }).BASEMAP_PKG;
const storeKv = () => (cf as unknown as { ENRICH_KV: KVNamespace }).ENRICH_KV;
const qOf = (qs: string) => { const p = parseQuery(new URL(`https://x/?${qs}`)); if (!p.ok) throw new Error(p.code); return p.q; };
const buildQ = (qs: string) => { const q = qOf(qs); return { lat0Micro: q.lat0Micro, lon0Micro: q.lon0Micro, rKm: q.rKm, epsMm: q.epsMm, mask: q.mask, levels: q.levels }; };
/** builderOver's counting builder, each call delayed: slower than the test's shortened wait. */
function slowOver(objects: Map<string, Uint8Array>, delayMs: number) {
  const b = builderOver(objects);
  const BASEMAP = { async fetch(input: RequestInfo, init?: RequestInit) { await new Promise((r) => setTimeout(r, delayMs)); return b.BASEMAP.fetch(input, init); } } as unknown as Fetcher;
  return { BASEMAP, n: b.n };
}
const at = (r: number, lat = "0.37") => QW.replace("r=97", `r=${r}`).replace("lat=0.37", `lat=${lat}`);
const basemapPath = (qs: string) => `/api/v1/blipscope/basemap?${qs}`;

describe("MS, A1's stored packages and O1's store id", () => {
  it("MS1: KV's store id lets a fresh isolate answer If-None-Match with 304-kv and no builder call", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    await storeKv().put("bm:store", s.storeId);
    const etag = await etagOf(qOf(at(101)), BASEMAP_REV, s.storeId);
    const { res, line } = await mkCall(() => call(get(basemapPath(at(101)), { "If-None-Match": etag }), { BASEMAP: b.BASEMAP }));
    expect([res.status, line.bm, b.n.calls]).toEqual([304, "304-kv", 0]);
  });

  it("MS2: a package kept in BASEMAP_PKG is served on a fresh isolate as stored, with no builder call; then it is a hit", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const built = await build(s.store, buildQ(at(102)), { commit: COMMIT });
    if (!built.ok) throw new Error("MS2's fixture did not build (BLIND)");
    await pkgBucket().put(pkgKey(s.storeId, await basemapDigest(qOf(at(102)))), built.bytes);
    await storeKv().put("bm:store", s.storeId);
    const first = await mkCall(() => call(get(basemapPath(at(102))), { BASEMAP: b.BASEMAP }));
    expect([first.res.status, first.line.bm, b.n.calls]).toEqual([200, "stored", 0]);
    expect(new Uint8Array(await first.res.arrayBuffer())).toEqual(built.bytes);
    const again = await mkCall(() => call(get(basemapPath(at(102))), { BASEMAP: b.BASEMAP }));
    expect([again.res.status, again.line.bm, b.n.calls]).toEqual([200, "hit", 0]);
  });

  it("MS3: a build slower than the wait answers 202 (Retry-After 15, known-miss); then its package is kept and the lock gone", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = slowOver(s.store.objects, 300);
    const digest = await basemapDigest(qOf(at(103)));
    await storeKv().put("bm:store", s.storeId);
    basemapTiming.syncWaitMs = 50;
    try {
      const { res, line } = await mkCall(() => call(get(basemapPath(at(103))), { BASEMAP: b.BASEMAP }));
      expect([res.status, res.headers.get("retry-after"), line.bm]).toEqual([202, "15", "known-miss"]);
    } finally { basemapTiming.syncWaitMs = SYNC_WAIT_MS; }
    expect(b.n.calls).toBe(1);
    expect(await pkgBucket().head(pkgKey(s.storeId, digest))).not.toBeNull();
    expect(await pkgBucket().head(lockKey(s.storeId, digest))).toBeNull();
  });

  it("MS4: while a fresh lock is held, a second request is 202 in-flight and starts no build", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const digest = await basemapDigest(qOf(at(104)));
    await storeKv().put("bm:store", s.storeId);
    await pkgBucket().put(lockKey(s.storeId, digest), "", { customMetadata: { at: String(Date.now()) } });
    const { res, line } = await mkCall(() => call(get(basemapPath(at(104))), { BASEMAP: b.BASEMAP }));
    expect([res.status, line.bm, b.n.calls]).toEqual([202, "in-flight", 0]);
  });

  it("MS5: a lock older than 45 s is taken over: the builder is called once", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    const digest = await basemapDigest(qOf(at(105)));
    await storeKv().put("bm:store", s.storeId);
    await pkgBucket().put(lockKey(s.storeId, digest), "", { customMetadata: { at: String(Date.now() - 60_000) } });
    const { res } = await mkCall(() => call(get(basemapPath(at(105))), { BASEMAP: b.BASEMAP }));
    expect([res.status, b.n.calls]).toEqual([200, 1]);
  });

  it("MS6: a build within the wait answers 200 and keeps its package; elsewhere it is then served as stored", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    await storeKv().put("bm:store", s.storeId);
    const first = await mkCall(() => call(get(basemapPath(at(106))), { BASEMAP: b.BASEMAP }));
    expect([first.res.status, b.n.calls]).toEqual([200, 1]);
    // "elsewhere": another isolate in a data centre whose Cache API does not hold the copy
    await (caches as unknown as { default: Cache }).default.delete(cacheKey(first.res.headers.get("etag")!));
    resetBasemapIsolate();
    const second = await mkCall(() => call(get(basemapPath(at(106))), { BASEMAP: b.BASEMAP }));
    expect([second.res.status, second.line.bm, b.n.calls]).toEqual([200, "stored", 1]);
  });

  it("MS7: a no_coverage build that ends after the 202 is kept as an error; the retry is 404 stored-error with no builder call", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = slowOver(s.store.objects, 300);
    const digest = await basemapDigest(qOf(at(107, "10.00")));
    await storeKv().put("bm:store", s.storeId);
    basemapTiming.syncWaitMs = 50;
    try {
      const first = await mkCall(() => call(get(basemapPath(at(107, "10.00"))), { BASEMAP: b.BASEMAP }));
      expect(first.res.status).toBe(202);
    } finally { basemapTiming.syncWaitMs = SYNC_WAIT_MS; }
    expect(await pkgBucket().head(errKey(s.storeId, digest))).not.toBeNull();
    resetBasemapIsolate();
    const retry = await mkCall(() => call(get(basemapPath(at(107, "10.00"))), { BASEMAP: b.BASEMAP }));
    expect([retry.res.status, retry.line.bm, b.n.calls]).toEqual([404, "stored-error", 1]);
  });

  it("MS8: every key A1 writes is the store id and the digest only, and no line holds the origin", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    await storeKv().put("bm:store", s.storeId);
    const real = pkgBucket(), keys: string[] = [];
    const rec = {
      put: (k: string, v: unknown, o?: R2PutOptions) => { keys.push(k); return real.put(k, v as never, o); },
      get: (k: string) => real.get(k), head: (k: string) => real.head(k), delete: (k: string) => real.delete(k),
    } as unknown as R2Bucket;
    const from = logs.length;
    expect((await mkCall(() => call(get(basemapPath(at(108))), { BASEMAP: b.BASEMAP, BASEMAP_PKG: rec }))).res.status).toBe(200);
    expect((await mkCall(() => call(get(basemapPath(at(108, "10.00"))), { BASEMAP: b.BASEMAP, BASEMAP_PKG: rec }))).res.status).toBe(404);
    expect(keys.length).toBeGreaterThanOrEqual(3); // CONTROL: a lock and a package, then a lock and an error
    for (const k of keys) {
      expect(k).toMatch(/^bm\/v1\/(pkg|pkg-lock)\/[0-9a-f]{12}\/[0-9a-f]{24}(\.err)?$/);
      for (const c of ["370000", "590000", "100000"]) expect(k).not.toContain(c);
    }
    for (const l of logs.slice(from)) for (const c of ["0.37", "0.59", "10.00"]) expect(l, l).not.toContain(c);
  });

  it("MS9: with BASEMAP_PKG unbound, /basemap is exactly as before A1: 200, one builder call, five reads", async () => {
    resetBasemapIsolate();
    const s = await mwStore(), b = builderOver(s.store.objects);
    await storeKv().put("bm:store", s.storeId);
    const { res } = await mkCall(() => call(get(basemapPath(at(109))), { BASEMAP: b.BASEMAP, BASEMAP_PKG: undefined }));
    expect([res.status, b.n.calls, b.n.reads]).toEqual([200, 1, 5]);
  });

  it("MX1: C1 reads up to 6 at a time and builds the same result whatever order the reads arrive in", async () => {
    const { store, query } = await mbStore("A8");
    let inflight = 0, max = 0, calls = 0;
    const late = { async get(k: string) { const n = calls++; inflight++; max = Math.max(max, inflight); await new Promise((r) => setTimeout(r, Math.max(1, 40 - 3 * n))); inflight--; return store.get(k); } };
    // whole outcomes, a throw included: the same build either way is the property, whatever the build makes
    const outcome = async (r: { get(k: string): Promise<Uint8Array | null> }) => { try { return await build(r, query, { commit: COMMIT }); } catch (e) { return { threw: String(e) }; } };
    const delayed = await outcome(late);
    const instant = await outcome(store);
    expect(delayed).toEqual(instant); // the same result, its bytes included, though later reads arrived first
    expect(calls).toBeGreaterThanOrEqual(6); // CONTROL: enough reads to overlap
    expect(max).toBeGreaterThanOrEqual(2);
    expect(max).toBeLessThanOrEqual(6);
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
