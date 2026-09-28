import { fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from "vitest";
import { ADSBFI_HOLD_MS } from "../src/upstreams/adsb_fi";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call, hexBody, makeAc, pointBody } from "./helpers";

// NO REQUEST TO adsb.fi INSIDE A MINUTE AFTER A 4xx (ruled 2026-09-28: "on any
// 4xx/429 from adsb.fi, back off (no retries inside a minute); the README says
// those can trigger an IP block"). adsb.fi's README: "Making excessive invalid
// HTTP requests results in a temporary IP address restriction. Requests returning
// a 400, 401, 403, 404, or 429 status code count toward the limit."
//
// HOW "NOT CALLED" IS PROVEN HERE. disableNetConnect alone proves nothing: an
// unmatched request throws, the chain catches it as a network error and fails
// over, and the test passes either way. So each test registers the adsb.fi
// interceptor it expects NOT to be used, checks it is still pending, and only then
// consumes it by hand (the same pattern as the partitioning tests).

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});

beforeEach(() => __resetBreakersForTests());
afterEach(() => {
  vi.restoreAllMocks();
  fetchMock.assertNoPendingInterceptors();
});

const LOL = "https://api.adsb.lol";
const FI = "https://opendata.adsb.fi";
const FI_ON = { UPSTREAM_ADSB_FI_ENABLED: "true" };
const fiTile = (lat: number) => `/api/v3/lat/${lat}.00/lon/20.00/dist/24`;
const lolTile = (lat: number) => `/v2/lat/${lat}.00/lon/20.00/dist/24`;
const blips = (lat: number) => apiRequest(`/v1/blips?lat=${lat}&lon=20&r=40`);

describe("adsb.fi 4xx hold", () => {
  it("is a minute", () => {
    expect(ADSBFI_HOLD_MS).toBeGreaterThanOrEqual(60_000);
  });

  for (const [n, status] of [429, 400, 403, 404].entries()) {
    const L = 40 + n * 4; // distinct tiles per case: the Worker caches per tile
    it(`a ${status} is not retried, and adsb.fi is skipped for the next minute`, async () => {
      // Two replies registered; a retry would consume the second.
      fetchMock.get(FI).intercept({ path: fiTile(L) }).reply(status, "").times(2);
      fetchMock.get(LOL).intercept({ path: lolTile(L) }).reply(200, pointBody([makeAc({ hex: "f10001", lat: L, lon: 20 })]));

      const r1 = await call(blips(L), FI_ON);
      expect(r1.headers.get("X-Upstream")).toBe("adsb_lol");
      expect(fetchMock.pendingInterceptors().length).toBe(1); // no retry
      await fetch(`${FI}${fiTile(L)}`);

      // A DIFFERENT tile, 0 s later: adsb.fi is healthy, but held.
      fetchMock.get(FI).intercept({ path: fiTile(L + 1) }).reply(200, pointBody([makeAc({ hex: "f10002", lat: L + 1, lon: 20 })]));
      fetchMock.get(LOL).intercept({ path: lolTile(L + 1) }).reply(200, pointBody([makeAc({ hex: "f10003", lat: L + 1, lon: 20 })]));
      const r2 = await call(blips(L + 1), FI_ON);
      expect(r2.headers.get("X-Upstream")).toBe("adsb_lol");
      expect(fetchMock.pendingInterceptors().length).toBe(1); // adsb.fi not called
      await fetch(`${FI}${fiTile(L + 1)}`);

      // 59 s later: still held.
      const t0 = Date.now();
      vi.spyOn(Date, "now").mockReturnValue(t0 + ADSBFI_HOLD_MS - 1_000);
      fetchMock.get(FI).intercept({ path: fiTile(L + 2) }).reply(200, pointBody([], t0 + ADSBFI_HOLD_MS - 1_000));
      fetchMock.get(LOL).intercept({ path: lolTile(L + 2) }).reply(200, pointBody([], t0 + ADSBFI_HOLD_MS - 1_000));
      const r3 = await call(blips(L + 2), FI_ON);
      expect(r3.headers.get("X-Upstream")).toBe("adsb_lol");
      expect(fetchMock.pendingInterceptors().length).toBe(1);
      await fetch(`${FI}${fiTile(L + 2)}`);

      // 61 s later: adsb.fi is the primary again.
      vi.spyOn(Date, "now").mockReturnValue(t0 + ADSBFI_HOLD_MS + 1_000);
      fetchMock.get(FI).intercept({ path: fiTile(L + 3) }).reply(200, pointBody([], t0 + ADSBFI_HOLD_MS + 1_000));
      const r4 = await call(blips(L + 3), FI_ON);
      expect(r4.headers.get("X-Upstream")).toBe("adsb_fi");
    });
  }

  it("CONTROL: a 503 is not a hold -- one quick retry, and the next tile goes to adsb.fi", async () => {
    // adsb.fi's README counts 4xx, not 5xx; 5xx keeps the retry-then-breaker path.
    fetchMock.get(FI).intercept({ path: fiTile(34) }).reply(503, "").times(2);
    fetchMock.get(LOL).intercept({ path: lolTile(34) }).reply(200, pointBody([]));
    const r1 = await call(blips(34), FI_ON);
    expect(r1.headers.get("X-Upstream")).toBe("adsb_lol");

    fetchMock.get(FI).intercept({ path: fiTile(35) }).reply(200, pointBody([]));
    const r2 = await call(blips(35), FI_ON);
    expect(r2.headers.get("X-Upstream")).toBe("adsb_fi");
  });

  it("CONTROL: a 429 from adsb.lol is still retried once (no hold on that feed)", async () => {
    fetchMock.get(LOL).intercept({ path: lolTile(36) }).reply(429, "");
    fetchMock.get(LOL).intercept({ path: lolTile(36) }).reply(200, pointBody([]));
    const r = await call(blips(36));
    expect(r.headers.get("X-Upstream")).toBe("adsb_lol");
  });

  it("hex: a 429 from adsb.fi is not retried (the hex path otherwise tries 3 times) and holds the point path too", async () => {
    fetchMock.get(FI).intercept({ path: "/api/v2/hex/f20001" }).reply(429, "").times(3);
    fetchMock.get(LOL).intercept({ path: "/v2/hex/f20001" }).reply(200, hexBody([{ hex: "f20001", r: "N1HOLD", t: "B738" }]));

    const res = await call(apiRequest("/v1/enrich/f20001"), FI_ON);
    expect(res.status).toBe(200);
    expect(((await res.json()) as { r: string }).r).toBe("N1HOLD");
    expect(fetchMock.pendingInterceptors().length).toBe(1); // 2 of 3 left: one attempt only
    // Retire the two unused replies (the interceptor was registered .times(3)).
    await fetch(`${FI}/api/v2/hex/f20001`);
    await fetch(`${FI}/api/v2/hex/f20001`);

    // The hold is per feed (per relay IP), not per operation.
    fetchMock.get(FI).intercept({ path: fiTile(37) }).reply(200, pointBody([]));
    fetchMock.get(LOL).intercept({ path: lolTile(37) }).reply(200, pointBody([]));
    const r2 = await call(blips(37), FI_ON);
    expect(r2.headers.get("X-Upstream")).toBe("adsb_lol");
    await fetch(`${FI}${fiTile(37)}`);
  });
});
