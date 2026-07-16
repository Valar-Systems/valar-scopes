import { USER_AGENT, type UpstreamAircraftFeed } from "./types";

// DISABLED pending commercial permission (email out to the operators). Same
// story as adsb.fi: ships dark, enabled by env-var flip when permission clears.
const BASE = "https://api.airplanes.live";

export const airplanesLive: UpstreamAircraftFeed = {
  id: "airplanes_live",
  enabled: (env) => env.UPSTREAM_AIRPLANES_LIVE_ENABLED === "true",
  pointUrl: (lat, lon, distNm) => `${BASE}/v2/point/${lat}/${lon}/${distNm}`,
  hexUrl: (hex) => `${BASE}/v2/hex/${hex}`,
  headers: () => ({ "User-Agent": USER_AGENT }),
};
