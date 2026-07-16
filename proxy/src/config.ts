import type { Env } from "./types";
import { SCHEMA_V } from "./schema";
import { enabledFeeds } from "./upstreams/chain";
import { upstreamOverallState } from "./upstreams/types";
import { jsonResponse } from "./util";

export type EnrichLevel = "off" | "watchlist" | "full";

export interface ModelConfig {
  pollActiveMs: number;
  pollIdleMs: number;
  pollNightMs: number; // wired to the device's solar-dim state; the fleet's biggest cost lever
  idleAfterMs: number;
  staleFactor: number; // device shows the stale indicator past staleFactor * interval
  enrich: EnrichLevel;
}

// Baked defaults. The device sends X-Blip-Model (variant::SLUG) and gets a flat,
// fully-resolved config back -- the per-model map never leaves the server, and
// the tier gap (S3 polls 2 s, C3 5 s; C3 enriches watchlist-only) is a pure
// server-side knob.
const BASE: ModelConfig = {
  pollActiveMs: 5000,
  pollIdleMs: 15000,
  pollNightMs: 60000,
  idleAfterMs: 600000,
  staleFactor: 3,
  enrich: "full",
};

const MODEL_DEFAULTS: Record<string, Partial<ModelConfig>> = {
  // Single-core, heap-tight: background enrichment only for watchlist hexes;
  // taps enrich on demand.
  "c3-128": { enrich: "watchlist" },
  // Dual-core + PSRAM: near-realtime motion.
  "s3-146": { pollActiveMs: 2000, pollIdleMs: 10000, pollNightMs: 45000, idleAfterMs: 900000 },
  "s3-21": { pollActiveMs: 2000, pollIdleMs: 10000, pollNightMs: 45000, idleAfterMs: 900000 },
};

// Fleet overrides live in KV key `cfg:fleet` -- one `wrangler kv key put`, no
// deploy: { rev, minFw, defaults: {...}, models: { "<slug>": {...} } }.
// Precedence (low -> high): BASE < baked model < KV defaults < KV model.
interface FleetConfig {
  rev?: number;
  minFw?: string;
  defaults?: Partial<ModelConfig>;
  models?: Record<string, Partial<ModelConfig>>;
}

export async function handleConfig(request: Request, env: Env): Promise<Response> {
  const model = (request.headers.get("X-Blip-Model") ?? "").trim();
  let fleet: FleetConfig = {};
  try {
    fleet = (await env.ENRICH_KV.get<FleetConfig>("cfg:fleet", "json")) ?? {};
  } catch {
    // malformed KV JSON must not brick config: serve baked defaults
  }

  const resolved: ModelConfig = {
    ...BASE,
    ...(MODEL_DEFAULTS[model] ?? {}),
    ...(fleet.defaults ?? {}),
    ...(fleet.models?.[model] ?? {}),
  };

  return jsonResponse({
    v: SCHEMA_V,
    rev: fleet.rev ?? 1,
    minFw: fleet.minFw ?? "0.0.0",
    enrich: resolved.enrich,
    pollActiveMs: resolved.pollActiveMs,
    pollIdleMs: resolved.pollIdleMs,
    pollNightMs: resolved.pollNightMs,
    idleAfterMs: resolved.idleAfterMs,
    staleFactor: resolved.staleFactor,
    upstreamState: upstreamOverallState(enabledFeeds(env).map((f) => f.id)),
  });
}
