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
  // ...but never sooner than this. The floor is what stops a fast poll from being a
  // hair trigger: staleFactor x interval measures how often the DEVICE asks, while the
  // freshness on offer is set by OUR tile TTL. At the 2 s active cadence s3-146/s3-21
  // ship with, staleFactor alone demands data 6 s old against a tile TTL that has never
  // been under 8 s -- those boards can sit amber on a healthy feed. See
  // AircraftManager::IsDataStale for the full arithmetic.
  //
  // KEEP THIS ABOVE THE RELAY TILE TTL + LAG. Worst case at a 30 s TTL is ~39 s
  // (30 TTL + ~1 relay fetch + 3 Worker fresh window + one poll interval). 45 s clears
  // it, and is exactly the current idle-tier threshold (3 x 15 s), so nothing else
  // moves. Tunable from KV (`cfg:fleet`) with no deploy and no OTA if a soak at 30 s
  // shows it flapping.
  minStaleMs: number;
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
  minStaleMs: 45000,
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
// deploy: { rev, minFw, defaults: {...}, models: { "<slug>": {...} }, devices: { "<id>": {...} } }.
// Precedence (low -> high): BASE < baked model < KV defaults < KV model < KV device.
interface FleetConfig {
  rev?: number;
  minFw?: string;
  defaults?: Partial<ModelConfig>;
  models?: Record<string, Partial<ModelConfig>>;
  // The per-device layer (Daniel, 2026-11-05; docs/bend-site-feed.md, "The feeder's night cadence"): one entry for the
  // Bend feeder, its day interval at night. Every device not listed is unchanged; deleting the entry undoes it.
  devices?: Record<string, Partial<ModelConfig>>;
}

const isPlainObject = (x: unknown): x is Record<string, unknown> =>
  typeof x === "object" && x !== null && !Array.isArray(x);

// `deviceId` is the VERIFIED, lowercased identity from the auth gate (index.ts authenticate), never the raw header:
// /config is reached only past that gate.
export async function handleConfig(request: Request, env: Env, deviceId: string): Promise<Response> {
  const model = (request.headers.get("X-Blip-Model") ?? "").trim();
  let fleet: FleetConfig = {};
  try {
    fleet = (await env.ENRICH_KV.get<FleetConfig>("cfg:fleet", "json")) ?? {};
  } catch {
    // malformed KV JSON must not brick config: serve baked defaults
  }
  // A `devices` or an entry that is not a plain object is ignored: the device then gets exactly what it gets without it.
  const devices: unknown = isPlainObject(fleet) ? fleet.devices : undefined;
  const entry: unknown = isPlainObject(devices) ? devices[deviceId] : undefined;
  const device: Partial<ModelConfig> = isPlainObject(entry) ? (entry as Partial<ModelConfig>) : {};

  const resolved: ModelConfig = {
    ...BASE,
    ...(MODEL_DEFAULTS[model] ?? {}),
    ...(fleet.defaults ?? {}),
    ...(fleet.models?.[model] ?? {}),
    ...device,
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
    minStaleMs: resolved.minStaleMs,
    upstreamState: upstreamOverallState(enabledFeeds(env).map((f) => f.id)),
  });
}
