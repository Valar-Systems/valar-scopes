import { describe, expect, it } from "vitest";
import { adsbFi, adsbFiB } from "../src/upstreams/adsb_fi";
import { FEEDS, enabledFeeds } from "../src/upstreams/chain";
import type { Env } from "../src/types";

// Guards on the adsb.fi adapter. It ships DISABLED (its terms are personal /
// non-commercial with no redistribution right -- see adsb_fi.ts), so it is never
// exercised by the request-path tests; without these, a latent break in a feed
// that only wakes up during an incident would go unnoticed until the incident.
const env = (over: Partial<Env> = {}): Env => ({ ...over }) as Env;

describe("adsb.fi upstream adapter", () => {
  it("is disabled unless explicitly enabled -- the licence blocker is the default", () => {
    expect(adsbFi.enabled(env())).toBe(false);
    expect(adsbFi.enabled(env({ UPSTREAM_ADSB_FI_ENABLED: "false" }))).toBe(false);
    expect(adsbFi.enabled(env({ UPSTREAM_ADSB_FI_ENABLED: "true" }))).toBe(true);
  });

  it("keeps the relay-b instance off until its base URL is configured", () => {
    // Both instances gate on the same flag (the licence covers the data, not a
    // relay), but the secondary additionally needs its base URL.
    expect(adsbFiB.enabled(env({ UPSTREAM_ADSB_FI_ENABLED: "true" }))).toBe(false);
    expect(
      adsbFiB.enabled(
        env({ UPSTREAM_ADSB_FI_ENABLED: "true", UPSTREAM_ADSB_FI_BASE_B: "https://r/fi" }),
      ),
    ).toBe(true);
  });

  it("uses /v3 for positions -- /v2/lat returns a schema we silently read as empty", () => {
    // THE regression this file exists for, and it fails SILENTLY, so nothing else
    // would catch it. adsb.fi's deprecated /v2/lat/lon/dist still answers 200 with
    // valid JSON, but shaped `{now: <seconds>, aircraft: [...]}` -- no `ac` key.
    // fetchPointChain reads `json.ac`, gets undefined, and reports zero aircraft
    // with no error. Positions must be /v3 (`{ac, now: <ms>}`).
    const url = adsbFi.pointUrl(env(), "44.10", "-121.30", 86);
    expect(url).toBe("https://opendata.adsb.fi/api/v3/lat/44.10/lon/-121.30/dist/86");
    expect(url).not.toContain("/v2/lat");
  });

  it("routes through the relay when a base URL is set, preserving the /fi prefix", () => {
    const e = env({
      UPSTREAM_ADSB_FI_BASE: "https://relay-a.valarsystems.com/fi",
      UPSTREAM_ADSB_FI_BASE_B: "https://relay-b.valarsystems.com/fi",
    });
    expect(adsbFi.pointUrl(e, "44.10", "-121.30", 86)).toBe(
      "https://relay-a.valarsystems.com/fi/v3/lat/44.10/lon/-121.30/dist/86",
    );
    expect(adsbFiB.hexUrl(e, "a835af")).toBe("https://relay-b.valarsystems.com/fi/v2/hex/a835af");
  });

  it("carries the relay key only when one is configured", () => {
    expect(adsbFi.headers(env())["X-Relay-Key"]).toBeUndefined();
    expect(adsbFi.headers(env({ RELAY_KEY: "k" }))["X-Relay-Key"]).toBe("k");
    // Every upstream must be identifiable by the operator.
    expect(adsbFi.headers(env())["User-Agent"]).toContain("Blipscope");
  });
});

describe("upstream chain posture", () => {
  it("ships with adsb.lol as the only enabled position source", () => {
    // The licensed source leads and is the ONLY one on. If this ever fails,
    // something enabled a feed we have no redistribution right to.
    expect(enabledFeeds(env({ UPSTREAM_ADSB_LOL_BASE: "https://relay-a" })).map((f) => f.id)).toEqual([
      "adsb_lol",
    ]);
  });

  it("registers both adsb.fi instances for health reporting", () => {
    expect(FEEDS.map((f) => f.id)).toContain("adsb_fi");
    expect(FEEDS.map((f) => f.id)).toContain("adsb_fi_b");
  });
});
