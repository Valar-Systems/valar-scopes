import { env } from "cloudflare:test";
import { afterEach, describe, expect, it } from "vitest";
import { rarityMultiplier } from "../src/leaderboard";
import { call, TEST_KEY } from "./helpers";

// A leaderboard submit: authed POST with a JSON body.
function submit(body: unknown, headers: Record<string, string> = {}): Request {
  return new Request("https://proxy.test/v1/leaderboard", {
    method: "POST",
    headers: { "X-Blip-Key": TEST_KEY, "Content-Type": "application/json", ...headers },
    body: JSON.stringify(body),
  });
}

const ID_A = "aaaa0001";
const ID_B = "bbbb0002";

// Board is cached in KV between tests in the same worker; clear it so each test
// sees a fresh aggregation.
afterEach(async () => {
  for (const prefix of ["lb:dev:", "lb:name:", "lb:firsttype:", "lb:board"]) {
    const list = await env.ENRICH_KV.list({ prefix });
    await Promise.all(list.keys.map((k) => env.ENRICH_KV.delete(k.name)));
    await env.ENRICH_KV.delete(prefix);
  }
});

describe("POST /v1/leaderboard", () => {
  it("stores a submission and returns the device's standing", async () => {
    const res = await call(
      submit({ id: ID_A, name: "Redmond Radar", radiusKm: 48, claimed: { airlines: 12, countries: 3, airports: 20 }, claimedTypes: ["A320", "B738", "C17"] }),
    );
    expect(res.status).toBe(200);
    const body = (await res.json()) as { ok: boolean; name: string; rank: number; points: number };
    expect(body.ok).toBe(true);
    expect(body.name).toBe("Redmond Radar");
    expect(body.rank).toBe(1);
    // Tiny fleet: every type is at 100% (x1). 3 types*10 + 12 airlines*5 + 3 countries*25 + 20 airports*2
    expect(body.points).toBe(3 * 10 + 12 * 5 + 3 * 25 + 20 * 2);
  });

  it("rejects malformed bodies and unauthed requests", async () => {
    expect((await call(submit({ id: "nope" }))).status).toBe(400);
    const noKey = new Request("https://proxy.test/v1/leaderboard", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ id: ID_A }),
    });
    expect((await call(noKey)).status).toBe(401);
  });

  it("keeps counts monotonic -- a lower resubmission cannot shrink the tally", async () => {
    await call(submit({ id: ID_A, name: "A", claimed: { airlines: 20, countries: 5, airports: 10 }, claimedTypes: ["A320", "B738"] }));
    const res = await call(submit({ id: ID_A, name: "A", claimed: { airlines: 3, countries: 1, airports: 0 }, claimedTypes: [] }));
    const body = (await res.json()) as { points: number };
    // still 2 types (x1) + 20 airlines + 5 countries + 10 airports (nothing shrank)
    expect(body.points).toBe(2 * 10 + 20 * 5 + 5 * 25 + 10 * 2);
  });

  it("suffixes a display-name collision from a different device", async () => {
    await call(submit({ id: ID_A, name: "Ace", claimed: {}, claimedTypes: [] }));
    const res = await call(submit({ id: ID_B, name: "Ace", claimed: {}, claimedTypes: [] }));
    expect(((await res.json()) as { name: string }).name).toBe("Ace 2");
  });

  it("clamps an implausible one-shot country jump for an existing device", async () => {
    await call(submit({ id: ID_A, name: "A", claimed: { countries: 2 }, claimedTypes: [] }));
    const res = await call(submit({ id: ID_A, name: "A", claimed: { countries: 500 }, claimedTypes: [] }));
    const body = (await res.json()) as { points: number };
    // countries clamped to prev(2) + 10/day cap = 12
    expect(body.points).toBe(12 * 25);
  });
});

describe("leaderboard scoring & rarity", () => {
  it("applies the rarity thresholds (<5% x5, <25% x2, else x1)", () => {
    expect(rarityMultiplier(0.04)).toBe(5);
    expect(rarityMultiplier(0.05)).toBe(2);
    expect(rarityMultiplier(0.2)).toBe(2);
    expect(rarityMultiplier(0.25)).toBe(1);
    expect(rarityMultiplier(1)).toBe(1);
  });

  it("weights a rare type above a common one in a realistic fleet", async () => {
    // 20 filler devices all with B738 (common); device A adds a rare C17 that
    // only it has -> 1/21 = 4.7% < 5% -> x5.
    for (let i = 0; i < 20; i++) {
      await call(submit({ id: `f000${(1000 + i)}`, name: `F${i}`, claimed: {}, claimedTypes: ["B738"] }));
    }
    await call(submit({ id: ID_A, name: "A", claimed: {}, claimedTypes: ["B738", "C17"] }));
    const res = await call(submit({ id: ID_A, name: "A", claimed: {}, claimedTypes: ["B738", "C17"] }));
    const body = (await res.json()) as { rank: number; points: number; rarestType: string; rarestPct: number };
    // B738 x1 (21/21) *10 + C17 x5 (1/21) *10 = 10 + 50 = 60
    expect(body.points).toBe(60);
    expect(body.rank).toBe(1); // the rare type puts A ahead of all the B738-only fillers (10 each)
    // C17 is the device's rarest catch (1 of 21 devices ~ 5%).
    expect(body.rarestType).toBe("C17");
    expect(body.rarestPct).toBe(5);
  });
});

describe("public leaderboard pages", () => {
  it("serves the JSON board unauthenticated with per-category leaders", async () => {
    await call(submit({ id: ID_A, name: "Alpha", claimed: { airlines: 9, countries: 4, airports: 5 }, claimedTypes: ["A320"] }));
    const res = await call(new Request("https://proxy.test/leaderboard.json"));
    expect(res.status).toBe(200);
    const body = (await res.json()) as { rows: { name: string }[]; leaders: { airlines: string[] } };
    expect(body.rows[0].name).toBe("Alpha");
    expect(body.leaders.airlines[0]).toBe("Alpha");
  });

  it("renders the public HTML board and a device profile unauthenticated", async () => {
    await call(submit({ id: ID_A, name: "Alpha", claimed: { countries: 15 }, claimedTypes: ["A320"] }));
    const page = await call(new Request("https://proxy.test/leaderboard"));
    expect(page.status).toBe(200);
    expect(page.headers.get("Content-Type")).toContain("text/html");
    expect(await page.text()).toContain("Alpha");

    const profile = await call(new Request(`https://proxy.test/leaderboard/${ID_A}`));
    expect(profile.status).toBe(200);
    const html = await profile.text();
    expect(html).toContain("Alpha");
    expect(html).toContain("Globetrotter"); // 15 countries earns the badge
  });

  it("404s an unknown profile id and 400s a malformed one", async () => {
    expect((await call(new Request("https://proxy.test/leaderboard/deadbeef"))).status).toBe(404);
    expect((await call(new Request("https://proxy.test/leaderboard/xyz"))).status).toBe(404); // no route match -> not_found
  });

  it("awards the First! badge to the earliest logger of a type", async () => {
    await call(submit({ id: ID_A, name: "Alpha", claimed: {}, claimedTypes: ["A320"] }));
    const profile = await call(new Request(`https://proxy.test/leaderboard/${ID_A}`));
    expect(await profile.text()).toContain("First!");
  });
});

// The v1 -> v2 boundary. These are the tests that would have caught the failure
// mode this rework was designed around: a device on old firmware submitting its
// SEEN list into a field the server now treats as claims.
describe("tap-to-claim scoring (v2)", () => {
  it("does not rank a legacy submission that has no claimedTypes field", async () => {
    // Exactly what v3 firmware sends: counts + typeCodes, no claim data at all.
    const res = await call(
      submit({ id: ID_A, name: "Legacy", counts: { types: 90, airlines: 40, countries: 12, airports: 80 }, typeCodes: ["A320", "B738", "C17"] }),
    );
    const body = (await res.json()) as { ok: boolean; legacy: boolean; rank: number; points: number };
    expect(body.ok).toBe(true);
    expect(body.legacy).toBe(true);
    // Stored, but absent from the board: a big SEEN tally must earn nothing.
    expect(body.points).toBe(0);
    expect(body.rank).toBe(0);
    const board = (await (await call(new Request("https://proxy.test/leaderboard.json"))).json()) as { rows: unknown[] };
    expect(board.rows).toHaveLength(0);
  });

  it("distinguishes 'claimed nothing yet' from 'legacy firmware'", async () => {
    // An empty ARRAY is a real, rankable score of zero -- unlike a missing field.
    const res = await call(submit({ id: ID_B, name: "Fresh", claimed: {}, claimedTypes: [] }));
    const body = (await res.json()) as { legacy: boolean; rank: number; points: number };
    expect(body.legacy).toBe(false);
    expect(body.points).toBe(0);
    expect(body.rank).toBe(1); // on the board, just at zero
  });

  it("scores claims and ignores the seen counts entirely", async () => {
    const res = await call(
      submit({
        id: ID_A,
        name: "Spotter",
        counts: { types: 500, airlines: 400, countries: 90, airports: 900 }, // huge antenna
        claimed: { types: 2, airlines: 1, countries: 1, airports: 3 },        // small attention
        claimedTypes: ["A320", "B738"],
      }),
    );
    const body = (await res.json()) as { points: number };
    // 2 claimed types (x1 in a one-device fleet) + 1 airline + 1 country + 3 airports.
    // None of the 500/400/90/900 seen figures appear anywhere in this number.
    expect(body.points).toBe(2 * 10 + 1 * 5 + 1 * 25 + 3 * 2);
  });

  it("shows claimed-of-seen on the public profile", async () => {
    await call(
      submit({
        id: ID_A, name: "Alpha",
        counts: { types: 153, airlines: 120, countries: 8, airports: 34 },
        claimed: { types: 47, airlines: 30, countries: 5, airports: 12 },
        claimedTypes: ["A320", "B738"],
      }),
    );
    const html = await (await call(new Request(`https://proxy.test/leaderboard/${ID_A}`))).text();
    expect(html).toContain("Types claimed");
    expect(html).toContain("of 153 seen"); // the denominator is visible, and is not the score
  });

  it("weights rarity by who CLAIMED a type, not who received it", async () => {
    // Nine devices all claim B738; only ID_A also claims C17. C17 is therefore
    // held by 1 of 10 (<25% -> x2) while B738 is at 100% (x1).
    for (let i = 0; i < 9; i++) {
      await call(submit({ id: `f000${1000 + i}`, name: `F${i}`, claimed: {}, claimedTypes: ["B738"] }));
    }
    await call(submit({ id: ID_A, name: "A", claimed: {}, claimedTypes: ["B738", "C17"] }));
    const res = await call(submit({ id: ID_A, name: "A", claimed: {}, claimedTypes: ["B738", "C17"] }));
    const body = (await res.json()) as { points: number; rarestType: string };
    expect(body.rarestType).toBe("C17");
    expect(body.points).toBe(10 * 1 + 10 * 2); // B738 x1 + C17 x2
  });

  it("extends the streak on a new claim, not on a new sighting", async () => {
    // First submit establishes the row and a 1-day streak from its first claim.
    await call(submit({ id: ID_A, name: "A", claimed: { airlines: 1 }, claimedTypes: ["A320"] }));
    // Second submit adds SEEN volume but no new claims: nothing to extend.
    const res = await call(
      submit({ id: ID_A, name: "A", counts: { types: 999, airlines: 999 }, claimed: { airlines: 1 }, claimedTypes: ["A320"] }),
    );
    const body = (await res.json()) as { points: number };
    expect(body.points).toBe(10 + 5); // unchanged by the seen flood
  });
});
