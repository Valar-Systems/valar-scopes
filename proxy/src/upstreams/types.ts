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
}
