// GET /api/v1/blipscope/basemap (docs/basemap-route.md §2.5, §3; A1.7's MW and MP rows). Reached past the proxy's
// per-IP limit, device auth and per-key limit, like /wx. It parses and normalises the query, answers a revalidation
// with no build, serves the Cache API, and otherwise calls the builder Worker over the BASEMAP service binding and
// VALIDATES what comes back before serving it: a package the validator refuses is never sent (500 builder_invalid).
//
// Nothing here logs, and no ETag or cache key holds a coordinate (MP1, MP3): both are built from the query's digest.
import { errorResponse } from "../util";
import { validate } from "./format";
import { type Query, etagOf, parseQuery, queryDigest } from "./query";

/** The builder's output version: bumped whenever the same inputs build different bytes (MD4 guards it). */
export const BASEMAP_REV = 1;

export interface BasemapEnv { BASEMAP?: Fetcher }

// ---- PENDING A RULING (A2.28): where the 304 learns the current store id without a store read. ---------------------
// The reading implemented: the store id from this isolate's last builder response, believed for STORE_TTL_MS. A cold
// isolate, or a stale one, asks the builder. Staleness after a store change is bounded by the TTL.
const STORE_TTL_MS = 60_000;
let known: { storeId: string; at: number } | null = null;
const knownStore = (now: number) => (known && now - known.at < STORE_TTL_MS ? known.storeId : null);
const learnStore = (storeId: string, now: number) => { known = { storeId, at: now }; };
/** Tests only: forget the isolate's store id (a "fresh builder" in MW's terms). */
export const resetBasemapIsolate = () => { known = null; };
// -------------------------------------------------------------------------------------------------------------------

const cacheKey = (etag: string) => `https://basemap-cache.internal/${etag.replace(/"/g, "")}`;
const packageHeaders = (etag: string) => ({ "content-type": "application/octet-stream", "cache-control": "no-cache", etag });
// The copy kept in the Cache API must be CACHEABLE: the Cache API does not keep a response marked no-cache (MW11 found
// this). The key holds rev and the store id, so an entry never outlives the store it was built from.
const CACHE_TTL_S = 7 * 24 * 3600;
const storedHeaders = (etag: string) => ({ "content-type": "application/octet-stream", "cache-control": `max-age=${CACHE_TTL_S}`, etag });

export async function handleBasemap(request: Request, env: BasemapEnv, ctx: ExecutionContext): Promise<Response> {
  if (!env.BASEMAP) return errorResponse(503, "not_configured");
  const p = parseQuery(new URL(request.url));
  if (!p.ok) return errorResponse(400, p.code);
  const q: Query = p.q;
  const inm = request.headers.get("if-none-match");
  const cache = (caches as unknown as { default: Cache }).default;

  // A revalidation or a cache hit, with no builder call, when this isolate knows the current store
  const id = knownStore(Date.now());
  if (id) {
    const etag = await etagOf(q, BASEMAP_REV, id);
    if (inm === etag) return new Response(null, { status: 304, headers: { etag, "cache-control": "no-cache" } });
    const hit = await cache.match(cacheKey(etag));
    if (hit) return new Response(hit.body, { status: 200, headers: packageHeaders(etag) });
  }

  // Otherwise the builder: the normalised query only, never the request's own parameters
  let res: Response;
  try {
    res = await env.BASEMAP.fetch("https://basemap.internal/build", {
      method: "POST", headers: { "content-type": "application/json" },
      body: JSON.stringify({ lat0Micro: q.lat0Micro, lon0Micro: q.lon0Micro, rKm: q.rKm, epsMm: q.epsMm, mask: q.mask, levels: q.levels }),
    });
  } catch { return errorResponse(503, "basemap_unavailable"); }
  if (res.status === 404) return errorResponse(404, "no_coverage");
  if (res.status === 422) return errorResponse(422, "too_large");
  if (res.status !== 200) return errorResponse(503, "basemap_unavailable");
  const storeId = res.headers.get("x-basemap-store") ?? "";
  const bytes = new Uint8Array(await res.arrayBuffer());
  if (!/^[0-9a-f]{12}$/.test(storeId) || validate(bytes).status !== "kOk") return errorResponse(500, "builder_invalid");
  learnStore(storeId, Date.now());
  const etag = await etagOf(q, BASEMAP_REV, storeId);
  ctx.waitUntil(cache.put(cacheKey(etag), new Response(bytes, { headers: storedHeaders(etag) })));
  if (inm === etag) return new Response(null, { status: 304, headers: { etag, "cache-control": "no-cache" } });
  return new Response(bytes, { status: 200, headers: packageHeaders(etag) });
}

/** The query's digest alone (MP3 reads it to show no coordinate reaches a key). */
export const basemapDigest = queryDigest;
