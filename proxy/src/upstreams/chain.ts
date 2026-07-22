import type { Env } from "../types";
import { intEnv } from "../util";
import { adsbLol, routesetRequest } from "./adsb_lol";
import {
  adsbdbAircraftUrl,
  adsbdbHeaders,
  adsbdbRouteUrl,
  parseAdsbdbAircraft,
  parseAdsbdbRoute,
} from "./adsbdb";
import { adsbFi } from "./adsb_fi";
import { airplanesLive } from "./airplanes_live";
import { breakerAllows, breakerRecord, breakerState, type UpstreamAircraftFeed } from "./types";

// The full set of feeds (for health reporting + enablement). Ordering for the
// actual fetch is per-operation below.
export const FEEDS: UpstreamAircraftFeed[] = [adsbLol, adsbFi, airplanesLive];

// Per-operation priority, split because the two endpoints fail differently under
// adsb.lol's keyless shared-egress 429 (a Cloudflare colo egress IP shared across
// tenants trips adsb.lol's anonymous per-IP limit):
//   - POSITIONS (/point, high-volume bulk): adsb.lol 429s this near-constantly,
//     which starved the picture to STALE. airplanes.live isn't limited that way,
//     so it LEADS for positions; adsb.lol is the fallback.
//   - METADATA (/hex, low-volume per-tap): adsb.lol carries the ICAO type inline
//     (airplanes.live often omits it) and 429s far less here, so it stays primary;
//     airplanes.live + the adsbdb type backfill cover any miss.
// Only enabled feeds are tried, so where airplanes.live is off (default vars) both
// orders collapse to adsb.lol-first -- unchanged behaviour.
const POINT_ORDER: UpstreamAircraftFeed[] = [airplanesLive, adsbLol, adsbFi];
const HEX_ORDER: UpstreamAircraftFeed[] = [adsbLol, airplanesLive, adsbFi];

function ordered(env: Env, order: UpstreamAircraftFeed[]): UpstreamAircraftFeed[] {
  return order.filter((f) => f.enabled(env));
}

export function enabledFeeds(env: Env): UpstreamAircraftFeed[] {
  return FEEDS.filter((f) => f.enabled(env));
}

export function feedHealth(env: Env): { id: string; enabled: boolean; state: "closed" | "open" }[] {
  return FEEDS.map((f) => ({ id: f.id, enabled: f.enabled(env), state: breakerState(f.id) }));
}

// Measured against adsb.lol: quiet tiles answer in ~0.5 s, but busy-basin
// tiles (LA: ~55 KB of aircraft) routinely take ~5 s. The blips MISS path has
// its own serve deadline (see blips.ts) so no device ever waits this long --
// this only bounds the background fetch.
const DEFAULT_TIMEOUT_MS = 8000;

function timeoutMs(env: Env): number {
  return intEnv(env.UPSTREAM_TIMEOUT_MS, DEFAULT_TIMEOUT_MS);
}

function retryDelayMs(env: Env): number {
  return intEnv(env.UPSTREAM_RETRY_DELAY_MS, DEFAULT_RETRY_DELAY_MS);
}

class HttpStatusError extends Error {
  constructor(public readonly status: number) {
    super(`HTTP ${status}`);
  }
}

async function fetchJsonOnce(url: string, init: RequestInit, ms: number): Promise<unknown> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), ms);
  try {
    const res = await fetch(url, { ...init, signal: controller.signal });
    if (!res.ok) throw new HttpStatusError(res.status);
    return await res.json();
  } finally {
    clearTimeout(timer);
  }
}

// Workers egress IPs are shared per-colo, so an anonymous upstream can 429 us
// because of OTHER tenants' traffic -- observed live against adsb.lol. Their
// limiter windows are short: one retry after a beat usually clears it. Definitive
// 4xx (not 429) are not retried. Foreground latency is protected by the blips
// serve deadline; this only lengthens background fetches.
const DEFAULT_RETRY_DELAY_MS = 400;

function retryable(err: unknown): boolean {
  return !(err instanceof HttpStatusError) || err.status === 429 || err.status >= 500;
}

async function fetchJsonWithTimeout(
  url: string,
  init: RequestInit,
  ms: number,
  retryDelayMs: number,
  attempts = 2,
): Promise<unknown> {
  for (let attempt = 1; ; attempt++) {
    try {
      return await fetchJsonOnce(url, init, ms);
    } catch (err) {
      if (attempt >= attempts || !retryable(err)) throw err;
      // Linear backoff: 1x, 2x, ... the base delay between attempts.
      await new Promise((resolve) => setTimeout(resolve, retryDelayMs * attempt));
    }
  }
}

// readsb-family feeds disagree on the `now` unit (API v2 uses epoch ms,
// aircraft.json-style uses epoch seconds); normalize to ms and fall back to
// our own clock when it's missing entirely.
function upstreamNowMs(now: unknown): number {
  if (typeof now === "number" && Number.isFinite(now)) {
    if (now > 1e12) return Math.round(now);
    if (now > 1e9) return Math.round(now * 1000);
  }
  return Date.now();
}

export interface PointResult {
  upstream: string;
  nowMs: number; // the upstream's own clock when it assembled the picture
  ac: unknown[];
}

function logUpstream(id: string, op: string, ok: boolean, ms: number, err?: unknown): void {
  console.log(
    JSON.stringify({ evt: "upstream", id, op, ok, ms, ...(err ? { err: String(err) } : {}) }),
  );
}

// Walk the enabled, breaker-closed feeds in priority order; first success wins.
export async function fetchPointChain(
  env: Env,
  latQ: string,
  lonQ: string,
  distNm: number,
): Promise<PointResult | null> {
  const feeds = ordered(env, POINT_ORDER);
  for (let i = 0; i < feeds.length; i++) {
    const feed = feeds[i];
    // The breaker exists to skip a flaky feed IN FAVOUR OF the next one. The
    // LAST enabled feed has nothing to fall over to, so an open breaker there
    // just manufactures a 30 s all-503 window with no upstream benefit (and with
    // failovers disabled, adsb.lol IS the terminal feed). Always try the terminal
    // feed; the per-call timeout + retry still bound its latency.
    const isTerminal = i === feeds.length - 1;
    if (!isTerminal && !breakerAllows(feed.id)) continue;
    const started = Date.now();
    try {
      const json = (await fetchJsonWithTimeout(
        feed.pointUrl(env, latQ, lonQ, distNm),
        { headers: feed.headers(env) },
        timeoutMs(env),
        retryDelayMs(env),
      )) as Record<string, unknown>;
      breakerRecord(feed.id, true);
      logUpstream(feed.id, "point", true, Date.now() - started);
      return {
        upstream: feed.id,
        nowMs: upstreamNowMs(json?.now),
        ac: Array.isArray(json?.ac) ? (json.ac as unknown[]) : [],
      };
    } catch (err) {
      breakerRecord(feed.id, false);
      logUpstream(feed.id, "point", false, Date.now() - started, err);
    }
  }
  return null;
}

export interface HexResult {
  upstream: string;
  raw: unknown | null; // null = upstream answered but doesn't know the aircraft
}

export async function fetchHexChain(env: Env, hex: string): Promise<HexResult | null> {
  const feeds = ordered(env, HEX_ORDER);
  for (let i = 0; i < feeds.length; i++) {
    const feed = feeds[i];
    // Same rule as the point chain: never let the breaker skip the terminal feed
    // (nothing to fall over to), only the ones that have a fallback after them.
    const isTerminal = i === feeds.length - 1;
    if (!isTerminal && !breakerAllows(feed.id)) continue;
    const started = Date.now();
    try {
      // 3 attempts: the hex path 429s harder than the point path under shared
      // colo egress, and an enrich response with empty fields is user-visible.
      const json = (await fetchJsonWithTimeout(
        feed.hexUrl(env, hex),
        { headers: feed.headers(env) },
        timeoutMs(env),
        retryDelayMs(env),
        3,
      )) as Record<string, unknown>;
      breakerRecord(feed.id, true);
      logUpstream(feed.id, "hex", true, Date.now() - started);
      const ac = Array.isArray(json?.ac) ? (json.ac as unknown[]) : [];
      return { upstream: feed.id, raw: ac.length > 0 ? ac[0] : null };
    } catch (err) {
      breakerRecord(feed.id, false);
      logUpstream(feed.id, "hex", false, Date.now() - started, err);
    }
  }
  return null;
}

// adsbdb aircraft-by-hex type backfill. Its own breaker (a broken/limited adsbdb
// must not open the aircraft-feed or route breakers). Called only when the
// position feed resolved a hex with no ICAO type.
const ADSBDB_AC_BREAKER_ID = "adsbdb_ac";

export interface AircraftMetaBackfill {
  r: string;
  t: string;
  tn: string;
}

export async function fetchAircraftMetaAdsbdb(
  env: Env,
  hex: string,
): Promise<AircraftMetaBackfill | null> {
  if (env.ROUTE_ADSBDB_ENABLED === "false") return null; // same master switch as the route source
  if (!breakerAllows(ADSBDB_AC_BREAKER_ID)) return null;
  const started = Date.now();
  try {
    let json: unknown;
    try {
      json = await fetchJsonWithTimeout(
        adsbdbAircraftUrl(hex),
        { headers: adsbdbHeaders() },
        timeoutMs(env),
        retryDelayMs(env),
        2,
      );
    } catch (err) {
      // adsbdb answers unknown aircraft with a JSON-bodied 404: a definitive
      // negative (nothing to backfill), not an outage -- don't trip the breaker.
      if (err instanceof HttpStatusError && err.status === 404) {
        breakerRecord(ADSBDB_AC_BREAKER_ID, true);
        logUpstream(ADSBDB_AC_BREAKER_ID, "aircraft", true, Date.now() - started);
        return null;
      }
      throw err;
    }
    breakerRecord(ADSBDB_AC_BREAKER_ID, true);
    logUpstream(ADSBDB_AC_BREAKER_ID, "aircraft", true, Date.now() - started);
    const parsed = parseAdsbdbAircraft(json);
    return parsed ? { r: parsed.r, t: parsed.t, tn: parsed.tn } : null;
  } catch (err) {
    breakerRecord(ADSBDB_AC_BREAKER_ID, false);
    logUpstream(ADSBDB_AC_BREAKER_ID, "aircraft", false, Date.now() - started, err);
    return null;
  }
}

export interface RouteResult {
  o: string;
  d: string;
  plausible: boolean;
  // Only definitive answers get negative-cached: an outage or an empty
  // routeset body must not blank a callsign's route for a whole KV TTL.
  definitive: boolean;
}

// Route sources, in order: adsb.lol routeset (primary; own breaker so a broken
// route endpoint can't open the shared-host aircraft-feed breaker), then
// adsbdb.com as fallback -- see upstreams/adsbdb.ts for why it's back.
const ROUTE_BREAKER_ID = "adsb_lol_route";
const ADSBDB_BREAKER_ID = "adsbdb_route";

// adsb.lol routeset. Returns null when it had no usable data (empty body --
// the currently observed behaviour -- or unknown route), letting the caller
// fall through to adsbdb. An alive-but-empty endpoint is breaker-SUCCESS:
// the breaker guards transport health, not data coverage.
async function fetchRouteAdsbLol(
  env: Env,
  callsign: string,
  lat: number | undefined,
  lon: number | undefined,
): Promise<RouteResult | null> {
  if (!breakerAllows(ROUTE_BREAKER_ID)) return null;
  const { url, init } = routesetRequest(env, callsign, lat, lon);
  const started = Date.now();
  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs(env));
    let text: string;
    try {
      const res = await fetch(url, { ...init, signal: controller.signal });
      if (!res.ok && res.status !== 201) throw new HttpStatusError(res.status);
      text = await res.text();
    } finally {
      clearTimeout(timer);
    }
    breakerRecord(ROUTE_BREAKER_ID, true);

    let json: unknown = null;
    try {
      json = text.trim() ? JSON.parse(text) : null;
    } catch {
      json = null; // alive but non-JSON: treat as no data
    }
    const arr = Array.isArray(json) ? (json as Record<string, unknown>[]) : [];
    const r0 = arr[0];
    logUpstream(ROUTE_BREAKER_ID, "route", true, Date.now() - started);
    if (!r0) return null;

    const plausible = r0.plausible === true || r0.plausible === 1;
    const codes =
      typeof r0._airport_codes_iata === "string" && r0._airport_codes_iata.length > 0
        ? r0._airport_codes_iata
        : typeof r0.airport_codes === "string"
          ? r0.airport_codes
          : "";
    if (!codes || codes === "unknown") return null; // let adsbdb have a say
    // Multi-leg strings look like "ABC-DEF-GHI": origin is the first leg,
    // destination the last.
    const parts = codes.split("-").filter((p) => p.length > 0);
    if (parts.length < 2) return null;
    return { o: parts[0] as string, d: parts[parts.length - 1] as string, plausible, definitive: true };
  } catch (err) {
    breakerRecord(ROUTE_BREAKER_ID, false);
    logUpstream(ROUTE_BREAKER_ID, "route", false, Date.now() - started, err);
    return null;
  }
}

async function fetchRouteAdsbdb(env: Env, callsign: string): Promise<RouteResult | null> {
  if (env.ROUTE_ADSBDB_ENABLED === "false") return null;
  if (!breakerAllows(ADSBDB_BREAKER_ID)) return null;
  const started = Date.now();
  try {
    let json: unknown;
    try {
      // 3 attempts, same as the hex path: adsbdb rate-limits the shared
      // Workers egress IPs too (observed live), and a missed route is
      // user-visible on the card.
      json = await fetchJsonWithTimeout(
        adsbdbRouteUrl(callsign),
        { headers: adsbdbHeaders() },
        timeoutMs(env),
        retryDelayMs(env),
        3,
      );
    } catch (err) {
      // adsbdb answers unknown callsigns with a JSON-bodied 404: that's a
      // definitive negative, not an outage.
      if (err instanceof HttpStatusError && err.status === 404) {
        breakerRecord(ADSBDB_BREAKER_ID, true);
        logUpstream(ADSBDB_BREAKER_ID, "route", true, Date.now() - started);
        return { o: "", d: "", plausible: true, definitive: true };
      }
      throw err;
    }
    breakerRecord(ADSBDB_BREAKER_ID, true);
    logUpstream(ADSBDB_BREAKER_ID, "route", true, Date.now() - started);
    const parsed = parseAdsbdbRoute(json);
    if (!parsed) return null;
    // No plausibility data from adsbdb; routes are used as-is (the firmware's
    // historical behaviour with this source).
    return { o: parsed.o, d: parsed.d, plausible: true, definitive: true };
  } catch (err) {
    breakerRecord(ADSBDB_BREAKER_ID, false);
    logUpstream(ADSBDB_BREAKER_ID, "route", false, Date.now() - started, err);
    return null;
  }
}

export async function fetchRoute(
  env: Env,
  callsign: string,
  lat: number | undefined,
  lon: number | undefined,
): Promise<RouteResult | null> {
  const lol = await fetchRouteAdsbLol(env, callsign, lat, lon);
  if (lol) return lol;
  return fetchRouteAdsbdb(env, callsign);
}
