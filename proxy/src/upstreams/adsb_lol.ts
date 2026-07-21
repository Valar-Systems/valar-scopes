import type { Env } from "../types";
import { USER_AGENT, type UpstreamAircraftFeed } from "./types";

// PRIMARY upstream. adsb.lol data is licensed ODbL 1.0 -- explicitly compatible
// with commercial use, with attribution (see README "Data sources & licensing";
// the device config page carries the credit line too).
const BASE = "https://api.adsb.lol";

// ---------------------------------------------------------------------------
// Feeder-key authentication -- SCHEME IS UNVERIFIED
//
// adsb.lol has published NO feeder-key specification. A key was requested for
// station `valar-systems-bend`; until they reply, everything below is an
// ASSUMPTION and must be treated as such -- do not let the confident-looking
// header name mislead a future reader into thinking it was confirmed.
//
// When their spec arrives, changing AUTH_SCHEME (and, if needed, the matching
// name constant) is the WHOLE change -- every adsb.lol request routes through
// authHeaders()/authQuery() below, so no call site moves:
//
//   "header" -> X-Api-Key: <key>            (current assumption)
//   "bearer" -> Authorization: Bearer <key>
//   "query"  -> ...?api_key=<key>           (why the URL builders take env)
//
// Applying the key itself stays `wrangler secret put ADSB_LOL_API_KEY`; nothing
// here needs a code change unless the SCHEME differs from the guess.
// ---------------------------------------------------------------------------
type AuthScheme = "header" | "bearer" | "query";

/** THE one line to change when adsb.lol publishes their spec. */
const AUTH_SCHEME: AuthScheme = "header";

const AUTH_HEADER_NAME = "X-Api-Key"; // used when AUTH_SCHEME === "header"
const AUTH_QUERY_PARAM = "api_key";   // used when AUTH_SCHEME === "query"

/** Auth headers for the configured scheme; empty when no key is set. */
function authHeaders(env: Env): Record<string, string> {
  const key = env.ADSB_LOL_API_KEY;
  if (!key) return {};
  switch (AUTH_SCHEME) {
    case "header": return { [AUTH_HEADER_NAME]: key };
    case "bearer": return { Authorization: `Bearer ${key}` };
    case "query":  return {}; // carried on the URL instead
  }
}

/** Query suffix ("?api_key=...") for the query scheme; "" otherwise. */
function authQuery(env: Env): string {
  const key = env.ADSB_LOL_API_KEY;
  if (!key || AUTH_SCHEME !== "query") return "";
  return `?${AUTH_QUERY_PARAM}=${encodeURIComponent(key)}`;
}

export const adsbLol: UpstreamAircraftFeed = {
  id: "adsb_lol",
  enabled: () => true,
  pointUrl: (env, lat, lon, distNm) =>
    `${BASE}/v2/lat/${lat}/lon/${lon}/dist/${distNm}${authQuery(env)}`,
  hexUrl: (env, hex) => `${BASE}/v2/hex/${hex}${authQuery(env)}`,
  headers: (env) => ({
    "User-Agent": USER_AGENT,
    ...authHeaders(env),
  }),
};

// Callsign -> route via adsb.lol's tar1090-style routeset API (their
// vrs-standing-data route DB). adsb.lol-only: the other feeds don't carry
// routes, so when adsb.lol is down routes simply resolve unknown.
export function routesetRequest(
  env: Env,
  callsign: string,
  lat: number | undefined,
  lon: number | undefined,
): { url: string; init: RequestInit } {
  return {
    url: `${BASE}/api/0/routeset${authQuery(env)}`,
    init: {
      method: "POST",
      headers: {
        "User-Agent": USER_AGENT,
        "Content-Type": "application/json",
        ...authHeaders(env),
      },
      // lat/lng feed the API's plausibility check (callsigns get reused across
      // legs); 0,0 when the caller has no position -> plausible stays false.
      body: JSON.stringify({ planes: [{ callsign, lat: lat ?? 0, lng: lon ?? 0 }] }),
    },
  };
}
