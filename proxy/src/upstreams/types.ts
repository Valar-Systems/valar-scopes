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

export function breakerRecord(id: string, ok: boolean): void {
  let b = breakers.get(id);
  if (!b) {
    b = { consecutiveFailures: 0, openedAt: null };
    breakers.set(id, b);
  }
  if (ok) {
    b.consecutiveFailures = 0;
    b.openedAt = null;
    return;
  }
  b.consecutiveFailures++;
  if (b.consecutiveFailures >= FAILURE_THRESHOLD) b.openedAt = Date.now(); // (re)open; a failed probe re-arms the cooldown
}

export function breakerState(id: string): "closed" | "open" {
  return breakers.get(id)?.openedAt ? "open" : "closed";
}

// Rolled-up upstream health for /v1/config's upstreamState field.
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
