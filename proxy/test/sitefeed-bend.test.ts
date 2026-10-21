// The Bend site feed (docs/bend-site-feed.md): B1-B9. Each check has a sabotage in the doc that turns it red.
import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from "vitest";
import { SCHEMA_V } from "../src/schema";
import {
  BEND,
  BendFeed,
  BendStore,
  RADIUS_KM,
  __resetSiteFeedForTests,
  covers,
  extract,
  fetchedKm,
  haversineKm,
  readSnapshot,
  type SiteFeedResponse,
  type Snapshot,
  type SnapStorage,
} from "../src/sitefeed/bend";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call, makeAc, pointBody } from "./helpers";

const LOL = "https://api.adsb.lol";
const R_EARTH_KM = 6371.0088;

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => {
  __resetBreakersForTests();
  __resetSiteFeedForTests();
});
afterEach(() => fetchMock.assertNoPendingInterceptors());

/** A point `km` due north of downtown: along a meridian, haversine is exactly R * dPhi. */
function northOf(km: number): { lat: number; lon: number } {
  return { lat: BEND.lat + (km / R_EARTH_KM) * (180 / Math.PI), lon: BEND.lon };
}

/** A fake Durable Object namespace that records every request and answers with `handler`. */
function fakeNs(handler: (req: Request) => Promise<Response>) {
  const calls: { method: string; path: string; body: string }[] = [];
  const ns = {
    idFromName: (n: string) => ({ name: n }),
    get: () => ({
      fetch: async (input: RequestInfo, init?: RequestInit) => {
        const req = new Request(input, init);
        calls.push({ method: req.method, path: new URL(req.url).pathname, body: req.method === "PUT" ? await req.clone().text() : "" });
        return handler(req);
      },
    }),
  };
  return { ns: ns as unknown as DurableObjectNamespace, calls };
}

/** A fake colo cache (Map-backed), so each test owns its cache. */
function fakeCache() {
  const m = new Map<string, string>();
  return {
    match: async (k: Request) => (m.has(k.url) ? new Response(m.get(k.url)) : undefined),
    put: async (k: Request, r: Response) => void m.set(k.url, await r.text()),
  } as unknown as Cache;
}

function metricsSpy() {
  const points: { blobs?: string[] }[] = [];
  return { m: { writeDataPoint: (p: { blobs?: string[] }) => points.push(p) } as unknown as AnalyticsEngineDataset, points };
}

const sitefeedEvents = (points: { blobs?: string[] }[]) =>
  points.filter((p) => p.blobs?.[0] === "sitefeed").map((p) => p.blobs?.[1]);

// A device tile AWAY from downtown (6.9 km off), radius bucket 160 -> the upstream is asked for 89 NM.
const TILE = { lat: "44.10", lon: "-121.25" };
const UPSTREAM_PATH = `/v2/lat/${TILE.lat}/lon/${TILE.lon}/dist/89`;
const deviceReq = (limit: number) => apiRequest(`/v1/blips?lat=44.1&lon=-121.25&r=160&limit=${limit}`);
const tileCacheKey = (limit: number) =>
  new Request(`https://blips-cache.internal/v1/blips?lat=${TILE.lat}&lon=${TILE.lon}&r=160&limit=${limit}&sv=${SCHEMA_V}`);

/** 20 aircraft inside the downtown circle (none of their coordinates contain the tile's strings) + 3 outside it. */
function bendSky() {
  const inside = Array.from({ length: 20 }, (_, i) =>
    makeAc({ hex: `b${String(i).padStart(5, "0")}`, flight: `BND${i} `, lat: 44.03 + i * 0.003, lon: -121.33 + i * 0.002 }),
  );
  const outside = [
    makeAc({ hex: "f00001", lat: 45.0, lon: -121.315 }), // ~105 km north: fetched, but not in the circle
    makeAc({ hex: "f00002", lat: 44.058, lon: -122.3 }), // ~79 km west
    makeAc({ hex: "f00003", lat: 43.4, lon: -121.315 }), // ~73 km south
  ];
  return [...inside, ...outside];
}

describe("B1 geometry", () => {
  it("covers() is true at the boundary and false 1 m past it; only planes within R are kept", () => {
    const boundary = fetchedKm(80) - RADIUS_KM; // the furthest a bucket-80 tile can be from downtown
    const inside = northOf(boundary - 0.001);
    const outside = northOf(boundary + 0.001);
    expect(covers(inside.lat, inside.lon, 80)).toBe(true);
    expect(covers(outside.lat, outside.lon, 80)).toBe(false);
    expect(covers(BEND.lat, BEND.lon, 40)).toBe(false); // a 40 km bucket can never hold a 48 km circle

    const near = northOf(RADIUS_KM - 0.01);
    const far = northOf(RADIUS_KM + 0.01);
    const kept = extract([
      { hex: "aaaaaa", cs: "", lat: near.lat, lon: near.lon, altFt: 1000, gsKt: 100, trackDeg: 0, vrateFpm: 0, category: 1, ageS: 1 },
      { hex: "bbbbbb", cs: "", lat: far.lat, lon: far.lon, altFt: 1000, gsKt: 100, trackDeg: 0, vrateFpm: 0, category: 1, ageS: 1 },
    ]);
    expect(kept.map((a) => a.hex)).toEqual(["aaaaaa"]);
    expect(haversineKm(BEND.lat, BEND.lon, near.lat, near.lon)).toBeLessThan(RADIUS_KM);
  });
});

describe("the writer, on the device path", () => {
  it("B2 privacy + B3 completeness: a snapshot from a tile away from downtown carries nothing of the tile, and all 20 planes", async () => {
    fetchMock.get(LOL).intercept({ path: UPSTREAM_PATH }).reply(200, pointBody(bendSky()));
    const { ns, calls } = fakeNs(async () => new Response(null, { status: 204 }));
    const res = await call(deviceReq(5), { SITE_FEED_BEND: "on", BEND_SNAPSHOT: ns });

    expect(res.status).toBe(200);
    expect((await res.json() as { n: number }).n).toBe(5); // the device got its own `limit`
    expect(calls).toHaveLength(1);
    expect(calls[0]!.method).toBe("PUT");
    const stored = JSON.parse(calls[0]!.body) as Snapshot & Record<string, unknown>;
    // B2: nothing but these keys, and none of the tile's text anywhere.
    expect(Object.keys(stored).sort()).toEqual(["aircraft", "source", "t"]);
    expect(calls[0]!.body).not.toContain(TILE.lat);
    expect(calls[0]!.body).not.toContain("44.1,");
    expect(calls[0]!.body).not.toContain(TILE.lon);
    expect(stored.source).toBe("adsb.lol");
    // B3: complete inside the circle, regardless of the device's limit=5; nothing from outside it.
    expect(stored.aircraft).toHaveLength(20);
    expect(stored.aircraft.every((a) => haversineKm(a.lat, a.lon, BEND.lat, BEND.lon) <= RADIUS_KM)).toBe(true);
    expect(stored.aircraft.some((a) => a.hex.startsWith("f0000"))).toBe(false);
  });

  it("B4 a Durable Object that rejects never slows or fails the device, and is counted once", async () => {
    // The switch-off response for this tile, then the same request with the feed on and its write rejecting.
    fetchMock.get(LOL).intercept({ path: UPSTREAM_PATH }).reply(200, pointBody(bendSky()));
    const off = await call(deviceReq(7), { SITE_FEED_BEND: "off" });
    const offBody = await off.text();
    await caches.default.delete(tileCacheKey(7));

    fetchMock.get(LOL).intercept({ path: UPSTREAM_PATH }).reply(200, pointBody(bendSky()));
    const { ns, calls } = fakeNs(async () => Promise.reject(new Error("durable object down")));
    const { m, points } = metricsSpy();
    const on = await call(deviceReq(7), { SITE_FEED_BEND: "on", BEND_SNAPSHOT: ns, METRICS: m });

    expect(off.status).toBe(200);
    expect(on.status).toBe(200);
    // The bodies differ only in `t` if the clock ticked between them; compare everything else.
    const strip = (s: string) => ({ ...JSON.parse(s), t: 0 });
    expect(strip(await on.text())).toEqual(strip(offBody));
    expect(calls).toHaveLength(1);
    expect(sitefeedEvents(points)).toEqual(["write_error"]);
  });

  it("B9 the switch: off makes no Durable Object call at all; on writes", async () => {
    fetchMock.get(LOL).intercept({ path: UPSTREAM_PATH }).reply(200, pointBody(bendSky()));
    const off = fakeNs(async () => new Response(null, { status: 204 }));
    await call(deviceReq(9), { SITE_FEED_BEND: undefined, BEND_SNAPSHOT: off.ns });
    expect(off.calls).toHaveLength(0);
    expect(await readSnapshot({ ...env, SITE_FEED_BEND: undefined, BEND_SNAPSHOT: off.ns }, Date.now(), fakeCache())).toEqual({
      available: false,
      reason: "disabled",
    });

    // On, against the REAL Durable Object binding: the device's fetch writes, and the reader sees it.
    await caches.default.delete(tileCacheKey(9));
    fetchMock.get(LOL).intercept({ path: UPSTREAM_PATH }).reply(200, pointBody(bendSky()));
    await call(deviceReq(9), { SITE_FEED_BEND: "on" });
    const got = await readSnapshot({ ...env, SITE_FEED_BEND: "on" }, Date.now(), fakeCache());
    expect(got.available).toBe(true);
    if (got.available) {
      expect(got.centre).toEqual({ lat: BEND.lat, lon: BEND.lon });
      expect(got.aircraft).toHaveLength(20);
      expect(got.source).toBe("adsb.lol");
    }
  });
});

describe("B5 the 30 s rule lives in storage", () => {
  it("a write 10 s later on a NEW instance is refused; 31 s later it is accepted", async () => {
    const map = new Map<string, unknown>();
    const storage: SnapStorage = {
      get: async <T>(k: string) => map.get(k) as T | undefined,
      put: async <T>(k: string, v: T) => void map.set(k, structuredClone(v)),
    };
    const snap: Snapshot = { t: 100, source: "adsb.fi", aircraft: [] };
    let now = 1_000_000;
    expect(await new BendStore(storage, () => now).write(snap)).toBe("written");
    now += 10_000;
    expect(await new BendStore(storage, () => now).write(snap)).toBe("refused_30s");
    now += 21_000;
    expect(await new BendStore(storage, () => now).write(snap)).toBe("written");
  });
});

describe("the reader, BendFeed.snapshot()", () => {
  const on = (ns: DurableObjectNamespace) => ({ ...env, SITE_FEED_BEND: "on", BEND_SNAPSHOT: ns });
  const stored = (t: number): Snapshot => ({
    t,
    source: "adsb.fi",
    aircraft: [{ hex: "abc123", cs: "X", lat: 44.06, lon: -121.3, alt_ft: 1, gs_kt: 1, track_deg: 1, vrate_fpm: 0, category: 1, seen_s: 1 }],
  });

  it("B6 no planes once the snapshot is more than 120 s old", async () => {
    const nowMs = 2_000_000_000;
    const t = Math.floor(nowMs / 1000);
    const old = fakeNs(async () => Response.json(stored(t - 121)));
    const r1 = await readSnapshot(on(old.ns), nowMs, fakeCache());
    expect(r1).toMatchObject({ available: true, age_s: 121, stale: true, aircraft: [] });
    const young = fakeNs(async () => Response.json(stored(t - 119)));
    const r2 = await readSnapshot(on(young.ns), nowMs, fakeCache());
    expect(r2).toMatchObject({ available: true, age_s: 119, stale: false });
    if (r2.available) expect(r2.aircraft).toHaveLength(1);
  });

  it("B7 reads are cached 10 s in the colo: one DO read for two calls 5 s apart, a second at 11 s", async () => {
    const nowMs = 2_000_000_000;
    const { ns, calls } = fakeNs(async () => Response.json(stored(Math.floor(nowMs / 1000))));
    const cache = fakeCache();
    await readSnapshot(on(ns), nowMs, cache);
    await readSnapshot(on(ns), nowMs + 5_000, cache);
    expect(calls.filter((c) => c.method === "GET")).toHaveLength(1);
    await readSnapshot(on(ns), nowMs + 11_000, cache);
    expect(calls.filter((c) => c.method === "GET")).toHaveLength(2);
  });

  it("B8 an empty snapshot is 'empty', and nothing is fetched from anywhere", async () => {
    const fetchSpy = vi.spyOn(globalThis, "fetch");
    const { ns, calls } = fakeNs(async () => new Response(null, { status: 404 }));
    const r = await readSnapshot(on(ns), Date.now(), fakeCache());
    expect(r).toEqual({ available: false, reason: "empty" });
    expect(calls.map((c) => `${c.method} ${c.path}`)).toEqual(["GET /get"]);
    expect(fetchSpy).not.toHaveBeenCalled();
    fetchSpy.mockRestore();
  });
});

describe("B12/B13 Bend only, credited with a link (adsb.fi's OK, Daniel 2026-10-21)", () => {
  const snapAt = (source: string): Snapshot => ({
    t: Math.floor(Date.now() / 1000),
    source,
    aircraft: [{ hex: "abc123", cs: "X", lat: 44.06, lon: -121.3, alt_ft: 1, gs_kt: 1, track_deg: 1, vrate_fpm: 0, category: 1, seen_s: 1 }],
  });

  it("B12 snapshot() takes no place, and a place passed anyway changes nothing", async () => {
    expect(BendFeed.prototype.snapshot.length).toBe(0);
    const names: string[] = [];
    const ns = {
      idFromName: (n: string) => (names.push(n), { name: n }),
      get: () => ({ fetch: async () => Response.json(snapAt("adsb.fi")) }),
    } as unknown as DurableObjectNamespace;
    const self = { env: { ...env, SITE_FEED_BEND: "on", BEND_SNAPSHOT: ns } };
    // Portland, passed as if the method accepted a place.
    const r = await (BendFeed.prototype.snapshot as (...a: unknown[]) => Promise<SiteFeedResponse>).call(self, 45.52, -122.68);
    expect(r.available).toBe(true);
    if (r.available) {
      expect(r.centre).toEqual({ lat: BEND.lat, lon: BEND.lon });
      expect(r.radius_km).toBe(RADIUS_KM);
    }
    expect(new Set(names)).toEqual(new Set(["bend"]));
  });

  it("B13 every available answer carries the source's link", async () => {
    for (const [source, url] of [["adsb.fi", "https://adsb.fi"], ["adsb.lol", "https://adsb.lol"]] as const) {
      const { ns } = fakeNs(async () => Response.json(snapAt(source)));
      const r = await readSnapshot({ ...env, SITE_FEED_BEND: "on", BEND_SNAPSHOT: ns }, Date.now(), fakeCache());
      expect(r).toMatchObject({ available: true, source, attribution_url: url });
    }
  });
});
