import { fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call, makeAc, pointBody, FIXTURE_NOW_MS } from "./helpers";

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});

beforeEach(() => __resetBreakersForTests());

// Every registered interceptor must be consumed: proves both that expected
// upstream calls happened AND that cache hits made no extra ones.
afterEach(() => fetchMock.assertNoPendingInterceptors());

const LOL = "https://api.adsb.lol";
const FI = "https://opendata.adsb.fi";

describe("/v1/blips", () => {
  it("normalizes, filters, sorts and caches per tile", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/lat/47.40/lon/8.55/dist/24" })
      .reply(
        200,
        pointBody([
          makeAc(), // full entry at the tile centre
          makeAc({ hex: "aaaaaa", lat: 47.41, lon: 8.56, flight: undefined, alt_baro: undefined, gs: undefined, track: undefined, baro_rate: undefined, category: undefined, seen_pos: undefined, r: undefined, t: undefined }),
          makeAc({ hex: "666666", alt_baro: "ground" }), // filtered: airborne-only
          makeAc({ hex: "777777", seen_pos: 120 }), // filtered: stale position
        ]),
      );

    const res = await call(apiRequest("/v1/blips?lat=47.399&lon=8.5501&r=40"));
    expect(res.status).toBe(200);
    expect(res.headers.get("X-Cache")).toBe("MISS");
    expect(res.headers.get("X-Upstream")).toBe("adsb_lol");
    const text = await res.text();
    expect(res.headers.get("Content-Length")).toBe(String(text.length));

    const body = JSON.parse(text);
    expect(body.v).toBe(1);
    expect(body.t).toBe(Math.floor(FIXTURE_NOW_MS / 1000));
    expect(body.n).toBe(2);
    // Nearest-first: the full entry sits on the tile centre.
    expect(body.a[0]).toEqual(["4b1817", "SWR123", 474000, 85500, 37000, 451, 231, -704, 4, 2]);
    expect(body.a[1][0]).toBe("aaaaaa");

    // Jittered coordinates in the same 0.05 deg tile + same bucket: served from
    // cache -- no second interceptor exists, so a real fetch would fail.
    const res2 = await call(apiRequest("/v1/blips?lat=47.401&lon=8.549&r=35"));
    expect(res2.status).toBe(200);
    expect(res2.headers.get("X-Cache")).toBe("HIT");
    expect(await res2.json()).toEqual(body);
  });

  it("keys the cache by radius bucket", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/lat/10.00/lon/10.00/dist/13" }).reply(200, pointBody([]));
    fetchMock.get(LOL).intercept({ path: "/v2/lat/10.00/lon/10.00/dist/46" }).reply(200, pointBody([]));

    const r1 = await call(apiRequest("/v1/blips?lat=10&lon=10&r=15")); // bucket 20 -> 13 NM
    const r2 = await call(apiRequest("/v1/blips?lat=10&lon=10&r=45")); // bucket 80 -> 46 NM
    expect(r1.headers.get("X-Cache")).toBe("MISS");
    expect(r2.headers.get("X-Cache")).toBe("MISS"); // different bucket = different entry
  });

  it("caps the response at limit, keeping the nearest", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/lat/20.00/lon/20.00/dist/8" })
      .reply(
        200,
        pointBody([
          makeAc({ hex: "c00003", lat: 20.3, lon: 20 }),
          makeAc({ hex: "c00001", lat: 20.01, lon: 20 }),
          makeAc({ hex: "c00002", lat: 20.1, lon: 20 }),
        ]),
      );

    const res = await call(apiRequest("/v1/blips?lat=20&lon=20&r=10&limit=2"));
    const body = (await res.json()) as { n: number; a: [string, ...unknown[]][] };
    expect(body.n).toBe(2);
    expect(body.a.map((row) => row[0])).toEqual(["c00001", "c00002"]);
  });

  it("serves stale immediately and revalidates in the background (SWR)", async () => {
    const swrEnv = { BLIPS_FRESH_TTL_MS: "0" }; // everything cached is instantly stale
    const tile = "/v2/lat/30.00/lon/30.00/dist/24";

    fetchMock.get(LOL).intercept({ path: tile }).reply(200, pointBody([makeAc({ hex: "d00001", lat: 30, lon: 30 })]));
    const r1 = await call(apiRequest("/v1/blips?lat=30&lon=30&r=40"), swrEnv);
    expect(r1.headers.get("X-Cache")).toBe("MISS");
    expect(((await r1.json()) as { n: number }).n).toBe(1);

    // Stale hit: served from cache (old picture) while the background
    // revalidation consumes this second interceptor.
    fetchMock
      .get(LOL)
      .intercept({ path: tile })
      .reply(200, pointBody([makeAc({ hex: "d00001", lat: 30, lon: 30 }), makeAc({ hex: "d00002", lat: 30.01, lon: 30 })]));
    const r2 = await call(apiRequest("/v1/blips?lat=30&lon=30&r=40"), swrEnv);
    expect(r2.headers.get("X-Cache")).toBe("STALE");
    expect(((await r2.json()) as { n: number }).n).toBe(1); // still the old picture

    // Next request sees the revalidated picture (and triggers the next cycle).
    fetchMock.get(LOL).intercept({ path: tile }).reply(200, pointBody([]));
    const r3 = await call(apiRequest("/v1/blips?lat=30&lon=30&r=40"), swrEnv);
    expect(r3.headers.get("X-Cache")).toBe("STALE");
    expect(((await r3.json()) as { n: number }).n).toBe(2); // the revalidated data
  });

  it("503s fast on a cold slow tile, then serves the warmed cache", async () => {
    const warmEnv = { BLIPS_SERVE_DEADLINE_MS: "50", UPSTREAM_TIMEOUT_MS: "5000" };
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/lat/70.00/lon/10.00/dist/24" })
      .reply(200, pointBody([makeAc({ hex: "f00001", lat: 70, lon: 10 })]))
      .delay(250); // slower than the 50 ms serve deadline

    const r1 = await call(apiRequest("/v1/blips?lat=70&lon=10&r=40"), warmEnv);
    expect(r1.status).toBe(503);
    expect(((await r1.json()) as { error: string }).error).toBe("warming");
    expect(r1.headers.get("Retry-After")).toBe("5");

    // call() waited on the execution context, so the background warm-up has
    // landed: the next poll is an instant cache hit.
    const r2 = await call(apiRequest("/v1/blips?lat=70&lon=10&r=40"), warmEnv);
    expect(r2.status).toBe(200);
    expect(r2.headers.get("X-Cache")).toBe("HIT");
    expect(((await r2.json()) as { n: number }).n).toBe(1);
  });

  it("returns 503 with Retry-After when upstreams fail and nothing is cached", async () => {
    fetchMock.get(LOL).intercept({ path: /\/v2\/lat\/40\.00.*/ }).replyWithError(new Error("boom"));

    const res = await call(apiRequest("/v1/blips?lat=40&lon=40&r=40"));
    expect(res.status).toBe(503);
    expect(res.headers.get("Retry-After")).toBe("10");
    expect(((await res.json()) as { error: string }).error).toBe("upstream_unavailable");
  });

  it("retries once through a transient 429 (shared-egress-IP throttle)", async () => {
    const tile = "/v2/lat/45.00/lon/45.00/dist/24";
    fetchMock.get(LOL).intercept({ path: tile }).reply(429, "slow down");
    fetchMock.get(LOL).intercept({ path: tile }).reply(200, pointBody([makeAc({ hex: "e10001", lat: 45, lon: 45 })]));

    const res = await call(apiRequest("/v1/blips?lat=45&lon=45&r=40"));
    expect(res.status).toBe(200);
    expect(res.headers.get("X-Upstream")).toBe("adsb_lol");
    expect(((await res.json()) as { n: number }).n).toBe(1);
  });

  it("fails over to adsb.fi when enabled and adsb.lol errors", async () => {
    const failoverEnv = { UPSTREAM_ADSB_FI_ENABLED: "true" };
    fetchMock.get(LOL).intercept({ path: /\/v2\/lat\/50\.00.*/ }).replyWithError(new Error("down"));
    fetchMock.get(FI).intercept({ path: "/api/v2/lat/50.00/lon/50.00/dist/24" }).reply(200, pointBody([makeAc({ hex: "e00001", lat: 50, lon: 50 })]));

    const res = await call(apiRequest("/v1/blips?lat=50&lon=50&r=40"), failoverEnv);
    expect(res.status).toBe(200);
    expect(res.headers.get("X-Upstream")).toBe("adsb_fi");
    expect(((await res.json()) as { n: number }).n).toBe(1);
  });

  it("opens the circuit breaker after 3 consecutive failures", async () => {
    const failoverEnv = { UPSTREAM_ADSB_FI_ENABLED: "true" };
    // Exactly 3 adsb.lol interceptors: the 4th request must NOT reach adsb.lol
    // (breaker open) -- assertNoPendingInterceptors + disableNetConnect prove it.
    fetchMock.get(LOL).intercept({ path: /\/v2\/lat\/.*/ }).replyWithError(new Error("down")).times(3);
    fetchMock.get(FI).intercept({ path: /\/api\/v2\/lat\/.*/ }).reply(200, pointBody([])).times(4);

    for (const lat of [60, 61, 62, 63]) {
      const res = await call(apiRequest(`/v1/blips?lat=${lat}&lon=10&r=40`), failoverEnv);
      expect(res.status).toBe(200);
      expect(res.headers.get("X-Upstream")).toBe("adsb_fi");
    }
  });

  it("never lets the breaker skip the sole enabled feed (adsb.lol terminal)", async () => {
    // Default env: both failovers off, so adsb.lol is the ONLY enabled feed and
    // therefore terminal. An open breaker on a terminal feed has nothing to fall
    // over to -- fast-failing it just manufactures a 30 s all-503 window -- so it
    // must be tried on EVERY request regardless of breaker state. Trip the 3-strike
    // breaker on three cold tiles, then prove request #4 STILL reaches adsb.lol.
    // (Pre-fix, #4 was skipped and returned 503 upstream_unavailable.)
    fetchMock.get(LOL).intercept({ path: /\/v2\/lat\/8[012]\.00\/lon\/10\.00\/dist\/24/ }).replyWithError(new Error("down")).times(3);
    fetchMock.get(LOL).intercept({ path: "/v2/lat/83.00/lon/10.00/dist/24" }).reply(200, pointBody([makeAc({ hex: "f80001", lat: 83, lon: 10 })]));

    for (const lat of [80, 81, 82]) {
      const res = await call(apiRequest(`/v1/blips?lat=${lat}&lon=10&r=40`));
      expect(res.status).toBe(503); // cold tile + adsb.lol down, nothing cached
    }
    // Breaker is now "open" after 3 strikes -- but adsb.lol is terminal, so the
    // 4th request still tries it and succeeds instead of self-inflicting a 503.
    const recovered = await call(apiRequest("/v1/blips?lat=83&lon=10&r=40"));
    expect(recovered.status).toBe(200);
    expect(recovered.headers.get("X-Upstream")).toBe("adsb_lol");
  });

  it("rejects bad parameters", async () => {
    expect((await call(apiRequest("/v1/blips?lat=999&lon=8&r=40"))).status).toBe(400);
    expect((await call(apiRequest("/v1/blips?lat=47&lon=8&r=nope"))).status).toBe(400);
    expect((await call(apiRequest("/v1/blips?lon=8&r=40"))).status).toBe(400);
  });
});
