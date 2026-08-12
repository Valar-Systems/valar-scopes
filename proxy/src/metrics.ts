import type { Env } from "./types";

export interface RequestMetric {
  ep: string;
  status: number;
  ms: number;
  model: string;
  colo: string;
  cache?: string; // HIT | STALE | MISS (blips), HIT | MISS (enrich KV)
  upstream?: string;
  upstreamMs?: number;
  // The authenticated device id, or "" -- see setDeviceAttribution() for why
  // this is only ever populated on the device-key path.
  dev?: string;
  fw?: string; // X-Blip-FW, digits only, same authentication rule as `dev`
  /**
   * Which credential authenticated this request: "device" (per-device HMAC key)
   * or "shared" (the BLIP_KEYS list). Undefined when no auth ran.
   *
   * Emitted as the X-Blip-Auth response header rather than only stored, because
   * the question it answers — "is this board on its own key yet?" — has to be
   * answerable at the bench while BOTH credentials still work. It is not written
   * to the dataset: `dev` already carries the same distinction there (it is
   * populated only on the device path), and adding a blob would shift the
   * positional indices existing queries depend on.
   */
  authPath?: "device" | "shared";
}

// Device attribution for a metric, applied ONLY once a request has proven it
// holds that device's key.
//
// WHY THE RULE IS "AUTHENTICATED OR NOTHING". X-Blip-Device and X-Blip-FW are
// device-supplied strings on an unauthenticated edge. If we recorded them before
// the key check, anyone could write arbitrary device ids into the dataset --
// which would not break serving, but would quietly make the fleet view a
// fiction, and a fleet view you cannot trust is worse than none. On the shared
// BLIP_KEYS path the caller has not proven WHICH device it is, so `dev` stays
// empty there too; those requests aggregate as "unattributed" rather than being
// guessed at.
//
// Shapes are re-checked here rather than assumed from deviceauth.ts, because
// this is the boundary where the value stops being a credential and becomes
// stored data.
export function setDeviceAttribution(
  m: RequestMetric,
  request: Request,
  deviceAuthed: boolean,
): void {
  if (!deviceAuthed) return;
  const id = (request.headers.get("X-Blip-Device") ?? "").trim().toLowerCase();
  if (/^[0-9a-f]{8,32}$/.test(id)) m.dev = id;
  const fw = (request.headers.get("X-Blip-FW") ?? "").trim();
  if (/^\d{1,6}$/.test(fw)) m.fw = fw;
}

// The endpoint as a bounded ROUTE, for the Analytics Engine blob and index.
//
// WHY. `ep` is the raw pathname, so /v1/enrich/<hex> and /v1/photo/<key> made
// index1 effectively unbounded -- one index value per airframe, plus one per
// path any internet scanner ever probed. That has two costs: Analytics Engine
// samples per index bucket, so unbounded cardinality degrades the aggregates it
// is meant to accelerate; and GROUP BY endpoint could never answer "how many
// enrichments did this device do?" because every row was its own group. This
// collapses it to ~12 values.
//
// The raw path is unaffected in the request LOG line, which is where per-request
// debugging actually happens. This changes what blob1/index1 mean from the
// deploy onward -- retained points from before keep the old shape, so a query
// spanning the boundary sees both. Filtering on the exact string ('/v1/blips')
// still behaves identically, because that route was never templated.
// Namespaced and legacy paths are DELIBERATELY SEPARATE entries, never merged
// into one template. Merging them would lose the only number that says when the
// aliases can go: legacy hit-rate is the deprecation instrument. When /v1/*
// counts sit at zero across a full fleet-update cycle, every device has taken
// the OTA and the alias layer can be deleted -- and until then, the count is
// also how you find the units that have not updated.
// Exported for test/missileer-routes.test.ts, which pins this list against the
// game service's route table in the other repo. Nothing at runtime reads it
// from outside this module.
export const KNOWN_ROUTES = new Set([
  // Cross-edition pages. "/" is the hub, so it belongs to no edition and is not
  // namespaced -- and it needs listing for the same reason as everything else:
  // unlisted paths bucket to "/other", and the root is where a Shopify buyer who
  // trims the path lands, which is traffic worth being able to see.
  "/",
  // Edition-namespaced (current)
  "/api/v1/blipscope/blips",
  "/api/v1/blipscope/config",
  "/api/v1/blipscope/airports",
  "/api/v1/blipscope/leaderboard",
  "/blipscope/leaderboard",
  "/blipscope/leaderboard.json",
  "/blipscope/support",
  // DEPRECATED aliases + redirects (retire when these read zero -- see above)
  "/v1/blips",
  "/v1/config",
  "/v1/airports",
  "/v1/leaderboard",
  "/leaderboard",
  "/leaderboard.json",
  // ---------------------------------------------------------------------
  // Missileer -- proxied to valar-eam-feed (see missileer.ts). Listed here for
  // the same reason as everything else: an unlisted path buckets to "/other"
  // and stops being distinguishable in analytics, so a Missileer outage would
  // be invisible in exactly the window we would be looking for it. No legacy
  // family, because this edition never had one.
  //
  // THIS LIST MIRRORS ANOTHER REPO'S ROUTE TABLE, which is a maintenance hazard
  // worth naming: the routes are registered in valar-eam-feed's
  // src/routes/game.ts, and nothing in THIS repo imports them. test/
  // missileer-routes.test.ts pins the two lists against each other so they
  // cannot drift silently -- see its header for what that can and cannot catch.
  //
  // Phase 1 added twelve endpoints and only /config was listed, so eleven paths
  // plus two id-bearing shapes were bucketing to "/other" -- i.e. the metrics
  // were blind across the whole game surface, during bring-up, which is the one
  // window they exist for.
  // ---------------------------------------------------------------------
  // Pages (server-rendered by valar-eam-feed; see its docs/missileer-routing.md)
  "/missileer",
  "/missileer/log",
  "/missileer/leaderboard",
  "/missileer/archive",
  "/missileer/sources",
  // API -- config and health
  "/api/v1/missileer/config",
  "/api/v1/missileer/status",
  // API -- placement (§8/§9). The wing-scoped capsule list is TEMPLATED below.
  "/api/v1/missileer/placement/wings",
  "/api/v1/missileer/seats/claim",
  "/api/v1/missileer/seats",
  // API -- identity
  "/api/v1/missileer/me",
  "/api/v1/missileer/me/callsign",
  "/api/v1/missileer/me/grid",
  // API -- votes. The per-vote actions are TEMPLATED below; /votes/live is a
  // literal path and must not be swallowed by that template.
  "/api/v1/missileer/votes",
  "/api/v1/missileer/votes/live",
  // API -- SSE
  "/api/v1/missileer/events",
  // Not edition-scoped: infrastructure, shared across editions
  "/healthz",
  "/credits",
]);

/**
 * Missileer's two id-bearing shapes.
 *
 * Templated for the same reason /v1/enrich/<hex> was: without this, ONE INDEX
 * VALUE PER CAPSULE AND PER VOTE. Analytics Engine samples per index bucket, so
 * unbounded cardinality degrades the aggregates the index exists to accelerate
 * -- and `GROUP BY endpoint` could never answer "how many votes were seconded
 * this week?" because every row would be its own group. Vote ids are a
 * bigserial, so this one grows without limit rather than merely being large.
 *
 * The ACTIONS ARE ENUMERATED rather than matched as a wildcard segment. A
 * wildcard would quietly absorb a typo'd or probed action into a real bucket;
 * enumerating means anything else falls through to "/other", which is the
 * correct home for a request that is not a route. Adding an action to the game
 * service means adding it here, and the drift test says so out loud.
 */
const MISSILEER_VOTE_ACTION = /^\/api\/v1\/missileer\/votes\/\d{1,19}\/(execute|second|inhibit|abort|preempt)$/;
const MISSILEER_WING_CAPSULES = /^\/api\/v1\/missileer\/placement\/wings\/\d{1,9}\/capsules$/;

export function routeTemplate(pathname: string): string {
  if (pathname.startsWith("/api/v1/blipscope/enrich/")) return "/api/v1/blipscope/enrich";
  if (pathname.startsWith("/api/v1/blipscope/photo/")) return "/api/v1/blipscope/photo";
  if (/^\/blipscope\/leaderboard\/[0-9a-f]{8,32}$/.test(pathname)) return "/blipscope/leaderboard/:id";
  if (pathname.startsWith("/v1/enrich/")) return "/v1/enrich";
  if (pathname.startsWith("/v1/photo/")) return "/v1/photo";
  if (/^\/leaderboard\/[0-9a-f]{8,32}$/.test(pathname)) return "/leaderboard/:id";
  // Checked BEFORE the exact-match set, like every other template. Order is not
  // load-bearing between these two and the literal Missileer paths -- both
  // regexes require a segment the literals do not have, so
  // /api/v1/missileer/votes/live cannot match MISSILEER_VOTE_ACTION (one
  // segment, not two) and /placement/wings cannot match the capsule shape.
  // Stated because "add /votes/:id" as a looser pattern later WOULD swallow
  // /votes/live, and it would look correct.
  if (MISSILEER_VOTE_ACTION.test(pathname)) return "/api/v1/missileer/votes/:id/:action";
  if (MISSILEER_WING_CAPSULES.test(pathname)) return "/api/v1/missileer/placement/wings/:id/capsules";
  // Anything unrecognised is one bucket. A 404 sweep must not be able to mint
  // index values, which is a cardinality attack on our own telemetry.
  return KNOWN_ROUTES.has(pathname) ? pathname : "/other";
}

// Cache hits dominate traffic and each Analytics Engine data point is billed:
// sample the boring class (blips HIT/STALE) 1:10 and carry the weight in a
// double so dashboards multiply it back. Misses, errors, and everything with an
// upstream call stay 1:1 -- those are the points worth full resolution.
const HIT_SAMPLE_RATE = 0.1;

// One structured log line per request, plus an Analytics Engine data point.
// Metrics must never fail a request.
export function record(env: Env, m: RequestMetric): void {
  console.log(JSON.stringify({ evt: "req", ...m }));
  try {
    const sampled = m.status < 400 && (m.cache === "HIT" || m.cache === "STALE");
    if (sampled && Math.random() >= HIT_SAMPLE_RATE) return;
    // blob5/blob6 (dev, fw) are APPENDED, never inserted: existing queries index
    // blobs positionally, so anything that shifts blob1..blob4 silently rewrites
    // the meaning of three months of retained points. Append-only is the rule for
    // this dataset -- same reasoning as the schema evolution rule in the README.
    const route = routeTemplate(m.ep);
    env.METRICS?.writeDataPoint({
      blobs: [route, m.cache ?? "", m.upstream ?? "", m.model, m.dev ?? "", m.fw ?? ""],
      doubles: [m.status, m.ms, m.upstreamMs ?? 0, sampled ? 1 / HIT_SAMPLE_RATE : 1],
      indexes: [route],
    });
  } catch {
    // never let telemetry break serving
  }
}

// Which part of an enrichment we could NOT resolve. Ordered by how fundamental
// the gap is -- a missing type makes the name and photo unanswerable, so only the
// root gap is reported per request.
//   "type"  -- no ICAO type resolved at all. Needs a per-hex fix (mil side table,
//              a photo override keyed on hex) or simply isn't in any upstream DB.
//   "name"  -- type resolved, no friendly name. A TYPE_NAMES / `tn:<CODE>` gap;
//              cheapest thing on this list to close, and closable without a deploy.
//   "photo" -- type + name resolved, no stock photo. A photo-library gap: run the
//              suggest/harvest/ingest pipeline for that code.
export type EnrichGap = "type" | "name" | "photo";

// Enrichment-gap telemetry. The point is to stop guessing what to add next: this
// makes the name/photo backlog fall out of real fleet demand, ranked by how many
// devices actually looked the thing up. See the README "Finding enrichment gaps"
// for the query that turns these points into a work list.
//
// Written 1:1 (no sampling): gaps should be the minority case now that the named
// type table is ~fully covered, and under-counting the tail is exactly the failure
// mode here -- the rare type nobody has a photo for is the one we want to see. If
// this ever becomes a volume problem, sample it the way record() samples cache
// hits (a rate plus a weight double) rather than dropping points silently.
//
// Deliberately also console.logged: request logs run at 100% during the pilot, so
// a gap is greppable in Workers Logs while debugging one specific device, not just
// aggregable days later.
export function recordEnrichGap(env: Env, gap: EnrichGap, type: string, hex: string): void {
  try {
    console.log(JSON.stringify({ evt: "enrich_gap", gap, t: type, hex }));
    env.METRICS?.writeDataPoint({
      blobs: ["enrich_gap", gap, type, hex],
      doubles: [1],
      indexes: ["enrich_gap"], // own index: gaps query separately from per-request points
    });
  } catch {
    // never let telemetry break serving
  }
}

// X-Blip-OTA-Mem: "<fwFrom>,<fwTo>,<preLargest>,<postLargest>,<result>", sent by
// a device on its first check-in after an update attempt (OtaUpdater.h's
// TakeOtaMemReport) and never again for that attempt.
//
// It answers the one question the bench gate could not: an OTA is easy to prove
// at a freshly-booted heap, but devices update from a *fragmented* one. Only the
// fleet reaches that state honestly, so the numbers ride a request the device was
// already making.
//
// Deliberately NOT console.logged: it lands as an Analytics Engine point only, so
// request logs stay exactly the size they were (they are real money at fleet
// scale -- see the README cost model). And it is device-supplied input, so it is
// validated to a fixed shape before it reaches storage; anything off-shape is
// dropped silently rather than allowed to shape a data point.
export function recordOtaMem(env: Env, raw: string | null, model: string, dev?: string): void {
  if (!raw) return;
  try {
    if (raw.length > 128) return; // nothing legitimate approaches this
    const parts = raw.split(",");
    if (parts.length !== 5) return;
    const nums = parts.slice(0, 4).map((p) => Number(p.trim()));
    // Reject NaN/Infinity/negatives and anything past a u32: these are a version
    // int and two heap sizes, all small and non-negative by construction.
    if (nums.some((n) => !Number.isFinite(n) || n < 0 || n > 0xffffffff)) return;
    const result = (parts[4] ?? "").trim().slice(0, 24).replace(/[^\w.-]/g, "");
    if (!result) return;
    const [fwFrom, fwTo, preLargest, postLargest] = nums as [number, number, number, number];
    // blob4 = the device, so a failed update names the unit to go and look at
    // instead of just a model. Appended, for the same reason as record()'s.
    env.METRICS?.writeDataPoint({
      blobs: ["ota", result, model, dev ?? ""],
      doubles: [fwFrom, fwTo, preLargest, postLargest],
      indexes: ["ota"], // own index: OTA points query separately from per-request ones
    });
  } catch {
    // never let telemetry break serving
  }
}
