// The builder Worker, `blipscope-basemap` (docs/basemap-route.md §2.1, §2.4). It is reached only through the proxy's
// BASEMAP service binding, never from the internet: it takes the proxy's NORMALISED query as JSON, builds over its R2
// binding, and answers the package or a JSON error. It does no auth, no rate limiting and no caching (the proxy's), and
// it logs nothing about the request: the query holds the origin.
import { type BuildQuery, type StoreReader, build } from "./build";

// The commit this bundle was built from, stamped at deploy by `deploy.sh --builder` with --define, exactly as the proxy
// is stamped (docs/basemap-deploy.md, Q2 as ruled). It travels inside the bundle it describes, so no dashboard var can
// make PROVENANCE's builder line drift from the code. basemap.wrangler.toml defaults it to "UNSTAMPED" in every env,
// and an unstamped bundle refuses every build (MW18).
declare const BUILD_COMMIT: string;

export interface BuilderEnv { STORE: R2Bucket }

/** The R2 binding as build.ts's StoreReader. */
export const r2Reader = (b: R2Bucket): StoreReader => ({
  async get(key) { const o = await b.get(key); return o ? new Uint8Array(await o.arrayBuffer()) : null; },
});

const json = (status: number, body: object) => new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });
const isInt = (v: unknown, lo: number, hi: number) => Number.isInteger(v) && (v as number) >= lo && (v as number) <= hi;

/** The proxy normalised the query (query.ts); this re-checks its SHAPE only, so a malformed call fails here, loudly. */
function asQuery(v: unknown): BuildQuery | null {
  const q = v as Partial<BuildQuery> | null;
  if (!q || !isInt(q.lat0Micro, -90_000_000, 90_000_000) || !isInt(q.lon0Micro, -180_000_000, 180_000_000) || !isInt(q.rKm, 1, 222)
    || !isInt(q.epsMm, 1_000, 10_000_000) || !isInt(q.mask, 1, 63) || !Array.isArray(q.levels) || q.levels.length > 4
    || !q.levels.every((l) => isInt(l?.radiusMm, 1_000_000, 250_000_000) && isInt(l?.n, 1, 1_024))) return null;
  return { lat0Micro: q.lat0Micro!, lon0Micro: q.lon0Micro!, rKm: q.rKm!, epsMm: q.epsMm!, mask: q.mask!, levels: q.levels!.map((l) => ({ radiusMm: l.radiusMm, n: l.n })) };
}

/** The builder's fetch handler for one stamped commit. The default export is the deployed bundle's own (MW18). */
export function makeBuilder(commit: string) {
  return {
    async fetch(request: Request, env: BuilderEnv): Promise<Response> {
      if (request.method !== "POST") return json(405, { error: "method_not_allowed" });
      if (!/^[0-9a-f]{40}$/.test(commit)) return json(503, { error: "builder_unstamped" }); // §2.4: never build unstamped
      let q: BuildQuery | null = null;
      try { q = asQuery(await request.json()); } catch { q = null; }
      if (!q) return json(400, { error: "bad_query" });
      const r = await build(r2Reader(env.STORE), q, { commit });
      if (!r.ok) return json(r.status, { error: r.code });
      // x-basemap-commit: the proxy's /healthz reports it (Q4), so D3 can prove which builder is bound
      return new Response(r.bytes, { status: 200, headers: { "content-type": "application/octet-stream", "x-basemap-store": r.storeId, "x-basemap-commit": commit } });
    },
  };
}

export default makeBuilder(BUILD_COMMIT);
