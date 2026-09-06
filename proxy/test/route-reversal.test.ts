import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from "vitest";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call } from "./helpers";

/* ============================================================================
 * THE RETURN LEG, WHICH NO GEOMETRIC CHECK CAN SEE.
 *
 * The CC0 mirror holds ONE row per callsign. ASA537 is SEA->BUR in the morning
 * and BUR->SEA in the afternoon, so on the return leg the mirror hands back the
 * outbound endpoints -- and every distance-based test passes, because a
 * reversal preserves geography. The corridor is the same corridor.
 *
 * The track is the only evidence that separates them. This file pins the rule
 * and, more importantly, pins the two ways it must NOT fire:
 *
 *   - it WITHHOLDS, it never swaps. A swap prints a route no aircraft flew
 *     whenever the guess is wrong, and a departure turn or a hold makes it
 *     wrong routinely. There is deliberately no test asserting a swapped
 *     result, because there is deliberately no code that can produce one.
 *   - it stays quiet near the destination, where an arriving aircraft's
 *     bearing swings through everything while it manoeuvres -- exactly when
 *     the route is certainly correct.
 *
 * Geometry, computed rather than eyeballed (SEA 47.4489,-122.3094;
 * BUR 34.2007,-118.3587; route 1510 km):
 *
 *   from 40.5,-121.0   ->  SEA 780 km brg 352.7 | BUR 738 km brg 160.8
 *   from 34.55,-118.35 ->  BUR  39 km brg 181.2
 *
 * so trk=160 is 0.8 deg off the bearing to BUR (on the leg) and trk=340 is
 * 179.2 deg off it (the return leg). Neither position trips the older
 * not_for_position test, which is what keeps these cases about ONE rule.
 * ========================================================================= */

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => __resetBreakersForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

const PROD_ROUTES = { ROUTE_ADSBDB_ENABLED: "false" } as const;
const HEX = "c0ffee";
const CS = "ASA537";

async function seed(): Promise<void> {
  await env.ENRICH_KV.put(
    `ac:${HEX}`,
    JSON.stringify({ found: true, r: "N537AS", t: "B739", tn: "Boeing 737-900", op: "Alaska", v: 99 }),
  );
  await env.ENRICH_KV.put(`rt:${CS}`, JSON.stringify({ o: "SEA", d: "BUR" }));
  await env.ENRICH_KV.put("ap:SEA", JSON.stringify([47.4489, -122.3094]));
  await env.ENRICH_KV.put("ap:BUR", JSON.stringify([34.2007, -118.3587]));
}

async function enrich(query: string): Promise<{ od: [string, string]; logs: string[] }> {
  const logs: string[] = [];
  const spy = vi
    .spyOn(console, "log")
    .mockImplementation((...a: unknown[]) => void logs.push(a.map(String).join(" ")));
  let body: Record<string, string>;
  try {
    const res = await call(apiRequest(`/v1/enrich/${HEX}?cs=${CS}&${query}`), PROD_ROUTES);
    body = (await res.json()) as Record<string, string>;
  } finally {
    spy.mockRestore();
  }
  return { od: [body.o ?? "", body.d ?? ""], logs };
}

describe("a route is withheld from an aircraft flying the other way", () => {
  it("CONTROL: on the leg, the route is served", async () => {
    await seed();
    const { od, logs } = await enrich("lat=40.5&lon=-121.0&trk=160");
    expect(od).toEqual(["SEA", "BUR"]);
    // The control matters more than the assertion above: without it, a rule
    // that withheld EVERYTHING would pass every other test in this file.
    expect(logs.some((l) => l.includes("route_stale"))).toBe(false);
  });

  it("on the return leg, the route is withheld -- and says why", async () => {
    await seed();
    const { od, logs } = await enrich("lat=40.5&lon=-121.0&trk=340");
    expect(od).toEqual(["", ""]);
    // The REASON is asserted, not just the withholding. `reversed` and
    // `not_for_position` are different facts about the world and the whole
    // point of logging this is to count how often the first one happens.
    expect(logs.some((l) => l.includes(`route_stale cs=${CS} SEA->BUR reversed`))).toBe(true);
  });

  it("says nothing about direction when the device sends no track", async () => {
    await seed();
    // Same position, same route, no trk: there is no evidence, so there is no
    // verdict. Blanking here would blank every pre-v9 device's card.
    const { od, logs } = await enrich("lat=40.5&lon=-121.0");
    expect(od).toEqual(["SEA", "BUR"]);
    expect(logs.some((l) => l.includes("route_stale"))).toBe(false);
  });

  it("stays quiet on final approach, where the bearing swings", async () => {
    await seed();
    // 39 km from BUR with a track 158.8 deg off the bearing to it -- over the
    // 135 deg threshold, and meaningless: this aircraft is manoeuvring to land
    // at the destination the card is about to name correctly.
    const { od, logs } = await enrich("lat=34.55&lon=-118.35&trk=340");
    expect(od).toEqual(["SEA", "BUR"]);
    expect(logs.some((l) => l.includes("route_stale"))).toBe(false);
  });

  it("an unknown airport code is not a contradiction", async () => {
    // Same rule as the distance test: with no coordinates there is nothing to
    // measure, and refusing on absence of evidence would blank every route
    // through a field we do not carry.
    await seed();
    await env.ENRICH_KV.delete("ap:BUR");
    const { od } = await enrich("lat=40.5&lon=-121.0&trk=340");
    expect(od).toEqual(["SEA", "BUR"]);
  });
});
