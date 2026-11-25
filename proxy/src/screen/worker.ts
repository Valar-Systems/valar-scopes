// The screen service, `blipscope-screen` (the screen registration, accepted 2026-11-25): a SEPARATE Worker script that
// serves the website and the app's screens by RPC, reusing the boards' handlers from phase 1 on. Phase 0 is its
// shape and its checks: every method passes its gates and then meets no handler ("not_built") until its phase.
//
// THE CALLER'S CLASS IS STRUCTURAL. Two entrypoints, each bound by exactly one caller:
//   ScreenWebsite  (the site's Worker)  takes NO tier: it carries only the map's free and cloud methods, so a
//                  Plus method is absent from the class, not refused at runtime (P-T1).
//   ScreenApp      (the app's Worker)   takes ctx.tier on every call, enforced from the one map, fail-closed (P-T2).
// The contract names a TIER and never a screen surface: which screen a call serves is the app's business.
// Every method is written out by hand, so test/screen-entry.test.ts can catch a class drifting from the map (P-F4).
//
// One public HTTP route, /health. Nothing here is imported by the boards' Worker (P-B2 reads their bundle).
import { WorkerEntrypoint } from "cloudflare:workers";
import { isCtxTier, releaseAllows, tierAllows } from "./gate";
import { type MethodName, type MethodRow, RELEASE_URL, WEBSITE_TIERS, mapCounts, methodRows } from "./map";
import { releaseMemo, releasedVersion } from "./release";

/** P-B2's marker: present exactly once in this script's bundle, and never in the boards'. */
export const SCREEN_MARKER = "blipscope-screen:entry:v1";

// Stamped at deploy, as the boards' and the builder's are; screen.wrangler.toml defaults it to "UNSTAMPED".
declare const BUILD_COMMIT: string;

// Phase 0 binds nothing. Phases 1-3 add the read-only KV wrapper, the weather bucket and the two service bindings.
// eslint-disable-next-line @typescript-eslint/no-empty-object-type
export interface ScreenEnv {}

export type Refusal = "bad_ctx" | "tier" | "not_released" | "not_built" | "unknown_method";
export type Answer = { ok: true; value: unknown } | { ok: false; reason: Refusal };
type Handler = (env: ScreenEnv, args: unknown[]) => Promise<Answer>;

/** The handlers, by method. Empty in phase 0: phase 1 adds snapshot and enrich, 2 photo, 3 weather, basemap, airports. */
const HANDLERS: Partial<Record<MethodName, Handler>> = {};

const refuse = (reason: Refusal): Answer => ({ ok: false, reason });
const readRelease = () => releasedVersion(() => fetch(RELEASE_URL), Date.now());

async function dispatch(env: ScreenEnv, method: MethodName, caller: "website" | "app", ctx: unknown, args: unknown[]): Promise<Answer> {
  const row: MethodRow | undefined = methodRows().find((r) => r.method === method);
  if (!row) return refuse("unknown_method");
  if (caller === "app") {
    const tier = typeof ctx === "object" && ctx !== null ? (ctx as { tier?: unknown }).tier : undefined;
    if (!isCtxTier(tier)) return refuse("bad_ctx");
    if (!tierAllows(tier, row.tier)) return refuse("tier");
  } else if (!WEBSITE_TIERS.includes(row.tier)) {
    return refuse("tier"); // unreachable while P-T1 holds: the website class has no such method
  }
  if (!("released" in row.firmware)) {
    // An unscheduled method never asks the release; a planned one asks it (memoised) and fails closed.
    const v = row.firmware.waitsFor === "unscheduled" ? null : await readRelease();
    if (!releaseAllows(row.firmware, v)) return refuse("not_released");
  }
  const h = HANDLERS[method];
  return h ? h(env, args) : refuse("not_built");
}

export class ScreenWebsite extends WorkerEntrypoint<ScreenEnv> {
  snapshot(args?: unknown): Promise<Answer> { return dispatch(this.env, "snapshot", "website", undefined, [args]); }
  enrich(args: unknown): Promise<Answer> { return dispatch(this.env, "enrich", "website", undefined, [args]); }
  photo(key: unknown): Promise<Answer> { return dispatch(this.env, "photo", "website", undefined, [key]); }
  airports(args?: unknown): Promise<Answer> { return dispatch(this.env, "airports", "website", undefined, [args]); }
  weather(args: unknown): Promise<Answer> { return dispatch(this.env, "weather", "website", undefined, [args]); }
  basemap(args?: unknown): Promise<Answer> { return dispatch(this.env, "basemap", "website", undefined, [args]); }
  airport(code: unknown): Promise<Answer> { return dispatch(this.env, "airport", "website", undefined, [code]); }
}

export class ScreenApp extends WorkerEntrypoint<ScreenEnv> {
  snapshot(ctx: unknown, args?: unknown): Promise<Answer> { return dispatch(this.env, "snapshot", "app", ctx, [args]); }
  enrich(ctx: unknown, args: unknown): Promise<Answer> { return dispatch(this.env, "enrich", "app", ctx, [args]); }
  photo(ctx: unknown, key: unknown): Promise<Answer> { return dispatch(this.env, "photo", "app", ctx, [key]); }
  airports(ctx: unknown, args?: unknown): Promise<Answer> { return dispatch(this.env, "airports", "app", ctx, [args]); }
  weather(ctx: unknown, args: unknown): Promise<Answer> { return dispatch(this.env, "weather", "app", ctx, [args]); }
  basemap(ctx: unknown, args?: unknown): Promise<Answer> { return dispatch(this.env, "basemap", "app", ctx, [args]); }
  airport(ctx: unknown, code: unknown): Promise<Answer> { return dispatch(this.env, "airport", "app", ctx, [code]); }
}

export default {
  async fetch(request: Request, _env: ScreenEnv): Promise<Response> {
    const url = new URL(request.url);
    if (request.method !== "GET" || url.pathname !== "/health") return new Response("not found", { status: 404 });
    const version = await readRelease();
    const m = releaseMemo();
    return Response.json({
      service: "blipscope-screen",
      marker: SCREEN_MARKER,
      commit: BUILD_COMMIT,
      release: { version, ageS: m ? Math.round((Date.now() - m.at) / 1000) : null },
      map: mapCounts(),
    }, { headers: { "Cache-Control": "no-store" } });
  },
};
