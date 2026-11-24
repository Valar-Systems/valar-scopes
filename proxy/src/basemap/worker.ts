// The builder Worker, `blipscope-basemap` (docs/basemap-route.md §2.1, §2.4). It is reached only through the proxy's
// BASEMAP service binding, never from the internet: it takes the proxy's NORMALISED query as JSON, builds over its R2
// binding, and answers the package or a JSON error. It does no auth, no rate limiting and no caching (the proxy's), and
// it logs nothing about the request: the query holds the origin. Its one line per build (BB1-BB3,
// docs/basemap-build-reads.md) is about the build's store reads, in counts and milliseconds only.
import { type BuildQuery, type BuildResult, type StoreReader, build } from "./build";
import type { RepairCounts } from "./sea";
import { CAP_BYTES } from "./budget";

// The commit this bundle was built from, stamped at deploy by `deploy.sh --builder` with --define, exactly as the proxy
// is stamped (docs/basemap-deploy.md, Q2 as ruled). It travels inside the bundle it describes, so no dashboard var can
// make PROVENANCE's builder line drift from the code. basemap.wrangler.toml defaults it to "UNSTAMPED" in every env,
// and an unstamped bundle refuses every build (MW18).
declare const BUILD_COMMIT: string;

export interface BuilderEnv { STORE: R2Bucket }

/** What one build read from the store (BB1-BB3): counts and milliseconds, nothing from the query. */
export interface ReadStat { reads: number; bytes: number; readMs: number; maxReadMs: number }

/** The R2 binding as build.ts's StoreReader, counting and timing each read into `stat` when given one. */
export const r2Reader = (b: R2Bucket, stat?: ReadStat): StoreReader => ({
  async get(key) {
    const t0 = Date.now();
    const o = await b.get(key);
    const v = o ? new Uint8Array(await o.arrayBuffer()) : null;
    if (stat) {
      const ms = Date.now() - t0;
      stat.reads++;
      stat.bytes += v ? v.length : 0;
      stat.readMs += ms;
      if (ms > stat.maxReadMs) stat.maxReadMs = ms;
    }
    return v;
  },
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

/**
 * The builder's one line per build, whatever its outcome (BB1-BB4, BL1): integers only, nothing from the query. A built
 * package or a too_large also says what the budget ladder measured (step 0, docs/basemap-dense.md): the rung that
 * fitted, the cap, and the bytes each layer adds (the ruled ladder records no per-rung nt); and (Q-W4, docs/basemap-dense-code.md R2-R5) D2's `steps` and the
 * build-wide `refusedBuilt`. A built package adds the drawn set's `refused` and its sea: `seaLost` = `seaRefused` +
 * `seaCollapsed` + `seaDropped`, with `refused5` (step 5's), `islandsDropped`, `seaRepairs` and `seaRepairVertices`.
 */
export function buildLine(r: BuildResult, stat: ReadStat, wallMs: number): Record<string, string | number> {
  const line: Record<string, string | number> = { evt: "bm_build", status: r.ok ? 200 : r.status, ...stat, wallMs };
  const ladder = r.ok ? r.ladder : r.status === 422 ? r.ladder : null;
  if (!ladder) return line;
  line.rung = ladder.rung;
  line.cap = CAP_BYTES;
  const b = ladder.sections;
  Object.assign(line, { secWater: b.water, secRivers: b.rivers, secRoads: b.roads, secPeaks: b.peaks, secPlaces: b.places, secProv: b.provenance });
  const rep = r.ok ? r.report : r.status === 422 ? r.report : null;
  if (!rep) return line;
  Object.assign(line, { steps: rep.radii.length, refusedBuilt: rep.refusedBuilt });
  if (rep.refused !== undefined) line.refused = rep.refused;
  const s = rep.sea;
  if (s) Object.assign(line, { seaLost: s.lost, seaRefused: s.refused, seaCollapsed: s.collapsed, seaDropped: s.dropped, refused5: s.refused5,
    islandsDropped: s.islandsDropped, seaRepairs: s.repairs, seaRepairVertices: s.repairVertices });
  return line;
}

/** One sea repair, as production may log it: COUNTS only -- never the record key, never the island's point (the ruling after
 *  01:50:28Z: a point inside the requested square is a location tied to a request). The full line is the harness's alone. */
const repairLine = (c: RepairCounts) => `sea_repair rings ${c.rings} restored ${c.restored} rounds ${c.rounds}`;
/** The production sea_repair lines for a build: one per repaired piece of a built package, and one for a repair that stopped the
 *  build (its 500's counts). T-SEALOG asserts on exactly these. */
export function seaRepairLines(r: BuildResult): string[] {
  if (r.ok) return (r.report.sea?.repairCounts ?? []).map(repairLine);
  if (r.status === 500 && r.repair) return [repairLine(r.repair)];
  return [];
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
      const t0 = Date.now(), stat: ReadStat = { reads: 0, bytes: 0, readMs: 0, maxReadMs: 0 };
      const r = await build(r2Reader(env.STORE, stat), q, { commit });
      console.log(JSON.stringify(buildLine(r, stat, Date.now() - t0)));
      for (const l of seaRepairLines(r)) console.log(l);
      if (!r.ok) return json(r.status, { error: r.code });
      // x-basemap-commit: the proxy's /healthz reports it (Q4), so D3 can prove which builder is bound
      return new Response(r.bytes, { status: 200, headers: { "content-type": "application/octet-stream", "x-basemap-store": r.storeId, "x-basemap-commit": commit } });
    },
  };
}

export default makeBuilder(BUILD_COMMIT);
