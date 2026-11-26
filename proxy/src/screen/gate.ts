// The screen service's two gates (the screen registration, accepted 2026-11-25). Both FAIL CLOSED, and both are
// graded as truth tables in plain Node by scripts/check-screen.ts (P-T2, P-F2), whose --selftest sabotages a COPY of
// this file at the two markers below. Type-only imports, so a copy of this file loads on its own.
import type { Firmware, Tier } from "./map";

/** The tiers an app call may assert (ScreenApp's ctx.tier). Anything else is not a tier: bad_ctx. */
export const CTX_TIERS = ["cloud", "plus", "business"] as const;
export type CtxTier = (typeof CTX_TIERS)[number];

const ALLOWS: Record<CtxTier, readonly Tier[]> = {
  cloud: ["free", "cloud"],
  plus: ["free", "cloud", "plus"],
  business: ["free", "cloud", "plus"], // venue screens: every Plus extra, included in Business (the owner, 04:40Z)
};

export const isCtxTier = (t: unknown): t is CtxTier => typeof t === "string" && (CTX_TIERS as readonly string[]).includes(t);

/** P-T2: may a call asserting `ctxTier` reach a method of `rowTier`? Unknown, missing or misspelt: never. */
export function tierAllows(ctxTier: unknown, rowTier: Tier): boolean {
  /*P-T2*/
  if (!isCtxTier(ctxTier)) return false;
  return ALLOWS[ctxTier].includes(rowTier);
}

/**
 * P-F2: may a method with this firmware row answer, given the published release `released` (null: unreadable)?
 * A released feature always may. A planned one only once a READ version is at least its "waits for". Unscheduled:
 * never. A version that is not a whole number counts as unreadable.
 */
export function releaseAllows(fw: Firmware, released: number | null): boolean {
  if ("released" in fw) return true;
  /*P-F2*/
  if (fw.waitsFor === "unscheduled") return false;
  return typeof released === "number" && Number.isInteger(released) && released >= fw.waitsFor;
}
