// Wire schema v1 -- the contract with the firmware. See README "Wire schema".
//
// FROZEN: field order, units, sentinels, tile/bucket quantization. Evolution is
// append-only: new trailing array fields / new JSON keys only (firmware ignores
// unknowns); anything breaking bumps SCHEMA_V, which the firmware asserts.
export const SCHEMA_V = 1;

// No nulls on the wire: unknown values use sentinels the firmware tests for.
export const SENTINEL = {
  ALT_FT: -100000,   // altitude can legitimately be negative (below MSL), so far below any real value
  GS_KT: -1,
  TRACK_DEG: -1,
  VRATE_FPM: -100000, // vertical rate is signed, same reasoning as altitude
  CATEGORY: 0,
  AGE_S: -1,
} as const;

// ADS-B emitter category ("A3") -> the OpenSky /states/all integer enum the
// firmware already consumes (0 = no info, 2..8 = light..rotorcraft, 9+ = other).
// Keeping OpenSky's numbering means zero firmware-side category changes.
const CATEGORY_MAP: Record<string, number> = {
  A0: 1, A1: 2, A2: 3, A3: 4, A4: 5, A5: 6, A6: 7, A7: 8,
  B0: 1, B1: 9, B2: 10, B3: 11, B4: 12, B5: 13, B6: 14, B7: 15,
  C0: 1, C1: 16, C2: 17, C3: 18, C4: 19,
};

export function mapCategory(category: unknown): number {
  if (typeof category !== "string") return SENTINEL.CATEGORY;
  return CATEGORY_MAP[category.toUpperCase()] ?? SENTINEL.CATEGORY;
}

// Position staleness cap: readsb point queries can return aircraft whose last
// position is minutes old; the device dead-reckons from `age`, and a multi-minute
// extrapolation teleports blips across the scope. 60 s keeps slow updaters
// without the teleporting tail.
export const MAX_POS_AGE_S = 60;

export interface NormalizedAircraft {
  hex: string;
  cs: string;
  lat: number; // degrees, float (integerized only at serialization)
  lon: number;
  altFt: number | null;
  gsKt: number | null;
  trackDeg: number | null;
  vrateFpm: number | null;
  category: number;
  ageS: number | null;
}

function num(v: unknown): number | null {
  return typeof v === "number" && Number.isFinite(v) ? v : null;
}

// One readsb-style aircraft entry -> normalized shape, or null when it doesn't
// belong in /v1/blips at all: no position, on the ground (the endpoint is
// airborne-only -- documented in the README), or a stale position.
export function normalizeReadsbAc(raw: unknown): NormalizedAircraft | null {
  const r = raw as Record<string, unknown> | null;
  if (r === null || typeof r !== "object") return null;

  const lat = num(r.lat);
  const lon = num(r.lon);
  if (lat === null || lon === null) return null;
  if (r.alt_baro === "ground") return null;

  const ageS = num(r.seen_pos);
  if (ageS !== null && ageS > MAX_POS_AGE_S) return null;

  const altFt = num(r.alt_baro) ?? num(r.alt_geom);
  const gsKt = num(r.gs);
  const track = num(r.track);
  const vrateFpm = num(r.baro_rate) ?? num(r.geom_rate);

  return {
    hex: typeof r.hex === "string" ? r.hex.toLowerCase() : "",
    cs: typeof r.flight === "string" ? r.flight.trim() : "",
    lat,
    lon,
    altFt: altFt === null ? null : Math.round(altFt),
    gsKt: gsKt === null ? null : Math.round(gsKt),
    trackDeg: track === null ? null : ((Math.round(track) % 360) + 360) % 360,
    vrateFpm: vrateFpm === null ? null : Math.round(vrateFpm),
    category: mapCategory(r.category),
    ageS: ageS === null ? null : Math.round(ageS),
  };
}

// Row order is the schema: [hex, cs, lat*1e4, lon*1e4, altFt, gsKt, trackDeg, vrateFpm, category, ageS]
function row(a: NormalizedAircraft): (string | number)[] {
  return [
    a.hex,
    a.cs,
    Math.round(a.lat * 1e4),
    Math.round(a.lon * 1e4),
    a.altFt ?? SENTINEL.ALT_FT,
    a.gsKt ?? SENTINEL.GS_KT,
    a.trackDeg ?? SENTINEL.TRACK_DEG,
    a.vrateFpm ?? SENTINEL.VRATE_FPM,
    a.category,
    a.ageS ?? SENTINEL.AGE_S,
  ];
}

export function blipsBody(tSec: number, list: NormalizedAircraft[]): string {
  return JSON.stringify({ v: SCHEMA_V, t: tSec, n: list.length, a: list.map(row) });
}

// ---- request quantization (part of the wire contract; documented) ------------

// Radius buckets (km): the request's r snaps UP to the next bucket so nearby
// devices with slightly different range settings share one cache entry.
export const R_BUCKETS_KM = [10, 20, 40, 80, 160] as const;

export function bucketForRadiusKm(r: number): number {
  for (const b of R_BUCKETS_KM) if (r <= b) return b;
  return R_BUCKETS_KM[R_BUCKETS_KM.length - 1] as number;
}

// Centre coordinates snap to a 0.05 deg (~5.5 km) tile for the same reason.
export const TILE_DEG = 0.05;

export function quantizeTile(v: number): string {
  return (Math.round(v / TILE_DEG) * TILE_DEG).toFixed(2);
}

// Upstream query radius: bucket + margin covering the worst-case tile-centre
// offset (half a tile diagonally ~ 3.9 km), converted to whole nautical miles
// for the readsb-style point API.
export function upstreamDistNm(bucketKm: number): number {
  return Math.ceil((bucketKm + 4) / 1.852);
}
