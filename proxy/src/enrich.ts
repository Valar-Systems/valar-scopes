import type { Env } from "./types";
import { recordEnrichGap, type RequestMetric } from "./metrics";
import { SCHEMA_V } from "./schema";
import { fetchAircraftMetaAdsbdb, fetchHexChain, fetchRoute } from "./upstreams/chain";
import { militaryCallsignOperator, militaryOperator } from "./military";
import { TYPE_NAMES } from "./typenames";
import { resolvePhoto } from "./photos";
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
  mil?: boolean; // upstream dbFlags bit 0 (military); absent on entries cached before it existed
}

interface RouteEntry {
  o: string;
  d: string;
}

// One row of the static military airframe side table (P2), ingested from the
// Mictronics aircraft-database export (ODC-By 1.0) by scripts/ingest-mildb.ts.
// Consulted at serve time only when the live DB record resolved empty, so it
// can never fight fresher upstream data; no TTL (each ingest run overwrites).
interface MilEntry {
  r?: string; // registration / serial
  t?: string; // ICAO type designator
  tn?: string; // friendly type name (the export's description)
}

function str(v: unknown): string {
  return typeof v === "string" ? v.trim() : "";
}

// ICAO type designators are 2-4 chars of [A-Z0-9]. Upstream DBs (Mictronics /
// tar1090, reached via adsb.lol) suffix an UNCONFIRMED type with " ?" -- e.g.
// "P8 ?". Passed through verbatim that breaks the friendly-name lookup and the
// type-keyed stock-photo join (both key on the bare code) and shows an ugly
// "Type: P8 ?" on the card. Keep only the leading alphanumeric run.
function normType(v: unknown): string {
  return str(v).toUpperCase().match(/^[A-Z0-9]+/)?.[0] ?? "";
}

async function buildMeta(env: Env, raw: unknown): Promise<AcMeta> {
  if (raw === null || typeof raw !== "object") return { found: false, r: "", t: "", tn: "", op: "" };
  const r = raw as Record<string, unknown>;
  const reg = str(r.r);
  const type = normType(r.t);
  const op = str(r.ownOp);
  // dbFlags bit 0 = military, when the upstream DB supplies it. Recorded so the
  // serve-time military floor can label hexes outside the static block table.
  const mil = typeof r.dbFlags === "number" && (r.dbFlags & 1) === 1;
  // Friendly name: the upstream's own description when its DB carries one, else
  // the KV override table (`tn:<CODE>`, updatable without a deploy), else the
  // baked common-types table, else empty.
  let tn = str(r.desc);
  if (!tn && type) {
    tn = (await env.ENRICH_KV.get(`tn:${type}`)) ?? TYPE_NAMES[type] ?? "";
  }
  return { found: true, r: reg, t: type, tn, op, mil };
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
  // Type backfill: airplanes.live (our failover when adsb.lol 429s) returns a
  // hex's registration but NOT its ICAO type, so a failover loses the type -- and
  // with it the type name and the type-keyed stock photo. When the feed gave us a
  // hex but no type, ask adsbdb (a different host, not subject to the same 429)
  // for the type. Merged in before caching, so ac:<hex> stores the full record
  // and the card's next request is a warm hit with the photo joined.
  if (!built.t) {
    const bf = await fetchAircraftMetaAdsbdb(env, hex);
    const bt = normType(bf?.t);
    if (bt) {
      built.t = bt;
      if (!built.r && bf?.r) built.r = bf.r;
      // Prefer our own naming (KV override, then the baked table) over adsbdb's
      // verbose description, for consistency with the primary-feed path.
      built.tn = (await env.ENRICH_KV.get(`tn:${bt}`)) ?? TYPE_NAMES[bt] ?? bf?.tn ?? "";
      built.found = true;
    }
  }
  // TTL by CONTENT, not mere upstream presence: many military hexes return a
  // live position with an all-empty DB record, and 30 d of cached emptiness
  // kept a later-appearing record blank for a month. An empty meta is a
  // negative answer whatever `found` says -- give it the 1 d TTL.
  const hasContent = !!(built.r || built.t || built.tn || built.op);
  await env.ENRICH_KV.put(`ac:${hex}`, JSON.stringify(built), {
    expirationTtl: built.found && hasContent ? AC_TTL_S : AC_NEG_TTL_S,
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

  // Military airframe side table (P2), consulted only when the live record
  // came back with neither a registration nor a type (the military-card
  // failure mode: live position, empty DB row). Serve-time like the operator
  // floor below, so negatively-cached entries benefit without waiting out
  // their TTL -- and one extra KV read only on the empty path. Fills reg /
  // type / type name; the operator stays the floor's job (the dataset
  // carries none). Runs before the photo join so the type unlocks the
  // generic type shot.
  let acR = acMeta?.r ?? "";
  let acT = acMeta?.t ?? "";
  let acTn = acMeta?.tn ?? "";
  if (!acR && !acT) {
    const mil = await env.ENRICH_KV.get<MilEntry>(`mil:${hex}`, "json");
    if (mil) {
      acR = str(mil.r);
      acT = normType(mil.t);
      acTn = str(mil.tn) || (acT ? (TYPE_NAMES[acT] ?? "") : "");
    }
  }

  // Sanitize the type designator at SERVE time too, not only where it is built:
  // an ac:<hex> entry cached before this normalization existed (or any future
  // dirty source) can still hold "P8 ?", and that value flows straight into the
  // response and the photo join below. Strip it here and re-resolve the friendly
  // name from the clean code, so already-cached hexes are fixed on the next
  // request instead of waiting out the 30 d TTL.
  acT = normType(acT);
  if (acT && !acTn) acTn = (await env.ENRICH_KV.get(`tn:${acT}`)) ?? TYPE_NAMES[acT] ?? "";

  // Stock-photo join: per-hex override first, then generic type stock (needs the
  // resolved type). Two fast KV reads worst case; a cold meta miss (no type yet)
  // still resolves a per-hex override, and the card's warm re-request picks up
  // the type shot. Absent library -> no `p`/`pk` fields (append-only schema).
  const photo = await resolvePhoto(env, hex, acT);

  // Military floor, applied at serve time so cached pre-floor entries get it
  // too: when the operator resolved empty, fill from (most-specific first)
  // the broadcast callsign's military designator (P3: RCH proves Air Mobility
  // Command), then the hex allocation table (nationally attributed), then
  // dbFlags as the catch-all generic. Never guesses types or registrations.
  let op = acMeta?.op ?? "";
  if (!op) {
    op = militaryCallsignOperator(cs) || militaryOperator(hex) || (acMeta?.mil ? "Military" : "");
  }

  // Report the ROOT gap only (a missing type makes name/photo unanswerable), so a
  // work list built from these points is actionable rather than triple-counting one
  // unknown airframe. Only for lookups that actually resolved something or that the
  // upstream chain answered -- a chain outage is an outage, not a library gap.
  if (metaOutcome !== "deadline") {
    if (!acT) recordEnrichGap(env, "type", "", hex);
    else if (!acTn) recordEnrichGap(env, "name", acT, hex);
    else if (!photo) recordEnrichGap(env, "photo", acT, hex);
  }

  const body: Record<string, string | number> = {
    v: SCHEMA_V,
    r: acR,
    t: acT,
    tn: acTn,
    op,
    o: route.o,
    d: route.d,
  };
  if (photo) {
    body.p = `/v1/photo/${photo.key}`;
    body.pk = photo.kind; // "hex" (per-airframe) | "type" (generic -> "representative photo")
  }
  return jsonResponse(body);
}
