import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { __ttlConstantsForTests, acTtlSeconds } from "../src/enrich";
import { apiRequest, call, hexBody, makeAc } from "./helpers";

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => __resetBreakersForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

const LOL = "https://api.adsb.lol";
const ADSBDB = "https://api.adsbdb.com";

// The defect these cover, measured in production 2026-08-12: a feed record that
// carried a registration but no ICAO type counted as "content", took the 30 d
// TTL, and so never re-attempted the adsbdb type backfill. SkyWest E175s and
// Alaska MAX were served typeless 20-111 times each while adsbdb had the type.
describe("acTtlSeconds: the TTL rule itself", () => {
  const { AC_TTL_S, AC_NEG_TTL_S } = __ttlConstantsForTests;

  it("gives a reg-only entry (no type) the SHORT ttl -- the actual 2026-08-12 defect", () => {
    // Before the fix this returned the 30 d TTL because a registration counted
    // as content, so the adsbdb backfill was never re-attempted.
    expect(acTtlSeconds({ found: true, t: "" })).toBe(AC_NEG_TTL_S);
  });

  it("gives an entry WITH a type the long ttl", () => {
    expect(acTtlSeconds({ found: true, t: "E75L" })).toBe(AC_TTL_S);
  });

  it("gives a not-found entry the short ttl whatever else is set", () => {
    expect(acTtlSeconds({ found: false, t: "E75L" })).toBe(AC_NEG_TTL_S);
  });

  it("the two TTLs are actually different, or every assertion above is vacuous", () => {
    expect(AC_TTL_S).toBeGreaterThan(AC_NEG_TTL_S);
  });
});

describe("ac:<hex> entries are written under that rule", () => {
  it("a reg-only entry (no type) is written and stamped v2", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/a37d56" })
      .reply(200, hexBody([makeAc({ hex: "a37d56", r: "N324BS", t: "", desc: "", ownOp: "B&S AIR INC" })]));
    // Backfill consulted because there is no type, and it also comes up empty.
    fetchMock.get(ADSBDB).intercept({ path: "/v0/aircraft/a37d56" }).reply(404, '{"response":"unknown aircraft"}');

    await call(apiRequest("/v1/enrich/a37d56"));

    const stored = await env.ENRICH_KV.get<{ found: boolean; r: string; t: string; v: number }>(
      "ac:a37d56",
      "json",
    );
    // The stamp is what lets a later deploy tell old entries from new ones.
    expect(stored).toMatchObject({ r: "N324BS", t: "", v: 2 });
    expect(acTtlSeconds(stored!)).toBe(__ttlConstantsForTests.AC_NEG_TTL_S);
  });

  it("an entry WITH a type is stamped and earns the long ttl", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/4b1817" })
      .reply(200, hexBody([makeAc({ desc: "Airbus A340-313", ownOp: "Swiss" })]));

    await call(apiRequest("/v1/enrich/4b1817"));

    const stored = await env.ENRICH_KV.get<{ found: boolean; t: string; v: number }>("ac:4b1817", "json");
    expect(stored).toMatchObject({ t: "A343", v: 2 });
    expect(acTtlSeconds(stored!)).toBe(__ttlConstantsForTests.AC_TTL_S);
  });
});

// A KV expirationTtl is fixed at WRITE time and a cache HIT returns without
// rewriting, so changing the rule above does nothing to entries already in KV.
// These cover the self-heal that drains them without a purge.
describe("legacy entries written under the old rule heal themselves", () => {
  it("re-resolves a pre-v2 reg-only entry and backfills the type", async () => {
    // Exactly the shape production is full of: found, a registration, no type,
    // no version stamp.
    await env.ENRICH_KV.put(
      "ac:a34c2f",
      JSON.stringify({ found: true, r: "N311SY", t: "", tn: "", op: "SKYWEST" }),
    );

    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/a34c2f" })
      .reply(200, hexBody([makeAc({ hex: "a34c2f", r: "N311SY", t: "", desc: "", ownOp: "SKYWEST" })]));
    fetchMock
      .get(ADSBDB)
      .intercept({ path: "/v0/aircraft/a34c2f" })
      .reply(
        200,
        JSON.stringify({ response: { aircraft: { icao_type: "E75L", registration: "N311SY", type: "175" } } }),
      );

    const res = await call(apiRequest("/v1/enrich/a34c2f"));
    // The whole point: the type appears on a card that had been blank.
    expect(await res.json()).toMatchObject({ r: "N311SY", t: "E75L" });

    const stored = await env.ENRICH_KV.get<{ t: string; v: number }>("ac:a34c2f", "json");
    expect(stored).toMatchObject({ t: "E75L", v: 2 });
  });

  it("does NOT re-resolve a pre-v2 entry that already has a type", async () => {
    // No interceptors registered: any upstream call fails the test. That is the
    // assertion -- a good legacy entry must be served straight from KV, or a
    // deploy would invalidate the entire warm cache at once.
    await env.ENRICH_KV.put(
      "ac:4ca7b3",
      JSON.stringify({ found: true, r: "EI-DYA", t: "B738", tn: "Boeing 737-800", op: "Ryanair" }),
    );
    const res = await call(apiRequest("/v1/enrich/4ca7b3"));
    expect(await res.json()).toMatchObject({ t: "B738", tn: "Boeing 737-800" });
  });

  it("does NOT re-resolve the found=false outage hold-down marker", async () => {
    // Re-resolving this would defeat the mechanism that stops the fleet
    // amplifying a 429 storm -- the marker exists precisely to be a quiet hit.
    await env.ENRICH_KV.put("ac:a5dfef", JSON.stringify({ found: false, r: "", t: "", tn: "", op: "" }));
    const res = await call(apiRequest("/v1/enrich/a5dfef"));
    expect(((await res.json()) as { t: string }).t).toBe("");
  });

  it("does NOT re-resolve a v2 type-less entry (it already carries the short TTL)", async () => {
    await env.ENRICH_KV.put(
      "ac:a1a109",
      JSON.stringify({ found: true, r: "N204BS", t: "", tn: "", op: "", v: 2 }),
    );
    const res = await call(apiRequest("/v1/enrich/a1a109"));
    expect(((await res.json()) as { r: string }).r).toBe("N204BS");
  });
});
