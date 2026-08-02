export interface Env {
  // Cloudflare Access. Both are required; with either missing the dashboard
  // refuses every request rather than serving unauthenticated (see access.ts).
  ACCESS_TEAM_DOMAIN?: string; // e.g. "valarsystems.cloudflareaccess.com"
  ACCESS_AUD?: string; // the Access application's Audience tag
  ACCESS_ALLOWED_EMAILS?: string; // optional belt-and-braces allowlist, comma separated

  // Analytics Engine SQL API. The token needs Account Analytics: Read and
  // NOTHING else -- it is a read credential for telemetry, not an account key.
  CF_ACCOUNT_ID?: string;
  CF_API_TOKEN?: string; // secret
  AE_DATASET?: string; // defaults to blipscope_proxy

  // The SAME namespace the device Worker binds as ENRICH_KV. The dashboard reads
  // leaderboard rows for device names and reads/writes the cfg:revoked entry.
  ENRICH_KV: KVNamespace;
}

// One row of the fleet table.
export interface DeviceRow {
  dev: string;
  model: string;
  fw: string;
  requests: number;
  errors: number;
  cards: number; // /v1/photo -- a detail card opened. See analytics.ts.
  enriches: number;
  staleServed: number;
  lastSeen: string;
  revoked: boolean;
  name?: string; // from the leaderboard row, when the device has opted in
}
