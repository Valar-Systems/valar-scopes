import type { Env } from "../types";
import { relayHeaders } from "./adsb_lol";
import { USER_AGENT, type UpstreamAircraftFeed } from "./types";

// adsb.fi -- THE CHAIN PRIMARY for positions and hex since 2026-07-31. Enabled in
// staging and production; see "Upstream licensing posture" in README.md and the
// SHIPPING ORDER block in chain.ts.
//
// PERMITTED COMMERCIALLY, IN WRITING (2026-08-05). Samuli granted use for our
// stated purpose -- a paid hardware product -- "including the caching system",
// conditional on staying within the Open Data API rate limit AND NOTHING ELSE.
// So the operative constraint on this source is the 1 req/s per-IP budget below,
// not a licence question.
//
// THIS COMMENT SAID THE OPPOSITE TWICE, and both times for the same reason, which
// is why the correction is recorded rather than just applied:
//   1. "adsb.fi 403s us" -- not adsb.fi policy at all. It was Cloudflare's shared
//      per-colo egress. Both relay IPs get HTTP 200 unauthenticated.
//   2. "personal, non-commercial use only ... no redistribution right" -- a
//      reading of their PUBLISHED TERMS while a written grant to us specifically
//      already existed in the thread. The published terms are the default; our
//      correspondence is the licence.
// Both readings were of a public page rather than of our own correspondence. If
// this looks wrong again, read the thread before the terms.
//
// Reached through OUR relays under the /fi prefix (the relay rewrites /fi/* ->
// opendata.adsb.fi/api/*), so adsb.fi sees one stable dedicated IP per relay and
// gets the same request-collapsing + 429 hold-down courtesy adsb.lol gets. Their
// public limit is 1 req/s per IP AND 4xx/429s count toward it, so never re-firing
// a 429 is mandatory here, not merely polite.
//
// Base URLs are per-env vars, mirroring adsb_lol.ts. Unset -> the primary falls
// back to opendata.adsb.fi DIRECT (dev/test only) and the secondary is disabled.
const DIRECT = "https://opendata.adsb.fi/api";
const baseA = (env: Env): string => env.UPSTREAM_ADSB_FI_BASE || DIRECT;
const baseB = (env: Env): string => env.UPSTREAM_ADSB_FI_BASE_B || "";

// One adapter shape, two instances (relay-a / relay-b) differing only by base URL
// and id -- same pattern as adsb_lol.ts, so each gets its own circuit breaker.
// BOTH gate on UPSTREAM_ADSB_FI_ENABLED: a source is a source regardless of which
// relay carries it, so there is exactly one switch for the whole thing. It stays
// default-OFF (dev/test fall back to the direct URL) -- not because of any licence
// doubt, but because an upstream that reaches a third party should never turn
// itself on from an unset variable.
function makeFeed(id: string, base: (env: Env) => string): UpstreamAircraftFeed {
  return {
    id,
    enabled: (env) => env.UPSTREAM_ADSB_FI_ENABLED === "true" && base(env).length > 0,
    // POSITIONS: /v3, NOT /v2 -- and the /v2 failure mode is SILENT, which is why
    // this is pinned by a test (test/upstreams.test.ts). adsb.fi's deprecated
    // /v2/lat/lon/dist still returns HTTP 200 and valid JSON, but in the OLD
    // aircraft.json schema: `{now: <epoch SECONDS>, aircraft: [...]}`. Our chain
    // reads `json.ac`, which is simply absent there -- so /v2 yields an empty
    // array and the feed reports ZERO AIRCRAFT with no error anywhere. A 500
    // would have been kinder. /v3 returns the readsb-standard {ac, now} shape
    // (now = epoch ms) that upstreamNowMs() already handles, and caps distance at
    // 250 NM (our largest R_BUCKETS_KM bucket is 160 km ~= 86 NM, never in play).
    pointUrl: (env, lat, lon, distNm) => `${base(env)}/v3/lat/${lat}/lon/${lon}/dist/${distNm}`,
    // Hex is unversioned-stable: /v2/hex and /v2/icao are the same endpoint. Use
    // /v2/hex so the path matches adsb.lol's and the relay's hex location block.
    // (/v2/icao additionally accepts comma-separated hexes -- a possible batched
    // enrichment win if this source ever clears licensing.)
    hexUrl: (env, hex) => `${base(env)}/v2/hex/${hex}`,
    headers: (env) => ({ "User-Agent": USER_AGENT, ...relayHeaders(env) }),
  };
}

export const adsbFi = makeFeed("adsb_fi", baseA); // via relay-a (or direct when unset)
export const adsbFiB = makeFeed("adsb_fi_b", baseB); // via relay-b (disabled unless set)
