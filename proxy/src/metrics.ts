import type { Env } from "./types";

export interface RequestMetric {
  ep: string;
  status: number;
  ms: number;
  model: string;
  colo: string;
  cache?: string; // HIT | STALE | MISS (blips), HIT | MISS (enrich KV)
  upstream?: string;
  upstreamMs?: number;
}

// Cache hits dominate traffic and each Analytics Engine data point is billed:
// sample the boring class (blips HIT/STALE) 1:10 and carry the weight in a
// double so dashboards multiply it back. Misses, errors, and everything with an
// upstream call stay 1:1 -- those are the points worth full resolution.
const HIT_SAMPLE_RATE = 0.1;

// One structured log line per request, plus an Analytics Engine data point.
// Metrics must never fail a request.
export function record(env: Env, m: RequestMetric): void {
  console.log(JSON.stringify({ evt: "req", ...m }));
  try {
    const sampled = m.status < 400 && (m.cache === "HIT" || m.cache === "STALE");
    if (sampled && Math.random() >= HIT_SAMPLE_RATE) return;
    env.METRICS?.writeDataPoint({
      blobs: [m.ep, m.cache ?? "", m.upstream ?? "", m.model],
      doubles: [m.status, m.ms, m.upstreamMs ?? 0, sampled ? 1 / HIT_SAMPLE_RATE : 1],
      indexes: [m.ep],
    });
  } catch {
    // never let telemetry break serving
  }
}
