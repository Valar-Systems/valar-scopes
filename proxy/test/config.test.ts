import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { apiRequest, call } from "./helpers";

interface ConfigBody {
  v: number;
  rev: number;
  minFw: string;
  enrich: string;
  pollActiveMs: number;
  pollIdleMs: number;
  pollNightMs: number;
  idleAfterMs: number;
  staleFactor: number;
  minStaleMs: number;
  upstreamState: string;
}

async function getConfig(model?: string): Promise<ConfigBody> {
  const res = await call(apiRequest("/v1/config", model ? { "X-Blip-Model": model } : {}));
  expect(res.status).toBe(200);
  return (await res.json()) as ConfigBody;
}

describe("/v1/config", () => {
  it("serves baked defaults for an unknown model", async () => {
    const c = await getConfig();
    expect(c.v).toBe(1);
    expect(c.rev).toBe(1);
    expect(c.minFw).toBe("0.0.0");
    expect(c.enrich).toBe("full");
    expect(c.pollActiveMs).toBe(5000);
    expect(c.pollIdleMs).toBe(15000);
    expect(c.pollNightMs).toBe(60000);
    expect(c.idleAfterMs).toBe(600000);
    expect(c.staleFactor).toBe(3);
    expect(c.minStaleMs).toBe(45000);
    expect(c.upstreamState).toBe("ok");
  });

  it("resolves the S3 tier server-side (faster cadence, full enrichment)", async () => {
    const c = await getConfig("s3-146");
    expect(c.pollActiveMs).toBe(2000);
    expect(c.pollIdleMs).toBe(10000);
    expect(c.pollNightMs).toBe(45000);
    expect(c.idleAfterMs).toBe(900000);
    expect(c.enrich).toBe("full");
  });

  it("resolves the C3 tier server-side (default cadence, watchlist enrichment)", async () => {
    const c = await getConfig("c3-128");
    expect(c.pollActiveMs).toBe(5000);
    expect(c.enrich).toBe("watchlist");
  });

  // ---- the stale-ladder floor ------------------------------------------------
  // These pin the REASON for minStaleMs, not just its presence. The device takes
  // max(staleFactor x poll, minStaleMs); when that arithmetic stops making sense the
  // symptom is a fleet showing amber on a healthy feed, which nothing else catches.
  it("floors the stale threshold above what a fast poll would demand", async () => {
    // THE LIVE BUG THIS FIXES, independent of any TTL change: at 2 s active polling,
    // staleFactor alone demands a picture younger than 6 s, while the relay tile TTL
    // has never been below 8 s. Both the 1.46" and 2.1" boards ship at that cadence,
    // so they can flag stale while someone is watching a perfectly healthy feed.
    for (const model of ["s3-146", "s3-21"]) {
      const c = await getConfig(model);
      const withoutFloor = c.staleFactor * c.pollActiveMs;
      expect(withoutFloor).toBeLessThan(8000); // below the tile TTL -- the bug
      expect(c.minStaleMs).toBeGreaterThan(withoutFloor); // ...and the floor binds
    }
  });

  it("leaves the idle and night tiers exactly where they were", async () => {
    // 45 s was not picked freehand: it IS the current idle-tier threshold
    // (staleFactor 3 x the 15 s idle poll), so the floor changes nothing at idle and
    // nothing at night, and only stops the fast tiers being stricter than the data can
    // possibly be. If the idle cadence ever moves, this fails loudly rather than
    // letting the floor silently start binding somewhere new.
    const c = await getConfig();
    expect(c.staleFactor * c.pollIdleMs).toBe(c.minStaleMs); // idle: unchanged
    expect(c.staleFactor * c.pollNightMs).toBeGreaterThan(c.minStaleMs); // night: unchanged
  });

  it("clears the relay tile TTL the pilot needs, with the lag stacked on top", async () => {
    // Worst-case HEALTHY age at the 30 s tile TTL the 50-board adsb.fi budget wants:
    // 30 (TTL) + ~1 (relay -> upstream fetch) + 3 (the Worker's fresh window) + one
    // poll interval since the device's own merge. The floor has to clear that, or the
    // TTL rise buys rate headroom at the price of a permanently amber fleet.
    const c = await getConfig();
    const worstCaseHealthyMs = 30_000 + 1_000 + 3_000 + c.pollActiveMs;
    expect(c.minStaleMs).toBeGreaterThan(worstCaseHealthyMs);
  });

  it("is retunable from KV, so a soak can move it without an OTA", async () => {
    // The point of shipping it as fleet config: if 30 s flaps in the soak, the fix is
    // a `wrangler kv key put`, not a firmware release to 50 boards.
    await env.ENRICH_KV.put("cfg:fleet", JSON.stringify({ defaults: { minStaleMs: 60000 } }));
    expect((await getConfig()).minStaleMs).toBe(60000);
    await env.ENRICH_KV.delete("cfg:fleet");
  });

  it("applies KV fleet overrides: defaults < model", async () => {
    await env.ENRICH_KV.put(
      "cfg:fleet",
      JSON.stringify({
        rev: 9,
        minFw: "3.5.0",
        defaults: { pollActiveMs: 7000 },
        models: { "c3-128": { pollNightMs: 90000 } },
      }),
    );

    const c3 = await getConfig("c3-128");
    expect(c3.rev).toBe(9);
    expect(c3.minFw).toBe("3.5.0");
    expect(c3.pollActiveMs).toBe(7000); // KV defaults override baked values
    expect(c3.pollNightMs).toBe(90000); // KV per-model overrides KV defaults
    expect(c3.enrich).toBe("watchlist"); // untouched baked model default survives

    const other = await getConfig();
    expect(other.pollActiveMs).toBe(7000);
    expect(other.pollNightMs).toBe(60000);
  });

  it("survives malformed KV config by serving baked defaults", async () => {
    await env.ENRICH_KV.put("cfg:fleet", "this is not json");
    const c = await getConfig();
    expect(c.pollActiveMs).toBe(5000);
    expect(c.rev).toBe(1);
  });
});
