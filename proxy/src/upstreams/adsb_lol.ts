import type { Env } from "../types";
import { USER_AGENT, type UpstreamAircraftFeed } from "./types";

// adsb.lol (ODbL 1.0 -- commercial use OK with attribution; see README "Data
// sources & licensing" and the device config-page credit line) reached via our
// DEDICATED-EGRESS RELAY. See relay/.
//
// Why the relay: Cloudflare Workers egress from a shared per-colo IP pool, so
// adsb.lol's anonymous per-IP limiter 429s us for OTHER tenants' traffic. A relay
// on a dedicated IP polls adsb.lol and the Worker consumes from it; adsb.lol then
// only ever sees the relay's one IP. Two relays -- relay-a (primary) and relay-b
// (secondary) -- give HA through the failover chain (relay-b is terminal, so the
// breaker never skips it; see chain.ts).
//
// Base URLs are per-env vars. Unset -> the primary falls back to api.adsb.lol
// DIRECT (dev/test + the pre-relay path) and the secondary feed is simply
// disabled, so nothing changes until the relay URLs are configured.
const DIRECT = "https://api.adsb.lol";
const baseA = (env: Env): string => env.UPSTREAM_ADSB_LOL_BASE || DIRECT;
const baseB = (env: Env): string => env.UPSTREAM_ADSB_LOL_BASE_B || "";

// --- Auth hop 1: Worker -> our relay ----------------------------------------
// X-Relay-Key authenticates the Worker to the relay's nginx (which 403s anything
// else). The relay STRIPS this header before forwarding upstream, so it never
// reaches adsb.lol. Empty on the direct dev path (no relay -> no key needed).
function relayHeaders(env: Env): Record<string, string> {
  return env.RELAY_KEY ? { "X-Relay-Key": env.RELAY_KEY } : {};
}

// --- Auth hop 2: relay -> adsb.lol (feeder key) -- SCHEME UNVERIFIED ---------
// adsb.lol has published NO feeder-key spec (a key was requested for station
// `valar-systems-bend`). With the relay in front this machinery is INERT (no key
// issued, and a real key is better applied AT THE RELAY so it isn't sprayed from
// the Worker) -- but it is harmless (emits nothing when ADSB_LOL_API_KEY is
// unset) and documents the one-line change if the spec ever lands:
//   "header" -> X-Api-Key: <key>            (current assumption)
//   "bearer" -> Authorization: Bearer <key>
//   "query"  -> ...?api_key=<key>           (why the URL builders take env)
type AuthScheme = "header" | "bearer" | "query";

/** THE one line to change when adsb.lol publishes their spec. */
const AUTH_SCHEME: AuthScheme = "header";

const AUTH_HEADER_NAME = "X-Api-Key"; // used when AUTH_SCHEME === "header"
const AUTH_QUERY_PARAM = "api_key";   // used when AUTH_SCHEME === "query"

function authHeaders(env: Env): Record<string, string> {
  const key = env.ADSB_LOL_API_KEY;
  if (!key) return {};
  switch (AUTH_SCHEME) {
    case "header": return { [AUTH_HEADER_NAME]: key };
    case "bearer": return { Authorization: `Bearer ${key}` };
    case "query":  return {}; // carried on the URL instead
  }
}

function authQuery(env: Env): string {
  const key = env.ADSB_LOL_API_KEY;
  if (!key || AUTH_SCHEME !== "query") return "";
  return `?${AUTH_QUERY_PARAM}=${encodeURIComponent(key)}`;
}

// One adapter shape, two instances differing only by base URL + id. Nothing else
// differs: both carry the relay key (hop 1) and the inert feeder key (hop 2), and
// a feed is enabled only when its base URL resolves (primary always does via the
// DIRECT fallback; secondary only when UPSTREAM_ADSB_LOL_BASE_B is set).
function makeFeed(id: string, base: (env: Env) => string): UpstreamAircraftFeed {
  return {
    id,
    enabled: (env) => base(env).length > 0,
    pointUrl: (env, lat, lon, distNm) =>
      `${base(env)}/v2/lat/${lat}/lon/${lon}/dist/${distNm}${authQuery(env)}`,
    hexUrl: (env, hex) => `${base(env)}/v2/hex/${hex}${authQuery(env)}`,
    headers: (env) => ({
      "User-Agent": USER_AGENT,
      ...relayHeaders(env),
      ...authHeaders(env),
    }),
  };
}

export const adsbLol = makeFeed("adsb_lol", baseA); // relay-a (or direct when unset)
export const adsbLolB = makeFeed("adsb_lol_b", baseB); // relay-b (disabled unless set)

// Callsign -> route via adsb.lol's tar1090-style routeset API (their
// vrs-standing-data route DB). adsb.lol-only: the other feeds don't carry routes,
// so when adsb.lol is unreachable routes simply resolve unknown. Routed through
// the PRIMARY relay base (relay-a); routes are a nice-to-have, not worth a second
// relay hop, so relay-b does not carry them.
export function routesetRequest(
  env: Env,
  callsign: string,
  lat: number | undefined,
  lon: number | undefined,
): { url: string; init: RequestInit } {
  return {
    url: `${baseA(env)}/api/0/routeset${authQuery(env)}`,
    init: {
      method: "POST",
      headers: {
        "User-Agent": USER_AGENT,
        "Content-Type": "application/json",
        ...relayHeaders(env),
        ...authHeaders(env),
      },
      // lat/lng feed the API's plausibility check (callsigns get reused across
      // legs); 0,0 when the caller has no position -> plausible stays false.
      body: JSON.stringify({ planes: [{ callsign, lat: lat ?? 0, lng: lon ?? 0 }] }),
    },
  };
}
