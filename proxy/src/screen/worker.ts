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
//
// EVERY CALL IS COUNTED, UNSAMPLED (phase 1, R1; src/screen/metrics.ts): one point per call on every path, refusals
// included, and one more when a handler throws. A throw never crosses RPC: the caller gets { ok: false, reason: "error" }.
//
// PHASE 1 (the enrich part; registration D3'): `snapshot` and `enrich` are BUILT. Their handlers below refuse what
// needs no data (bad_model, a malformed hex, a place that is not numbers) with 0 reads, keep the per-location shield
// (RL_SCREEN), and send everything else as ONE RPC to the one ScreenDetailsDO, whose core is src/screen/details.ts.
// The DO answers { answer, outcome }: dispatch counts the outcome and returns the answer (they differ only for a
// blocked hex: answer.ts).
import { DurableObject, WorkerEntrypoint } from "cloudflare:workers";
import { type Answer, type Counted, type Refusal, refusal } from "./answer";
import { isHex6, normHex } from "./classes";
import { type CoreEnv, type CoreRequest, DetailsCore, parseModel, parsePlace } from "./details";
import { isCtxTier, releaseAllows, tierAllows } from "./gate";
import { type MethodName, type MethodRow, RELEASE_URL, WEBSITE_TIERS, mapCounts, methodRows } from "./map";
import { type Caller, countError, countRequest } from "./metrics";
import { releaseMemo, releasedVersion } from "./release";

export type { Answer, Refusal } from "./answer";
// TEST SEAMS ONLY, re-exported so a test reaches them through this module; no RPC method calls either.
export { __setClock as __setClockForTests } from "./clock";
export { __tapEnrichRequest as __tapEnrichRequestForTests } from "./details";

/** P-B2's marker: present exactly once in this script's bundle, and never in the boards'. */
export const SCREEN_MARKER = "blipscope-screen:entry:v1";

// Stamped at deploy, as the boards' and the builder's are; screen.wrangler.toml defaults it to "UNSTAMPED".
declare const BUILD_COMMIT: string;
// /health's `logs` (R2), read by the full-logs watch (scripts/screen-logs-verdict.mjs). LOGS_RATE is stamped per env by
// screen.wrangler.toml and equals the env's head_sampling_rate (check-screen.ts, P-O2). FULL_LOGS_UNTIL is "null" in the
// toml and stamped only by `deploy.sh --screen production-fulllogs --until <time>` (screen-deploy-check.mjs, P-U). A
// bundle nothing stamped (this repo's tests) has neither: `typeof` reads "undefined" instead of throwing, both report
// null, and the watch reads that as BLIND.
declare const LOGS_RATE: number | undefined;
declare const FULL_LOGS_UNTIL: string | null | undefined;

// The bindings, all optional in the type because this repo's tests run without them; a deployed env missing one is
// refused by scripts/check-screen.ts (P-O4 for the counter). SITE_FEED and ENRICH_KV are read inside the DO (CoreEnv).
export interface ScreenEnv {
  SCREEN_METRICS?: AnalyticsEngineDataset;
  SCREEN_DETAILS?: DurableObjectNamespace<ScreenDetailsDO>;
  /** The per-location shield (D8): a Workers rate-limit binding, keyed by the caller's class. Coarse and approximate;
   *  the strict limit is the DO's. miniflare cannot emulate it, so it is optional, like the boards' RL_IP. */
  RL_SCREEN?: { limit(o: { key: string }): Promise<{ success: boolean }> };
}

type Handler = (env: ScreenEnv, args: unknown[], caller: Caller) => Promise<Counted>;

/** The one object every snapshot and enrich call reaches (D3'). */
const DETAILS_OBJECT = "screen";
function detailsObject(env: ScreenEnv) {
  const ns = env.SCREEN_DETAILS;
  if (!ns) throw new Error("SCREEN_DETAILS is not bound");
  return ns.get(ns.idFromName(DETAILS_OBJECT));
}
async function shieldAllows(env: ScreenEnv, caller: Caller): Promise<boolean> {
  return env.RL_SCREEN ? (await env.RL_SCREEN.limit({ key: caller })).success : true;
}
const argsOf = (a: unknown): Record<string, unknown> => (a && typeof a === "object" ? (a as Record<string, unknown>) : {});

/** The handlers, by method. Phase 1 builds snapshot and enrich; 2 adds photo, 3 weather, basemap and airports. */
const HANDLERS: Partial<Record<MethodName, Handler>> = {
  async snapshot(env, [raw], caller) {
    const a = argsOf(raw);
    const model = parseModel(a.model);
    if (!model) return refusal("bad_model");
    const place = a.place === undefined ? undefined : parsePlace(a.place);
    if (place === null) return refusal("place_not_allowed");
    if (!(await shieldAllows(env, caller))) return refusal("limited");
    const req: CoreRequest = { method: "snapshot", caller, model, ...(place ? { place } : {}) };
    return (await detailsObject(env).serve(req)) as Counted;
  },
  async enrich(env, [raw], caller) {
    const a = argsOf(raw);
    const model = parseModel(a.model);
    if (!model) return refusal("bad_model");
    const hex = normHex(a.hex);
    if (!isHex6(hex)) return refusal("not_in_snapshot"); // a hex of no possible shape cannot be in the snapshot
    if (!(await shieldAllows(env, caller))) return refusal("limited");
    return (await detailsObject(env).serve({ method: "enrich", caller, model, hex })) as Counted;
  },
};
const BUILT: Readonly<Partial<Record<MethodName, Handler>>> = { ...HANDLERS };

/** TEST SEAM ONLY (test/screen-metrics.test.ts, P-M1e): plant a handler, or put the BUILT one back with undefined.
 *  Not reachable by RPC (only the two classes' methods are), and nothing in the service calls it. */
export function __setHandlerForTests(method: MethodName, h: Handler | undefined): void {
  const restore = h ?? BUILT[method];
  if (restore) HANDLERS[method] = restore;
  else delete HANDLERS[method];
}

const readRelease = () => releasedVersion(() => fetch(RELEASE_URL), Date.now());

/** Every call goes through here: answered, then counted, whatever the path. */
async function dispatch(env: ScreenEnv, method: MethodName, caller: Caller, ctx: unknown, args: unknown[]): Promise<Answer> {
  let c: Counted;
  try {
    c = await answerCall(env, method, caller, ctx, args);
  } catch (e) {
    // The error's class only, into the (sampled) logs; the count below is the record.
    console.error(JSON.stringify({ evt: "screen_error", method, caller, name: e instanceof Error ? e.name : "unknown" }));
    countError(env, method, caller);
    c = refusal("error");
  }
  /*P-M1*/
  countRequest(env, method, caller, c.outcome);
  return c.answer;
}

async function answerCall(env: ScreenEnv, method: MethodName, caller: Caller, ctx: unknown, args: unknown[]): Promise<Counted> {
  const row: MethodRow | undefined = methodRows().find((r) => r.method === method);
  if (!row) return refusal("unknown_method");
  if (caller === "app") {
    const tier = typeof ctx === "object" && ctx !== null ? (ctx as { tier?: unknown }).tier : undefined;
    if (!isCtxTier(tier)) return refusal("bad_ctx");
    if (!tierAllows(tier, row.tier)) return refusal("tier");
  } else if (!WEBSITE_TIERS.includes(row.tier)) {
    return refusal("tier"); // unreachable while P-T1 holds: the website class has no such method
  }
  if (!("released" in row.firmware)) {
    // An unscheduled method never asks the release; a planned one asks it (memoised) and fails closed.
    const v = row.firmware.waitsFor === "unscheduled" ? null : await readRelease();
    if (!releaseAllows(row.firmware, v)) return refusal("not_released");
  }
  const h = HANDLERS[method];
  return h ? h(env, args, caller) : refusal("not_built");
}

/**
 * THE ONE OBJECT (D3'): the strict per-class call counter, the per-hex details store, the resolution cap and the two
 * cfg: caches, behind ONE RPC method, `serve`. Its state is created lazily and kept beside the object rather than in
 * a field, so the object holds nothing RPC could expose and a test can drive its prototype (test/screen-enrich.test.ts).
 */
const cores = new WeakMap<object, DetailsCore>();
/** A module function, not a method: every method of the class is callable over RPC, whatever TypeScript calls it. */
function coreOf(o: ScreenDetailsDO, env: CoreEnv, ctx: DurableObjectState): DetailsCore {
  let c = cores.get(o);
  if (!c) {
    c = new DetailsCore(env, (p) => ctx.waitUntil(p));
    cores.set(o, c);
  }
  return c;
}
export class ScreenDetailsDO extends DurableObject<CoreEnv> {
  serve(req: CoreRequest): Promise<Counted> {
    return coreOf(this, this.env, this.ctx).serve(req);
  }
  /** Writes the read-only KV wrapper has dropped: a single number, never a per-outcome count (P-H3). */
  stats(): { kvWritesDropped: number } {
    return { kvWritesDropped: cores.get(this)?.dropped() ?? 0 };
  }
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
  // /health reads no binding, so it takes its env as unknown: typed ScreenEnv (all optional, a "weak" type), any env
  // sharing none of its members -- the test pool's -- is a type error, for a value this handler never reads.
  async fetch(request: Request, _env: unknown): Promise<Response> {
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
      logs: {
        rate: typeof LOGS_RATE === "number" ? LOGS_RATE : null,
        until: typeof FULL_LOGS_UNTIL === "string" ? FULL_LOGS_UNTIL : null,
      },
    }, { headers: { "Cache-Control": "no-store" } });
  },
};
