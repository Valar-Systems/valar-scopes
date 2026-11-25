// THE DETAILS CORE: everything a screen call does past its shape checks (phase 1, the enrich part; registration D3',
// accepted 2026-11-25). It runs inside the ONE ScreenDetailsDO (worker.ts), reached by ONE RPC per call, so:
//   - the call limit is STRICT and GLOBAL: one object holds the fixed-window counter per entrypoint class (P-L1);
//   - the per-hex store is single: concurrent first asks share one resolution (P-C1), and new resolutions are capped
//     per minute, globally (P-L1b);
//   - the two cfg: keys are read by one object, at most once a minute each.
//
// THE ORDER, and why it is this order:
//   admit -> read the site feed -> [snapshot: the place gate] -> unavailable / stale -> the blocked list ->
//   [enrich: membership -> blocked -> class -> the store, single-flight, the cap]
//   The list is read BEFORE membership on every call, so whether the list is consulted never depends on which hex
//   was asked: a blocked hex and an absent one make the same I/O (the P-E2b ruling).
//
// DETAILS ARE THE BOARDS' ANSWER, UNCHANGED (D2). A resolution calls the boards' handleEnrich with a synthetic
// Request built from the SNAPSHOT's own lat, lon and trk (P-S3; enrich takes no place) and the model's square at
// fw 7 (P-V1), through three wrappers and no edit to handleEnrich:
//   1. a read-only ENRICH_KV: its writes are dropped and counted (rokv.ts, P-K1);
//   2. no METRICS: the service writes 0 enrich_gap points (P-G1);
//   3. the routeset pre-check: a callsign is passed only when the route mirror holds it, so a mirror miss is a blank
//      route and never an uncached routeset POST (P-R1).
//
// The joined snapshot never waits on a cold resolution (D4): a public aircraft not yet resolved goes out without
// `details`, and its resolution runs in the background through ctx.waitUntil (which keeps a DO in memory).
import { handleEnrich, routeCacheKey } from "../enrich";
import type { SiteFeed } from "../index";
import type { RequestMetric } from "../metrics";
import { FULLBLEED_MIN_FW, squareSizeFor } from "../photos";
import { bucketForRadiusKm, quantizeTile } from "../schema";
import type { Env } from "../types";
import { type Counted, answered, refusal } from "./answer";
import { BLOCKED_KEY, type Blocked, CachedKey, PLACES_KEY, type Places, parseBlocked, parsePlaces } from "./cfg";
import { classify, isHex6, normHex } from "./classes";
import { now } from "./clock";
import type { Caller } from "./metrics";
import { type ReadOnlyKv, readOnlyKv } from "./rokv";

/** The site feed's answer, typed from the boards' own entrypoint (src/index.ts's neutral alias), never restated. */
export type SiteFeedAnswer = Awaited<ReturnType<InstanceType<typeof SiteFeed>["snapshot"]>>;
type Available = Extract<SiteFeedAnswer, { available: true }>;
type SiteAircraft = Available["aircraft"][number];

export type Model = "s3-128" | "s3-146" | "s3-21";
export interface Place { lat: number; lon: number; r: number }
export interface CoreRequest { method: "snapshot" | "enrich"; caller: Caller; model: Model; hex?: string; place?: Place }
export type Details = Record<string, unknown>;

/** The DO's env: the screen script's bindings and vars (screen.wrangler.toml), plus the boards' upstream vars (P-K2). */
export interface CoreEnv {
  SITE_FEED?: { snapshot(): Promise<SiteFeedAnswer> };
  ENRICH_KV?: KVNamespace;
  SCREEN_LIMIT_WEBSITE_PER_MIN?: string;
  SCREEN_LIMIT_APP_PER_MIN?: string;
  SCREEN_RESOLVE_PER_MIN?: string;
  [name: string]: unknown;
}

const DEFAULT_MODEL: Model = "s3-128";
const WINDOW_MS = 60_000;
const DETAILS_TTL_MS = 30 * 60_000;
const STORE_PRUNE_AT = 5_000;

/**
 * `model` (D6): absent means s3-128; otherwise exactly a key the boards give a square to at fw 7 (squareSizeFor, so
 * the two lists cannot drift). Another case, whitespace, null or a number is not a model: null, never coerced.
 */
export function parseModel(v: unknown): Model | null {
  if (v === undefined) return DEFAULT_MODEL;
  /*P-D1*/
  if (typeof v !== "string" || v !== v.trim()) return null;
  return squareSizeFor(String(FULLBLEED_MIN_FW), v) !== null ? (v as Model) : null;
}

/** A place is { lat, lon, r }, each a finite number; anything else is not a place. */
export function parsePlace(v: unknown): Place | null {
  if (!v || typeof v !== "object") return null;
  const { lat, lon, r } = v as Record<string, unknown>;
  const fin = (x: unknown): x is number => typeof x === "number" && Number.isFinite(x);
  return fin(lat) && fin(lon) && fin(r) ? { lat, lon, r } : null;
}

/** A limit var: a whole number of calls per minute. Missing or malformed FAILS CLOSED: 0. */
function limitVar(env: CoreEnv, name: string): number {
  const v = env[name];
  return typeof v === "string" && /^[0-9]+$/.test(v) ? Number(v) : 0;
}

let tap: ((r: Request) => void | Promise<void>) | undefined;
/**
 * TEST SEAM ONLY: see each synthetic Request before handleEnrich does (P-S3), or HOLD the resolution there by
 * returning a promise (P-T1: a resolution in flight while other calls arrive). worker.ts re-exports it. Unset, it
 * costs one microtask.
 */
export function __tapEnrichRequest(f: ((r: Request) => void | Promise<void>) | undefined): void {
  tap = f;
}

export class DetailsCore {
  private readonly ro: ReadOnlyKv;
  private readonly enrichEnv: Env;
  private readonly places = new CachedKey<Places>(PLACES_KEY, parsePlaces, "circle");
  private readonly blocked = new CachedKey<Blocked>(BLOCKED_KEY, parseBlocked, { ok: false });
  private readonly store = new Map<string, { at: number; d: Details }>();
  private readonly inflight = new Map<string, Promise<Details>>();
  private window = -1;
  private calls: Record<Caller, number> = { website: 0, app: 0 };
  private resolutions = 0;

  constructor(private readonly env: CoreEnv, private readonly waitUntil: (p: Promise<unknown>) => void) {
    if (!env.ENRICH_KV) throw new Error("ENRICH_KV is not bound");
    this.ro = readOnlyKv(env.ENRICH_KV);
    /*P-G1*/
    this.enrichEnv = { ...env, ENRICH_KV: this.ro.kv, METRICS: undefined } as unknown as Env;
  }

  /** Writes the read-only wrapper has dropped (P-K1's evidence that it is in the path). */
  dropped(): number {
    return this.ro.dropped();
  }

  async serve(req: CoreRequest): Promise<Counted> {
    const t = now();
    this.roll(t);
    if (!this.admit(req.caller)) return refusal("limited");
    if (!this.env.SITE_FEED) throw new Error("SITE_FEED is not bound");
    const feed = await this.env.SITE_FEED.snapshot();
    return req.method === "snapshot" ? this.snapshot(req, feed, t) : this.enrich(req, feed, t);
  }

  private roll(t: number): void {
    const w = Math.floor(t / WINDOW_MS);
    if (w === this.window) return;
    this.window = w;
    this.calls = { website: 0, app: 0 };
    this.resolutions = 0;
  }

  /** The strict, global, per-class call limit (P-L1): this object is the only counter. */
  private admit(caller: Caller): boolean {
    const limit = limitVar(this.env, caller === "app" ? "SCREEN_LIMIT_APP_PER_MIN" : "SCREEN_LIMIT_WEBSITE_PER_MIN");
    /*P-L1*/
    if (this.calls[caller] >= limit) return false;
    this.calls[caller]++;
    return true;
  }

  /** The global cap on NEW resolutions per minute (P-L1b). Over it, a resolution is deferred: 0 upstream. */
  private takeResolution(): boolean {
    /*P-L1b*/
    if (this.resolutions >= limitVar(this.env, "SCREEN_RESOLVE_PER_MIN")) return false;
    this.resolutions++;
    return true;
  }

  private async snapshot(req: CoreRequest, feed: SiteFeedAnswer, t: number): Promise<Counted> {
    if (req.place) {
      const mode = await this.places.get(this.ro.kv, t);
      if (!(feed.available && sameCircle(req.place, feed))) return refusal(mode === "any" ? "not_built" : "place_not_allowed");
    }
    if (!feed.available || feed.stale) return answered(feed); // the site feed's own answer: a stale one has no aircraft
    const bl = await this.blocked.get(this.ro.kv, t);
    if (!bl.ok) return refusal("blocked_unknown");
    const aircraft: Array<Record<string, unknown>> = [];
    for (const a of feed.aircraft) {
      const hex = normHex(a.hex);
      if (bl.set.has(hex)) continue;
      const cls = classify(a);
      const row: Record<string, unknown> = { ...a, class: cls };
      /*P-J1*/
      if (cls === "public" && isHex6(hex)) {
        const d = this.cached(hex, req.model, t);
        if (d) row.details = d;
        else this.background(hex, a, req.model);
      }
      aircraft.push(row);
    }
    return answered({ ...feed, aircraft });
  }

  private async enrich(req: CoreRequest, feed: SiteFeedAnswer, t: number): Promise<Counted> {
    const hex = req.hex ?? "";
    if (!feed.available) return refusal("not_in_snapshot");
    /*P-S2*/
    if (feed.stale) return refusal("stale");
    const bl = await this.blocked.get(this.ro.kv, t);
    if (!bl.ok) return refusal("blocked_unknown");
    /*P-S1*/
    const a = feed.aircraft.find((x) => normHex(x.hex) === hex);
    if (!a) return refusal("not_in_snapshot");
    /*P-E2b*/
    if (bl.set.has(hex)) return { answer: { ok: false, reason: "not_in_snapshot" }, outcome: "blocked" };
    /*P-E1*/
    if (classify(a) === "private") return refusal("private");
    const hit = this.cached(hex, req.model, t);
    if (hit) return answered(hit);
    let p = this.inflight.get(key(hex, req.model));
    if (!p) {
      if (!this.takeResolution()) return refusal("limited");
      p = this.start(hex, a, req.model);
    }
    return answered(await p);
  }

  private cached(hex: string, model: Model, t: number): Details | undefined {
    const e = this.store.get(key(hex, model));
    return e && t - e.at < DETAILS_TTL_MS ? e.d : undefined;
  }

  /** A snapshot's cold public aircraft: resolve in the background unless one is already in flight or the cap is hit. */
  private background(hex: string, a: SiteAircraft, model: Model): void {
    if (this.inflight.has(key(hex, model)) || !this.takeResolution()) return;
    this.start(hex, a, model).catch(() => undefined);
  }

  /** One resolution, registered as in flight BEFORE anything awaits, so concurrent asks share it (P-C1). */
  private start(hex: string, a: SiteAircraft, model: Model): Promise<Details> {
    const k = key(hex, model);
    const p = this.resolve(hex, a, model)
      .then((d) => {
        this.store.set(k, { at: now(), d });
        this.prune();
        return d;
      })
      .finally(() => this.inflight.delete(k));
    /*P-C1*/
    this.inflight.set(k, p);
    this.waitUntil(p.catch(() => undefined));
    return p;
  }

  private prune(): void {
    if (this.store.size < STORE_PRUNE_AT) return;
    const t = now();
    for (const [k, e] of this.store) if (t - e.at >= DETAILS_TTL_MS) this.store.delete(k);
  }

  private async resolve(hex: string, a: SiteAircraft, model: Model): Promise<Details> {
    const cs = await this.mirroredCallsign(a);
    const u = new URL(`https://screen.internal/v1/enrich/${hex}`);
    /*P-S3*/
    u.searchParams.set("lat", String(a.lat));
    u.searchParams.set("lon", String(a.lon));
    if (typeof a.track_deg === "number") u.searchParams.set("trk", String(a.track_deg));
    if (cs) u.searchParams.set("cs", cs);
    /*P-V1*/
    const request = new Request(u.toString(), { headers: { "X-Blip-Model": model, "X-Blip-FW": String(FULLBLEED_MIN_FW) } });
    await tap?.(request);
    const meta: RequestMetric = { ep: "screen-enrich", status: 0, ms: 0, model, colo: "" };
    const ctx = { waitUntil: (p: Promise<unknown>) => this.waitUntil(p), passThroughOnException: () => undefined, props: {} };
    const res = await handleEnrich(request, this.enrichEnv, ctx as unknown as ExecutionContext, hex, meta);
    if (res.status !== 200) throw new Error(`handleEnrich answered ${res.status}`);
    return (await res.json()) as Details;
  }

  /**
   * The routeset pre-check (P-R1): the callsign, in handleEnrich's own normal form, only when the route mirror holds
   * it under either of its keys; otherwise "", so handleEnrich asks no route source at all.
   */
  private async mirroredCallsign(a: SiteAircraft): Promise<string> {
    const cs = typeof a.cs === "string" ? a.cs.trim().toUpperCase() : "";
    if (!/^[A-Z0-9]{2,8}$/.test(cs)) return "";
    const trk = typeof a.track_deg === "number" ? a.track_deg : undefined;
    /*P-R1*/
    for (const k of new Set([routeCacheKey(cs, trk), `rt:${cs}`])) if ((await this.ro.kv.get(k)) !== null) return cs;
    return "";
  }
}

const key = (hex: string, model: Model) => `${hex}|${model}`;

/** Under `circle`, a place is the site feed's circle in the boards' own quantisation (D7): its tile, its bucket. */
function sameCircle(p: Place, feed: Available): boolean {
  return quantizeTile(p.lat) === quantizeTile(feed.centre.lat)
    && quantizeTile(p.lon) === quantizeTile(feed.centre.lon)
    && bucketForRadiusKm(p.r) === bucketForRadiusKm(feed.radius_km);
}
