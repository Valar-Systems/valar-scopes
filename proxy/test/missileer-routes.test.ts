import { describe, expect, it, vi } from "vitest";
import { KNOWN_ROUTES, routeTemplate } from "../src/metrics";
import { isMissileerPath } from "../src/missileer";
import { call as workerCall } from "./helpers";

/**
 * Missileer route metrics — the allow-list, pinned against the game service's
 * route table.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CATCHES, AND WHAT IT CANNOT
 *
 * The routes are registered in ANOTHER REPO (valar-eam-feed,
 * src/routes/game.ts) and nothing here imports them, so `SERVICE_ROUTES` below
 * is a transcription. That is a real limitation and worth stating rather than
 * implying otherwise:
 *
 *   CATCHES  a path in the manifest with no allow-list entry (it would bucket
 *            to "/other" and go dark in analytics);
 *   CATCHES  an allow-list entry with no manifest entry (a route that was
 *            removed, renamed, or invented here by mistake);
 *   CATCHES  an id-bearing path that was listed literally instead of templated,
 *            which is the cardinality bug wearing a passing test;
 *   CANNOT   catch a route added to the SERVICE and to neither list. No test in
 *            this repo can — the data does not exist here.
 *
 * The mitigation for the last one is procedural and named in
 * valar-eam-feed/docs/missileer-routing.md: adding a game endpoint means adding
 * it here. This file exists so the OTHER three failures are loud, because Phase
 * 1 shipped twelve endpoints with one of them listed and nothing said a word.
 * ---------------------------------------------------------------------------
 */

interface ServiceRoute {
  /** As registered in valar-eam-feed's src/routes/game.ts (prefix /api/v1/missileer). */
  readonly path: string;
  /** A concrete URL a real caller would produce. */
  readonly example: string;
  /** The bucket it must collapse to. Differs from `path` only for id-bearing shapes. */
  readonly template: string;
}

/**
 * Transcribed from valar-eam-feed src/routes/game.ts (17 registrations) plus
 * the five page surfaces reserved in docs/missileer-routing.md.
 *
 * `/ingest/*` is deliberately ABSENT: the Pi posts transcripts and clips
 * straight to Render, not through this Worker, so those paths never reach
 * isMissileerPath() and listing them here would assert something false.
 */
const SERVICE_ROUTES: readonly ServiceRoute[] = [
  // -- pages (server-rendered by valar-eam-feed) --------------------------
  { path: "/missileer", example: "/missileer", template: "/missileer" },
  { path: "/missileer/log", example: "/missileer/log", template: "/missileer/log" },
  { path: "/missileer/leaderboard", example: "/missileer/leaderboard", template: "/missileer/leaderboard" },
  { path: "/missileer/archive", example: "/missileer/archive", template: "/missileer/archive" },
  { path: "/missileer/sources", example: "/missileer/sources", template: "/missileer/sources" },

  // -- config + health ----------------------------------------------------
  { path: "/config", example: "/api/v1/missileer/config", template: "/api/v1/missileer/config" },
  { path: "/status", example: "/api/v1/missileer/status", template: "/api/v1/missileer/status" },

  // -- placement (§8/§9) --------------------------------------------------
  {
    path: "/placement/wings",
    example: "/api/v1/missileer/placement/wings",
    template: "/api/v1/missileer/placement/wings",
  },
  {
    path: "/placement/wings/:wingId/capsules",
    example: "/api/v1/missileer/placement/wings/2/capsules",
    template: "/api/v1/missileer/placement/wings/:id/capsules",
  },
  { path: "/seats/claim", example: "/api/v1/missileer/seats/claim", template: "/api/v1/missileer/seats/claim" },
  { path: "/seats", example: "/api/v1/missileer/seats", template: "/api/v1/missileer/seats" },

  // -- identity -----------------------------------------------------------
  { path: "/me", example: "/api/v1/missileer/me", template: "/api/v1/missileer/me" },
  { path: "/me/callsign", example: "/api/v1/missileer/me/callsign", template: "/api/v1/missileer/me/callsign" },
  { path: "/me/grid", example: "/api/v1/missileer/me/grid", template: "/api/v1/missileer/me/grid" },

  // -- votes (§3 steps 1, 4, 5) -------------------------------------------
  { path: "/votes", example: "/api/v1/missileer/votes", template: "/api/v1/missileer/votes" },
  { path: "/votes/live", example: "/api/v1/missileer/votes/live", template: "/api/v1/missileer/votes/live" },
  {
    path: "/votes/:voteId/execute",
    example: "/api/v1/missileer/votes/91827/execute",
    template: "/api/v1/missileer/votes/:id/:action",
  },
  {
    path: "/votes/:voteId/second",
    example: "/api/v1/missileer/votes/91827/second",
    template: "/api/v1/missileer/votes/:id/:action",
  },
  {
    path: "/votes/:voteId/inhibit",
    example: "/api/v1/missileer/votes/91827/inhibit",
    template: "/api/v1/missileer/votes/:id/:action",
  },
  {
    path: "/votes/:voteId/abort",
    example: "/api/v1/missileer/votes/91827/abort",
    template: "/api/v1/missileer/votes/:id/:action",
  },
  {
    path: "/votes/:voteId/preempt",
    example: "/api/v1/missileer/votes/91827/preempt",
    template: "/api/v1/missileer/votes/:id/:action",
  },

  // -- SSE ----------------------------------------------------------------
  { path: "/events", example: "/api/v1/missileer/events", template: "/api/v1/missileer/events" },
];

const isMissileerTemplate = (t: string): boolean =>
  t === "/missileer" || t.startsWith("/missileer/") || t.startsWith("/api/v1/missileer/");

describe("Missileer route allow-list", () => {
  it("gives every route the service registers a bucket that is not /other", () => {
    const dark = SERVICE_ROUTES.filter((r) => routeTemplate(r.example) === "/other");
    // Named in the failure, because "expected 0 to be 1" would not tell anyone
    // which endpoint went dark.
    expect(dark.map((r) => r.example)).toEqual([]);
  });

  it("maps each route to exactly the bucket it should have", () => {
    for (const r of SERVICE_ROUTES) {
      expect(routeTemplate(r.example), `${r.path} -> wrong bucket`).toBe(r.template);
    }
  });

  it("has no allow-list entry the service does not register", () => {
    // The other direction. A stale entry is not harmless: it is a bucket that
    // will read zero forever, which looks exactly like a healthy endpoint
    // nobody is using.
    const expected = new Set(SERVICE_ROUTES.map((r) => r.template).filter((t) => !t.includes(":")));
    const listed = [...KNOWN_ROUTES].filter(isMissileerTemplate);
    expect(listed.slice().sort()).toEqual([...expected].sort());
  });

  it("routes every one of its examples through the Missileer branch", () => {
    // If a path is not recognised as Missileer it never reaches the proxy at
    // all, so a correct metrics bucket for it would be describing traffic that
    // cannot happen.
    for (const r of SERVICE_ROUTES) {
      expect(isMissileerPath(r.example), `${r.example} not recognised as Missileer`).toBe(true);
    }
  });
});

describe("cardinality", () => {
  it("collapses vote ids instead of minting an index value per vote", () => {
    // Vote ids are a bigserial: unbounded, not merely large. This is the same
    // failure /v1/enrich/<hex> was collapsed to fix.
    const a = routeTemplate("/api/v1/missileer/votes/1/second");
    const b = routeTemplate("/api/v1/missileer/votes/99999999999/second");
    expect(a).toBe(b);
    expect(a).toBe("/api/v1/missileer/votes/:id/:action");
  });

  it("collapses wing ids", () => {
    expect(routeTemplate("/api/v1/missileer/placement/wings/1/capsules")).toBe(
      routeTemplate("/api/v1/missileer/placement/wings/3/capsules"),
    );
  });

  it("keeps /votes/live out of the per-vote template", () => {
    // The trap: a looser /votes/:id pattern would swallow this and it would
    // still look right. The squadron board is a literal route and has to keep
    // its own bucket.
    expect(routeTemplate("/api/v1/missileer/votes/live")).toBe("/api/v1/missileer/votes/live");
  });

  it("sends an unknown vote action to /other rather than into a real bucket", () => {
    // Actions are enumerated, not wildcarded, so a probe or a typo cannot land
    // in a bucket that reads as real traffic.
    expect(routeTemplate("/api/v1/missileer/votes/12/detonate")).toBe("/other");
    expect(routeTemplate("/api/v1/missileer/votes/abc/second")).toBe("/other");
  });

  it("does not let a Missileer path mint an index value by 404-sweeping", () => {
    expect(routeTemplate("/api/v1/missileer/../../etc/passwd")).toBe("/other");
    expect(routeTemplate("/missileer/nope")).toBe("/other");
  });
});

describe("MISSILEER_ORIGIN unset", () => {
  // The shared helper, so this exercises the real worker entry point with a
  // real ExecutionContext. MISSILEER_ORIGIN is not set on the test env, which
  // is precisely the state this block is about -- and note the request carries
  // no device key: the Missileer branch is reached BEFORE Blipscope's auth gate
  // (missileer.ts rule 1), and a 401 here would mean that ordering had broken.
  const call = (path: string) =>
    workerCall(new Request(`https://scopes.valarsystems.com${path}`));

  /**
   * ONE test, not two, and the latch is why.
   *
   * The warning fires once per isolate. Split across two tests, whichever ran
   * first would consume the latch and the second would assert `<= 1` against a
   * guaranteed 0 — passing without evidence that the warning fires at all, and
   * silently depending on test order. Doing every assertion inside one capture
   * makes "fires" and "fires only once" the same observation.
   *
   * This must also be the first request in the file: the describes above call
   * routeTemplate/isMissileerPath directly and never reach the handler, so the
   * latch is untouched when this runs.
   */
  it("warns exactly once per isolate and returns a 503 that says why", async () => {
    const spy = vi.spyOn(console, "warn").mockImplementation(() => {});
    try {
      const res = await call("/api/v1/missileer/config");
      await call("/missileer/log");
      await call("/api/v1/missileer/status");

      expect(res.status).toBe(503);
      const body = (await res.json()) as { error: string; message?: string };
      expect(body.error).toBe("missileer_unconfigured");
      // During bring-up the person hitting this is usually not the person with
      // log access, so the reason has to be in the response as well as the log.
      expect(body.message).toMatch(/MISSILEER_ORIGIN/);

      const warnings = spy.mock.calls
        .map((c) => String(c[0]))
        .filter((s) => s.includes("missileer_unconfigured"));
      // Exactly one across three requests: it fires, and a scanner hammering
      // /missileer/* cannot turn a config warning into a log flood. Request
      // logs are real money at fleet scale (see the README cost model).
      expect(warnings).toHaveLength(1);
      expect(warnings[0]).toMatch(/wrangler\.toml/);
    } finally {
      spy.mockRestore();
    }
  });
});
