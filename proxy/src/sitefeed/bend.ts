// The Bend site feed (docs/bend-site-feed.md): live aircraft over Bend, Oregon, for valarscopes.com.
//
// SELF-CONTAINED AND REMOVABLE. Everything lives in this file, behind one switch (SITE_FEED_BEND = "on").
// Removing it is one small PR; the steps are in proxy/README.md, "Bend site feed".
//
// WHAT IT NEVER DOES. It never fetches an upstream: there is no import from ../upstreams here, and the
// reader returns "empty" rather than asking anyone. Its data comes only from fetches the device path
// already made -- and only when a device's fetched area happens to cover downtown Bend.
//
// WHAT IT NEVER CARRIES. The device's tile, radius bucket and limit, and anything that would let a reader
// infer them. The snapshot is cut from the FULL upstream list (the device's `limit` cut is a disc around
// its own tile, whose edge would give the tile away) to a fixed circle around downtown, and it is written
// only when that whole circle lies inside the fetched area -- so it is complete, and it says nothing about
// where the fetch was centred.
//
//   writer (device path)    -> per-isolate 30 s guard -> waitUntil(DO PUT)   [never awaited by the device]
//   BendSnapshotDO          -> refuses a write within 30 s of the STORED row's own time (global by construction)
//   BendFeed.snapshot()     -> colo cache (10 s) -> DO GET -> no planes past 120 s
import { DurableObject, WorkerEntrypoint } from "cloudflare:workers";
import type { NormalizedAircraft } from "../schema";
import { upstreamDistNm } from "../schema";
import type { Env } from "../types";

/** Downtown Bend, Oregon (Daniel, 2026-10-21). The snapshot's centre, always -- never a device's tile. */
export const BEND = { lat: 44.058, lon: -121.315 } as const;
/** 30 mi (Daniel, 2026-10-21): the circle the snapshot keeps. */
export const RADIUS_KM = 48.28;
/** At most one stored write per 30 s, enforced on the STORED row's own time. */
export const WRITE_MIN_INTERVAL_MS = 30_000;
/** Past this, the reader serves no planes (the site shows the age and an empty sky). */
export const MAX_SHOW_AGE_S = 120;
/** Reads are cached this long in the visitor's colo, so a spike can't flood the one instance. */
export const COLO_CACHE_MS = 10_000;

const CACHE_KEY = "https://sitefeed.internal/bend/v1";
const DO_BASE = "https://bend-snapshot.internal";
const DO_NAME = "bend";
const R_EARTH_KM = 6371.0088;

export const ATTRIBUTION: Record<string, string> = {
  "adsb.fi": "aircraft data: adsb.fi",
  "adsb.lol": "aircraft data © adsb.lol contributors, ODbL 1.0",
};

export function enabled(env: Env): boolean {
  return env.SITE_FEED_BEND === "on" && env.BEND_SNAPSHOT !== undefined;
}

function rad(d: number): number {
  return (d * Math.PI) / 180;
}

export function haversineKm(lat1: number, lon1: number, lat2: number, lon2: number): number {
  const dLat = rad(lat2 - lat1);
  const dLon = rad(lon2 - lon1);
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(rad(lat1)) * Math.cos(rad(lat2)) * Math.sin(dLon / 2) ** 2;
  return 2 * R_EARTH_KM * Math.asin(Math.min(1, Math.sqrt(h)));
}

/** The radius the upstream was asked for, around the tile centre (blips.ts asks in whole NM). */
export function fetchedKm(bucketKm: number): number {
  return upstreamDistNm(bucketKm) * 1.852;
}

/** Does the whole downtown circle lie inside the area this fetch covered? */
export function covers(tileLat: number, tileLon: number, bucketKm: number): boolean {
  return haversineKm(tileLat, tileLon, BEND.lat, BEND.lon) + RADIUS_KM <= fetchedKm(bucketKm);
}

/** Does this fetch touch the downtown circle at all? (Only those count a "skipped_not_covered".) */
function touches(tileLat: number, tileLon: number, bucketKm: number): boolean {
  return haversineKm(tileLat, tileLon, BEND.lat, BEND.lon) < RADIUS_KM + fetchedKm(bucketKm);
}

export interface SiteAircraft {
  hex: string;
  cs: string;
  lat: number;
  lon: number;
  alt_ft: number | null;
  gs_kt: number | null;
  track_deg: number | null;
  vrate_fpm: number | null;
  category: number;
  seen_s: number | null;
}

/** What is stored: the upstream's time, its operator, the planes. Nothing from the request. */
export interface Snapshot {
  t: number;
  source: string;
  aircraft: SiteAircraft[];
}

interface Stored extends Snapshot {
  writtenAt: number;
}

/** adsb_fi / adsb_fi_b -> "adsb.fi", adsb_lol / adsb_lol_b -> "adsb.lol"; anything else is not served. */
export function sourceName(upstreamId: string): string | null {
  if (upstreamId === "adsb_fi" || upstreamId === "adsb_fi_b") return "adsb.fi";
  if (upstreamId === "adsb_lol" || upstreamId === "adsb_lol_b") return "adsb.lol";
  return null;
}

/** The planes inside the downtown circle, from the FULL upstream list. */
export function extract(all: NormalizedAircraft[]): SiteAircraft[] {
  return all
    .filter((a) => haversineKm(a.lat, a.lon, BEND.lat, BEND.lon) <= RADIUS_KM)
    .map((a) => ({
      hex: a.hex,
      cs: a.cs,
      lat: a.lat,
      lon: a.lon,
      alt_ft: a.altFt,
      gs_kt: a.gsKt,
      track_deg: a.trackDeg,
      vrate_fpm: a.vrateFpm,
      category: a.category,
      seen_s: a.ageS,
    }));
}

function count(env: Env, event: string): void {
  try {
    // Counts only: no tile, no device, no location.
    env.METRICS?.writeDataPoint({ blobs: ["sitefeed", event], doubles: [1], indexes: ["sitefeed"] });
  } catch {
    /* a metrics failure must not reach the device path either */
  }
}

// One send per isolate per 30 s. Only keeps Durable Object traffic small; the 30 s RULE is the DO's.
let lastSendMs = 0;
let lastSkipCountMs = 0;

/** Test seam: reset the per-isolate guards. */
export function __resetSiteFeedForTests(): void {
  lastSendMs = 0;
  lastSkipCountMs = 0;
}

export type OnFetched = (all: NormalizedAircraft[], upstreamId: string, upstreamNowMs: number) => void;

/**
 * The device-path hook, or undefined when the feed is off (then the device path does nothing at all).
 * The returned function never throws and never makes the caller wait: the write is handed to waitUntil.
 */
export function bendWriter(env: Env, ctx: ExecutionContext, latQ: string, lonQ: string, bucketKm: number): OnFetched | undefined {
  if (!enabled(env)) return undefined;
  return (all, upstreamId, upstreamNowMs) => {
    try {
      const tileLat = parseFloat(latQ);
      const tileLon = parseFloat(lonQ);
      const now = Date.now();
      if (!covers(tileLat, tileLon, bucketKm)) {
        if (touches(tileLat, tileLon, bucketKm) && now - lastSkipCountMs >= WRITE_MIN_INTERVAL_MS) {
          lastSkipCountMs = now;
          count(env, "skipped_not_covered");
        }
        return;
      }
      const source = sourceName(upstreamId);
      if (!source) return;
      if (now - lastSendMs < WRITE_MIN_INTERVAL_MS) return;
      lastSendMs = now;
      const snap: Snapshot = { t: Math.floor(upstreamNowMs / 1000), source, aircraft: extract(all) };
      const stub = env.BEND_SNAPSHOT!.get(env.BEND_SNAPSHOT!.idFromName(DO_NAME));
      ctx.waitUntil(
        Promise.resolve()
          .then(() => stub.fetch(`${DO_BASE}/put`, { method: "PUT", body: JSON.stringify(snap) }))
          .then((r) => count(env, r.status === 204 ? "written" : r.status === 409 ? "refused_30s" : "write_error"))
          .catch(() => count(env, "write_error")),
      );
    } catch {
      count(env, "write_error");
    }
  };
}

/** The storage the 30 s rule reads. A Durable Object's storage, or a test's shared Map. */
export interface SnapStorage {
  get<T>(key: string): Promise<T | undefined>;
  put<T>(key: string, value: T): Promise<void>;
}

/**
 * The Durable Object's logic. The 30 s rule reads the STORED row's own `writtenAt` on every write, never
 * an instance field -- so it holds across isolates, colos, and a restart of the object itself.
 */
export class BendStore {
  constructor(
    private readonly storage: SnapStorage,
    private readonly now: () => number = Date.now,
  ) {}

  async write(snap: Snapshot): Promise<"written" | "refused_30s"> {
    const prev = await this.storage.get<Stored>("snap");
    const now = this.now();
    if (prev && now - prev.writtenAt < WRITE_MIN_INTERVAL_MS) return "refused_30s";
    await this.storage.put<Stored>("snap", { ...snap, writtenAt: now });
    return "written";
  }

  async read(): Promise<Snapshot | undefined> {
    const s = await this.storage.get<Stored>("snap");
    return s ? { t: s.t, source: s.source, aircraft: s.aircraft } : undefined;
  }
}

function validSnapshot(x: unknown): x is Snapshot {
  const s = x as Snapshot;
  return !!s && Number.isFinite(s.t) && typeof s.source === "string" && s.source in ATTRIBUTION && Array.isArray(s.aircraft);
}

/** One global instance (idFromName "bend"). PUT /put stores (204) or refuses (409); GET /get reads. */
export class BendSnapshotDO extends DurableObject<Env> {
  async fetch(request: Request): Promise<Response> {
    const store = new BendStore(this.ctx.storage);
    const path = new URL(request.url).pathname;
    if (request.method === "PUT" && path === "/put") {
      let body: unknown;
      try {
        body = await request.json();
      } catch {
        return new Response(null, { status: 400 });
      }
      if (!validSnapshot(body)) return new Response(null, { status: 400 });
      const r = await store.write({ t: body.t, source: body.source, aircraft: body.aircraft });
      return new Response(null, { status: r === "written" ? 204 : 409 });
    }
    if (request.method === "GET" && path === "/get") {
      const s = await store.read();
      return s ? Response.json(s) : new Response(null, { status: 404 });
    }
    return new Response(null, { status: 405 });
  }
}

export type SiteFeedResponse =
  | { available: false; reason: "disabled" | "empty" }
  | {
      available: true;
      v: 1;
      centre: { lat: number; lon: number };
      radius_km: number;
      t: number;
      age_s: number;
      stale: boolean;
      source: string;
      attribution: string;
      aircraft: SiteAircraft[];
    };

interface Cached {
  cachedAt: number;
  snap: Snapshot | null;
}

/** The reader. Colo cache first (its expiry checked on cachedAt, not left to the cache), then the DO. */
export async function readSnapshot(env: Env, nowMs: number = Date.now(), cache: Cache = caches.default): Promise<SiteFeedResponse> {
  if (!enabled(env)) return { available: false, reason: "disabled" };
  const key = new Request(CACHE_KEY);
  let snap: Snapshot | null | undefined;
  const hit = await cache.match(key);
  if (hit) {
    const c = (await hit.json()) as Cached;
    if (nowMs - c.cachedAt < COLO_CACHE_MS) snap = c.snap;
  }
  if (snap === undefined) {
    const stub = env.BEND_SNAPSHOT!.get(env.BEND_SNAPSHOT!.idFromName(DO_NAME));
    const r = await stub.fetch(`${DO_BASE}/get`);
    snap = r.status === 200 ? ((await r.json()) as Snapshot) : null;
    const body: Cached = { cachedAt: nowMs, snap };
    await cache.put(
      key,
      new Response(JSON.stringify(body), {
        headers: { "Content-Type": "application/json", "Cache-Control": `max-age=${Math.ceil(COLO_CACHE_MS / 1000)}` },
      }),
    );
  }
  if (!snap) return { available: false, reason: "empty" };
  const age_s = Math.max(0, Math.floor(nowMs / 1000) - snap.t);
  const stale = age_s > MAX_SHOW_AGE_S;
  return {
    available: true,
    v: 1,
    centre: { lat: BEND.lat, lon: BEND.lon },
    radius_km: RADIUS_KM,
    t: snap.t,
    age_s,
    stale,
    source: snap.source,
    attribution: ATTRIBUTION[snap.source] ?? "",
    aircraft: stale ? [] : snap.aircraft,
  };
}

/** The service-binding entrypoint: `[[services]] service = "blipscope-proxy", entrypoint = "BendFeed"`. */
export class BendFeed extends WorkerEntrypoint<Env> {
  async snapshot(): Promise<SiteFeedResponse> {
    return readSnapshot(this.env);
  }
}
