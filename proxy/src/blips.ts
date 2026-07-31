import type { Env } from "./types";
import type { RequestMetric } from "./metrics";
import {
  blipsBody,
  bucketForRadiusKm,
  normalizeReadsbAc,
  quantizeTile,
  upstreamDistNm,
  SCHEMA_V,
  type NormalizedAircraft,
} from "./schema";
import { fetchPointChain } from "./upstreams/chain";
import { errorResponse, intEnv, jsonResponse } from "./util";

// Freshness window: a cached tile younger than this is served as-is (HIT).
// Older entries serve immediately as STALE while one background revalidation
// refreshes the tile (classic stale-while-revalidate).
const DEFAULT_FRESH_TTL_MS = 3000;

// Storage ceiling: how long a stale tile may keep serving under SWR while the
// upstream chain is down. Past this the entry evicts and devices get 503 --
// they keep their last picture and show the stale indicator.
const SWR_MAX_AGE_S = 600;

const DEFAULT_LIMIT = 25;
// Raised 60 to match the firmware's MAX_AIRCRAFT tracked cap: a device on a wide
// radius over a busy basin asks for up to 60 so the whole scope fills instead of
// the nearest 25 clustering in the centre. Still a compact reply (~5 KB).
const MAX_LIMIT = 60;

// Cold-miss serve deadline. The firmware's HTTP client aborts reads after ~5 s,
// and busy-basin tiles can take ~5 s upstream -- so a cold tile must not make
// the device wait for the upstream. Past this deadline the device gets a fast
// 503 ("warming", it keeps its last picture) while the fetch finishes in the
// background and caches, so the next poll a few seconds later hits a warm tile.
const DEFAULT_SERVE_DEADLINE_MS = 3500;

// Staleness cap. A cached tile older than this is refreshed IN the request path
// (bounded by the serve deadline) instead of relying purely on a background
// waitUntil revalidation. Under low traffic Cloudflare recycles the isolate between
// polls and can cut a waitUntil before it writes cache, so a purely-background
// refresh lets staleness run away (observed: a tile climbing past 4 min with one
// board polling). The in-band path writes cache inline when the build lands in time,
// which is not subject to isolate recycling -- so served staleness stays bounded at
// any traffic level. Kept above the fresh TTL so the common moderately-stale poll
// still takes the cheap background path.
const DEFAULT_STALE_SERVE_MS = 12000;

// One background revalidation per cache key per isolate; a poll storm on a
// popular tile must not fan out into a poll storm on the upstream.
const inflightRevalidations = new Set<string>();

interface BuiltTile {
  body: string;
  stored: Response; // the cacheable copy (internal headers; never sent to a device)
}

function sq(x: number): number {
  return x * x;
}

async function buildFresh(
  env: Env,
  latQ: string,
  lonQ: string,
  bucketKm: number,
  limit: number,
  meta?: RequestMetric,
): Promise<BuiltTile | null> {
  const started = Date.now();
  const result = await fetchPointChain(env, latQ, lonQ, upstreamDistNm(bucketKm));
  if (meta) {
    meta.upstreamMs = Date.now() - started;
    if (result) meta.upstream = result.upstream;
  }
  if (!result) return null;

  // Normalize, then keep the `limit` nearest to the tile centre. Distance is the
  // same cheap planar metric the firmware uses (lon scaled by cos lat); we only
  // need to RANK neighbours, not measure them.
  const latC = parseFloat(latQ);
  const lonC = parseFloat(lonQ);
  const cosLat = Math.cos((latC * Math.PI) / 180);
  const list = result.ac
    .map(normalizeReadsbAc)
    .filter((a): a is NormalizedAircraft => a !== null)
    .map((a) => ({ a, d2: sq((a.lon - lonC) * cosLat) + sq(a.lat - latC) }))
    .sort((x, y) => x.d2 - y.d2)
    .slice(0, limit)
    .map((x) => x.a);

  const t = Math.floor(result.nowMs / 1000);
  const body = blipsBody(t, list);
  const bytes = new TextEncoder().encode(body);
  const stored = new Response(bytes, {
    headers: {
      "Content-Type": "application/json",
      "Content-Length": String(bytes.byteLength),
      "Cache-Control": `s-maxage=${SWR_MAX_AGE_S}`,
      "X-Fetched-At": String(Date.now()),
      "X-Upstream": result.upstream,
    },
  });
  return { body, stored };
}

// What actually goes to the device: the cached wire body re-wrapped with
// device-facing headers (the stored copy's headers are internal bookkeeping).
function served(body: string, cacheState: "HIT" | "STALE" | "MISS", upstream: string): Response {
  return jsonResponse(body, 200, { "X-Cache": cacheState, "X-Upstream": upstream });
}

async function revalidate(
  env: Env,
  cacheKey: Request,
  latQ: string,
  lonQ: string,
  bucketKm: number,
  limit: number,
): Promise<void> {
  try {
    const built = await buildFresh(env, latQ, lonQ, bucketKm, limit);
    if (built) await caches.default.put(cacheKey, built.stored);
    // On failure the stale entry simply keeps serving until SWR_MAX_AGE_S.
  } finally {
    inflightRevalidations.delete(cacheKey.url);
  }
}

export async function handleBlips(
  request: Request,
  env: Env,
  ctx: ExecutionContext,
  meta: RequestMetric,
): Promise<Response> {
  const url = new URL(request.url);
  const lat = parseFloat(url.searchParams.get("lat") ?? "");
  const lon = parseFloat(url.searchParams.get("lon") ?? "");
  const r = parseFloat(url.searchParams.get("r") ?? "");
  if (!Number.isFinite(lat) || Math.abs(lat) > 90) return errorResponse(400, "bad_lat");
  if (!Number.isFinite(lon) || Math.abs(lon) > 180) return errorResponse(400, "bad_lon");
  if (!Number.isFinite(r) || r <= 0) return errorResponse(400, "bad_r");
  const limitRaw = parseInt(url.searchParams.get("limit") ?? "", 10);
  const limit = Number.isFinite(limitRaw) ? Math.min(Math.max(limitRaw, 1), MAX_LIMIT) : DEFAULT_LIMIT;

  const latQ = quantizeTile(lat);
  const lonQ = quantizeTile(lon);
  const bucketKm = bucketForRadiusKm(r);

  // Cache key = quantized tile + radius bucket + limit + schema version, on a
  // synthetic host. Every device in the same tile/bucket shares one entry (and
  // one upstream fetch per TTL window). NB: the edge cache is per-colo.
  const cacheKey = new Request(
    `https://blips-cache.internal/v1/blips?lat=${latQ}&lon=${lonQ}&r=${bucketKm}&limit=${limit}&sv=${SCHEMA_V}`,
  );
  const freshTtlMs = intEnv(env.BLIPS_FRESH_TTL_MS, DEFAULT_FRESH_TTL_MS);
  const staleServeMs = intEnv(env.BLIPS_STALE_SERVE_MS, DEFAULT_STALE_SERVE_MS);
  const deadlineMs = intEnv(env.BLIPS_SERVE_DEADLINE_MS, DEFAULT_SERVE_DEADLINE_MS);

  const stored = await caches.default.match(cacheKey);
  if (stored) {
    const fetchedAt = Number(stored.headers.get("X-Fetched-At") ?? 0);
    const upstream = stored.headers.get("X-Upstream") ?? "";
    const body = await stored.text();
    const age = Date.now() - fetchedAt;
    if (age <= freshTtlMs) {
      meta.cache = "HIT";
      return served(body, "HIT", upstream);
    }

    // Very stale: the background path isn't keeping up (see DEFAULT_STALE_SERVE_MS).
    // Refresh IN the request path, bounded by the serve deadline: start the build,
    // race the deadline, and if it lands write cache INLINE (guaranteed to run, unlike
    // a recycled waitUntil) and serve the fresh body. If the deadline wins, serve the
    // stale body and let the already in-flight build finish under waitUntil.
    if (age > staleServeMs && !inflightRevalidations.has(cacheKey.url)) {
      inflightRevalidations.add(cacheKey.url);
      const buildPromise = buildFresh(env, latQ, lonQ, bucketKm, limit, meta);
      const winner = await Promise.race([
        buildPromise,
        new Promise<"deadline">((resolve) => setTimeout(() => resolve("deadline"), deadlineMs)),
      ]);
      if (winner && winner !== "deadline") {
        await caches.default.put(cacheKey, winner.stored);
        inflightRevalidations.delete(cacheKey.url);
        meta.cache = "MISS";
        return served(winner.body, "MISS", winner.stored.headers.get("X-Upstream") ?? upstream);
      }
      ctx.waitUntil(
        buildPromise
          .then(async (built) => {
            if (built) await caches.default.put(cacheKey, built.stored);
          })
          .catch(() => {})
          .finally(() => inflightRevalidations.delete(cacheKey.url)),
      );
      meta.cache = "STALE";
      return served(body, "STALE", upstream);
    }

    // Moderately stale: answer immediately with the old picture -- its body still
    // carries the ORIGINAL `t`, so the device's stale indicator stays honest -- and
    // refresh in the background.
    if (!inflightRevalidations.has(cacheKey.url)) {
      inflightRevalidations.add(cacheKey.url);
      ctx.waitUntil(revalidate(env, cacheKey, latQ, lonQ, bucketKm, limit));
    }
    meta.cache = "STALE";
    return served(body, "STALE", upstream);
  }

  meta.cache = "MISS";
  const buildPromise = buildFresh(env, latQ, lonQ, bucketKm, limit, meta);
  const winner = await Promise.race([
    buildPromise,
    new Promise<"deadline">((resolve) => setTimeout(() => resolve("deadline"), deadlineMs)),
  ]);

  if (winner === "deadline") {
    // Cold tile + slow upstream: answer fast and warm the cache in the
    // background for the next poll (see DEFAULT_SERVE_DEADLINE_MS above).
    ctx.waitUntil(
      buildPromise
        .then(async (built) => {
          if (built) await caches.default.put(cacheKey, built.stored);
        })
        .catch(() => {}),
    );
    return errorResponse(503, "warming", { "Retry-After": "5" });
  }

  if (!winner) return errorResponse(503, "upstream_unavailable", { "Retry-After": "10" });
  await caches.default.put(cacheKey, winner.stored);
  return served(winner.body, "MISS", meta.upstream ?? "");
}
