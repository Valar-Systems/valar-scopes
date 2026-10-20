// The weather ingest, (a') (docs/wx-server.md, "The ingest"). Idempotent by construction:
//   - a stored frame is never re-written: it is created with If-None-Match: *, so of two writers (an
//     overlapping cron, a retry) at most one succeeds;
//   - `latest` only moves forward: it is replaced only with etagMatches on the record just read, and only
//     by a later frame; a lost race re-reads once and gives up if the stored frame is now >= ours.
// Web APIs only (fetch, DecompressionStream, R2): runs in the ingest Worker and, for WS8, under Node.

import { FrameBuilder, frameMeta, readHeader, type GridGeom } from "./grid";
import { decodeMrms, parseGrib2 } from "./mrms";
import type { LatestRecord } from "./handler";
import { type Product, frameKey, framesPrefix, latestKey } from "./products";

export const NOAA_BUCKET = "https://noaa-mrms-pds.s3.amazonaws.com";
export const RETAIN_S = 3600;
const LOOKBACK_S = 20 * 60;

export type Fetch = (input: string) => Promise<Response>;

const day = (ms: number) => new Date(ms).toISOString().slice(0, 10).replace(/-/g, "");
const hms = (ms: number) => new Date(ms).toISOString().slice(11, 19).replace(/:/g, "");

/** Frame epoch (UTC s) from a NOAA key, or null when the key is not one of this product's frames. */
export function epochOfKey(p: Product, key: string): number | null {
  const m = key.match(/_(\d{8})-(\d{6})\.grib2\.gz$/);
  if (!m || !key.includes(`/${p.noaaStem}_`)) return null;
  const [, d, t] = m as unknown as [string, string, string];
  const ms = Date.parse(`${d.slice(0, 4)}-${d.slice(4, 6)}-${d.slice(6, 8)}T${t.slice(0, 2)}:${t.slice(2, 4)}:${t.slice(4, 6)}Z`);
  return Number.isFinite(ms) ? ms / 1000 : null;
}

/** The newest frame NOAA has published in the last 20 min. Lists the previous day's folder too when the
 *  window crosses midnight UTC. */
export async function newestNoaaKey(p: Product, nowMs: number, doFetch: Fetch): Promise<string | null> {
  const from = nowMs - LOOKBACK_S * 1000;
  const days = day(from) === day(nowMs) ? [day(nowMs)] : [day(from), day(nowMs)];
  let best: string | null = null;
  for (const d of days) {
    const startAfter = `${p.noaaPrefix}/${d}/${p.noaaStem}_${day(from) === d ? `${d}-${hms(from)}` : `${d}-000000`}.grib2.gz`;
    const url = `${NOAA_BUCKET}/?list-type=2&prefix=${encodeURIComponent(`${p.noaaPrefix}/${d}/`)}&start-after=${encodeURIComponent(startAfter)}`;
    const res = await doFetch(url);
    if (!res.ok) throw new Error(`NOAA list ${res.status}`);
    for (const m of (await res.text()).matchAll(/<Key>([^<]+)<\/Key>/g)) {
      const k = m[1] as string;
      if (epochOfKey(p, k) !== null && (best === null || k > best)) best = k;
    }
  }
  return best;
}

async function inflate(gz: Uint8Array): Promise<Uint8Array> {
  const out = new Response(new Blob([gz]).stream().pipeThrough(new DecompressionStream("gzip")));
  return new Uint8Array(await out.arrayBuffer());
}

/** Decode one NOAA frame into a WXG1 object. The geometry is read first (decodeMrms, moved from the spike
 *  unchanged, reports it only at the end), so the tiled buffer exists before the first row arrives. */
export async function buildFrame(p: Product, epoch: number, gz: Uint8Array): Promise<FrameBuilder> {
  const { grid } = parseGrib2(await inflate(gz));
  if (!grid.n2s || !grid.w2e) throw new Error("MRMS grid is not north->south, west->east");
  const g: GridGeom = { ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj };
  const fb = new FrameBuilder(frameMeta(p.id, epoch, g));
  const codes = new Uint8Array(g.ni);
  await decodeMrms(gz, (row, vals) => {
    for (let j = 0; j < vals.length; j++) codes[j] = p.codeOf(vals[j] as number);
    fb.setRow(row, codes);
  });
  return fb;
}

export type LatestOutcome = "advanced" | "created" | "kept-newer" | "conflict";

/** Move `latest` to `rec` only if rec is later than what is stored. Forward-only under any interleaving. */
export async function advanceLatest(bucket: R2Bucket, p: Product, rec: LatestRecord): Promise<LatestOutcome> {
  const body = JSON.stringify(rec);
  for (let attempt = 0; attempt < 2; attempt++) {
    const cur = await bucket.get(latestKey(p));
    if (!cur) {
      const put = await bucket.put(latestKey(p), body, { onlyIf: new Headers({ "If-None-Match": "*" }) });
      if (put) return "created";
      continue;                                         // someone created it first: re-read
    }
    const stored = JSON.parse(await cur.text()) as LatestRecord;
    if (stored.epoch >= rec.epoch) return "kept-newer";
    const put = await bucket.put(latestKey(p), body, { onlyIf: { etagMatches: cur.etag } });
    if (put) return "advanced";
  }
  return "conflict";
}

export interface IngestResult {
  status: "no-frame" | "already-stored" | "lost-race" | "stored";
  key?: string; epoch?: number; latest?: LatestOutcome; pruned?: number; bytes?: number;
}

/** One check, for one product: the newest NOAA frame, stored once, `latest` moved forward, old frames
 *  pruned. Safe to call any number of times, in parallel. */
export async function ingestOnce(bucket: R2Bucket, p: Product, nowMs: number, doFetch: Fetch): Promise<IngestResult> {
  const key = await newestNoaaKey(p, nowMs, doFetch);
  if (!key) return { status: "no-frame" };
  const epoch = epochOfKey(p, key)!;
  const out: IngestResult = { status: "stored", key, epoch };
  if (await bucket.head(frameKey(p, epoch))) {
    out.status = "already-stored";
  } else {
    const res = await doFetch(`${NOAA_BUCKET}/${key}`);
    if (!res.ok) throw new Error(`NOAA get ${res.status}`);
    const fb = await buildFrame(p, epoch, new Uint8Array(await res.arrayBuffer()));
    const put = await bucket.put(frameKey(p, epoch), fb.bytes, { onlyIf: new Headers({ "If-None-Match": "*" }) });
    out.bytes = fb.bytes.length;
    if (!put) out.status = "lost-race";
  }
  // `latest` is advanced whether this run wrote the frame or found it stored: a run that died between
  // its frame write and its latest write is completed by the next one.
  const m = await bucket.head(frameKey(p, epoch));
  if (m) {
    const g = await frameGeometry(bucket, p, epoch);
    out.latest = await advanceLatest(bucket, p, { product: p.id, epoch, key: frameKey(p, epoch), ingestedAt: nowMs, ...g });
  }
  out.pruned = await pruneOld(bucket, p, nowMs);
  return out;
}

async function frameGeometry(bucket: R2Bucket, p: Product, epoch: number) {
  const o = await bucket.get(frameKey(p, epoch), { range: { offset: 0, length: 64 } });
  if (!o) throw new Error("frame vanished between head and get");
  const m = readHeader(new Uint8Array(await o.arrayBuffer()));
  return { ni: m.ni, nj: m.nj, la1: m.la1, lo1: m.lo1, di: m.di, dj: m.dj, tile: m.tile };
}

/** Delete this product's frames older than RETAIN_S. Never the frame `latest` names. */
export async function pruneOld(bucket: R2Bucket, p: Product, nowMs: number): Promise<number> {
  const cut = nowMs / 1000 - RETAIN_S;
  const cur = await bucket.get(latestKey(p));
  const keep = cur ? (JSON.parse(await cur.text()) as LatestRecord).key : null;
  const old: string[] = [];
  let cursor: string | undefined;
  do {
    const l = await bucket.list({ prefix: framesPrefix(p), cursor });
    for (const o of l.objects) {
      const e = Number(o.key.slice(framesPrefix(p).length).replace(/\.wxg$/, ""));
      if (Number.isFinite(e) && e < cut && o.key !== keep) old.push(o.key);
    }
    cursor = l.truncated ? l.cursor : undefined;
  } while (cursor);
  if (old.length) await bucket.delete(old);
  return old.length;
}
