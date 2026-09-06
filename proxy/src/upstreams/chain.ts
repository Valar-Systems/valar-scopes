import type { Env } from "../types";
import { intEnv } from "../util";
import { adsbLol, adsbLolB, routesetRequest } from "./adsb_lol";
import {
  adsbdbAircraftUrl,
  adsbdbHeaders,
  adsbdbRouteUrl,
  parseAdsbdbAircraft,
  parseAdsbdbRoute,
} from "./adsbdb";
import { adsbFi, adsbFiB } from "./adsb_fi";
import { airplanesLive } from "./airplanes_live";
import { breakerAllows, breakerRecord, breakerState, type UpstreamAircraftFeed } from "./types";

// The full set of feeds (for health reporting + enablement). Ordering for the
// actual fetch is per-operation below. adsb_lol + adsb_lol_b are the same data
// via two dedicated-egress relays (relay-a / relay-b); airplanes.live is
// prohibited by operator and ships permanently dark (see airplanes_live.ts).
export const FEEDS: UpstreamAircraftFeed[] = [adsbLol, adsbLolB, adsbFi, adsbFiB, airplanesLive];

// ---- SHIPPING ORDER (2026-07-30): adsb.fi primary, adsb.lol licensed fallback ----
//
// THE ORDER OF EVENTS, stated plainly because an earlier version of this comment
// said "reordered once adsb.fi granted permission IN WRITING" and that is not what
// happened. It reverses the sequence, and the reversal flatters us:
//
//   2026-07-30  The long-standing "adsb.fi 403s us" was found to be Cloudflare's
//               shared per-colo egress, not adsb.fi policy -- both relay IPs answer
//               200 unauthenticated. The reorder went in THAT DAY, on those
//               operational grounds. (Landed 2026-07-31, commit 8ad96e0.)
//   2026-08-05  Samuli's written grant arrived: commercial use for a paid hardware
//               product "including the caching system", conditional on the Open Data
//               API rate limit and nothing else.
//
// So for six days the fleet's PRIMARY position source was one we had not yet asked.
// Their published terms at the time read as non-commercial, no-redistribution. That
// was a real exposure, it is closed now, and it is written here rather than left to
// be reconstructed from commit dates by someone who needs the answer quickly.
//
// The reasoning for the order itself, recorded so it isn't re-litigated:
//
//   adsb.fi leads on operational grounds -- ~19x the rate headroom (3600 req/h/IP vs
//   the ~190/h where adsb.lol starts 429ing us), better throughput (~90 KB/s vs the
//   ~12 KB/s anonymous cap), and an operator who answers email. The written grant
//   confirms the choice; it did not prompt it.
//   Measured over a 21.3 h two-relay soak: adsb.fi returned 0 % 429 across 10,037
//   position fetches; adsb.lol ran 68 % 429 with a 720 s unbroken degraded run.
//   Live rate as of 2026-08-13: 170 and 167 upstream position fetches/h on relay-a
//   and relay-b -- 4.7 % and 4.6 % of the 1 req/s limit the permission rests on.
//
//   adsb.lol stays in the chain because its ODbL 1.0 grant is a right no operator can
//   revoke -- that is worth more as a fallback than as a primary. Its maintainer is
//   unresponsive and has publicly flagged that the public API may have to be withdrawn,
//   so the fleet's primary path must not depend on it. The $50/mo sponsorship continues
//   REGARDLESS of chain position: it funds the licensed foundation, not a slot in this
//   array (see the sponsorship posture in README.md).
//
// NOT APPROVED, deliberately: merging aircraft from both sources into one picture.
// One response, one source, failover only -- the loops below return the FIRST feed that
// answers freshly and never combine two. Blending would make provenance, attribution and
// dedupe everyone's problem forever; the failover chain keeps each response traceable to
// exactly one operator.
//
// Per-operation priority: each source is a relay PAIR, and which half of the pair
// leads is decided per cache key by partitionOrder() below. The loser of that draw
// is the failover leg. relay-b (or relay-a, on a key that hashes the other way) is
// the terminal feed, which the breaker never skips (see the loops below), so a
// single-relay outage fails over rather than blanking the fleet. The trailing feeds
// are inert (airplanes.live prohibited + dark), so `ordered()` filters them; with the
// relay URLs unset (dev/test) each chain collapses to a single direct feed --
// unchanged legacy behaviour.
//
// adsb_lol_b stays TERMINAL (last), which is load-bearing: the breaker may never skip
// the terminal feed, so the one leg the chain can always fall back to is the one with
// the irrevocable licence. Enabling adsb.fi does not change that. airplanes.live sits
// mid-array but is hardcoded dark, so `ordered()` always filters it out.
const POINT_ORDER: UpstreamAircraftFeed[] = [adsbFi, adsbFiB, airplanesLive, adsbLol, adsbLolB];
const HEX_ORDER: UpstreamAircraftFeed[] = [adsbFi, adsbFiB, airplanesLive, adsbLol, adsbLolB];

// ---- tile partitioning across the relay pair ---------------------------------
// REVERSES A PRIOR DECISION, deliberately and with a different reason -- recorded
// here so it does not read as drift.
//
// WHAT WAS REJECTED BEFORE (commit a10dc30, 2026-07-22): sharding ENRICHMENT (hex)
// onto relay-b to halve our per-IP rate against adsb.lol. That was correctly called
// the wrong fix, because the load itself was the bug -- a hex retry storm against a
// hostile, un-cacheable per-IP limit. The right fix was to stop generating it
// (KV-authoritative enrichment + negative caching + the relay's /v2/hex hold-down),
// and it worked: hex is now ~0.6 upstream fetches/min fleet-wide.
//
// WHY THIS IS NOT THAT: three things changed.
//   1. The primary is adsb.fi, whose operator has SANCTIONED polling from both our
//      IPs in writing ("feel free to continue polling from your two locations").
//      Spreading load is now the arrangement we were granted, not a limit we are
//      routing around.
//   2. POSITION load is irreducible in a way hex never was. A live picture cannot be
//      cached long -- the fetch rate is (distinct hot tiles) / CACHE_TTL by
//      construction, and no amount of being well-behaved lowers it. At the 8 s TTL
//      that is ~8 hot tiles per IP; the 50-board pilot needs ~50.
//   3. Concentration was measurably costing us. Over 24 h on 2026-08-07, relay-a
//      took 43 x HTTP 429 from adsb.fi and relay-b took ZERO, while relay-b carried
//      0.4 % of Worker traffic. adsb.fi counts 429s toward the same limit, so the
//      idle IP was not merely wasted -- the loaded one was digging a hole.
//
// WHAT THIS IS NOT: round-robin. Alternating relays PER REQUEST would put every tile
// in both relay caches, so each relay refreshes every tile independently: total
// upstream fetches DOUBLE and the per-IP rate does not fall at all. That is not a
// theory -- the fi-bench poller ran on both boxes against the same tile and spent
// 12,728 upstream fetches for one tile's worth of freshness, exactly 2x. Partitioning
// on the cache key is what makes the capacity real: the key picks the relay, so no
// key is ever fetched twice and the two IPs actually add.
//
// The pairs, primary-first. A swap only ever happens WITHIN a pair, never across one,
// so source priority (adsb.fi before adsb.lol) is untouched -- and so is the rule that
// the terminal feed carries the irrevocable ODbL licence, since both halves of the
// adsb.lol pair are adsb.lol.
const RELAY_PAIRS: [UpstreamAircraftFeed, UpstreamAircraftFeed][] = [
  [adsbFi, adsbFiB],
  [adsbLol, adsbLolB],
];

// FNV-1a plus a murmur3 finalizer. Needs to be STABLE ACROSS COLOS AND DEPLOYS, not
// cryptographic: Workers in different PoPs must route the same key to the same relay,
// or the key lands in both relay caches and we pay the fetch twice -- the exact
// duplication this exists to avoid. Nothing here may ever depend on request-local
// state (colo, time, isolate).
//
// THE FINALIZER IS NOT DECORATION -- without it this function is broken for the one
// use it has. Bare FNV-1a cannot be used with `% 2`: the step is `h = (h ^ c) * prime`
// with an ODD prime, and multiplying mod 2^32 by an odd number preserves bit 0, so the
// low bit degenerates to `1 XOR (XOR of the low bits of every input character)`. Tile
// keys are digits and punctuation, so that made the relay choice a parity check on the
// coordinate digits: '2'/'4'/'6' all share a low bit, and the two radius buckets of a
// tile ("...,24" vs "...,46") could NEVER split. Aggregate balance still looked like a
// clean 50/50, which is exactly why this needed catching by construction rather than
// by eyeballing a distribution. fmix32 avalanches the whole word so bit 0 depends on
// every input bit.
export function partitionHash(key: string): number {
  let h = 0x811c9dc5;
  for (let i = 0; i < key.length; i++) {
    h ^= key.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  h ^= h >>> 16;
  h = Math.imul(h, 0x85ebca6b) >>> 0;
  h ^= h >>> 13;
  h = Math.imul(h, 0xc2b2ae35) >>> 0;
  h ^= h >>> 16;
  return h >>> 0;
}

// Even hash -> the default order (relay-a leads). Odd -> relay-b leads and relay-a is
// the failover. Positions in the array are preserved, so the inert middle feed and the
// terminal slot keep their meaning.
export function partitionOrder(
  order: UpstreamAircraftFeed[],
  key: string,
): UpstreamAircraftFeed[] {
  if (partitionHash(key) % 2 === 0) return order;
  const out = [...order];
  for (const [a, b] of RELAY_PAIRS) {
    const ia = out.indexOf(a);
    const ib = out.indexOf(b);
    if (ia >= 0 && ib >= 0) {
      out[ia] = b;
      out[ib] = a;
    }
  }
  return out;
}

// Optional per-env priority override: UPSTREAM_FEED_ORDER = "adsb_lol,adsb_lol_b".
// THE ROLLBACK KNOB. The default order below is the shipping decision; this exists so
// demoting a misbehaving primary is a var edit (dashboard, no deploy) at 2 a.m. rather
// than a code change and a release. Applies to both the point and hex chains.
// Deliberately non-destructive: unknown ids are ignored, and any feed you DIDN'T list is
// appended in its default position rather than dropped -- so a typo can only reorder the
// chain, never shorten it, and the terminal-feed guarantee (the last enabled feed is
// always tried) is impossible to configure away.
function applyOrderOverride(
  env: Env,
  order: UpstreamAircraftFeed[],
  raw: string | undefined,
): UpstreamAircraftFeed[] {
  if (!raw) return order;
  const wanted = raw
    .split(",")
    .map((s) => s.trim())
    .filter((s) => s.length > 0);
  const front: UpstreamAircraftFeed[] = [];
  for (const id of wanted) {
    const feed = order.find((f) => f.id === id);
    if (feed && !front.includes(feed)) front.push(feed);
  }
  return [...front, ...order.filter((f) => !front.includes(f))];
}

// partitionKey picks which half of each relay pair leads (see partitionOrder). The
// override is applied AFTER partitioning on purpose: pinning a feed in
// UPSTREAM_FEED_ORDER hoists it to the front for every key, which is precisely the
// 2 a.m. behaviour wanted -- naming a relay turns partitioning off for that source
// without a deploy. An unset key (no partitioning) is the legacy order.
function ordered(
  env: Env,
  order: UpstreamAircraftFeed[],
  partitionKey?: string,
): UpstreamAircraftFeed[] {
  const base = partitionKey ? partitionOrder(order, partitionKey) : order;
  return applyOrderOverride(env, base, env.UPSTREAM_FEED_ORDER).filter((f) => f.enabled(env));
}

export function enabledFeeds(env: Env): UpstreamAircraftFeed[] {
  return FEEDS.filter((f) => f.enabled(env));
}

export function feedHealth(env: Env): { id: string; enabled: boolean; state: "closed" | "open" }[] {
  // THE ROUTE SOURCES BELONG HERE TOO. /healthz listed the four AIRCRAFT feeds
  // and nothing else, so during a five-day fleet-wide route blackout it read
  // `"ok":true` with every upstream `"closed"` -- accurately, about a subsystem
  // that was not the one failing. A health endpoint that cannot express a
  // subsystem's failure is not neutral about it; it actively reassures.
  //
  // NOT added to upstreamOverallState() in config.ts, deliberately: that field
  // tells a DEVICE whether the position picture is trustworthy, and a dead route
  // source must not make a device believe the sky is down.
  return [
    ...FEEDS.map((f) => ({ id: f.id, enabled: f.enabled(env), state: breakerState(f.id) })),
    { id: ROUTE_BREAKER_ID, enabled: true, state: breakerState(ROUTE_BREAKER_ID) },
    {
      id: ADSBDB_BREAKER_ID,
      enabled: env.ROUTE_ADSBDB_ENABLED !== "false",
      state: breakerState(ADSBDB_BREAKER_ID),
    },
  ];
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

function logUpstream(
  id: string,
  op: string,
  ok: boolean,
  ms: number,
  err?: unknown,
  ageS?: number,
): void {
  console.log(
    JSON.stringify({
      evt: "upstream",
      id,
      op,
      ok,
      ms,
      ...(err ? { err: String(err) } : {}),
      ...(ageS !== undefined ? { ageS } : {}),
    }),
  );
}

// A source that was NOT CALLED, and why. Distinct evt from "upstream" because it
// is not an attempt and must never be counted as one: the whole point is that a
// skip currently looks identical to an attempt that was never needed.
function logSkip(id: string, op: string, reason: string): void {
  console.log(JSON.stringify({ evt: "upstream_skip", id, op, reason }));
}

// ---- degraded-feed threshold (the stale-200 failover fix) --------------------
// THE BUG: our relays serve last-good data on an upstream 429 (proxy_cache_use_stale)
// and answer the Worker with a perfectly good HTTP 200. The chain only ever looked at
// transport success, so relay-a "succeeded" even while serving a 12-minute-old picture,
// and the loop returned before relay-b was ever tried. Measured over 21.3 h: relay-a
// took 2950 adsb.lol position fetches to relay-b's 42 -- a 70:1 split of what is
// supposed to be a primary/failover pair, with an entire dedicated IP sitting idle.
//
// THE SIGNAL: every readsb-family feed stamps the picture with its own `now`, which we
// already parse (upstreamNowMs). Age = our clock - that stamp. Measured skew between
// both upstreams and our relays is ~0.4 s (and most of that is fetch latency), so this
// needs no relay header plumbing and works identically for any upstream.
//
// CHOOSING N: the healthy worst case is (relay CACHE_TTL) + (upstream fetch time) +
// skew. At CACHE_TTL=8s that is 8 + ~16 (an adsb.lol busy tile at its ~12 KB/s anon
// cap) + ~1 = ~25 s, so 45 s sits at ~1.8x the honest worst case -- late enough never to
// flap on a slow busy tile, early enough to fail over before the device's own stale
// ladder (staleFactor 3 x 15 s idle poll = 45 s) starts showing amber. Keep the rule
// N >= 2 x CACHE_TTL + 25 s: RAISING THE RELAY TTL MUST RAISE THIS or every fetch
// looks degraded and the chain walks all feeds on every request.
const DEFAULT_FEED_MAX_AGE_MS = 45_000;

// Walk the enabled, breaker-closed feeds in priority order; first success wins.
export async function fetchPointChain(
  env: Env,
  latQ: string,
  lonQ: string,
  distNm: number,
): Promise<PointResult | null> {
  // The partition key is exactly the relay's cache key for this fetch
  // (lat/lon/dist -> one nginx cache entry), so a key can never be resident on both
  // relays at once. Keying on anything coarser (the tile alone) would let two radius
  // buckets of the same tile disagree about which relay owns them.
  const feeds = ordered(env, POINT_ORDER, `${latQ},${lonQ},${distNm}`);
  const maxAgeMs = intEnv(env.BLIPS_FEED_MAX_AGE_MS, DEFAULT_FEED_MAX_AGE_MS);
  // Freshest degraded answer seen so far. A stale picture still beats no picture, so
  // we never DISCARD one -- we only decline to stop walking the chain on it. This
  // makes the fix strictly non-regressive: every request that used to get data still
  // gets data, and some now get fresher data from a feed we previously never reached.
  let bestStale: PointResult | null = null;
  let bestStaleAgeMs = Infinity;

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
      // Transport succeeded, so the breaker closes regardless of data age: a relay
      // serving stale-but-valid data is DEGRADED, not down. Opening the breaker here
      // would skip it entirely and throw away the fallback copy we may still need.
      breakerRecord(feed.id, true);
      const result: PointResult = {
        upstream: feed.id,
        nowMs: upstreamNowMs(json?.now),
        ac: Array.isArray(json?.ac) ? (json.ac as unknown[]) : [],
      };
      const ageMs = Date.now() - result.nowMs;

      if (ageMs <= maxAgeMs) {
        logUpstream(feed.id, "point", true, Date.now() - started, undefined, Math.round(ageMs / 1000));
        return result;
      }
      // Degraded: a 200 carrying a picture older than N. Keep it as a fallback and
      // give the next feed -- a different relay on a different egress IP, with its own
      // independent cache and last-good copy -- a real chance to do better.
      logUpstream(feed.id, "point", true, Date.now() - started, "degraded_stale", Math.round(ageMs / 1000));
      if (ageMs < bestStaleAgeMs) {
        bestStale = result;
        bestStaleAgeMs = ageMs;
      }
    } catch (err) {
      breakerRecord(feed.id, false);
      logUpstream(feed.id, "point", false, Date.now() - started, err);
    }
  }
  // Every feed was degraded (or failed): serve the freshest picture anyone had.
  return bestStale;
}

export interface HexResult {
  upstream: string;
  raw: unknown | null; // null = upstream answered but doesn't know the aircraft
}

export async function fetchHexChain(env: Env, hex: string): Promise<HexResult | null> {
  // Hex is the relay's cache key here, and hex volume is small (~0.6 upstream
  // fetches/min fleet-wide after the KV-authoritative rework). Partitioning it is not
  // about rate -- it is about BURST: hex is the path that actually took 429s, because
  // a card open fires a lookup with no tile cadence smoothing it.
  const feeds = ordered(env, HEX_ORDER, hex);
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
  if (!breakerAllows(ROUTE_BREAKER_ID)) {
    logSkip(ROUTE_BREAKER_ID, "route", "breaker_open");
    return null;
  }
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
  // A DISABLED SOURCE MUST SAY SO. Silence here is why a fleet-wide route
  // blackout read as "adsbdb is fine, nobody asked it": the tail showed adsb.lol
  // being tried and nothing at all about the fallback, which looks exactly like
  // a fallback that was never needed.
  if (env.ROUTE_ADSBDB_ENABLED === "false") {
    logSkip(ADSBDB_BREAKER_ID, "route", "disabled_by_config");
    return null;
  }
  if (!breakerAllows(ADSBDB_BREAKER_ID)) {
    logSkip(ADSBDB_BREAKER_ID, "route", "breaker_open");
    return null;
  }
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
