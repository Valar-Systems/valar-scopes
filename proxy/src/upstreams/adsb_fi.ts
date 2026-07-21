import { USER_AGENT, type UpstreamAircraftFeed } from "./types";

// DISABLED pending commercial permission (email out to the operators). The
// adapter ships so failover is one env-var flip away; until then adsb.lol
// outages are covered by stale-while-revalidate + the device stale indicator.
const BASE = "https://opendata.adsb.fi/api";

export const adsbFi: UpstreamAircraftFeed = {
  id: "adsb_fi",
  enabled: (env) => env.UPSTREAM_ADSB_FI_ENABLED === "true",
  pointUrl: (_env, lat, lon, distNm) => `${BASE}/v2/lat/${lat}/lon/${lon}/dist/${distNm}`,
  hexUrl: (_env, hex) => `${BASE}/v2/icao/${hex}`, // adsb.fi names the hex lookup /v2/icao
  headers: () => ({ "User-Agent": USER_AGENT }),
};
