import type { Env } from "../types";

// Identify ourselves to every upstream: operators can see who we are and reach
// us before reaching for a ban. Sent on all upstream requests.
export const USER_AGENT = "BlipscopeProxy/1.0 (+daniel@valarsystems.com)";

// A readsb-style aircraft feed (point + hex lookups). All current upstreams
// speak the same v2 API shape; adapters only differ in base URL, paths,
// headers, and whether they're enabled.
export interface UpstreamAircraftFeed {
  id: string;
  enabled(env: Env): boolean;
  // env is passed to the URL builders (not just headers) so an upstream whose auth
  // is a QUERY PARAMETER can be supported by changing that upstream alone -- see
  // AUTH_SCHEME in adsb_lol.ts. Unused by feeds that authenticate via headers.
  pointUrl(env: Env, lat: string, lon: string, distNm: number): string;
  hexUrl(env: Env, hex: string): string;
  headers(env: Env): Record<string, string>;
  // Set on a feed whose operator counts 4xx responses against us (adsb.fi). After
  // ANY 4xx from it (400/401/403/404/429 ...) the chain skips this feed for this
  // many ms -- even as the terminal feed -- and never retries a 4xx from it inside
  // a call. See HOLD in chain.ts.
  holdOn4xxMs?: number;
}

// ---- 4xx hold ------------------------------------------------------------------
// Per-isolate, like the breakers below: an isolate that saw a 4xx from a held feed
// sends it nothing more until the hold expires. Keyed by feed id, i.e. per relay,
// because adsb.fi's limit is per IP and each relay is its own IP.
const holds = new Map<string, number>(); // feed id -> held until (epoch ms)

export function holdFeed(id: string, ms: number): void {
  const until = Date.now() + ms;
  if ((holds.get(id) ?? 0) < until) holds.set(id, until);
  console.log(JSON.stringify({ evt: "hold", id, ms }));
}

export function feedHeld(id: string): boolean {
  const until = holds.get(id);
  if (until === undefined) return false;
  if (Date.now() < until) return true;
  holds.delete(id);
  return false;
}

// ---- circuit breaker ---------------------------------------------------------
// Per-isolate. Workers have no cross-isolate shared state short of a Durable
// Object; per-isolate breakers are the standard pattern and converge fleet-wide
// within a few requests per PoP. closed -> open after N consecutive failures ->
// half-open probe after the cooldown -> closed again on success.

const FAILURE_THRESHOLD = 3;
const OPEN_COOLDOWN_MS = 30_000;

interface BreakerState {
  consecutiveFailures: number;
  openedAt: number | null;
}

const breakers = new Map<string, BreakerState>();

export function breakerAllows(id: string): boolean {
  const b = breakers.get(id);
  if (!b || b.openedAt === null) return true;
  return Date.now() - b.openedAt >= OPEN_COOLDOWN_MS; // half-open: one probe through
}

// A BREAKER THAT CHANGES STATE SILENTLY IS A DECISION NOBODY CAN OBSERVE.
//
// Every other upstream event is logged (`evt: "upstream"` in chain.ts), but a
// breaker OPENING logged nothing and an open breaker's skip logged nothing
// either -- so the whole-fleet symptom of a latched breaker is an ABSENCE of
// log lines, which is also what a code path that never runs looks like, and
// what a feed nobody calls looks like. Three different states, one observation.
//
// That cost real time on 2026-09-06: the production tail showed route fetches to
// adsb.lol and nothing whatsoever about adsbdb, and distinguishing "adsbdb is
// switched off" from "adsbdb's breaker is open" from "adsbdb answered" needed
// the source rather than the logs.
//
// Logged on TRANSITION only, not per call: a breaker's state changes at most
// twice per cooldown, so this cannot become a hot path, and a per-call log would
// bury the transition it exists to surface.
function logBreaker(id: string, state: string, consecutiveFailures: number): void {
  console.log(JSON.stringify({ evt: "breaker", id, state, consecutiveFailures }));
}

export function breakerRecord(id: string, ok: boolean): void {
  let b = breakers.get(id);
  if (!b) {
    b = { consecutiveFailures: 0, openedAt: null };
    breakers.set(id, b);
  }
  const wasOpen = b.openedAt !== null;
  if (ok) {
    const failures = b.consecutiveFailures;
    b.consecutiveFailures = 0;
    b.openedAt = null;
    if (wasOpen) logBreaker(id, "closed", failures);
    return;
  }
  b.consecutiveFailures++;
  if (b.consecutiveFailures >= FAILURE_THRESHOLD) {
    b.openedAt = Date.now(); // (re)open; a failed probe re-arms the cooldown
    // "reopened" is distinct from "open" on purpose: it is the half-open probe
    // failing, which is the signal that an outage is CONTINUING rather than a
    // fresh one starting. Collapsing them would make a 3-hour outage and three
    // separate blips look identical.
    logBreaker(id, wasOpen ? "reopened" : "open", b.consecutiveFailures);
  }
}

export function breakerState(id: string): "closed" | "open" {
  return breakers.get(id)?.openedAt ? "open" : "closed";
}

// Rolled-up upstream health for /api/v1/blipscope/config's upstreamState field.
export function upstreamOverallState(ids: string[]): "ok" | "degraded" | "down" {
  if (ids.length === 0) return "down";
  const open = ids.filter((id) => breakerState(id) === "open").length;
  if (open === 0) return "ok";
  return open === ids.length ? "down" : "degraded";
}

// Tests only: breaker state is module-scoped and would leak across test cases.
export function __resetBreakersForTests(): void {
  breakers.clear();
  holds.clear();
}
