import { USER_AGENT, type UpstreamAircraftFeed } from "./types";

// PROHIBITED BY OPERATOR -- ships PERMANENTLY dark.
// airplanes.live sent a written refusal ("do not use our data"), dated July 2026.
// `enabled()` is hardcoded false so NO env-var flip can turn this on; the
// UPSTREAM_AIRPLANES_LIVE_ENABLED var is now a dead no-op. Do NOT re-enable this,
// and do NOT list airplanes.live as a reserve/failover option anywhere -- the
// data source is off the table, not merely undecided. Kept only so historical
// references still compile. See README "Upstream licensing posture".
const BASE = "https://api.airplanes.live";

export const airplanesLive: UpstreamAircraftFeed = {
  id: "airplanes_live",
  enabled: () => false, // prohibited by operator (written refusal, July 2026) -- never flip on
  pointUrl: (_env, lat, lon, distNm) => `${BASE}/v2/point/${lat}/${lon}/${distNm}`,
  hexUrl: (_env, hex) => `${BASE}/v2/hex/${hex}`,
  headers: () => ({ "User-Agent": USER_AGENT }),
};
