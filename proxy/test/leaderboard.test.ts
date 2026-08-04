import { env } from "cloudflare:test";
import { afterEach, describe, expect, it, vi } from "vitest";
import { rarityMultiplier } from "../src/leaderboard";
import { call, TEST_KEY } from "./helpers";

// Capture the structured console lines the submit path emits, so a diagnostic
// that is supposed to fire can be shown firing rather than assumed to.
async function captureLogs<T>(fn: () => Promise<T>): Promise<{ result: T; events: any[] }> {
  const spy = vi.spyOn(console, "log").mockImplementation(() => {});
  try {
    const result = await fn();
    const events = spy.mock.calls
      .map((c) => c[0])
      .filter((a): a is string => typeof a === "string")
      .map((s) => { try { return JSON.parse(s); } catch { return null; } })
      .filter((o): o is any => o !== null);
    return { result, events };
  } finally {
    spy.mockRestore();
  }
}

// Edition-namespaced paths (docs/web-url-convention.md). The deprecated
// unprefixed ones still work -- pages by 301, APIs by internal alias -- and that
// is asserted as its own contract in "legacy URL compatibility" below rather
// than by leaving the rest of the suite pointed at the old paths, which would
// have tested the aliases everywhere and the real paths nowhere.
const API = "/api/v1/blipscope";
const PAGE = "/blipscope";

// A leaderboard submit: authed POST with a JSON body.
function submit(body: unknown, headers: Record<string, string> = {}, path = `${API}/leaderboard`): Request {
  return new Request(`https://proxy.test${path}`, {
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
    const noKey = new Request(`https://proxy.test${API}/leaderboard`, {
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
  it("serves both scopes in one response, shaped for the static page", async () => {
    await call(submit({
      id: ID_A, name: "Alpha",
      counts: { types: 156, airlines: 120, countries: 8, airports: 183 },
      claimed: { types: 1, airlines: 9, countries: 4, airports: 5 },
      claimedTypes: ["A320"],
    }));
    const res = await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`));
    expect(res.status).toBe(200);
    // A browser page, not a device: this route overrides the shared no-store.
    expect(res.headers.get("Cache-Control")).toContain("max-age=60");
    const body = (await res.json()) as any;

    // The deploy runbook greps this instead of a sentence from the page, so the
    // copy can be rewritten without disarming the gate before the KV reset.
    expect(body.scoring).toBe("claims-v2");

    // Both scopes present without a second request; season carries its own id.
    expect(body.lifetime.rows[0].name).toBe("Alpha");
    expect(body.season.rows[0].name).toBe("Alpha");
    expect(body.season.id).toMatch(/^\d{4}-\d{2}$/);

    // The page reads row.claimed; row.seen is the denominator behind "1 of 156".
    expect(body.lifetime.rows[0].claimed.airlines).toBe(9);
    expect(body.lifetime.rows[0].seen.types).toBe(156);

    // Leaders carry the number that earned them, not just a name.
    expect(body.lifetime.leaders.airlines).toEqual({ name: "Alpha", count: 9 });
  });

  it("ranks the season scope by season points, not lifetime points", async () => {
    // Both submit the same claims, so lifetime is a tie broken by insertion; the
    // point is that each scope reports ITS OWN points under the same key, which
    // is what lets the page render a row without knowing which tab it is on.
    await call(submit({ id: ID_A, name: "Alpha", claimed: { airlines: 20 }, claimedTypes: ["A320", "B738"] }));
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    const life = body.lifetime.rows.find((r: any) => r.name === "Alpha");
    const seas = body.season.rows.find((r: any) => r.name === "Alpha");

    // Lifetime: 2 claimed types (x1 rarity in a one-device fleet) + 20 airlines.
    expect(life.points).toBe(2 * 10 + 20 * 5);

    // Season is NOT the same number, and the difference is deliberate. A device's
    // count-based categories are measured as growth since its FIRST submission
    // (monthStartCounts is seeded from it), so a scope that joins the board
    // already holding 20 claimed airlines does not get to bank all 20 as this
    // month's progress. Types are different: each carries the month it was
    // claimed, so genuinely-new claims do count immediately.
    expect(seas.points).toBe(2 * 10);
  });

  it("serves the board page as static markup that fetches its own data", async () => {
    await call(submit({ id: ID_A, name: "Alpha", claimed: { countries: 15 }, claimedTypes: ["A320"] }));
    const page = await call(new Request(`https://proxy.test${PAGE}/leaderboard`));
    expect(page.status).toBe(200);
    expect(page.headers.get("Content-Type")).toContain("text/html");
    const html = await page.text();

    // Static: no spotter name is templated in. The page fetches the JSON itself,
    // which is what lets the markup be iterated on without touching scoring.
    expect(html).not.toContain("Alpha");
    expect(html).toContain("fetch('/blipscope/leaderboard.json')");
    expect(html).toContain("Spotting Leaderboard");
  });

  it("still renders a device profile server-side", async () => {
    await call(submit({ id: ID_A, name: "Alpha", claimed: { countries: 15 }, claimedTypes: ["A320"] }));
    const profile = await call(new Request(`https://proxy.test${PAGE}/leaderboard/${ID_A}`));
    expect(profile.status).toBe(200);
    const html = await profile.text();
    expect(html).toContain("Alpha");
    expect(html).toContain("Globetrotter"); // 15 claimed countries earns the badge
  });

  it("404s an unknown profile id and 400s a malformed one", async () => {
    expect((await call(new Request(`https://proxy.test${PAGE}/leaderboard/deadbeef`))).status).toBe(404);
    expect((await call(new Request(`https://proxy.test${PAGE}/leaderboard/xyz`))).status).toBe(404); // no route match -> not_found
  });

  it("awards the First! badge to the earliest logger of a type", async () => {
    await call(submit({ id: ID_A, name: "Alpha", claimed: {}, claimedTypes: ["A320"] }));
    const profile = await call(new Request(`https://proxy.test${PAGE}/leaderboard/${ID_A}`));
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
    const board = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    expect(board.lifetime.rows).toHaveLength(0);
    expect(board.season.rows).toHaveLength(0);
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
    const html = await (await call(new Request(`https://proxy.test${PAGE}/leaderboard/${ID_A}`))).text();
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

describe("legacy URL compatibility (docs/web-url-convention.md)", () => {
  // PAGES REDIRECT. Browsers follow 301s and a permanent redirect is what gets a
  // bookmark rewritten. Every page path already in someone's history, and on the
  // config page of every device that has not taken an OTA, arrives here.
  it("301s every deprecated page path to its namespaced equivalent", async () => {
    const moves: Array<[string, string]> = [
      ["/leaderboard", "/blipscope/leaderboard"],
      ["/leaderboard.json", "/blipscope/leaderboard.json"],
      [`/leaderboard/${ID_A}`, `/blipscope/leaderboard/${ID_A}`],
    ];
    for (const [from, to] of moves) {
      const res = await call(new Request(`https://proxy.test${from}`, { redirect: "manual" }));
      expect(res.status, `${from} should 301`).toBe(301);
      expect(new URL(res.headers.get("Location") as string).pathname, `${from} target`).toBe(to);
    }
  });

  it("preserves the query string through a page redirect", async () => {
    // A dropped query turns a redirect into a subtly different request, and the
    // page would render without noticing.
    const res = await call(new Request("https://proxy.test/leaderboard.json?scope=season", { redirect: "manual" }));
    const loc = new URL(res.headers.get("Location") as string);
    expect(loc.pathname).toBe("/blipscope/leaderboard.json");
    expect(loc.search).toBe("?scope=season");
  });

  // APIS MUST NOT REDIRECT. Deployed ESP32 firmware is not guaranteed to follow a
  // 301, and on the submit POST following one would mean re-sending the body. A
  // redirect here breaks the whole fleet on deploy and looks like an outage.
  it("NEVER redirects an API path -- the aliases are internal", async () => {
    const legacy = ["/v1/blips", "/v1/config", "/v1/airports", "/v1/enrich/abc123", "/v1/photo/whatever"];
    for (const p of legacy) {
      const res = await call(new Request(`https://proxy.test${p}`, {
        headers: { "X-Blip-Key": TEST_KEY },
        redirect: "manual",
      }));
      expect([301, 302, 307, 308], `${p} must not redirect`).not.toContain(res.status);
    }
  });

  it("serves the leaderboard POST identically on both path families", async () => {
    const payload = { id: ID_A, name: "Twin", claimed: { airlines: 3, countries: 2, airports: 4 }, claimedTypes: ["A320", "B738"] };
    const viaNew = await call(submit(payload));
    const viaOld = await call(submit(payload, {}, "/v1/leaderboard"));
    expect(viaOld.status).toBe(viaNew.status);
    // Same handler, so the second submit is simply idempotent against the first:
    // identical stored row, identical standing back.
    expect(await viaOld.json()).toEqual(await viaNew.json());
  });

  it("rejects an unauthed legacy API call exactly as the namespaced one does", async () => {
    // The alias must not slip past the auth gate. A prefix that fails to reach
    // that check is not a 404, it is an endpoint without authentication.
    for (const p of [`${API}/blips`, "/v1/blips"]) {
      const res = await call(new Request(`https://proxy.test${p}`));
      expect(res.status, `${p} unauthed`).toBe(401);
    }
  });

  it("still 405s a POST to a non-submit endpoint on both families", async () => {
    for (const p of [`${API}/blips`, "/v1/blips"]) {
      const res = await call(new Request(`https://proxy.test${p}`, {
        method: "POST",
        headers: { "X-Blip-Key": TEST_KEY },
        body: "{}",
      }));
      expect(res.status, `${p} POST`).toBe(405);
    }
  });

  it("404s an unknown endpoint on both families, and anything outside them", async () => {
    for (const p of [`${API}/nope`, "/v1/nope"]) {
      const res = await call(new Request(`https://proxy.test${p}`, { headers: { "X-Blip-Key": TEST_KEY } }));
      expect(res.status, p).toBe(404);
    }
    // Not an API prefix at all -> 404 before auth, and NOT routed into a handler.
    const stray = await call(new Request("https://proxy.test/api/v1/missileer/blips", { headers: { "X-Blip-Key": TEST_KEY } }));
    expect(stray.status).toBe(404);
  });

  it("emits the namespaced photo path in the enrich response", async () => {
    // Server-supplied and treated as opaque by firmware, so this string alone
    // moves the fleet's photo fetches -- it is the one path with no OTA gate.
    const { leaderboardHtml } = await import("../src/pages.generated");
    expect(leaderboardHtml).toContain("/blipscope/leaderboard.json");
    expect(leaderboardHtml).not.toContain("fetch('/leaderboard.json')");
  });
});

describe("clamp logging makes silent drift self-announcing", () => {
  it("says nothing when nothing is clamped", async () => {
    // The signal is the absence of noise, so the quiet case is the one that
    // matters most: if a healthy submit logged a clamp, the log would be
    // worthless the moment a real one appeared.
    const { events } = await captureLogs(() =>
      call(submit({ id: ID_A, name: "Quiet", claimed: { types: 2, airlines: 1, countries: 1, airports: 2 }, claimedTypes: ["A320", "B738"] })),
    );
    expect(events.filter((e) => e.evt === "lb_clamp")).toHaveLength(0);
  });

  it("logs a floor clamp with the stored and submitted counts", async () => {
    await call(submit({ id: ID_A, name: "Drifty", claimed: { types: 2, airlines: 6, countries: 0, airports: 0 }, claimedTypes: ["A320", "B738"] }));
    // A later submit carrying FEWER airlines: monotonicity holds the stored
    // total at 6, which is exactly the shape the v1 contamination had.
    const { events } = await captureLogs(() =>
      call(submit({ id: ID_A, name: "Drifty", claimed: { types: 2, airlines: 4, countries: 0, airports: 0 }, claimedTypes: ["A320", "B738"] })),
    );
    const clamp = events.find((e) => e.evt === "lb_clamp");
    expect(clamp).toBeDefined();
    expect(clamp.id).toBe(ID_A);
    expect(clamp.clamps).toContainEqual({ field: "airlines", stored: 6, submitted: 4, kind: "floor" });
  });

  it("logs the types map outgrowing what the device claims -- the v1 signature", async () => {
    // Seed a row holding three claimed types, then submit only two. The merged
    // claim map keeps all three, so stored (3) exceeds submitted (2): precisely
    // the 57-vs-53 contamination, now announced instead of silent.
    await call(submit({ id: ID_A, name: "Excess", claimed: { types: 3, airlines: 0, countries: 0, airports: 0 }, claimedTypes: ["A320", "B738", "C17"] }));
    const { events } = await captureLogs(() =>
      call(submit({ id: ID_A, name: "Excess", claimed: { types: 2, airlines: 0, countries: 0, airports: 0 }, claimedTypes: ["A320", "B738"] })),
    );
    const clamp = events.find((e) => e.evt === "lb_clamp");
    expect(clamp).toBeDefined();
    expect(clamp.clamps).toContainEqual({ field: "types", stored: 3, submitted: 2, kind: "map-excess" });
  });

  it("logs a cap clamp distinctly from a floor clamp", async () => {
    // Countries are rate-limited to +10/day after the first submit, so a jump
    // from 1 to 40 stores 11. That is the anti-spoof working, NOT drift, and it
    // must not read the same in the log.
    await call(submit({ id: ID_A, name: "Fast", claimed: { types: 1, airlines: 0, countries: 1, airports: 0 }, claimedTypes: ["A320"] }));
    const { events } = await captureLogs(() =>
      call(submit({ id: ID_A, name: "Fast", claimed: { types: 1, airlines: 0, countries: 40, airports: 0 }, claimedTypes: ["A320"] })),
    );
    const clamp = events.find((e) => e.evt === "lb_clamp");
    expect(clamp).toBeDefined();
    expect(clamp.clamps).toContainEqual({ field: "countries", stored: 11, submitted: 40, kind: "cap" });
  });

  it("does not log a clamp for a legacy submission's expected zero claims", async () => {
    // Legacy firmware sends no claimedTypes, so the stored claim map is empty
    // while the submitted count is whatever its SEEN tally was. That gap is by
    // design; logging it would bury real drift under every un-updated device.
    const { events } = await captureLogs(() =>
      call(submit({ id: ID_B, name: "Old", counts: { types: 90, airlines: 40, countries: 12, airports: 80 }, typeCodes: ["A320"] })),
    );
    expect(events.filter((e) => e.evt === "lb_clamp" && e.clamps.some((c: any) => c.field === "types"))).toHaveLength(0);
  });
});

describe("the deploy gate", () => {
  // The runbook's step 2 proves the v2 Worker is bound BEFORE step 3 runs an
  // irreversible KV reset against it. It used to grep a sentence from the board
  // page; the page is now a file the author edits freely, so the gate moved to a
  // machine-readable marker. This test is what stops that marker being renamed
  // without anyone noticing the deploy check had silently stopped checking.
  it("exposes the scoring marker the deploy runbook greps for", async () => {
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    expect(body.scoring).toBe("claims-v2");
  });

  it("keeps the marker present on an empty board", async () => {
    // The reset empties every row, and step 2 may well run against a board with
    // nothing on it. The marker must not depend on there being data.
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    expect(body.lifetime.rows).toHaveLength(0);
    expect(body.scoring).toBe("claims-v2");
  });
});

describe("season category leaders", () => {
  // Regression: the season leader for TYPES was computed as a count delta, which
  // is always 0 for a device on its first submission (monthStartCounts is seeded
  // from it). Every season leader came back with an empty name, and the page
  // hides the whole "category leaders" block when no leader has a name -- so the
  // season tab quietly lost a section. Types are counted from their claim month,
  // which is how season POINTS already worked.
  it("names a types leader on the season tab from a first submission", async () => {
    await call(submit({
      id: ID_A, name: "Redmond Radar",
      claimed: { types: 4, airlines: 30, countries: 5, airports: 12 },
      claimedTypes: ["A320", "B738", "C17", "B77W"],
    }));
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    expect(body.season.leaders.types).toEqual({ name: "Redmond Radar", count: 4 });
    expect(body.lifetime.leaders.types).toEqual({ name: "Redmond Radar", count: 4 });
  });

  it("leaves count-only categories empty until they actually grow", async () => {
    // Airlines/countries/airports arrive as tallies with no per-item date, so a
    // first submission has no measurable season growth. Empty is the honest
    // answer -- crediting the opening balance would let a device that joins
    // mid-season with a full book top the season board on day one.
    await call(submit({
      id: ID_A, name: "Redmond Radar",
      claimed: { types: 4, airlines: 30, countries: 5, airports: 12 },
      claimedTypes: ["A320", "B738", "C17", "B77W"],
    }));
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    expect(body.season.leaders.airlines).toEqual({ name: "", count: 0 });
  });
});

describe("self-hosted fonts", () => {
  // The board page used to pull three families from Google, which sent every
  // visitor's IP to a third party. These are served from the Worker instead.
  it("serves each font immutably with the right type", async () => {
    for (const name of ["inter.woff2", "mono.woff2", "grotesk.woff2"]) {
      const res = await call(new Request(`https://proxy.test/fonts/${name}`));
      expect(res.status, name).toBe(200);
      expect(res.headers.get("Content-Type")).toBe("font/woff2");
      expect(res.headers.get("Cache-Control")).toContain("immutable");
      const buf = await res.arrayBuffer();
      expect(buf.byteLength, name).toBeGreaterThan(10_000);
      // woff2 magic: "wOF2". A truncated or base64-mangled embed would still
      // return 200 with the wrong bytes, and the browser's failure is silent.
      expect(new TextDecoder().decode(new Uint8Array(buf, 0, 4))).toBe("wOF2");
    }
  });

  it("serves every font the page actually asks for", async () => {
    // The CI staleness check proves fonts/ and the generated module agree; it
    // says nothing about whether the PAGE requests names that exist. Renaming a
    // font (which the immutable cache header REQUIRES on any change) would
    // otherwise 404 silently and drop every visitor to system type -- a failure
    // that looks like a styling opinion rather than a bug.
    const html = await (await call(new Request(`https://proxy.test${PAGE}/leaderboard`))).text();
    const wanted = [...html.matchAll(/url\(\/fonts\/([^)]+)\)/g)].map((m) => m[1] as string);
    expect(wanted.length).toBe(3);
    for (const name of wanted) {
      expect((await call(new Request(`https://proxy.test/fonts/${name}`))).status, name).toBe(200);
    }
  });

  it("404s an unknown font without touching the filesystem", async () => {
    // Exact-name map lookup, so traversal has nothing to traverse.
    for (const p of ["nope.woff2", "../src/index.ts", "..%2F..%2Fetc%2Fpasswd"]) {
      const res = await call(new Request(`https://proxy.test/fonts/${p}`));
      expect(res.status, p).toBe(404);
    }
  });
});

describe("v1 rows cannot produce a self-contradicting profile", () => {
  // The exact row a real device had in KV: written by the v1 Worker, so `types`
  // is a SEEN map, there is no `claimed` object, and there is no `legacy` field
  // either -- which is why `!r.legacy` let it rank.
  function writeV1Row(id: string, name: string) {
    return env.ENRICH_KV.put(`lb:dev:${id}`, JSON.stringify({
      id, name, model: "s3-128", verified: true, radiusKm: 16,
      counts: { types: 12, airlines: 120, countries: 0, airports: 186 },
      // no `claimed`, no `legacy` -- the v1 shape exactly
      types: { B39M: "2026-07", A320: "2026-07", B738: "2026-07" },
      seasonMonth: "2026-08",
      monthStartCounts: { types: 0, airlines: 0, countries: 0, airports: 0 },
      streakDays: 0, streakLastDay: "", createdAt: 1, updatedAt: 2,
    }));
  }

  it("does not rank a v1 row, so no score appears beside zero claims", async () => {
    await writeV1Row(ID_A, "Bend-Man");
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    // Previously this row ranked #1 with points from its SEEN types and a
    // rarestType, while every claimed counter read 0.
    expect(body.lifetime.rows).toHaveLength(0);
    expect((await call(new Request(`https://proxy.test${PAGE}/leaderboard/${ID_A}`))).status).toBe(404);
  });

  it("re-admits the device the moment its firmware submits once", async () => {
    await writeV1Row(ID_A, "Bend-Man");
    await call(submit({
      id: ID_A, name: "Bend-Man",
      counts: { types: 153, airlines: 120, countries: 0, airports: 186 },
      claimed: { types: 15, airlines: 9, countries: 0, airports: 4 },
      claimedTypes: ["B39M", "A320", "B738"],
    }));
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    const row = body.lifetime.rows[0];
    expect(row.name).toBe("Bend-Man");
    // Counters are non-zero AND the score is consistent with them.
    expect(row.claimed.types).toBe(3); // authoritative from the claim map, not the device's own number
    expect(row.points).toBeGreaterThan(0);
    expect(row.seen.types).toBe(153); // absolute lifetime count, straight from the device
  });

  it("never publishes a score or a rarest catch beside zero claimed types", async () => {
    // The property itself, over every row the endpoint will serve -- not just
    // the one shape known to break it.
    await writeV1Row(ID_A, "Bend-Man");
    await call(submit({ id: ID_B, name: "Real", claimed: { airlines: 2 }, claimedTypes: ["C17"] }));
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    for (const scope of ["lifetime", "season"]) {
      for (const r of body[scope].rows) {
        const c = r.claimed;
        const claims = c.types + c.airlines + c.countries + c.airports;
        // A score has to be explainable by something claimed...
        if (claims === 0) expect(r.points, `${scope} ${r.name} points`).toBe(0);
        // ...and a "rarest catch" needs claimed types to have been rarest among.
        if (c.types === 0) expect(r.rarestType, `${scope} ${r.name} rarest`).toBe("");
      }
    }
  });
});

describe("profile links", () => {
  // The page builds row links as /leaderboard/<id>. Before this, `id` was not in
  // the JSON at all -- the old server-rendered board built links from a value the
  // endpoint never exposed, so a client-side page had no key to use.
  it("emits the id that the profile route actually accepts", async () => {
    await call(submit({ id: ID_A, name: "Alpha", claimed: { countries: 1 }, claimedTypes: ["A320"] }));
    const body = (await (await call(new Request(`https://proxy.test${PAGE}/leaderboard.json`))).json()) as any;
    const id = body.lifetime.rows[0].id;
    expect(id).toBe(ID_A);
    expect(id).toMatch(/^[0-9a-f]{8,32}$/); // the shape the route's regex requires
    const profile = await call(new Request(`https://proxy.test${PAGE}/leaderboard/${id}`));
    expect(profile.status).toBe(200);
    expect(await profile.text()).toContain("Alpha");
  });
});
