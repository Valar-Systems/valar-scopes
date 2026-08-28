import type { Env } from "./types";
import { SCHEMA_V } from "./schema";
import { errorResponse, jsonResponse } from "./util";

// GET /api/v1/blipscope/airports?lat&lon&r -- the airport overlay's long tail. The firmware
// bakes ~260 curated majors into flash (include/Airports.h); this endpoint
// supersedes that table in cloud mode with the full OurAirports dataset
// (public domain), pre-tiled into KV by scripts/ingest-airports.ts:
//
//   apt:<floor(lat)>:<floor(lon)>  ->  [[lat, lon, "RDM", "M"], ...]
//
// One-degree tiles keep any radar-radius query to a handful of KV reads. The
// response is deliberately device-shaped: hard-capped, priority-sorted
// (large > medium > small, nearest first within a kind), parsed straight off
// the socket by a C3-class heap. Airports move never, so both the tiles and
// the rendered response cache long.

// One tile row: [lat, lon, code, kind] with kind L/M/S.
type TileAirport = [number, number, string, string];

const MAX_R_KM = 250;
const DEFAULT_R_KM = 100;
const MAX_RESULTS = 60;
// A 250 km radius at high latitude spans many longitude tiles; past this we
// clamp the tile walk rather than the radius so the worst case stays bounded.
const MAX_TILES = 30;

const KIND_PRIORITY: Record<string, number> = { L: 0, M: 1, S: 2 };

function haversineKm(lat1: number, lon1: number, lat2: number, lon2: number): number {
  const rad = Math.PI / 180;
  const dLat = (lat2 - lat1) * rad;
  const dLon = (lon2 - lon1) * rad;
  const a =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(lat1 * rad) * Math.cos(lat2 * rad) * Math.sin(dLon / 2) ** 2;
  return 6371 * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

export async function handleAirports(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const lat = parseFloat(url.searchParams.get("lat") ?? "");
  const lon = parseFloat(url.searchParams.get("lon") ?? "");
  if (!Number.isFinite(lat) || !Number.isFinite(lon) || Math.abs(lat) > 85 || Math.abs(lon) > 180)
    return errorResponse(400, "bad_position");
  let r = parseFloat(url.searchParams.get("r") ?? "");
  if (!Number.isFinite(r) || r <= 0) r = DEFAULT_R_KM;
  r = Math.min(r, MAX_R_KM);

  // Serve co-located devices from the edge cache: the key rounds the position
  // to ~0.1 deg, plenty for a static-geography answer at radar scale.
  const cache = caches.default;
  const cacheKey = new Request(
    `https://cache.blipscope/v1/airports?lat=${lat.toFixed(1)}&lon=${lon.toFixed(1)}&r=${Math.round(r)}`,
  );
  const cached = await cache.match(cacheKey);
  if (cached) return cached;

  // Tile walk: every 1-degree tile the radius circle can touch.
  const latSpan = r / 111;
  const lonSpan = r / (111 * Math.max(0.2, Math.cos((lat * Math.PI) / 180)));
  const keys: string[] = [];
  for (let ty = Math.floor(lat - latSpan); ty <= Math.floor(lat + latSpan); ty++) {
    for (let tx = Math.floor(lon - lonSpan); tx <= Math.floor(lon + lonSpan); tx++) {
      // Wrap longitude tiles across the antimeridian.
      const wx = tx < -180 ? tx + 360 : tx > 179 ? tx - 360 : tx;
      keys.push(`apt:${ty}:${wx}`);
      if (keys.length >= MAX_TILES) break;
    }
    if (keys.length >= MAX_TILES) break;
  }

  const tiles = await Promise.all(keys.map((k) => env.ENRICH_KV.get<TileAirport[]>(k, "json")));
  const hits: { a: TileAirport; d: number }[] = [];
  for (const tile of tiles) {
    if (!tile) continue;
    for (const a of tile) {
      const d = haversineKm(lat, lon, a[0], a[1]);
      if (d <= r) hits.push({ a, d });
    }
  }
  hits.sort((x, y) => {
    const p = (KIND_PRIORITY[x.a[3]] ?? 9) - (KIND_PRIORITY[y.a[3]] ?? 9);
    return p !== 0 ? p : x.d - y.d;
  });

  const body = { v: SCHEMA_V, a: hits.slice(0, MAX_RESULTS).map((h) => h.a) };
  const res = jsonResponse(body);
  res.headers.set("Cache-Control", "public, max-age=86400"); // geography is static
  await cache.put(cacheKey, res.clone());
  return res;
}

// ---------------------------------------------------------------------------
// GET /api/v1/blipscope/airport/<CODE> -- ONE airport, by code.
//
// A different question from the overlay above, and therefore a different key
// family. The overlay asks "what is near this position", which is why it is
// pre-tiled; this asks "where is LHR", which a tile walk cannot answer without
// scanning the planet.
//
//   ap:<CODE>  ->  [lat, lon, elevFt|null]
//
// WHO NEEDS IT. Follow Mode's arc and globe faces draw a route between two
// airport codes that arrive on the enrich path, and the firmware's baked table
// is ~250 majors -- enough for most airline city pairs, and a miss degrades to
// an honest code-only arc. This closes the long tail. It also carries
// ELEVATION, which nothing else does, and which is what makes an AGL figure
// honest rather than wrong by the field elevation (Follow spec C5).
//
// BY CODE, NEVER BY CALLSIGN. The device may only ever ask this endpoint about
// an AIRPORT. A lookup keyed on the followed aircraft would be a request whose
// existence names the follow target, which the Follow spec's privacy invariant
// forbids outright -- so the shape of this route is itself part of that
// guarantee.
const CODE_RE = /^[A-Z0-9]{3,4}$/;

export async function handleAirportByCode(env: Env, rawCode: string): Promise<Response> {
  const code = decodeURIComponent(rawCode).trim().toUpperCase();
  // Shape-checked before it reaches KV: the key space is ours, and an unbounded
  // suffix on a key prefix is how a lookup endpoint becomes a scanner.
  if (!CODE_RE.test(code)) return errorResponse(400, "bad_code");

  const raw = await env.ENRICH_KV.get(`ap:${code}`, "text");
  // A code we do not carry is a 404 and NOT an error. The device is expected to
  // ask about codes that miss -- the route mirror emits both IATA and ICAO
  // forms, and a small field may be in neither our data nor anyone's.
  if (!raw) return errorResponse(404, "unknown_airport");

  let row: unknown;
  try {
    row = JSON.parse(raw);
  } catch {
    return errorResponse(404, "unknown_airport");
  }
  if (!Array.isArray(row) || row.length < 2) return errorResponse(404, "unknown_airport");
  const [lat, lon, elev] = row as [number, number, number | null];
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) return errorResponse(404, "unknown_airport");

  // elev is null when the source figure failed the sanity band. Passed through
  // as null rather than 0: a device that gets 0 will subtract 0 and report a
  // confident wrong AGL, where null makes it decline.
  return jsonResponse(
    { v: SCHEMA_V, code, lat, lon, elev: typeof elev === "number" ? elev : null },
    200,
    { "Cache-Control": "public, max-age=604800" },
  );
}
