import type { Env } from "./types";
import { recordEnrichGap, type RequestMetric } from "./metrics";
import { SCHEMA_V } from "./schema";
import { fetchAircraftMetaAdsbdb, fetchHexChain, fetchRoute } from "./upstreams/chain";
import { isNonIcaoAddress } from "./icaoalloc";
import { canonicalOperator, militaryCallsignOperator, militaryOperator } from "./military";
import { TYPE_NAMES } from "./typenames";
import { resolvePhoto, squareSizeFor } from "./photos";
import { errorResponse, intEnv, jsonResponse } from "./util";

// Cold-lookup serve deadline, same philosophy as the blips one: under a 429
// storm the upstream chain can burn many seconds in retries, and the firmware's
// detail card fills progressively and re-requests anyway. Answer with whatever
// resolved in time (uncached fields come back empty) and let the chain finish
// in the background -- its KV puts make the card's next request a warm hit.
const DEFAULT_ENRICH_SERVE_DEADLINE_MS = 2500;

// KV TTLs. Aircraft metadata is near-static (30 d); unknown aircraft get a day
// so a fresh registration isn't blanked for a month. Routes are per-callsign
// schedules (24 h), definitive negatives included -- a callsign that resolved
// unknown (or implausible for the given position) stays unknown for the day.
// Non-definitive outcomes (outages, empty routeset bodies) are never cached.
const AC_TTL_S = 30 * 86400;
const AC_NEG_TTL_S = 86400;
const RT_TTL_S = 86400;

// Fleet hold-down on a FAILED hex fetch (chain down / 429). Without this, a failed
// lookup cached nothing and every device re-fired the same hex on the next poll -- a
// self-amplifying 429 storm. Caching a brief empty marker means the whole fleet holds
// that hex down for a short window instead of hammering. SHORT (90s, not the 1 d
// negative) so a transient outage doesn't blank a real airframe for long; the airframe
// resolves for real on the next attempt after the marker expires.
const AC_FAIL_TTL_S = 90;

interface AcMeta {
  found: boolean;
  r: string; // registration
  t: string; // ICAO type designator
  tn: string; // friendly type name
  op: string; // operator / registered owner
  mil?: boolean; // upstream dbFlags bit 0 (military); absent on entries cached before it existed
  v?: number; // AC_META_V of the rule that wrote it; absent on pre-2026-08-12 entries
}

// Bumped when the RULE that decides an entry's TTL changes, so entries written
// under an older rule can be told apart from ones written under this one.
//
// v2 (2026-08-12): a missing TYPE now forces the short TTL -- see the hasContent
// comment in resolveMeta. Needed because a KV expirationTtl is fixed at write
// time and a cache HIT returns without rewriting, so changing the rule does
// NOTHING to entries already in KV: they sit out their original 30 days. Rather
// than purge (which needs KV write credentials nobody should need for a code
// change, and fixes only today's entries), a type-less entry from an older rule
// is treated as a MISS once and re-resolved. It then carries this stamp and
// obeys the new TTL, so the legacy population drains itself in one pass.
const AC_META_V = 2;

interface RouteEntry {
  o: string;
  d: string;
}

// One row of the static military airframe side table (P2), ingested from the
// Mictronics aircraft-database export (ODC-By 1.0) by scripts/ingest-mildb.ts.
// Consulted at serve time only when the live DB record resolved empty, so it
// can never fight fresher upstream data; no TTL (each ingest run overwrites).
interface MilEntry {
  /** Operator, from plane-alert-db or a hand override; Mictronics has none. */
  op?: string;
  r?: string; // registration / serial
  t?: string; // ICAO type designator
  tn?: string; // friendly type name (the export's description)
}

function str(v: unknown): string {
  return typeof v === "string" ? v.trim() : "";
}

// ICAO type designators are 2-4 chars of [A-Z0-9]. Upstream DBs (Mictronics /
// tar1090, reached via adsb.lol) suffix an UNCONFIRMED type with " ?" -- e.g.
// "P8 ?". Passed through verbatim that breaks the friendly-name lookup and the
// type-keyed stock-photo join (both key on the bare code) and shows an ugly
// "Type: P8 ?" on the card. Keep only the leading alphanumeric run.
function normType(v: unknown): string {
  return str(v).toUpperCase().match(/^[A-Z0-9]+/)?.[0] ?? "";
}

async function buildMeta(env: Env, raw: unknown): Promise<AcMeta> {
  if (raw === null || typeof raw !== "object") return { found: false, r: "", t: "", tn: "", op: "" };
  const r = raw as Record<string, unknown>;
  const reg = str(r.r);
  const type = normType(r.t);
  const op = str(r.ownOp);
  // dbFlags bit 0 = military, when the upstream DB supplies it. Recorded so the
  // serve-time military floor can label hexes outside the static block table.
  const mil = typeof r.dbFlags === "number" && (r.dbFlags & 1) === 1;
  // Friendly name: the upstream's own description when its DB carries one, else
  // the KV override table (`tn:<CODE>`, updatable without a deploy), else the
  // baked common-types table, else empty.
  let tn = str(r.desc);
  if (!tn && type) {
    tn = (await env.ENRICH_KV.get(`tn:${type}`)) ?? TYPE_NAMES[type] ?? "";
    // Every source missed: the card will show the raw designator. Report it so the
    // FLEET tells us what it actually sees instead of us guessing from synthetic
    // samples -- which is how "TWEN" (Tecnam P2010) reached a customer's screen.
    //
    // Cheap by construction: this runs only inside buildMeta, i.e. on an ac:<hex>
    // KV MISS -- a never-before-seen airframe -- measured at ~59-74/h FLEET-WIDE,
    // and only the unresolved subset of that logs. It is NOT on the warm path.
    // Feed the results back via scripts/ingest-typenames.ts or a curated row.
    if (!tn) console.log(JSON.stringify({ evt: "tn_miss", t: type }));
  }
  return { found: true, r: reg, t: type, tn, op, mil };
}

// One re-resolve for entries written before AC_META_V, so the legacy population
// heals itself instead of needing a purge. Deliberately NARROW -- three exclusions,
// each load-bearing:
//
//   entry has a type      -> the valuable case; never re-fetch it. Without this,
//                            a deploy would invalidate every good entry at once.
//   entry has found=false -> the AC_FAIL_TTL_S hold-down marker written during a
//                            429 storm. Re-resolving THAT would defeat the very
//                            mechanism that stops the fleet amplifying an outage.
//   entry is already v2    -> written under the new rule; it carries the 1 d TTL
//                            and will churn on its own. Re-resolving would loop.
//
// So the target is exactly: found, no type, pre-v2 -- the reg-only entries that
// took a 30 d TTL under the old rule and never re-attempted the type backfill.
function staleUnderNewRule(e: AcMeta): boolean {
  return !e.t && e.found && (e.v ?? 0) < AC_META_V;
}

// How long an ac:<hex> entry is allowed to live. Its own named function because
// this rule is the whole of the 2026-08-12 defect and needs to be assertable on
// its own -- a KV expirationTtl cannot be read back, so a test that went through
// KV could only ever check the entry's CONTENT and would pass with the TTL
// wrong, which is exactly how the original slipped through.
export function acTtlSeconds(m: Pick<AcMeta, "found" | "t">): number {
  return m.found && m.t ? AC_TTL_S : AC_NEG_TTL_S;
}

export const __ttlConstantsForTests = { AC_TTL_S, AC_NEG_TTL_S } as const;

// Aircraft metadata: KV, else one upstream hex lookup, then KV for next time.
async function resolveMeta(env: Env, hex: string, meta: RequestMetric): Promise<AcMeta | null> {
  const cached = await env.ENRICH_KV.get<AcMeta>(`ac:${hex}`, "json");
  if (cached && !staleUnderNewRule(cached)) {
    meta.cache = "HIT";
    return cached;
  }
  meta.cache = "MISS";
  const started = Date.now();
  const res = await fetchHexChain(env, hex);
  meta.upstreamMs = Date.now() - started;
  if (!res) {
    // Chain down (typically a 429 storm): hold this hex down fleet-wide for a short
    // window so devices stop re-firing the same failing lookup every poll. A brief
    // empty marker, NOT the 1 d negative -- a transient outage must not blank a real
    // airframe for long. The next attempt after it expires resolves for real.
    await env.ENRICH_KV.put(`ac:${hex}`, JSON.stringify({ found: false, r: "", t: "", tn: "", op: "" } satisfies AcMeta), {
      expirationTtl: AC_FAIL_TTL_S,
    });
    return null; // serve empties now; the marker makes the next lookup a quiet KV hit
  }
  meta.upstream = res.upstream;
  const built = await buildMeta(env, res.raw);
  // Type backfill: airplanes.live (our failover when adsb.lol 429s) returns a
  // hex's registration but NOT its ICAO type, so a failover loses the type -- and
  // with it the type name and the type-keyed stock photo. When the feed gave us a
  // hex but no type, ask adsbdb (a different host, not subject to the same 429)
  // for the type. Merged in before caching, so ac:<hex> stores the full record
  // and the card's next request is a warm hit with the photo joined.
  if (!built.t) {
    const bf = await fetchAircraftMetaAdsbdb(env, hex);
    const bt = normType(bf?.t);
    if (bt) {
      built.t = bt;
      if (!built.r && bf?.r) built.r = bf.r;
      // Prefer our own naming (KV override, then the baked table) over adsbdb's
      // verbose description, for consistency with the primary-feed path.
      built.tn = (await env.ENRICH_KV.get(`tn:${bt}`)) ?? TYPE_NAMES[bt] ?? bf?.tn ?? "";
      built.found = true;
    }
  }
  // TTL by CONTENT, not mere upstream presence: many military hexes return a
  // live position with an all-empty DB record, and 30 d of cached emptiness
  // kept a later-appearing record blank for a month. An empty meta is a
  // negative answer whatever `found` says -- give it the 1 d TTL.
  //
  // The TYPE specifically decides this, not "any field at all". A registration
  // used to count as content, so an entry that resolved a reg but no type took
  // the full 30 d and never re-attempted the backfill above -- which is how
  // SkyWest E175s and Alaska MAX 8/9 were served typeless 20-111 times each
  // while adsbdb had E75L/B38M all along (measured 2026-08-12). Type is the
  // field that unlocks the friendly name AND the type photo, so an entry
  // missing it is incomplete however much else resolved, and belongs on the
  // short TTL where tomorrow's request tries again.
  built.v = AC_META_V;
  await env.ENRICH_KV.put(`ac:${hex}`, JSON.stringify(built), {
    expirationTtl: acTtlSeconds(built),
  });
  return built;
}

/// Coordinates for an airport code, or null when we do not carry it.
async function airportLatLon(env: Env, code: string): Promise<[number, number] | null> {
  const raw = await env.ENRICH_KV.get(`ap:${code.toUpperCase()}`, "text");
  if (!raw) return null;
  try {
    const row = JSON.parse(raw);
    if (!Array.isArray(row) || row.length < 2) return null;
    const [la, lo] = row as [number, number];
    return Number.isFinite(la) && Number.isFinite(lo) ? [la, lo] : null;
  } catch {
    return null;
  }
}

function gcKm(aLat: number, aLon: number, bLat: number, bLon: number): number {
  const rad = Math.PI / 180;
  const dLat = (bLat - aLat) * rad;
  const dLon = (bLon - aLon) * rad;
  const h =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(aLat * rad) * Math.cos(bLat * rad) * Math.sin(dLon / 2) ** 2;
  return 6371 * 2 * Math.atan2(Math.sqrt(h), Math.sqrt(1 - h));
}

/// Can this route belong to an aircraft at (lat, lon)?
///
/// The same one-sided test the firmware applies (follow::RouteContradicted): is
/// the aircraft further from BOTH endpoints than they are from each other? On a
/// real leg the aircraft lies roughly between them, so neither distance exceeds
/// the route length by much. Deliberately crude -- flights leave the geodesic by
/// hundreds of km routinely, and a tight cross-track threshold would reject
/// correct routes. This asks a question with no innocent answer.
///
/// UNKNOWN CODES ARE NOT CONTRADICTIONS. If either endpoint is missing from the
/// airport table there is nothing to measure, and refusing on absence of
/// evidence would blank every route through a field we do not carry.
async function routeContradicted(
  env: Env,
  o: string,
  d: string,
  lat: number,
  lon: number,
): Promise<boolean> {
  const [op, dp] = await Promise.all([airportLatLon(env, o), airportLatLon(env, d)]);
  if (!op || !dp) return false;
  const routeKm = gcKm(op[0], op[1], dp[0], dp[1]);
  if (routeKm <= 1) return false;
  const toO = gcKm(lat, lon, op[0], op[1]);
  const toD = gcKm(lat, lon, dp[0], dp[1]);
  const MARGIN_KM = 100;
  return toO > routeKm + MARGIN_KM && toD > routeKm + MARGIN_KM;
}

/* ===========================================================================
 * THE ROUTE CACHE KEY CARRIES DIRECTION OF TRAVEL.
 *
 * WHY THE CALLSIGN ALONE IS WRONG. The upstream disambiguates legs for us --
 * routesetRequest() sends lat/lng precisely because "callsigns get reused across
 * legs" -- and then this cache threw that away by keying on the callsign. The
 * first caller's leg won `rt:<cs>` for the whole TTL and every other device
 * tracking a DIFFERENT leg of the same flight number got served that answer.
 *
 * WHY A POSITION TILE DOES NOT FIX IT. ASA537 flies SEA->BUR in the morning and
 * BUR->SEA in the afternoon. Same corridor, therefore the same tiles. The
 * morning leg populates the tile, the afternoon leg hits it, and the card is
 * reversed exactly as before. A position tile separates DIFFERENT ROUTES IN
 * DIFFERENT PLACES (the b16e859 case: a Bend aircraft showing MCO->BWI); it
 * cannot separate the same route in both directions, because reversal preserves
 * geography. Neither can a cached plausibility verdict -- that is a corridor
 * test, and a corridor is symmetric. Same blindness as routeContradicted(),
 * one layer down.
 *
 * And the TTL is 24 h, so it does not save us either: a same-day out-and-back
 * sits entirely inside one entry's lifetime.
 *
 * WHY DIRECTION IS A KEY AND NOT A HEURISTIC, WHICH IS THE WHOLE ARGUMENT. A
 * heuristic that GUESSES a reversal and swaps the displayed endpoints shows a
 * customer a confidently wrong card when it guesses wrong. A cache key that
 * guesses wrong just MISSES -- and a miss is a fresh fetch with the true
 * position, which returns the right answer. Being wrong here costs one upstream
 * request and never a wrong route. That inversion is why the objection to a
 * swap-detector does not apply to a key.
 *
 * It also disposes of the departure-turn case that killed the detector: an
 * aircraft leaving SEA briefly tracking north lands in a different bucket,
 * misses, and fetches correctly.
 *
 * THE SEPARATION PROPERTY, WHICH IS EXACT RATHER THAN EMPIRICAL. A reversal is
 * exactly 180 degrees. Adding 180 to a track advances the bucket index by
 * exactly TRACK_BUCKETS/2, which is non-zero mod TRACK_BUCKETS for any EVEN
 * bucket count -- so the two legs are guaranteed to land in different buckets,
 * for 2, 4 or 8 buckets, and regardless of where the boundaries are placed.
 * An ODD count breaks the guarantee, which is why the count is asserted even.
 *
 * Four buckets of 90 degrees: guaranteed separation, minimal fragmentation.
 * Err toward MORE buckets, never fewer -- more costs redundant fetches, fewer
 * costs stale hits, and only one of those is a wrong card.
 * ======================================================================== */
export const TRACK_BUCKETS = 4;
if (TRACK_BUCKETS % 2 !== 0) {
  // Not decoration: an odd count silently destroys the separation guarantee
  // above, and the symptom would be an occasional reversed card -- i.e. the
  // original bug, back, looking like bad upstream data.
  throw new Error("TRACK_BUCKETS must be EVEN or a reversal can share a bucket");
}

/** Coarse direction bucket, or "" when the caller sent no usable track. */
export function trackBucket(trk: number | undefined): string {
  if (trk === undefined || !Number.isFinite(trk)) return "";
  // Normalised the long way round on purpose: `%` follows the sign of the
  // DIVIDEND in JS, so a negative track would otherwise land on a negative
  // bucket index. See the cross-language rounding entry in CLAUDE.md.
  const norm = ((trk % 360) + 360) % 360;
  return String(Math.floor(norm / (360 / TRACK_BUCKETS)) % TRACK_BUCKETS);
}

/**
 * Cache key for a route.
 *
 * Falls back to the bare `rt:<cs>` when no track is known, which keeps this
 * backward compatible with devices that do not yet send one: they behave
 * exactly as before rather than missing every lookup. The fix therefore
 * activates per-device as firmware ships, and never regresses an old one.
 *
 * TODO(retire the no-track fallback) -- THE EXPIRY CONDITION IS A QUERY, NOT A
 * MEMORY, AND NOT A DATE. Do not retire this on a recollection that "the fleet
 * has updated". Run:
 *
 *     npx wrangler kv key list --prefix fw: --binding ENRICH_KV --env production
 *
 * A fw: row is written per device by Instrument A (src/fleet.ts) on the
 * authenticated path, so that listing IS the enrolled fleet. While ANY row sits
 * on a version that predates trk, the bare `rt:<cs>` fallback above is doing its
 * job: those devices cannot send a direction, and serving them a shared key is
 * the conservative, backward-compatible answer.
 *
 * WHAT CHANGES IS THE MEANING OF AN ABSENT trk, NOT THE CODE. Today an absent
 * trk means "old firmware". Once every enrolled unit in that listing is on a
 * trk-sending version, it stops meaning that and starts meaning ANOMALOUS -- a
 * caller that is not one of our devices, or one whose track is genuinely
 * unavailable. At that point the shared key is no longer compatibility; it is a
 * hole, and it is the original reversed-card bug still live for whatever is
 * coming through it.
 *
 * THE END STATE IS A CACHE BYPASS FOR NO-TRACK CALLERS, NOT A TIGHTER FALLBACK
 * KEY. The instinct is to invent a sharper key for the no-track case. There is
 * nothing to sharpen it WITH -- the absent direction is the whole problem, so
 * any key built without it is the same shared key wearing a longer name. A
 * no-track caller should MISS the cache and take the upstream's positional
 * answer, which is the one path that cannot serve somebody else's leg.
 *
 * Read the listing, not this comment. scripts/reconcile-fleet.py prints the fw:
 * table on every run, so the condition is visible to anyone asking anything
 * about the fleet rather than only to whoever remembers this paragraph exists --
 * which is the point of the standing entry in CLAUDE.md about instruments that
 * fire correctly into a void.
 */
export function routeCacheKey(cs: string, trk: number | undefined): string {
  const b = trackBucket(trk);
  return b === "" ? `rt:${cs}` : `rt:${cs}:${b}`;
}

// Route: KV, else the route-source chain (adsb.lol routeset, then adsbdb).
async function resolveRoute(
  env: Env,
  cs: string,
  lat: number | undefined,
  lon: number | undefined,
  trk: number | undefined,
): Promise<RouteEntry> {
  if (!cs) return { o: "", d: "" };
  const rtKey = routeCacheKey(cs, trk);

  // TWO POPULATIONS LIVE UNDER `rt:`, AND THE BUCKETED KEY ONLY EVER ADDRESSED ONE.
  //
  //   the RUNTIME CACHE -- an upstream answer for one leg, written with a TTL,
  //   which is what the direction bucket above exists to separate; and
  //
  //   the CC0 MIRROR -- 619,103 `rt:<CALLSIGN>` rows written by
  //   scripts/ingest-routes.ts without a TTL and without a bucket. A static
  //   schedule table, one row per callsign.
  //
  // The bucket was introduced for the first and silently orphaned the second:
  // `rt:ASA537:1` is a key no writer has ever produced. That would have cost one
  // redundant fetch per lookup -- the argument made in the block comment above,
  // "a miss is a fresh fetch with the true position, which returns the right
  // answer" -- except that PREMISE HAD ALREADY EXPIRED five days earlier. On
  // 2026-08-26 the mirror became the ONLY route source (ROUTE_ADSBDB_ENABLED
  // went to "false"; adsb.lol's routeset had already gone to 201-with-an-empty-
  // body), so a miss stopped being a fetch and became a blank card, for every
  // aircraft, on every device that sends a track. Which is all of them.
  //
  // Measured in production 2026-09-06: nine consecutive enrich requests, nine
  // upstream route fetches, zero KV hits -- while `rt:AAL1719` and `rt:ASA773`
  // sat in KV with the right answers in them.
  //
  // So the bucketed key is consulted first (it is the fresher, leg-specific
  // answer when it exists) and the mirror is the floor beneath it. The mirror
  // row goes through the same geometric check as any cached entry -- see the
  // limits of that check in the note on the withheld path below.
  const mirrorKey = `rt:${cs}`;
  let cached = await env.ENRICH_KV.get<RouteEntry>(rtKey, "json");
  if (!cached && rtKey !== mirrorKey) {
    cached = await env.ENRICH_KV.get<RouteEntry>(mirrorKey, "json");
  }
  if (cached) {
    // THE CACHED BRANCH IS CHECKED TOO, WHICH IT WAS NOT.
    //
    // SWA986 drew Orlando -> Baltimore for an aircraft overhead in central
    // Oregon. The route was not wrong when it was stored -- it was that
    // aeroplane's leg, it passed the plausibility test below, and it was cached
    // under the CALLSIGN. A flight number is reused across legs and days, so the
    // next aircraft to fly SWA986 got the previous leg served straight out of
    // KV, and the test that exists to catch exactly this was three lines further
    // down, past a `return`.
    //
    // The comment below already says "callsigns get reused across legs". The
    // knowledge was here; only the cached path did not go through the check.
    //
    // WHY NOT REUSE r.plausible: that flag is the UPSTREAM's verdict, computed
    // when we fetch. There is no fetch on this path, so the check has to be
    // geometric and local -- which is also what makes it free of an upstream
    // that might be wrong.
    if (lat !== undefined && lon !== undefined && cached.o && cached.d) {
      const bad = await routeContradicted(env, cached.o, cached.d, lat, lon);
      if (bad) {
        // NOT DELETED. The entry is correct for the aircraft it was cached
        // from, and another unit may be looking at that one right now; deleting
        // would turn one wrong answer into a cache stampede on every device
        // tracking the real flight. Withheld from THIS caller only.
        console.log(`route_stale cs=${cs} ${cached.o}->${cached.d} not_for_position`);
        return { o: "", d: "" };
      }
    }
    // WHAT THIS CHECK STILL CANNOT SEE, STATED SO IT IS NOT MISTAKEN FOR COVER.
    // A REVERSAL preserves geography, so a corridor test is blind to it (the
    // block comment on TRACK_BUCKETS says exactly this). A mirror row is a
    // single unordered schedule entry, so an aircraft flying the return leg is
    // served the outbound endpoints and the check passes. That is not a
    // regression introduced here -- it is how the mirror behaved from
    // 2026-08-26, when it became the sole source, through 2026-09-01 -- but it
    // is NOT fixed by this fallback either, and the bucket does not fix it for
    // mirror rows because there is only ever one row to bucket.
    //
    // The fix, when it is chosen, is to disambiguate against the TRACK: we hold
    // both endpoints' coordinates in `ap:` already, so the bearing from o to d
    // is computable and comparable to trk. That is a heuristic, and the comment
    // on TRACK_BUCKETS rejects heuristics FOR THE CACHE KEY on the grounds that
    // a wrong guess shows a confident wrong card while a wrong key merely
    // misses. That argument does not transfer unexamined to a source with one
    // row per callsign, where the alternative to guessing is not a fetch -- it
    // is a blank. Deciding between "reversed sometimes" and "blank on the return
    // leg" is a product call and is deliberately not made here.
    return cached;
  }

  const r = await fetchRoute(env, cs, lat, lon);
  if (!r) return { o: "", d: "" };
  // With a live position we trust the plausibility check (callsigns get reused
  // across legs): an implausible route is worse than none on the card.
  const usable = lat !== undefined && lon !== undefined ? r.plausible : true;
  const route = usable ? { o: r.o, d: r.d } : { o: "", d: "" };
  if (r.definitive) {
    await env.ENRICH_KV.put(rtKey, JSON.stringify(route), { expirationTtl: RT_TTL_S });
  }
  return route;
}

function maybeFloat(v: string | null): number | undefined {
  if (v === null) return undefined;
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : undefined;
}

export async function handleEnrich(
  request: Request,
  env: Env,
  ctx: ExecutionContext,
  hexRaw: string,
  meta: RequestMetric,
): Promise<Response> {
  const hex = hexRaw.toLowerCase();
  // 6 hex digits, optionally readsb's "~" prefix for non-ICAO (TIS-B) addresses.
  if (!/^~?[0-9a-f]{6}$/.test(hex)) return errorResponse(400, "bad_hex");

  // Non-ICAO address: a TIS-B/ADS-R track ID, not an airframe. No registry can
  // ever answer for one, so serve the empty body immediately -- no upstream
  // fetch, no KV read or write, and NOT counted as an enrichment gap (it is not
  // a gap; there is nothing to find). Firmware from this release skips the
  // request entirely via the mirrored table in SpecialAircraft.cpp; this branch
  // is what protects the fleet already in the field, which will keep asking.
  if (isNonIcaoAddress(hex)) {
    meta.cache = "SKIP";
    return jsonResponse({ v: SCHEMA_V, r: "", t: "", tn: "", op: "", o: "", d: "" });
  }

  const url = new URL(request.url);
  const csRaw = (url.searchParams.get("cs") ?? "").trim().toUpperCase();
  const cs = /^[A-Z0-9]{2,8}$/.test(csRaw) ? csRaw : "";
  // Optional live position: feeds the route plausibility check.
  const lat = maybeFloat(url.searchParams.get("lat"));
  const lon = maybeFloat(url.searchParams.get("lon"));
  // Direction of travel, for the route cache key -- see routeCacheKey(). Absent
  // on firmware that predates it, which falls back to the old callsign-only key.
  const trk = maybeFloat(url.searchParams.get("trk"));

  // The two lookups are independent; run them concurrently to keep tap->card
  // latency down (the firmware budget is sub-second on the warm path). Each
  // races the serve deadline INDIVIDUALLY, so a fast KV hit on one side isn't
  // held hostage by a slow upstream chain on the other -- and losers keep
  // running via waitUntil below, caching for the card's next request.
  const deadlineMs = intEnv(env.ENRICH_SERVE_DEADLINE_MS, DEFAULT_ENRICH_SERVE_DEADLINE_MS);
  const deadline = new Promise<"deadline">((resolve) =>
    setTimeout(() => resolve("deadline"), deadlineMs),
  );
  const metaPromise = resolveMeta(env, hex, meta);
  const routePromise = resolveRoute(env, cs, lat, lon, trk);
  const [metaOutcome, routeOutcome] = await Promise.all([
    Promise.race([metaPromise, deadline]),
    Promise.race([routePromise, deadline]),
  ]);

  const acMeta = metaOutcome === "deadline" ? null : metaOutcome;
  const route = routeOutcome === "deadline" ? { o: "", d: "" } : routeOutcome;
  if (metaOutcome === "deadline" || routeOutcome === "deadline") {
    ctx.waitUntil(Promise.allSettled([metaPromise, routePromise]).then(() => {}));
  }

  // Military airframe side table (P2), consulted only when the live record
  // came back with neither a registration nor a type (the military-card
  // failure mode: live position, empty DB row). Serve-time like the operator
  // floor below, so negatively-cached entries benefit without waiting out
  // their TTL -- and one extra KV read only on the empty path. Fills reg /
  // type / type name; the operator stays the floor's job (the dataset
  // carries none). Runs before the photo join so the type unlocks the
  // generic type shot.
  let acR = acMeta?.r ?? "";
  let acT = acMeta?.t ?? "";
  let acTn = acMeta?.tn ?? "";
  let acOpSide = "";
  // TWO INDEPENDENT NEEDS, AND THEY MUST NOT SHARE ONE GATE.
  //
  // This block used to run only when the live record resolved NEITHER a
  // registration NOR a type, which is right for identity and WRONG for the
  // operator -- and the difference is not academic. Measured against production
  // on 2026-08-20: ae222c is a US Navy P-8 that adsb.fi's own DB knows, so it
  // arrived with r=167951 and t=P8, the gate was false, `pa:ae222c` was never
  // read, and its curated op ("United States Navy") was unreachable. The card
  // fell back to the address-block floor and said "US military".
  //
  // So the curated operator was systematically missing for exactly the hexes the
  // upstream already knew -- which is backwards, because operator is the field
  // plane-alert-db uniquely supplies and the upstream leaves empty.
  //
  // The military hint bounds the cost. These are military tables, so consulting
  // them for an operator is only worth up to three KV reads when the hex is
  // plausibly military at all; ordinary civil traffic with a blank operator does
  // not pay for a lookup that is certain to miss. militaryOperator() is an
  // in-memory range scan, so the hint itself is free.
  const needIdentity = !acR && !acT;
  const looksMilitary = Boolean(acMeta?.mil) || militaryOperator(hex) !== "";
  const needOperator = !(acMeta?.op ?? "") && looksMilitary;
  if (needIdentity || needOperator) {
    // THREE SIDE TABLES, MOST AUTHORITATIVE FIRST. The order is expressed HERE,
    // in the read path, and deliberately not by which loader ran last:
    //
    //   ovr:  hand-written per-hex overrides. Highest precedence because a human
    //         decided it, and because it is the seam a tactical-code table would
    //         grow out of (see the AE6842 note in scripts/README or the commit).
    //   pa:   plane-alert-db (ODbL), curated military. PRIMARY for US military --
    //         it has 217 P-8s in the AE block and carries the OPERATOR, where
    //         Mictronics is a civil-registry aggregation and US military airframes
    //         are not in civil registries by design.
    //   mil:  Mictronics (ODC-By). Primary for everything else, which is most of
    //         the world's military aviation.
    //
    // Two loaders writing one key and relying on "run this one second" works
    // exactly until someone re-runs the other, and then silently reverts with
    // nothing to see in either script.
    const side =
      (await env.ENRICH_KV.get<MilEntry>(`ovr:${hex}`, "json")) ??
      (await env.ENRICH_KV.get<MilEntry>(`pa:${hex}`, "json")) ??
      (await env.ENRICH_KV.get<MilEntry>(`mil:${hex}`, "json"));
    if (side) {
      // Identity only when the live record had none -- a curated row must never
      // overwrite a registration or type the upstream actually resolved.
      if (needIdentity) {
        acR = str(side.r);
        acT = normType(side.t);
        acTn = str(side.tn) || (acT ? (TYPE_NAMES[acT] ?? "") : "");
      }
      // plane-alert-db carries an operator; Mictronics has no column for one.
      // Held aside rather than written straight into `op`, so the floor below
      // still runs in the right order when this is empty.
      // Taken whenever the row has one, independent of the identity gate above.
      acOpSide = canonicalOperator(str((side as { op?: string }).op));
    }
  }

  // Sanitize the type designator at SERVE time too, not only where it is built:
  // an ac:<hex> entry cached before this normalization existed (or any future
  // dirty source) can still hold "P8 ?", and that value flows straight into the
  // response and the photo join below. Strip it here and re-resolve the friendly
  // name from the clean code, so already-cached hexes are fixed on the next
  // request instead of waiting out the 30 d TTL.
  acT = normType(acT);
  if (acT && !acTn) acTn = (await env.ENRICH_KV.get(`tn:${acT}`)) ?? TYPE_NAMES[acT] ?? "";

  // Stock-photo join: per-hex override first, then generic type stock (needs the
  // resolved type). Two fast KV reads worst case; a cold meta miss (no type yet)
  // still resolves a per-hex override, and the card's warm re-request picks up
  // the type shot. Absent library -> no `p`/`pk` fields (append-only schema).
  //
  // The variant is decided HERE, from the request's own headers, rather than by
  // the device asking for a size. The device just fetches the path it is handed,
  // so a firmware that cannot draw a square can never be handed one -- see
  // squareSizeFor(), which defaults to the legacy rectangle for anything it does
  // not positively recognise.
  const square = squareSizeFor(request.headers.get("X-Blip-FW"), request.headers.get("X-Blip-Model"));
  const photo = await resolvePhoto(env, hex, acT, square);

  // Military floor, applied at serve time so cached pre-floor entries get it
  // too: when the operator resolved empty, fill from (most-specific first)
  // the broadcast callsign's military designator (P3: RCH proves Air Mobility
  // Command), then the hex allocation table (nationally attributed), then
  // dbFlags as the catch-all generic. Never guesses types or registrations.
  let op = acMeta?.op ?? "";
  if (!op) {
    // A CURATED operator outranks every floor below it: "United States Navy" from
    // plane-alert-db is a fact somebody recorded about this airframe, where
    // militaryOperator() only knows what the ADDRESS BLOCK proves ("US military").
    // Both are truthful; this one is more specific, so it goes first.
    op =
      acOpSide ||
      militaryCallsignOperator(cs) ||
      militaryOperator(hex) ||
      (acMeta?.mil ? "Military" : "");
  }

  // Report the ROOT gap only (a missing type makes name/photo unanswerable), so a
  // work list built from these points is actionable rather than triple-counting one
  // unknown airframe. Only for lookups that actually resolved something or that the
  // upstream chain answered -- a chain outage is an outage, not a library gap.
  if (metaOutcome !== "deadline") {
    if (!acT) recordEnrichGap(env, "type", "", hex);
    else if (!acTn) recordEnrichGap(env, "name", acT, hex);
    else if (!photo) recordEnrichGap(env, "photo", acT, hex);
  }

  const body: Record<string, string | number> = {
    v: SCHEMA_V,
    r: acR,
    t: acT,
    tn: acTn,
    op,
    o: route.o,
    d: route.d,
  };
  if (photo) {
    // The photo path is SERVER-SUPPLIED: firmware treats it as an opaque string
    // and concatenates it onto its cloud base, so changing it here moves the
    // whole fleet -- including devices that will never take another OTA -- the
    // instant this deploys, with no firmware change at all. The Worker deploy is
    // atomic, so the new /api/v1/blipscope/photo route exists before any device
    // can be handed this string.
    //
    // The /api/v1/blipscope/photo alias is kept anyway, for URLs already cached on a device
    // across the deploy moment, and so the deprecation story is the same for all
    // six endpoints rather than five-plus-a-special-case.
    body.p = `/api/v1/blipscope/photo/${photo.key}`;
    body.pk = photo.kind; // "hex" (per-airframe) | "type" (generic -> "representative photo")
  }
  return jsonResponse(body);
}
