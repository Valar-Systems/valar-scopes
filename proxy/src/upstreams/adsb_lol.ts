import type { Env } from "../types";
import { USER_AGENT, type UpstreamAircraftFeed } from "./types";

// PRIMARY upstream. adsb.lol data is licensed ODbL 1.0 -- explicitly compatible
// with commercial use, with attribution (see README "Data sources & licensing";
// the device config page carries the credit line too).
const BASE = "https://api.adsb.lol";

export const adsbLol: UpstreamAircraftFeed = {
  id: "adsb_lol",
  enabled: () => true,
  pointUrl: (lat, lon, distNm) => `${BASE}/v2/lat/${lat}/lon/${lon}/dist/${distNm}`,
  hexUrl: (hex) => `${BASE}/v2/hex/${hex}`,
  headers: (env) => ({
    "User-Agent": USER_AGENT,
    // adsb.lol has signalled keys-for-feeders; send ours once issued (we feed).
    ...(env.ADSB_LOL_API_KEY ? { "X-Api-Key": env.ADSB_LOL_API_KEY } : {}),
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
    url: `${BASE}/api/0/routeset`,
    init: {
      method: "POST",
      headers: {
        "User-Agent": USER_AGENT,
        "Content-Type": "application/json",
        ...(env.ADSB_LOL_API_KEY ? { "X-Api-Key": env.ADSB_LOL_API_KEY } : {}),
      },
      // lat/lng feed the API's plausibility check (callsigns get reused across
      // legs); 0,0 when the caller has no position -> plausible stays false.
      body: JSON.stringify({ planes: [{ callsign, lat: lat ?? 0, lng: lon ?? 0 }] }),
    },
  };
}
