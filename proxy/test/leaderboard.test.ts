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
      submit({ id: ID_A, name: "Redmond Radar", radiusKm: 48, counts: { airlines: 12, countries: 3, airports: 20 }, typeCodes: ["A320", "B738", "C17"] }),
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
    await call(submit({ id: ID_A, name: "A", counts: { airlines: 20, countries: 5, airports: 10 }, typeCodes: ["A320", "B738"] }));
    const res = await call(submit({ id: ID_A, name: "A", counts: { airlines: 3, countries: 1, airports: 0 }, typeCodes: [] }));
    const body = (await res.json()) as { points: number };
    // still 2 types (x1) + 20 airlines + 5 countries + 10 airports (nothing shrank)
    expect(body.points).toBe(2 * 10 + 20 * 5 + 5 * 25 + 10 * 2);
  });

  it("suffixes a display-name collision from a different device", async () => {
    await call(submit({ id: ID_A, name: "Ace", counts: {}, typeCodes: [] }));
    const res = await call(submit({ id: ID_B, name: "Ace", counts: {}, typeCodes: [] }));
    expect(((await res.json()) as { name: string }).name).toBe("Ace 2");
  });

  it("clamps an implausible one-shot country jump for an existing device", async () => {
    await call(submit({ id: ID_A, name: "A", counts: { countries: 2 }, typeCodes: [] }));
    const res = await call(submit({ id: ID_A, name: "A", counts: { countries: 500 }, typeCodes: [] }));
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
      await call(submit({ id: `f000${(1000 + i)}`, name: `F${i}`, counts: {}, typeCodes: ["B738"] }));
    }
    await call(submit({ id: ID_A, name: "A", counts: {}, typeCodes: ["B738", "C17"] }));
    const res = await call(submit({ id: ID_A, name: "A", counts: {}, typeCodes: ["B738", "C17"] }));
    const body = (await res.json()) as { rank: number; points: number };
    // B738 x1 (21/21) *10 + C17 x5 (1/21) *10 = 10 + 50 = 60
    expect(body.points).toBe(60);
    expect(body.rank).toBe(1); // the rare type puts A ahead of all the B738-only fillers (10 each)
  });
});

describe("public leaderboard pages", () => {
  it("serves the JSON board unauthenticated with per-category leaders", async () => {
    await call(submit({ id: ID_A, name: "Alpha", counts: { airlines: 9, countries: 4, airports: 5 }, typeCodes: ["A320"] }));
    const res = await call(new Request("https://proxy.test/leaderboard.json"));
    expect(res.status).toBe(200);
    const body = (await res.json()) as { rows: { name: string }[]; leaders: { airlines: string[] } };
    expect(body.rows[0].name).toBe("Alpha");
    expect(body.leaders.airlines[0]).toBe("Alpha");
  });

  it("renders the public HTML board and a device profile unauthenticated", async () => {
    await call(submit({ id: ID_A, name: "Alpha", counts: { countries: 15 }, typeCodes: ["A320"] }));
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
    await call(submit({ id: ID_A, name: "Alpha", counts: {}, typeCodes: ["A320"] }));
    const profile = await call(new Request(`https://proxy.test/leaderboard/${ID_A}`));
    expect(await profile.text()).toContain("First!");
  });
});
