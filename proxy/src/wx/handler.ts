// GET /api/v1/blipscope/wx/<product>?lat=&lon=&r= -- one WXC1 crop of the newest frame (docs/wx-server.md,
// "The endpoint"). Reached only after the proxy's device auth and per-key rate limit, like /blips.
//
// R2 reads per request: 0 for a refusal, 1 (`latest`) for a 304, a stale frame or a box off the grid, and
// 1 + at most 4 tiles otherwise. The tests count them through a wrapped binding.

import type { Env } from "../types";
import { errorResponse } from "../util";
import { CAP_BYTES, boxMissesGrid, cropCellBox, encodeBox, quantise } from "./crop";
import { type FrameMeta, NO_COVERAGE, frameMeta, tileBytes, tileOffset, tiledCellAt, tilesFor } from "./grid";
import { type Product, cropEtag, frameKey, latestKey, productById } from "./products";

/** What `latest` holds. Written only by the ingest (ingest.ts), forward-only. */
export interface LatestRecord {
  product: string;
  epoch: number;
  key: string;
  ingestedAt: number;   // ms
  ni: number; nj: number; la1: number; lo1: number; di: number; dj: number; tile: number;
}

export async function readLatest(bucket: R2Bucket, p: Product): Promise<LatestRecord | null> {
  const o = await bucket.get(latestKey(p));
  if (!o) return null;
  const rec = JSON.parse(await o.text()) as LatestRecord;
  return rec.product === p.id ? rec : null;
}

/** /healthz's `wx` field: the age of each product's newest frame, or null when there is none -- never a
 *  made-up age. "not_configured" without the binding. */
export async function wxHealth(env: Env, products: readonly Product[]): Promise<unknown> {
  if (!env.WX) return "not_configured";
  const out: Record<string, { frameAgeS: number | null }> = {};
  for (const p of products) {
    const rec = await readLatest(env.WX, p).catch(() => null);
    out[p.id] = { frameAgeS: rec ? Math.round(Date.now() / 1000 - rec.epoch) : null };
  }
  return out;
}

export async function handleWx(request: Request, env: Env, productId: string): Promise<Response> {
  if (!env.WX) return errorResponse(503, "not_configured");
  const p = productById(productId);
  if (!p) return errorResponse(404, "not_found");

  // The same validation as /blips, so a device's one query is valid for both.
  const url = new URL(request.url);
  const lat = parseFloat(url.searchParams.get("lat") ?? "");
  const lon = parseFloat(url.searchParams.get("lon") ?? "");
  const r = parseFloat(url.searchParams.get("r") ?? "");
  if (!Number.isFinite(lat) || Math.abs(lat) > 90) return errorResponse(400, "bad_lat");
  if (!Number.isFinite(lon) || Math.abs(lon) > 180) return errorResponse(400, "bad_lon");
  if (!Number.isFinite(r) || r <= 0) return errorResponse(400, "bad_r");
  const q = quantise(lat, lon, r);

  const rec = await readLatest(env.WX, p);
  if (!rec || Date.now() / 1000 - rec.epoch > p.maxAgeS) return errorResponse(503, "wx_stale");

  const etag = cropEtag(p, rec.epoch, q.lat2, q.lon2, q.rKm);
  const headers = { ETag: etag, "X-Wx-Frame": String(rec.epoch), "Cache-Control": "no-cache" };
  if (request.headers.get("If-None-Match") === etag) return new Response(null, { status: 304, headers });

  const m: FrameMeta = frameMeta(p.id, rec.epoch,
    { ni: rec.ni, nj: rec.nj, la1: rec.la1, lo1: rec.lo1, di: rec.di, dj: rec.dj }, rec.tile);
  const box = cropCellBox(m, q);
  let cellAt: (r: number, c: number) => number;
  if (boxMissesGrid(m, box)) {
    cellAt = () => NO_COVERAGE;          // outside CONUS: the all-14 crop, without reading a tile
  } else {
    const want = tilesFor(m, box.r0, box.r0 + box.rows - 1, box.c0, box.c0 + box.cols - 1);
    const got = await Promise.all(want.map(async ([ty, tx]) => {
      const o = await env.WX!.get(frameKey(p, rec.epoch), { range: { offset: tileOffset(m, ty, tx), length: tileBytes(m.tile) } });
      return o ? [`${ty}-${tx}`, new Uint8Array(await o.arrayBuffer())] as const : null;
    }));
    if (got.some((t) => !t || t[1].length !== tileBytes(m.tile))) return errorResponse(503, "wx_stale");
    cellAt = tiledCellAt(m, new Map(got as Array<readonly [string, Uint8Array]>));
  }
  const crop = encodeBox(m, rec.epoch, box, cellAt);
  if (crop.bytes.length > CAP_BYTES) return errorResponse(503, "wx_too_large");   // cannot happen at <= 405 cells; refused, not truncated
  return new Response(crop.bytes, { status: 200, headers: { ...headers, "Content-Type": "application/octet-stream" } });
}
