import type { Env } from "./types";
import type { RequestMetric } from "./metrics";
import { SCHEMA_V } from "./schema";
import { fetchHexChain, fetchRoute } from "./upstreams/chain";
import { TYPE_NAMES } from "./typenames";
import { errorResponse, intEnv, jsonResponse } from "./util";

// Cold-lookup serve deadline, same philosophy as the blips one: under a 429
// storm the upstream chain can burn many seconds in retries, and the firmware's
// detail card fills progressively and re-requests anyway. Answer with whatever
// resolved in time (uncached fields come back empty) and let the chain finish
// in the background -- its KV puts make the card's next request a warm hit.
const DEFAULT_ENRICH_SERVE_DEADLINE_MS = 2500;

// KV TTLs. Aircraft metadata is near-static (30 d); unknown aircraft get a day
// so a fresh registration isn't blanked for a month. Routes are per-callsign
// schedules (24 h), definitive negatives included -- a callsign that resolved
// unknown (or implausible for the given position) stays unknown for the day.
// Non-definitive outcomes (outages, empty routeset bodies) are never cached.
const AC_TTL_S = 30 * 86400;
const AC_NEG_TTL_S = 86400;
const RT_TTL_S = 86400;

interface AcMeta {
  found: boolean;
  r: string; // registration
  t: string; // ICAO type designator
  tn: string; // friendly type name
  op: string; // operator / registered owner
}

interface RouteEntry {
  o: string;
  d: string;
}

function str(v: unknown): string {
  return typeof v === "string" ? v.trim() : "";
}

async function buildMeta(env: Env, raw: unknown): Promise<AcMeta> {
  if (raw === null || typeof raw !== "object") return { found: false, r: "", t: "", tn: "", op: "" };
  const r = raw as Record<string, unknown>;
  const reg = str(r.r);
  const type = str(r.t).toUpperCase();
  const op = str(r.ownOp);
  // Friendly name: the upstream's own description when its DB carries one, else
  // the KV override table (`tn:<CODE>`, updatable without a deploy), else the
  // baked common-types table, else empty.
  let tn = str(r.desc);
  if (!tn && type) {
    tn = (await env.ENRICH_KV.get(`tn:${type}`)) ?? TYPE_NAMES[type] ?? "";
  }
  return { found: true, r: reg, t: type, tn, op };
}

// Aircraft metadata: KV, else one upstream hex lookup, then KV for next time.
async function resolveMeta(env: Env, hex: string, meta: RequestMetric): Promise<AcMeta | null> {
  const cached = await env.ENRICH_KV.get<AcMeta>(`ac:${hex}`, "json");
  if (cached) {
    meta.cache = "HIT";
    return cached;
  }
  meta.cache = "MISS";
  const started = Date.now();
  const res = await fetchHexChain(env, hex);
  meta.upstreamMs = Date.now() - started;
  if (!res) return null; // chain down: serve empties now, cache nothing, next tap retries
  meta.upstream = res.upstream;
  const built = await buildMeta(env, res.raw);
  await env.ENRICH_KV.put(`ac:${hex}`, JSON.stringify(built), {
    expirationTtl: built.found ? AC_TTL_S : AC_NEG_TTL_S,
  });
  return built;
}

// Route: KV, else the route-source chain (adsb.lol routeset, then adsbdb).
async function resolveRoute(
  env: Env,
  cs: string,
  lat: number | undefined,
  lon: number | undefined,
): Promise<RouteEntry> {
  if (!cs) return { o: "", d: "" };
  const cached = await env.ENRICH_KV.get<RouteEntry>(`rt:${cs}`, "json");
  if (cached) return cached;

  const r = await fetchRoute(env, cs, lat, lon);
  if (!r) return { o: "", d: "" };
  // With a live position we trust the plausibility check (callsigns get reused
  // across legs): an implausible route is worse than none on the card.
  const usable = lat !== undefined && lon !== undefined ? r.plausible : true;
  const route = usable ? { o: r.o, d: r.d } : { o: "", d: "" };
  if (r.definitive) {
    await env.ENRICH_KV.put(`rt:${cs}`, JSON.stringify(route), { expirationTtl: RT_TTL_S });
  }
  return route;
}

function maybeFloat(v: string | null): number | undefined {
  if (v === null) return undefined;
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : undefined;
}

export async function handleEnrich(
  request: Request,
  env: Env,
  ctx: ExecutionContext,
  hexRaw: string,
  meta: RequestMetric,
): Promise<Response> {
  const hex = hexRaw.toLowerCase();
  // 6 hex digits, optionally readsb's "~" prefix for non-ICAO (TIS-B) addresses.
  if (!/^~?[0-9a-f]{6}$/.test(hex)) return errorResponse(400, "bad_hex");

  const url = new URL(request.url);
  const csRaw = (url.searchParams.get("cs") ?? "").trim().toUpperCase();
  const cs = /^[A-Z0-9]{2,8}$/.test(csRaw) ? csRaw : "";
  // Optional live position: feeds the route plausibility check.
  const lat = maybeFloat(url.searchParams.get("lat"));
  const lon = maybeFloat(url.searchParams.get("lon"));

  // The two lookups are independent; run them concurrently to keep tap->card
  // latency down (the firmware budget is sub-second on the warm path). Each
  // races the serve deadline INDIVIDUALLY, so a fast KV hit on one side isn't
  // held hostage by a slow upstream chain on the other -- and losers keep
  // running via waitUntil below, caching for the card's next request.
  const deadlineMs = intEnv(env.ENRICH_SERVE_DEADLINE_MS, DEFAULT_ENRICH_SERVE_DEADLINE_MS);
  const deadline = new Promise<"deadline">((resolve) =>
    setTimeout(() => resolve("deadline"), deadlineMs),
  );
  const metaPromise = resolveMeta(env, hex, meta);
  const routePromise = resolveRoute(env, cs, lat, lon);
  const [metaOutcome, routeOutcome] = await Promise.all([
    Promise.race([metaPromise, deadline]),
    Promise.race([routePromise, deadline]),
  ]);

  const acMeta = metaOutcome === "deadline" ? null : metaOutcome;
  const route = routeOutcome === "deadline" ? { o: "", d: "" } : routeOutcome;
  if (metaOutcome === "deadline" || routeOutcome === "deadline") {
    ctx.waitUntil(Promise.allSettled([metaPromise, routePromise]).then(() => {}));
  }

  return jsonResponse({
    v: SCHEMA_V,
    r: acMeta?.r ?? "",
    t: acMeta?.t ?? "",
    tn: acMeta?.tn ?? "",
    op: acMeta?.op ?? "",
    o: route.o,
    d: route.d,
  });
}
