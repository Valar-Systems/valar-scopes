import type { Env } from "./types";
import { SCHEMA_V } from "./schema";
import { errorResponse, jsonResponse } from "./util";

// GET /v1/airports?lat&lon&r -- the airport overlay's long tail. The firmware
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
