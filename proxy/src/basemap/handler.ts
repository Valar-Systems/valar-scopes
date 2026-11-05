// GET /api/v1/blipscope/basemap (docs/basemap-route.md §2.5, §3; A1.7's MW and MP rows). Reached past the proxy's
// per-IP limit, device auth and per-key limit, like /wx. It parses and normalises the query, answers a revalidation
// with no build, serves the Cache API, and otherwise calls the builder Worker over the BASEMAP service binding and
// VALIDATES what comes back before serving it: a package the validator refuses is never sent (500 builder_invalid).
//
// Nothing here writes a log line of its own, and no ETag or cache key holds a coordinate (MP1, MP3): both are built from
// the query's digest. The branch a request took rides on the proxy's own request line as `bm` (below).
//
// A1 (docs/basemap-a1.md), only where BASEMAP_PKG is bound (MS9: unbound, everything above is unchanged): the store id
// also comes from KV (`bm:store`, O1), a package once built is kept in BASEMAP_PKG under its store id and the query's
// digest, and a build that cannot finish within SYNC_WAIT_MS is answered 202 and finished under waitUntil. One build per
// query at a time: an R2 create-once lock, taken over only when stale (LOCK_STALE_MS). No key holds a coordinate (MS8).
import { errorResponse } from "../util";
import { validate } from "./format";
import { type Query, etagOf, parseQuery, queryDigest } from "./query";
import { STORE_KV_KEY } from "./store";

/** The builder's output version: bumped whenever the same inputs build different bytes (MD4 guards it). */
export const BASEMAP_REV = 1;

/** Which way a request went (docs/basemap-branch-log.md; MK1-MK7; docs/basemap-a1.md, MS1-MS9): set on the caller's
 *  metric, so it reaches the proxy's `evt:"req"` log line and never the dataset point. Fixed strings: no device id, no
 *  query. A 503 not_configured or a 400 returns before any branch and carries none. */
export type BasemapBranch = "304-known" | "304-kv" | "hit" | "stored" | "stored-error" | "known-miss" | "in-flight" | "unknown-store";

export interface BasemapEnv { BASEMAP?: Fetcher; BASEMAP_PKG?: R2Bucket; ENRICH_KV?: KVNamespace }

/** A1's constants, as ruled (Q-A1c). Mutable for tests only, which shorten the waits. */
export const SYNC_WAIT_MS = 2_000, RETRY_AFTER_S = 15, LOCK_STALE_MS = 45_000;
export const basemapTiming = { syncWaitMs: SYNC_WAIT_MS, lockStaleMs: LOCK_STALE_MS };
/** A1's keys: the store id and the query's 24-hex digest only, never a coordinate (MS8). */
export const pkgKey = (storeId: string, digest: string) => `bm/v1/pkg/${storeId}/${digest}`;
export const errKey = (storeId: string, digest: string) => `bm/v1/pkg/${storeId}/${digest}.err`;
export const lockKey = (storeId: string, digest: string) => `bm/v1/pkg-lock/${storeId}/${digest}`;

// ---- Where the 304 learns the current store id without a store read (ruled as recommended, A2.40). ---------------
// The store id from this isolate's last builder response, believed for STORE_TTL_MS. A cold
// isolate, or a stale one, asks the builder. Staleness after a store change is bounded by the TTL.
const STORE_TTL_MS = 60_000;
let known: { storeId: string; at: number } | null = null;
const knownStore = (now: number) => (known && now - known.at < STORE_TTL_MS ? known.storeId : null);
const learnStore = (storeId: string, now: number) => { known = { storeId, at: now }; };
/** Tests only: forget the isolate's store id and last build (a "fresh builder" in MW's terms). */
export const resetBasemapIsolate = () => { known = null; lastBuild = null; };
// -------------------------------------------------------------------------------------------------------------------

// ---- What /healthz reports (docs/basemap-deploy.md, Q4 as ruled; MH1-MH3) -----------------------------------------
// The store id and builder commit of this isolate's LAST builder 200, with no TTL: what the bound builder said, null
// until it has said anything. Never a made-up value.
let lastBuild: { storeId: string; commit: string } | null = null;
export function basemapHealth(env: BasemapEnv): "not_configured" | { rev: number; store: string | null; commit: string | null } {
  if (!env.BASEMAP) return "not_configured";
  return { rev: BASEMAP_REV, store: lastBuild ? lastBuild.storeId : null, commit: lastBuild ? lastBuild.commit : null };
}
// -------------------------------------------------------------------------------------------------------------------

export const cacheKey = (etag: string) => `https://basemap-cache.internal/${etag.replace(/"/g, "")}`;
const packageHeaders = (etag: string) => ({ "content-type": "application/octet-stream", "cache-control": "no-cache", etag });
// The copy kept in the Cache API must be CACHEABLE: the Cache API does not keep a response marked no-cache (MW11 found
// this). The key holds rev and the store id, so an entry never outlives the store it was built from.
const CACHE_TTL_S = 7 * 24 * 3600;
const storedHeaders = (etag: string) => ({ "content-type": "application/octet-stream", "cache-control": `max-age=${CACHE_TTL_S}`, etag });

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));
/** A1's 202: the build is running (or another request's is); come back after Retry-After. */
const accepted = () => new Response(JSON.stringify({ status: "building" }), {
  status: 202, headers: { "content-type": "application/json", "retry-after": String(RETRY_AFTER_S), "cache-control": "no-cache" },
});

/** O1: the sealed store's id from KV, or null. Anything that is not 12 hex is treated as absent. cacheTtl 60, not the
 *  plan's 30: the local runtime refuses 30 ("must be at least 60"), and that refusal, caught here, read as "no store id"
 *  in every MS row (docs/basemap-a1.md, the run). A failed read falls back to today's build, never to a wrong answer. */
export const STORE_KV_TTL_S = 60;
async function storeFromKv(env: BasemapEnv): Promise<string | null> {
  if (!env.ENRICH_KV) return null;
  try { const v = await env.ENRICH_KV.get(STORE_KV_KEY, { cacheTtl: STORE_KV_TTL_S }); return v && /^[0-9a-f]{12}$/.test(v) ? v : null; } catch { return null; }
}

type Built = { bytes: Uint8Array; storeId: string };
/** The builder, once: the normalised query only, never the request's own parameters. A Response is the final answer
 *  (an error); a Built is a package the validator accepted. */
async function callBuilder(env: BasemapEnv, q: Query): Promise<Response | Built> {
  let res: Response;
  try {
    res = await env.BASEMAP!.fetch("https://basemap.internal/build", {
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
  const commit = res.headers.get("x-basemap-commit") ?? "";
  if (!/^[0-9a-f]{40}$/.test(commit)) return errorResponse(500, "builder_invalid");
  learnStore(storeId, Date.now());
  lastBuild = { storeId, commit };
  return { bytes, storeId };
}

/** A1: one build, its package (or its 404/422) kept in BASEMAP_PKG, and the lock released -- what a 202 leaves running.
 *  Keeping is best-effort: a failed put costs a later rebuild, never this answer. */
async function buildAndKeep(env: BasemapEnv, q: Query, pkg: R2Bucket, digest: string, id?: string, locked = false): Promise<Response | Built> {
  try {
    const r = await callBuilder(env, q);
    try {
      if (!(r instanceof Response)) await pkg.put(pkgKey(r.storeId, digest), r.bytes);
      else if (id && (r.status === 404 || r.status === 422)) await pkg.put(errKey(id, digest), JSON.stringify({ status: r.status, code: r.status === 404 ? "no_coverage" : "too_large" }));
    } catch { /* kept nothing: the next request builds again */ }
    return r;
  } finally {
    if (locked && id) await pkg.delete(lockKey(id, digest)).catch(() => undefined);
  }
}

/** A1's lock: R2's create-once write (`If-None-Match: *`, atomic). True when this request may build. A lock older than
 *  lockStaleMs belongs to a build that died; it is taken over on its own etag, so only one taker wins. */
async function takeLock(pkg: R2Bucket, key: string): Promise<boolean> {
  const at = { at: String(Date.now()) };
  if (await pkg.put(key, "", { onlyIf: new Headers({ "If-None-Match": "*" }), customMetadata: at })) return true;
  const held = await pkg.head(key);
  if (!held) return (await pkg.put(key, "", { onlyIf: new Headers({ "If-None-Match": "*" }), customMetadata: at })) !== null;
  const since = Number(held.customMetadata?.at ?? held.uploaded.getTime());
  if (Date.now() - since < basemapTiming.lockStaleMs) return false;
  return (await pkg.put(key, "", { onlyIf: { etagMatches: held.etag }, customMetadata: at })) !== null;
}

/** A validated package as the answer: 304 when the client already holds it, else the bytes; a copy for the Cache API. */
async function answerBuilt(b: Built, q: Query, inm: string | null, cache: Cache, ctx: ExecutionContext): Promise<Response> {
  const etag = await etagOf(q, BASEMAP_REV, b.storeId);
  ctx.waitUntil(cache.put(cacheKey(etag), new Response(b.bytes, { headers: storedHeaders(etag) })));
  if (inm === etag) return new Response(null, { status: 304, headers: { etag, "cache-control": "no-cache" } });
  return new Response(b.bytes, { status: 200, headers: packageHeaders(etag) });
}

export async function handleBasemap(request: Request, env: BasemapEnv, ctx: ExecutionContext, log: { bm?: BasemapBranch } = {}): Promise<Response> {
  if (!env.BASEMAP) return errorResponse(503, "not_configured");
  const p = parseQuery(new URL(request.url));
  if (!p.ok) return errorResponse(400, p.code);
  const q: Query = p.q;
  const inm = request.headers.get("if-none-match");
  const cache = (caches as unknown as { default: Cache }).default;
  const pkg = env.BASEMAP_PKG; // A1 only where bound (MS9)

  // A revalidation or a cache hit, with no builder call, when this isolate knows the current store -- or, under A1, when
  // KV names it (O1)
  const mem = knownStore(Date.now());
  const id = mem ?? (pkg ? await storeFromKv(env) : null);
  if (id) {
    const etag = await etagOf(q, BASEMAP_REV, id);
    if (inm === etag && !mem) { log.bm = "304-kv"; return new Response(null, { status: 304, headers: { etag, "cache-control": "no-cache" } }); }
    if (inm === etag) { log.bm = "304-known"; return new Response(null, { status: 304, headers: { etag, "cache-control": "no-cache" } }); }
    const hit = await cache.match(cacheKey(etag));
    if (hit) { log.bm = "hit"; return new Response(hit.body, { status: 200, headers: packageHeaders(etag) }); }
    log.bm = "known-miss";
    if (pkg) {
      const digest = await queryDigest(q);
      const kept = await pkg.get(pkgKey(id, digest));
      if (kept) {
        const bytes = new Uint8Array(await kept.arrayBuffer());
        if (validate(bytes).status === "kOk") {
          log.bm = "stored";
          ctx.waitUntil(cache.put(cacheKey(etag), new Response(bytes, { headers: storedHeaders(etag) })));
          return new Response(bytes, { status: 200, headers: packageHeaders(etag) });
        }
      }
      const err = await pkg.get(errKey(id, digest));
      if (err) {
        const e = await err.json<{ status?: unknown; code?: unknown }>().catch(() => ({ status: null, code: null }));
        if ((e.status === 404 && e.code === "no_coverage") || (e.status === 422 && e.code === "too_large")) {
          log.bm = "stored-error";
          return errorResponse(e.status, e.code);
        }
      }
      if (!(await takeLock(pkg, lockKey(id, digest)))) { log.bm = "in-flight"; return accepted(); }
      // This request builds, raced against the wait: the answer when it is quick, else 202 and waitUntil finishes it
      const work = buildAndKeep(env, q, pkg, digest, id, true);
      ctx.waitUntil(work.then(() => undefined, () => undefined));
      const quick = await Promise.race([work, sleep(basemapTiming.syncWaitMs).then(() => null)]);
      if (quick === null) return accepted();
      return quick instanceof Response ? quick : answerBuilt(quick, q, inm, cache, ctx);
    }
  } else log.bm = "unknown-store";

  // Otherwise the builder, synchronously, as before A1 -- and, where BASEMAP_PKG is bound, its package kept
  const r = pkg ? await buildAndKeep(env, q, pkg, await queryDigest(q)) : await callBuilder(env, q);
  return r instanceof Response ? r : answerBuilt(r, q, inm, cache, ctx);
}

/** The query's digest alone (MP3 reads it to show no coordinate reaches a key). */
export const basemapDigest = queryDigest;
