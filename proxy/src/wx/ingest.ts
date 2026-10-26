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

export type Fetch = (input: string, init?: { signal?: AbortSignal }) => Promise<Response>;

/** One NOAA request's attempt policy (docs/wx-ingest-noaa-blips.md). NOAA's S3 is reached through Cloudflare, and
 *  on 2026-10-23 it answered 520/522/525 or hung for 24-288 s on about a quarter of the runs. */
export interface Retry { timeoutMs: number; attempts: number; waitsMs: number[] }
export interface IngestOpts { list: Retry; get: Retry }
export const DEFAULT_OPTS: IngestOpts = {
  list: { timeoutMs: 8_000, attempts: 3, waitsMs: [1_000, 2_000] },     // worst case 27 s
  get: { timeoutMs: 20_000, attempts: 2, waitsMs: [1_000] },            // about 13 MB; worst case 41 s
};

/** Thrown when every attempt at a NOAA request failed; `attempts` is how many were made. */
export class NoaaError extends Error {
  constructor(message: string, readonly attempts: number) { super(message); }
}

/** One NOAA request, retried. Each attempt is aborted at its timeout, body included (read() runs inside it). A 4xx
 *  answer is final; a 5xx, a network error or a timeout is retried after the stated wait. */
async function noaaRequest<T>(what: string, url: string, r: Retry, doFetch: Fetch,
                              read: (res: Response) => Promise<T>): Promise<{ value: T; attempts: number }> {
  let last = "";
  let a = 0;
  while (a < r.attempts) {
    a++;
    try {
      const res = await doFetch(url, { signal: AbortSignal.timeout(r.timeoutMs) });
      if (res.ok) return { value: await read(res), attempts: a };
      last = `${what} ${res.status}`;
      if (res.status >= 400 && res.status < 500) break;
    } catch (err) {
      last = `${what} ${err instanceof Error ? err.message : String(err)}`;
    }
    if (a < r.attempts) await new Promise((done) => setTimeout(done, r.waitsMs[a - 1] ?? 0));
  }
  throw new NoaaError(`${last} (${a} attempt${a === 1 ? "" : "s"})`, a);
}

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

/** Every frame NOAA has published in the last 20 min, oldest first, and the listing attempts used. Lists the
 *  previous day's folder too when the window crosses midnight UTC. */
export async function noaaKeys(p: Product, nowMs: number, doFetch: Fetch,
                               opts: IngestOpts = DEFAULT_OPTS): Promise<{ keys: string[]; attempts: number }> {
  const from = nowMs - LOOKBACK_S * 1000;
  const days = day(from) === day(nowMs) ? [day(nowMs)] : [day(from), day(nowMs)];
  const keys = new Set<string>();
  let attempts = 0;
  for (const d of days) {
    const startAfter = `${p.noaaPrefix}/${d}/${p.noaaStem}_${day(from) === d ? `${d}-${hms(from)}` : `${d}-000000`}.grib2.gz`;
    const url = `${NOAA_BUCKET}/?list-type=2&prefix=${encodeURIComponent(`${p.noaaPrefix}/${d}/`)}&start-after=${encodeURIComponent(startAfter)}`;
    const r = await noaaRequest("NOAA list", url, opts.list, doFetch, (res) => res.text());
    attempts = Math.max(attempts, r.attempts);
    for (const m of r.value.matchAll(/<Key>([^<]+)<\/Key>/g)) {
      const k = m[1] as string;
      if (epochOfKey(p, k) !== null) keys.add(k);
    }
  }
  return { keys: [...keys].sort(), attempts };
}

/** The newest frame NOAA has published in the last 20 min. */
export async function newestNoaaKey(p: Product, nowMs: number, doFetch: Fetch,
                                    opts: IngestOpts = DEFAULT_OPTS): Promise<string | null> {
  const { keys } = await noaaKeys(p, nowMs, doFetch, opts);
  return keys.length ? (keys[keys.length - 1] as string) : null;
}

async function inflate(gz: Uint8Array): Promise<Uint8Array> {
  const out = new Response(new Blob([gz]).stream().pipeThrough(new DecompressionStream("gzip")));
  return new Uint8Array(await out.arrayBuffer());
}

/** Decode one NOAA frame into a WXG1 object. The geometry is read first (decodeMrms, moved from the spike
 *  unchanged, reports it only at the end), so the tiled buffer exists before the first row arrives. */
export async function buildFrame(p: Product, epoch: number, gz: Uint8Array,
                                 onStage?: (name: string) => void): Promise<FrameBuilder> {
  const { grid } = parseGrib2(await inflate(gz));
  onStage?.("gunzip-geom");                    // the stage hooks exist for WS8's live meter only
  if (!grid.n2s || !grid.w2e) throw new Error("MRMS grid is not north->south, west->east");
  const g: GridGeom = { ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj };
  const fb = new FrameBuilder(frameMeta(p.id, epoch, g));
  onStage?.("alloc-tiled");
  const codes = new Uint8Array(g.ni);
  await decodeMrms(gz, (row, vals) => {
    for (let j = 0; j < vals.length; j++) codes[j] = p.codeOf(vals[j] as number);
    fb.setRow(row, codes);
  }, onStage);
  onStage?.("tiled");
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
  status: "no-frame" | "already-stored" | "lost-race" | "stored" | "backfilled";
  key?: string; epoch?: number; latest?: LatestOutcome; pruned?: number; bytes?: number;
  attempts?: { list: number; get: number };
}

/** One check, for one product. It builds AT MOST ONE frame: the newest frame NOAA has that the bucket lacks --
 *  the newest frame if it is new ("stored"), otherwise the newest hole in the 20-min window ("backfilled"), so a
 *  NOAA blip does not leave a hole in the loop. One build per check keeps an invocation's worst case at two
 *  builds, as before (cpu_ms covers the whole invocation). `latest` is then moved forward to the newest stored
 *  frame, and old frames are pruned. Safe to call any number of times, in parallel. */
export async function ingestOnce(bucket: R2Bucket, p: Product, nowMs: number, doFetch: Fetch,
                                 opts: IngestOpts = DEFAULT_OPTS): Promise<IngestResult> {
  const listed = await noaaKeys(p, nowMs, doFetch, opts);
  const attempts = { list: listed.attempts, get: 0 };
  const keys = listed.keys;
  if (!keys.length) return { status: "no-frame", attempts };
  const newest = keys[keys.length - 1] as string;
  let target: string | null = null;
  for (let i = keys.length - 1; i >= 0; i--) {
    const k = keys[i] as string;
    const e = epochOfKey(p, k)!;
    if (e < nowMs / 1000 - RETAIN_S) break;              // would be pruned at once
    if (!(await bucket.head(frameKey(p, e)))) { target = k; break; }
  }
  const key = target ?? newest;
  const epoch = epochOfKey(p, key)!;
  const out: IngestResult = { status: target === null ? "already-stored" : key === newest ? "stored" : "backfilled",
                              key, epoch, attempts };
  if (target !== null) {
    const got = await noaaRequest("NOAA get", `${NOAA_BUCKET}/${key}`, opts.get, doFetch,
                                  async (res) => new Uint8Array(await res.arrayBuffer()));
    attempts.get = got.attempts;
    const fb = await buildFrame(p, epoch, got.value);
    const put = await bucket.put(frameKey(p, epoch), fb.bytes, { onlyIf: new Headers({ "If-None-Match": "*" }) });
    out.bytes = fb.bytes.length;
    if (!put) out.status = "lost-race";
  }
  // `latest` is advanced to the NEWEST frame whenever it is stored -- whether this check wrote it, backfilled an
  // older one, or found everything stored: a run that died between its frame write and its latest write is
  // completed by the next one. advanceLatest is forward-only, so a backfill never moves it back.
  const newestEpoch = epochOfKey(p, newest)!;
  if (await bucket.head(frameKey(p, newestEpoch))) {
    const g = await frameGeometry(bucket, p, newestEpoch);
    out.latest = await advanceLatest(bucket, p, { product: p.id, epoch: newestEpoch, key: frameKey(p, newestEpoch),
                                                 ingestedAt: nowMs, ...g });
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
