import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from "vitest";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call } from "./helpers";

/* ============================================================================
 * AN INSTRUMENT NOBODY HAS WATCHED FIRE IS NOT AN INSTRUMENT.
 *
 * These two log lines were added because a fleet-wide route blackout produced
 * NO log line anywhere that distinguished the three states it could have been
 * in: a source that was switched off, a source whose breaker had latched, and a
 * source that answered fine and was never needed. All three are silence.
 *
 * So the point of this file is not coverage. It is that the emission has been
 * OBSERVED rather than reasoned about -- the failure mode for a probe is that
 * it never plants, and a green test that asserts nothing about the payload is
 * exactly what that looks like. Both assertions below therefore name the
 * reason/state string, not just a count.
 * ========================================================================= */

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => __resetBreakersForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

/** Capture console.log for the duration of one call. */
async function captureLogs(fn: () => Promise<void>): Promise<string[]> {
  const lines: string[] = [];
  const spy = vi
    .spyOn(console, "log")
    .mockImplementation((...a: unknown[]) => void lines.push(a.map(String).join(" ")));
  try {
    await fn();
  } finally {
    spy.mockRestore();
  }
  return lines;
}

describe("a skipped upstream and a tripped breaker both say so", () => {
  it("names WHY a route source was not called, and WHEN its breaker opened", async () => {
    await env.ENRICH_KV.put(
      "ac:beef01",
      JSON.stringify({ found: true, r: "N1", t: "B738", tn: "Boeing 737-800", op: "x", v: 99 }),
    );

    const lines = await captureLogs(async () => {
      // ZZZ999 is deliberately absent from the mirror, so the lookup falls
      // through to the chain. No interceptor is registered and net connect is
      // disabled, so adsb.lol's routeset fails; four passes take it past the
      // 3-failure threshold. adsbdb is off, as it is in production.
      for (let i = 0; i < 4; i++) {
        await call(apiRequest("/v1/enrich/beef01?cs=ZZZ999&lat=47&lon=-122&trk=160"), {
          ROUTE_ADSBDB_ENABLED: "false",
        });
      }
    });

    const skips = lines.filter((l) => l.includes('"evt":"upstream_skip"'));
    const breakers = lines.filter((l) => l.includes('"evt":"breaker"'));

    // The fallback is off, and the log says so in those words -- the thing that
    // was missing while the fleet had no routes.
    expect(skips.some((l) => l.includes('"id":"adsbdb_route"') && l.includes('"disabled_by_config"'))).toBe(
      true,
    );
    // The transition is logged once, with the failure count that caused it.
    expect(
      breakers.some((l) => l.includes('"id":"adsb_lol_route"') && l.includes('"state":"open"')),
    ).toBe(true);

    // CONTROL: the breaker line is emitted on the TRANSITION, not per call. Four
    // requests, one open. Without this the test would pass just as happily
    // against a per-call log, which would bury the signal it exists to surface.
    expect(breakers.filter((l) => l.includes('"state":"open"')).length).toBe(1);
  });
});
