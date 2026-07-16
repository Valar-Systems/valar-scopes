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

// X-Blip-OTA-Mem: "<fwFrom>,<fwTo>,<preLargest>,<postLargest>,<result>", sent by
// a device on its first check-in after an update attempt (OtaUpdater.h's
// TakeOtaMemReport) and never again for that attempt.
//
// It answers the one question the bench gate could not: an OTA is easy to prove
// at a freshly-booted heap, but devices update from a *fragmented* one. Only the
// fleet reaches that state honestly, so the numbers ride a request the device was
// already making.
//
// Deliberately NOT console.logged: it lands as an Analytics Engine point only, so
// request logs stay exactly the size they were (they are real money at fleet
// scale -- see the README cost model). And it is device-supplied input, so it is
// validated to a fixed shape before it reaches storage; anything off-shape is
// dropped silently rather than allowed to shape a data point.
export function recordOtaMem(env: Env, raw: string | null, model: string): void {
  if (!raw) return;
  try {
    if (raw.length > 128) return; // nothing legitimate approaches this
    const parts = raw.split(",");
    if (parts.length !== 5) return;
    const nums = parts.slice(0, 4).map((p) => Number(p.trim()));
    // Reject NaN/Infinity/negatives and anything past a u32: these are a version
    // int and two heap sizes, all small and non-negative by construction.
    if (nums.some((n) => !Number.isFinite(n) || n < 0 || n > 0xffffffff)) return;
    const result = (parts[4] ?? "").trim().slice(0, 24).replace(/[^\w.-]/g, "");
    if (!result) return;
    const [fwFrom, fwTo, preLargest, postLargest] = nums as [number, number, number, number];
    env.METRICS?.writeDataPoint({
      blobs: ["ota", result, model],
      doubles: [fwFrom, fwTo, preLargest, postLargest],
      indexes: ["ota"], // own index: OTA points query separately from per-request ones
    });
  } catch {
    // never let telemetry break serving
  }
}
