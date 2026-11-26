// What a screen call answers, and what the counter records for it (phase 1, the enrich part). Kept apart from
// worker.ts so the details core (details.ts) and the entrypoints share one definition without importing each other.
//
// ANSWER AND OUTCOME ARE TWO THINGS, ON PURPOSE (the P-E2b ruling). For every path but one they are the same word.
// The exception: a hex on the blocked list answers the CALLER exactly as an absent hex does, `not_in_snapshot`,
// while the COUNTER records `blocked`. A distinct answer would be an oracle for the list C1 keeps out of the public
// repo. `blocked` is therefore an Outcome and never a Refusal: no caller can be handed it.
import type { Outcome } from "./metrics";

export type Refusal =
  | "bad_ctx" | "tier" | "not_released" | "not_built" | "unknown_method" | "error"
  | "not_in_snapshot" | "stale" | "private" | "blocked_unknown" | "place_not_allowed" | "bad_model" | "limited";
export type Answer = { ok: true; value: unknown } | { ok: false; reason: Refusal };
export interface Counted { answer: Answer; outcome: Outcome }

export const refusal = (r: Refusal): Counted => ({ answer: { ok: false, reason: r }, outcome: r });
export const answered = (value: unknown): Counted => ({ answer: { ok: true, value }, outcome: "ok" });
