import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call } from "./helpers";

/* ============================================================================
 * THE MIRROR MUST STILL BE REACHABLE FROM THE KEY THE FIRMWARE ACTUALLY SENDS.
 *
 * This file exists because routekey.test.ts is green and the fleet had no
 * routes for five days.
 *
 * TWO POPULATIONS SHARED ONE KEY NAMESPACE, and only one of them was in mind:
 *
 *   1. RUNTIME CACHE  -- an upstream answer for one callsign, written with a
 *      TTL. A callsign is reused across legs, so this entry can be the WRONG
 *      leg for the next aircraft to fly it. That is the bug c5d8c84 fixed, and
 *      keying on direction of travel is the right fix for THIS population.
 *
 *   2. THE CC0 MIRROR -- 619,103 `rt:<CALLSIGN>` keys ingested 2026-08-26 by
 *      scripts/ingest-routes.ts, written WITHOUT a TTL and WITHOUT a bucket.
 *      This is a static schedule database, not an observation of a leg: there
 *      is one entry per callsign and direction does not change it.
 *
 * On 2026-08-26 population 2 became the ONLY route source in production
 * (ROUTE_ADSBDB_ENABLED="false"; adsb.lol's routeset had already gone to
 * 201-with-an-empty-body). On 2026-09-01 c5d8c84 moved the lookup key to
 * `rt:<cs>:<bucket>` for any device sending `trk` -- which the firmware does --
 * and every one of those 619,103 keys became unreachable in the same deploy.
 *
 * routekey.test.ts asserts, deliberately and correctly for population 1:
 *
 *     expect(routeCacheKey("ASA537", t)).not.toBe("rt:ASA537");
 *
 * ...so the suite proves the lookup key differs from the mirror's key, and
 * nothing anywhere asserted the mirror was still READ. The test encoded the
 * outage as the requirement. That is why the check below goes through
 * handleEnrich against a seeded KV rather than through routeCacheKey: a key
 * function can only be asked what it returns, never whether anything is there.
 *
 * THE ENVIRONMENT IS PINNED TO PRODUCTION'S, and that is load-bearing. The
 * top-level [vars] in wrangler.toml carry ROUTE_ADSBDB_ENABLED="true", so a
 * test that inherited the default would have adsbdb rescue every lookup and
 * pass while production stayed dark -- a check whose own environment lacks the
 * property it is checking. Production is "false"; so is this.
 * ========================================================================= */

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => __resetBreakersForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

// Production's route configuration: the mirror is the only source.
const PROD_ROUTES = { ROUTE_ADSBDB_ENABLED: "false" } as const;

const HEX = "a1b2c3";
const CS = "ASA537";

/** Seed the metadata cache so the test exercises the ROUTE path only. */
async function seedMeta(): Promise<void> {
  await env.ENRICH_KV.put(
    `ac:${HEX}`,
    JSON.stringify({
      found: true,
      r: "N537AS",
      t: "B739",
      tn: "Boeing 737-900",
      op: "Alaska Airlines",
      v: 99,
    }),
  );
}

/** Seed one mirror row EXACTLY as scripts/ingest-routes.ts writes it. */
async function seedMirrorRow(): Promise<void> {
  // The key shape is transcribed from the writer, not invented here:
  //   ingest-routes.ts:311  pairs.push([`rt:${cs}`, JSON.stringify({ o, d })])
  // If the ingest's key shape ever changes, this line is what must change with
  // it -- which is the point of deriving a check's input from the other side.
  await env.ENRICH_KV.put(`rt:${CS}`, JSON.stringify({ o: "SEA", d: "BUR" }));
}

describe("the CC0 route mirror survives the direction-of-travel key", () => {
  it("CONTROL: a device that sends no track reads the mirror", async () => {
    await seedMeta();
    await seedMirrorRow();

    // No interceptors are registered, and net connect is disabled: if this path
    // reaches for an upstream at all, it cannot succeed. A populated o/d here
    // therefore proves the answer came from KV and nowhere else.
    const res = await call(apiRequest(`/v1/enrich/${HEX}?cs=${CS}&lat=47.0&lon=-122.3`), PROD_ROUTES);
    const body = (await res.json()) as Record<string, string>;

    expect(res.status).toBe(200);
    expect([body.o, body.d]).toEqual(["SEA", "BUR"]);
  });

  it("REGRESSION: a device that sends a track reads it too", async () => {
    await seedMeta();
    await seedMirrorRow();

    // trk=160 is ASA537 southbound SEA->BUR, the leg the mirror row describes,
    // and the exact shape the firmware sends (AircraftManager.cpp:602).
    // Before the fix this returned {o:"",d:""}: `rt:ASA537:1` is not a key any
    // writer has ever produced, so the lookup missed, adsb.lol answered with an
    // empty body and adsbdb was switched off.
    const res = await call(
      apiRequest(`/v1/enrich/${HEX}?cs=${CS}&lat=47.0&lon=-122.3&trk=160`),
      PROD_ROUTES,
    );
    const body = (await res.json()) as Record<string, string>;

    expect(res.status).toBe(200);
    expect([body.o, body.d]).toEqual(["SEA", "BUR"]);
  });

  it("a mirror hit is still subject to the geometric plausibility check", async () => {
    // The mirror is authoritative about the SCHEDULE, not about which leg the
    // aircraft overhead is flying -- a callsign is reused, which is the whole
    // reason the bucket exists. So a mirror hit must go through the same
    // positional test the cached branch does, or reading it back re-opens the
    // wrong-leg bug (b16e859) through a different door.
    await seedMeta();
    await seedMirrorRow();
    // routeContradicted() needs BOTH endpoints' coordinates and treats an
    // unknown code as "no contradiction". Without these two rows this test
    // passes with the check never running -- which is how its first draft
    // passed against the UNFIXED code, for the entirely different reason that
    // nothing was found at all.
    await env.ENRICH_KV.put("ap:SEA", JSON.stringify([47.4489, -122.3094]));
    await env.ENRICH_KV.put("ap:BUR", JSON.stringify([34.2007, -118.3587]));

    // Miami: ~4,300 km from SEA and ~3,750 km from BUR, against a 1,530 km
    // route. Deliberately not Bend, Oregon -- Bend is 400 km from SEA and sits
    // INSIDE the corridor envelope, so the check correctly declines to fire
    // there. The check asks "is this aircraft nowhere near either endpoint",
    // not "is it on the airway".
    const res = await call(
      apiRequest(`/v1/enrich/${HEX}?cs=${CS}&lat=25.79&lon=-80.29&trk=160`),
      PROD_ROUTES,
    );
    const body = (await res.json()) as Record<string, string>;

    expect(res.status).toBe(200);
    expect([body.o, body.d]).toEqual(["", ""]);
  });

  // THE RETURN LEG IS ASSERTED IN test/route-reversal.test.ts, not here.
  // When this file was written it was not asserted anywhere and this comment
  // said so, because a reversal preserves geography and no distance test can
  // see one. The rule that closes it compares the TRACK against the bearing to
  // the destination and WITHHOLDS -- it never swaps. Kept as a pointer rather
  // than deleted: the next person to read this file will wonder about exactly
  // this case, and an absent answer reads as an unconsidered one.
});
