import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { isNonIcaoAddress } from "../src/icaoalloc";
import { apiRequest, call, hexBody, makeAc } from "./helpers";

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => __resetBreakersForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

const LOL = "https://api.adsb.lol";

describe("isNonIcaoAddress", () => {
  // The addresses that actually drove this: real hexes taken off the production
  // gap list on 2026-08-12, not invented ones.
  it.each(["2b811b", "2b81da", "2b9d2e", "2bbb77", "299e2a"])(
    "rejects %s (observed TIS-B track ID from the backlog)",
    (hex) => {
      expect(isNonIcaoAddress(hex)).toBe(true);
    },
  );

  it("rejects readsb's explicit ~ non-ICAO marker and anything malformed", () => {
    expect(isNonIcaoAddress("~adfa2c")).toBe(true);
    expect(isNonIcaoAddress("zzzzzz")).toBe(true);
    expect(isNonIcaoAddress("abc")).toBe(true);
  });

  // THE CONTROL THAT MATTERS. A too-greedy table would blank real aircraft, and
  // that failure is silent -- the card just stays empty, exactly like the bug
  // this fixes. These are real airframes spanning the blocks nearest the holes.
  it.each([
    ["4b1817", "Swiss A340"],
    ["a37d56", "US, adjacent to the 0x9-0xa boundary"],
    ["ae6861", "US military -- must still reach the military floor"],
    ["ae1460", "US military C-17, resolves fully from the mil: table"],
    ["43c6f5", "UK"],
    ["410000", "UK, inside an EMPTY /16 that this table deliberately omits"],
    ["3c6444", "Germany"],
    ["c02655", "Canada"],
    ["7c8064", "Australia"],
    ["300000", "Italy, first address after the 0x23-0x2f hole"],
    ["220000", "last address before the hole"],
    // Regression: an earlier draft blacklisted four further "empty in the
    // registry" regions and blanked this one, which the enrich suite uses as a
    // fixture. A block earns a place in the table by being SEEN in live
    // traffic, not by being absent from a registry snapshot.
    ["f40001", "inside a registry-empty region that is deliberately NOT listed"],
    ["b00000", "ditto"],
    ["ca0000", "ditto"],
    ["910000", "ditto"],
  ])("accepts %s (%s)", (hex) => {
    expect(isNonIcaoAddress(hex)).toBe(false);
  });

  it("brackets the 0x230000-0x2fffff hole exactly", () => {
    expect(isNonIcaoAddress("22ffff")).toBe(false); // one below
    expect(isNonIcaoAddress("230000")).toBe(true); // first
    expect(isNonIcaoAddress("2fffff")).toBe(true); // last
    expect(isNonIcaoAddress("300000")).toBe(false); // one above
  });
});

describe("/v1/enrich short-circuits non-ICAO addresses", () => {
  it("answers empty WITHOUT touching any upstream", async () => {
    // fetchMock has disableNetConnect and no interceptor registered, so any
    // upstream call at all would throw here rather than quietly succeed --
    // that is what makes this assertion mean something.
    const res = await call(apiRequest("/v1/enrich/2b811b"));
    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({ v: 1, r: "", t: "", tn: "", op: "", o: "", d: "" });
  });

  it("writes nothing to KV for one", async () => {
    await call(apiRequest("/v1/enrich/2b9d2e"));
    expect(await env.ENRICH_KV.get("ac:2b9d2e")).toBeNull();
  });

  // Negative control for the two above: a REAL hex on the same path must still
  // do the upstream fetch and the KV write. Without this, deleting the entire
  // enrich body would leave both tests above passing.
  it("still enriches a real hex normally", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/4b1817" })
      .reply(200, hexBody([makeAc({ desc: "Airbus A340-313", ownOp: "Swiss" })]));

    const res = await call(apiRequest("/v1/enrich/4b1817"));
    expect(((await res.json()) as { t: string }).t).toBe("A343");
    expect(await env.ENRICH_KV.get("ac:4b1817")).not.toBeNull();
  });
});
