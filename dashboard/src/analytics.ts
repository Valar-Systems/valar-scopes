import type { DeviceRow, Env } from "./types";

// Analytics Engine reads.
//
// THE SAMPLING RULE, which every query here obeys: the device Worker samples
// successful cache HIT/STALE points 1:10 and carries the correction in double4.
// So a count is always SUM(double4) and NEVER count(*). Getting this wrong
// under-reports the busiest devices by 10x -- the ones you would most want to
// look at -- so it is asserted in the tests rather than left as a comment.
//
// WHAT THESE NUMBERS CAN AND CANNOT TELL YOU. A device polls on a timer whether
// or not anyone is in the room, so `requests` measures UPTIME, not engagement.
// The one unambiguous interaction signal on the wire today is /v1/photo: the
// firmware fetches a photo exactly once per aircraft when a DETAIL CARD IS
// OPENED, which only happens on a tap. That is what `cards` counts. /v1/enrich
// is background work the device does on its own and is NOT interaction.
//
// The poll cadence is itself touch-derived (the device polls fast for 10 minutes
// after a touch, slow otherwise), so request RATE does carry an engagement
// signal -- but reading it back out requires knowing each model's three
// cadences, and a derived number that looks precise and isn't is worse than an
// honest one. `cards` is reported instead.

const SQL_ENDPOINT = (accountId: string) =>
  `https://api.cloudflare.com/client/v4/accounts/${accountId}/analytics_engine/sql`;

export interface SqlResult<T> {
  data: T[];
  rows: number;
}

export async function runSql<T>(env: Env, sql: string): Promise<SqlResult<T>> {
  if (!env.CF_ACCOUNT_ID || !env.CF_API_TOKEN) throw new Error("analytics not configured");
  const res = await fetch(SQL_ENDPOINT(env.CF_ACCOUNT_ID), {
    method: "POST",
    headers: {
      Authorization: `Bearer ${env.CF_API_TOKEN}`,
      "Content-Type": "text/plain",
    },
    body: sql,
  });
  if (!res.ok) {
    const detail = (await res.text()).slice(0, 400);
    throw new Error(`analytics query failed (${res.status}): ${detail}`);
  }
  return (await res.json()) as SqlResult<T>;
}

function dataset(env: Env): string {
  // Identifier position in the SQL, so it is validated rather than interpolated
  // blind. Only a dataset name can reach the query text.
  const d = (env.AE_DATASET ?? "blipscope_proxy").trim();
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(d)) throw new Error("bad dataset name");
  return d;
}

// Hours are clamped so a hand-edited URL cannot ask for a year of scan.
export function clampHours(raw: string | null): number {
  const n = Number(raw ?? 24);
  if (!Number.isFinite(n)) return 24;
  return Math.min(720, Math.max(1, Math.floor(n)));
}

interface RawDeviceRow {
  dev: string;
  model: string;
  fw: string;
  requests: number | string;
  errors: number | string;
  cards: number | string;
  enriches: number | string;
  stale_served: number | string;
  last_seen: string;
}

const num = (v: number | string | null | undefined): number => {
  const n = typeof v === "number" ? v : Number(v ?? 0);
  return Number.isFinite(n) ? Math.round(n) : 0;
};

// ENDPOINT FILTERS MATCH TWO PATH FAMILIES, on purpose. Blipscope's APIs moved
// to the edition-namespaced /api/v1/blipscope/... convention (see
// docs/web-url-convention.md); the old /v1/... paths remain as aliases until the
// fleet has updated. Analytics Engine points are RETAINED, not rewritten, so any
// window spanning the cutover legitimately holds both shapes -- and a device on
// old firmware keeps producing the old one long after. Filtering on the new path
// alone would silently read 0 for cards and enriches, which looks exactly like a
// fleet that stopped fetching photos rather than a query that stopped matching.
// Drop the legacy strings only once /v1/* hit-rate has been zero for a full
// fleet-update cycle (that count is the deprecation instrument -- see
// proxy/src/metrics.ts, which deliberately keeps the two as separate templates).

// One row per device that has checked in inside the window.
export async function fleetRows(env: Env, hours: number): Promise<DeviceRow[]> {
  const ds = dataset(env);
  const sql = `
    SELECT
      blob5 AS dev,
      argMax(blob4, timestamp) AS model,
      argMax(blob6, timestamp) AS fw,
      SUM(double4) AS requests,
      SUM(IF(double1 >= 400, double4, 0)) AS errors,
      SUM(IF(blob1 IN ('/api/v1/blipscope/photo', '/v1/photo'), double4, 0)) AS cards,
      SUM(IF(blob1 IN ('/api/v1/blipscope/enrich', '/v1/enrich'), double4, 0)) AS enriches,
      SUM(IF(blob2 = 'STALE', double4, 0)) AS stale_served,
      MAX(timestamp) AS last_seen
    FROM ${ds}
    WHERE timestamp > NOW() - INTERVAL '${hours}' HOUR
      AND blob5 != ''
    GROUP BY dev
    ORDER BY requests DESC
    LIMIT 500`;
  const out = await runSql<RawDeviceRow>(env, sql);
  return out.data.map((r) => ({
    dev: r.dev,
    model: r.model || "?",
    fw: r.fw || "?",
    requests: num(r.requests),
    errors: num(r.errors),
    cards: num(r.cards),
    enriches: num(r.enriches),
    staleServed: num(r.stale_served),
    lastSeen: r.last_seen,
    revoked: false, // filled in by the caller from the denylist
  }));
}

export interface FleetTotals {
  devices: number;
  requests: number;
  errors: number;
  cards: number;
  unattributed: number;
}

export async function fleetTotals(env: Env, hours: number): Promise<FleetTotals> {
  const ds = dataset(env);
  const sql = `
    SELECT
      uniq(IF(blob5 != '', blob5, NULL)) AS devices,
      SUM(double4) AS requests,
      SUM(IF(double1 >= 400, double4, 0)) AS errors,
      SUM(IF(blob1 IN ('/api/v1/blipscope/photo', '/v1/photo'), double4, 0)) AS cards,
      SUM(IF(blob5 = '', double4, 0)) AS unattributed
    FROM ${ds}
    WHERE timestamp > NOW() - INTERVAL '${hours}' HOUR`;
  const out = await runSql<Record<string, number | string>>(env, sql);
  const r = out.data[0] ?? {};
  return {
    devices: num(r.devices),
    requests: num(r.requests),
    errors: num(r.errors),
    cards: num(r.cards),
    unattributed: num(r.unattributed),
  };
}

export interface FwRow {
  fw: string;
  model: string;
  devices: number;
}

// The rollout view: who is on which firmware. This is the query that answers
// "did that OTA actually land?" without asking anyone to plug in a cable.
export async function firmwareSpread(env: Env, hours: number): Promise<FwRow[]> {
  const ds = dataset(env);
  const sql = `
    SELECT blob6 AS fw, blob4 AS model, uniq(blob5) AS devices
    FROM ${ds}
    WHERE timestamp > NOW() - INTERVAL '${hours}' HOUR AND blob5 != ''
    GROUP BY fw, model
    ORDER BY model, fw`;
  const out = await runSql<{ fw: string; model: string; devices: number | string }>(env, sql);
  return out.data.map((r) => ({ fw: r.fw || "?", model: r.model || "?", devices: num(r.devices) }));
}

export interface OtaRow {
  dev: string;
  model: string;
  result: string;
  fwFrom: number;
  fwTo: number;
  when: string;
}

// OTA outcomes, newest first. A `fail-*` row names the exact unit to go and look
// at, which is the whole reason the device dimension was added to these points.
export async function otaOutcomes(env: Env, hours: number): Promise<OtaRow[]> {
  const ds = dataset(env);
  const sql = `
    SELECT blob4 AS dev, blob3 AS model, blob2 AS result,
           double1 AS fw_from, double2 AS fw_to, timestamp AS when
    FROM ${ds}
    WHERE timestamp > NOW() - INTERVAL '${hours}' HOUR AND blob1 = 'ota'
    ORDER BY when DESC
    LIMIT 100`;
  const out = await runSql<{
    dev: string; model: string; result: string;
    fw_from: number | string; fw_to: number | string; when: string;
  }>(env, sql);
  return out.data.map((r) => ({
    dev: r.dev || "(unattributed)",
    model: r.model || "?",
    result: r.result,
    fwFrom: num(r.fw_from),
    fwTo: num(r.fw_to),
    when: r.when,
  }));
}

// What the fleet looked up and we could not answer. Already documented in the
// proxy README as the enrichment backlog; surfaced here so it is a page rather
// than a SQL snippet someone has to remember.
export async function enrichGaps(env: Env, hours: number): Promise<{ gap: string; type: string; lookups: number }[]> {
  const ds = dataset(env);
  const sql = `
    SELECT blob2 AS gap, blob3 AS type, SUM(_sample_interval) AS lookups
    FROM ${ds}
    WHERE timestamp > NOW() - INTERVAL '${hours}' HOUR AND blob1 = 'enrich_gap'
    GROUP BY gap, type
    ORDER BY lookups DESC
    LIMIT 40`;
  const out = await runSql<{ gap: string; type: string; lookups: number | string }>(env, sql);
  return out.data.map((r) => ({ gap: r.gap, type: r.type || "(none)", lookups: num(r.lookups) }));
}
