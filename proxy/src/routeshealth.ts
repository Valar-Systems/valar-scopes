// /healthz's `routes` field: the age of the rt: route mirror (docs/route-mirror-age.md).
//
// scripts/ingest-routes.ts writes `meta:routes` LAST, after every data key and the sentinel, so its `builtAt` is the
// build that is actually being served. For 79 days nothing read it: the ingest's comment said /healthz reported it,
// and no file here did. A correct number nobody reads is indistinguishable from no number at all, so it is read here,
// and smoke-prod.sh and reconcile-fleet.py read it from here.
//
// NEVER A MADE-UP AGE. A missing key, an unparsable value, or a builtAt in the future reads null with stale true and a
// reason. A future builtAt is not clamped to 0: a clamp would turn evidence of a wrong meta into the most reassuring
// age there is. "Absent" and "cannot read" stay distinct, the ingest's own rule, and a read that throws is not cached,
// so the next /healthz retries.
import type { Env } from "./types";

/** The rule's one home. smoke and reconcile read it from the body (`maxAgeDays`), never restate it. */
export const ROUTES_MAX_AGE_DAYS = 10;
const DAY_MS = 86_400_000;
const CACHE_MS = 10 * 60_000;
const META_KEY = "meta:routes";

type Why = "absent" | "unparsable" | "future" | "unreadable";
export interface RoutesHealth {
  builtAt: string | null;
  ageDays: number | null;
  rows: number | null;
  stale: boolean;
  maxAgeDays: number;
  why?: Why;
  /** The SOURCE's own commit date (W13), informational: an upstream that stops changing stays visible while a verified
   *  re-seal keeps builtAt fresh. Shown only when meta holds it in the one form the writer writes; never invented. */
  sourceCommittedAt?: string;
}

/** The writer's --source-date form (ingest-routes.ts ISO_SECONDS, refused otherwise as SD1): this pair must agree. */
const SOURCE_DATE = /^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$/;

// What the meta said, parsed once per isolate per CACHE_MS. The AGE is computed per response, never cached.
type Read = { builtMs: number; builtAt: string; rows: number | null; sourceCommittedAt?: string } | { why: "absent" | "unparsable" };
let cache: { at: number; read: Read } | null = null;

/** Tests only: forget the isolate's cached read. */
export function resetRoutesHealth(): void {
  cache = null;
}

function parse(raw: string | null): Read {
  if (raw === null) return { why: "absent" };
  let m: unknown;
  try { m = JSON.parse(raw); } catch { return { why: "unparsable" }; }
  if (!m || typeof m !== "object") return { why: "unparsable" };
  const { builtAt, rows, sourceCommittedAt: sc } = m as { builtAt?: unknown; rows?: unknown; sourceCommittedAt?: unknown };
  if (typeof builtAt !== "string") return { why: "unparsable" };
  const builtMs = Date.parse(builtAt);
  if (!Number.isFinite(builtMs)) return { why: "unparsable" };
  // A value not in the writer's one form is OMITTED, never coerced: half a date is a different date (RH9s).
  const sourceCommittedAt = typeof sc === "string" && SOURCE_DATE.test(sc) && Number.isFinite(Date.parse(sc)) ? sc : undefined;
  return { builtMs, builtAt, rows: typeof rows === "number" && Number.isInteger(rows) && rows >= 0 ? rows : null,
    ...(sourceCommittedAt ? { sourceCommittedAt } : {}) };
}

const none = (why: Why): RoutesHealth => ({ builtAt: null, ageDays: null, rows: null, stale: true, maxAgeDays: ROUTES_MAX_AGE_DAYS, why });

export async function routesHealth(env: Env): Promise<RoutesHealth> {
  const now = Date.now();
  let read: Read;
  if (cache && now - cache.at < CACHE_MS) {
    read = cache.read;
  } else {
    try {
      read = parse(await env.ENRICH_KV.get(META_KEY));
    } catch {
      return none("unreadable"); // not cached: the next call retries
    }
    cache = { at: now, read };
  }
  if ("why" in read) return none(read.why);
  const ageMs = now - read.builtMs;
  if (ageMs < 0) return none("future");
  return {
    builtAt: read.builtAt,
    ageDays: Math.floor(ageMs / DAY_MS),
    rows: read.rows,
    stale: ageMs > ROUTES_MAX_AGE_DAYS * DAY_MS,
    maxAgeDays: ROUTES_MAX_AGE_DAYS,
    ...(read.sourceCommittedAt ? { sourceCommittedAt: read.sourceCommittedAt } : {}),
  };
}
