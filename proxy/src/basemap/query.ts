// The basemap request (docs/basemap-route.md §3; A1.7's MQ rows): parse, validate and normalise, then the ETag.
//
// lat, lon and r are /wx's, parsed as /wx parses them and quantised by crop.ts's OWN quantise (imported, never a
// second implementation), so a package's origin is /wx's quantised point (format §2; MQ3 sweeps it). Unknown
// parameters are ignored and never enter the key (MQ9). Nothing here logs.
import { quantise } from "../wx/crop";
import { MASK } from "./build";

/** The radius bound, MAX_DEGREES x KM_PER_DEGREE in src/AircraftManager.cpp (MQ5 parses them from there). */
export const MAX_R_KM = 222;
export const EPS_MIN_MM = 1_000, EPS_MAX_MM = 10_000_000;
export const LEVEL_R_MIN_MM = 1_000_000, LEVEL_R_MAX_MM = 250_000_000, LEVEL_N_MAX = 1_024, LEVELS_MAX = 4, TERRAIN_BYTES_MAX = 2_097_152;
const LAYER_BITS: Record<string, number> = MASK;

export interface Query { lat2: string; lon2: string; lat0Micro: number; lon0Micro: number; rKm: number; epsMm: number; mask: number; levels: Array<{ radiusMm: number; n: number }> }
export type Parsed = { ok: true; q: Query } | { ok: false; code: "bad_lat" | "bad_lon" | "bad_r" | "bad_eps" | "bad_layers" | "bad_levels" };

const UINT = /^\d+$/;

export function parseQuery(url: URL): Parsed {
  const p = url.searchParams;
  const lat = parseFloat(p.get("lat") ?? ""), lon = parseFloat(p.get("lon") ?? ""), r = parseFloat(p.get("r") ?? "");
  if (!Number.isFinite(lat) || Math.abs(lat) > 90) return { ok: false, code: "bad_lat" };
  if (!Number.isFinite(lon) || Math.abs(lon) > 180) return { ok: false, code: "bad_lon" };
  if (!Number.isFinite(r) || r <= 0) return { ok: false, code: "bad_r" };
  const c = quantise(lat, lon, r);
  if (c.rKm > MAX_R_KM) return { ok: false, code: "bad_r" };

  const eps = p.get("eps") ?? "";
  if (!UINT.test(eps)) return { ok: false, code: "bad_eps" };
  const epsMm = Number(eps);
  if (epsMm < EPS_MIN_MM || epsMm > EPS_MAX_MM) return { ok: false, code: "bad_eps" };

  const names = (p.get("layers") ?? "").split(",");
  let mask = 0;
  for (const n of names) {
    const bit = Object.hasOwn(LAYER_BITS, n) ? LAYER_BITS[n]! : 0;
    if (!bit || mask & bit) return { ok: false, code: "bad_layers" }; // unknown, empty or repeated
    mask |= bit;
  }

  const lv = p.get("levels");
  const levels: Query["levels"] = [];
  if (mask & MASK.terrain) {
    if (!lv) return { ok: false, code: "bad_levels" };
    const pairs = lv.split(",");
    if (pairs.length > LEVELS_MAX) return { ok: false, code: "bad_levels" };
    let bytes = 0;
    for (const pr of pairs) {
      const parts = pr.split(":");
      if (parts.length !== 2 || !UINT.test(parts[0]!) || !UINT.test(parts[1]!)) return { ok: false, code: "bad_levels" };
      const radiusMm = Number(parts[0]), n = Number(parts[1]);
      if (radiusMm < LEVEL_R_MIN_MM || radiusMm > LEVEL_R_MAX_MM || n < 1 || n > LEVEL_N_MAX) return { ok: false, code: "bad_levels" };
      if (levels.some((l) => l.radiusMm === radiusMm)) return { ok: false, code: "bad_levels" };
      levels.push({ radiusMm, n });
      bytes += 8 + n * n;
    }
    if (bytes > TERRAIN_BYTES_MAX) return { ok: false, code: "bad_levels" };
    levels.sort((a, b) => a.radiusMm - b.radiusMm);
  } else if (lv !== null) return { ok: false, code: "bad_levels" }; // levels without terrain

  const hundredths = (x: number) => Math.round(x * 100); // exact: c.lat is a two-decimal value
  return { ok: true, q: { lat2: c.lat2, lon2: c.lon2, lat0Micro: hundredths(c.lat) * 10_000, lon0Micro: hundredths(c.lon) * 10_000, rKm: c.rKm, epsMm, mask, levels } };
}

/** The normalised query as the ETag and the cache key see it: every field, in a fixed order. */
export const queryKey = (q: Query) => [q.lat2, q.lon2, q.rKm, q.epsMm, q.mask, q.levels.map((l) => `${l.radiusMm}:${l.n}`).join(";")].join("|");

const hex = (b: ArrayBuffer) => [...new Uint8Array(b)].map((x) => x.toString(16).padStart(2, "0")).join("");
/** K1 (docs/basemap-name-key.md): the secret as given when it is 32 bytes as 64 lowercase hex. Anything else is no key and
 *  fails closed (NK5), so a truncated paste or a trailing newline cannot key a name. */
export const nameKeyOf = (raw: string | undefined) => (raw !== undefined && /^[0-9a-f]{64}$/.test(raw) ? raw : undefined);

/** 24 hex digits of the normalised query's digest (never 16: the repo's device-id hook). No coordinate appears in it. With
 *  a name key it is an HMAC, because the input is small enough to enumerate and a stored name must give nothing to someone
 *  who can list the bucket (K1). With none it is the plain sha256, on the path that stores nothing (NK4). */
export async function queryDigest(q: Query, nameKey?: string): Promise<string> {
  const text = new TextEncoder().encode(queryKey(q));
  if (!nameKey) return hex(await crypto.subtle.digest("SHA-256", text)).slice(0, 24);
  const raw = Uint8Array.from(nameKey.match(/../g)!, (b) => parseInt(b, 16));
  const key = await crypto.subtle.importKey("raw", raw, { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  return hex(await crypto.subtle.sign("HMAC", key, text)).slice(0, 24);
}

/** §3: the ETag from the INPUTS, so a revalidation needs no build: "bm1-<rev>-<storeId>-<digest>". */
export async function etagOf(q: Query, rev: number, storeId: string, nameKey?: string): Promise<string> {
  if (!Number.isInteger(rev) || rev < 0) throw new Error("rev is not a whole number");
  if (!/^[0-9a-f]{12}$/.test(storeId)) throw new Error("store id is not 12 hex");
  return `"bm1-${rev}-${storeId}-${await queryDigest(q, nameKey)}"`;
}
