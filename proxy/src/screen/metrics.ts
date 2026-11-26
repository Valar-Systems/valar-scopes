// The screen service's UNSAMPLED counter (phase 1, R1). The owner's ruling, 2026-11-25: "every request and every error
// is still counted unsampled (the metrics store)". Workers Logs in production keeps 10% (screen.wrangler.toml, P-O1);
// this keeps all of it. dispatch() writes ONE point per call on EVERY path, refusals included, and one more for a
// handler that throws. test/screen-metrics.test.ts grades it (P-M1, P-M1e).
//
// COUNTS, NEVER SUBJECTS (CLAUDE.md, "Usage telemetry"). A point carries the method, the caller's class and the
// outcome, each from a closed set, and the number 1. No hex, place, key, id or argument of any call: the two writers
// below take only those enums, so carrying a subject would take a change of signature, not an accident.
//
// ONE WRITE SITE. scripts/screen-deploy-check.mjs refuses a deploy unless the env's own bundle holds the binding's
// write call exactly once (P-M2), and asserts this file's text holds it exactly once, so the two cannot drift. Do not
// add a second write; do not spell the call out in a comment here, since the count is over this file's text.
//
// The binding is optional in the type because this repo's tests run without it. A deployed env without it would count
// nothing, which scripts/check-screen.ts's P-O4 refuses for staging, production and production-fulllogs.
import type { MethodName } from "./map";

export type Caller = "website" | "app";
// Phase 1's enrich part adds 8 (P-M1, extended). `blocked` is counted but never answered (answer.ts, the P-E2b ruling).
export const OUTCOMES = [
  "ok", "bad_ctx", "tier", "not_released", "not_built", "unknown_method", "error",
  "not_in_snapshot", "stale", "private", "blocked", "blocked_unknown", "place_not_allowed", "bad_model", "limited",
] as const;
export type Outcome = (typeof OUTCOMES)[number];
export interface MetricsEnv { SCREEN_METRICS?: AnalyticsEngineDataset }

/** The one write. It never throws into a call: a failed write is logged (sampled) and the answer goes back unchanged. */
function write(env: MetricsEnv, index: string, blobs: string[]): void {
  if (!env.SCREEN_METRICS) return;
  try {
    env.SCREEN_METRICS.writeDataPoint({ indexes: [index], blobs, doubles: [1] });
  } catch (e) {
    console.error(JSON.stringify({ evt: "screen_metrics_write_failed", name: e instanceof Error ? e.name : "unknown" }));
  }
}

/** One point per call, on every path: blobs [method, caller, outcome], indexed by the method. */
export function countRequest(env: MetricsEnv, method: MethodName, caller: Caller, outcome: Outcome): void {
  write(env, method, [method, caller, outcome]);
}

/** One more point when a handler throws: blobs ["error", method, caller], indexed "error" (no method is named that). */
export function countError(env: MetricsEnv, method: MethodName, caller: Caller): void {
  write(env, "error", ["error", method, caller]);
}
