// Workers Rate Limiting binding (beta; not in @cloudflare/workers-types yet).
export interface RateLimit {
  limit(options: { key: string }): Promise<{ success: boolean }>;
}

export interface Env {
  ENRICH_KV: KVNamespace;
  METRICS?: AnalyticsEngineDataset;

  // Optional: absent in `wrangler dev`/tests (miniflare can't emulate the beta
  // binding), enforced in staging/production.
  RL_IP?: RateLimit;
  RL_KEY?: RateLimit;

  // Secrets.
  BLIP_KEYS?: string;        // comma-separated accepted (shared) device keys (rotation = old+new)
  DEVICE_KEY_SECRET?: string; // HMAC secret for per-device keys (additive; shared keys still work)
  ADSB_LOL_API_KEY?: string; // optional feeder key, once adsb.lol issues them
  RELAY_KEY?: string;        // X-Relay-Key: authenticates the Worker to our egress relay (see relay/)

  // Vars.
  UPSTREAM_ADSB_LOL_BASE?: string;          // relay-a base URL; unset -> api.adsb.lol direct (dev/test)
  UPSTREAM_ADSB_LOL_BASE_B?: string;        // relay-b base URL; unset -> the secondary feed is disabled
  UPSTREAM_ADSB_FI_ENABLED?: string;        // "true" once commercial permission clears
  UPSTREAM_AIRPLANES_LIVE_ENABLED?: string; // DEAD no-op: airplanes.live is prohibited by operator (see airplanes_live.ts)
  ROUTE_ADSBDB_ENABLED?: string;            // default on; "false" once adsb.lol routeset recovers

  // Tunables with baked defaults; overridable per-env (and by tests).
  UPSTREAM_TIMEOUT_MS?: string;       // default 8000 (bounds background fetches)
  UPSTREAM_RETRY_DELAY_MS?: string;   // default 400 (pause before the single 429/5xx retry)
  BLIPS_FRESH_TTL_MS?: string;        // default 3000
  BLIPS_SERVE_DEADLINE_MS?: string;   // default 3500 (cold-miss wait cap; see blips.ts)
  BLIPS_STALE_SERVE_MS?: string;      // default 12000 (in-band refresh past this age; see blips.ts)
  ENRICH_SERVE_DEADLINE_MS?: string;  // default 2500 (cold-lookup wait cap; see enrich.ts)
}
