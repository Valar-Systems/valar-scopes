// Weatherscope's NWS fetch layer (docs/weatherscope-stage4.md S4.11.3): one call shape, one hold-and-last-good store,
// per-isolate coalescing, and the breakers from upstreams/types.ts as they are.
//
// THE STORE. Each key keeps ONE entry in the Cache API: what was last fetched, with the time it was fetched. Freshness
// is decided here from that time (as sitefeed/bend.ts does), never left to the cache's own expiry, so:
//   - inside the hold, the entry is served as `ok` and NWS is not called;
//   - past the hold, NWS is called; if that fails, the same entry is served as `stale`, with ITS time;
//   - the entry outlives its hold by a day (LAST_GOOD_S), which is how long a last good copy lasts.
// A failure with no entry is `none`, which the routes answer as `unavailable` -- never as an empty list.
//
// NOTHING ABOUT THE DEVICE REACHES NWS. The only request-derived value in any URL is the quantised point, and the log
// lines carry the kind, the status and the time only: a grid or a station is a location to within a few km (S4.11.4).

import { breakerAllows, breakerRecord } from "../upstreams/types";

export const NWS = "https://api.weather.gov";
/** W3's own User-Agent (S4.9): NWS refuses a call without one, so every call carries it. */
export const UA = "Weatherscope (support@valarsystems.com)";
export const BREAKERS = ["nws_points", "nws_alerts", "nws_stations", "nws_obs", "nws_forecast"] as const;
export type BreakerId = (typeof BREAKERS)[number];

const TIMEOUT_MS = 8_000;   // W3's slowest single call was 3,567 ms
export const LAST_GOOD_S = 86_400;
const KEY_HOST = "https://cache.weatherscope/v1/";

let clock = (): number => Math.floor(Date.now() / 1000);
export const nowS = (): number => clock();
/** Tests only: the holds are decided on this clock, so a test can step past one. */
export function __setClockForTests(f: (() => number) | null): void {
  clock = f ?? ((): number => Math.floor(Date.now() / 1000));
}

/** NWS answered, but not with something usable. */
export class NwsRefused extends Error {}

function logCall(kind: string, status: number, t0: number): void {
  console.log(JSON.stringify({ evt: "nws", kind, status, ms: Date.now() - t0 }));
}

// The same line upstreams/chain.ts writes for a source it did not call, so one reader counts both.
function logSkip(id: string, op: string, reason: string): void {
  console.log(JSON.stringify({ evt: "upstream_skip", id, op, reason }));
}

/** One GET to NWS. Returns the status and, for a 2xx, the parsed body. Anything off NWS's own origin is refused before
 *  a socket opens, so a URL taken from an NWS document can never send a call elsewhere. */
export async function nwsGet(url: string, kind: string): Promise<{ status: number; json: unknown }> {
  if (!url.startsWith(NWS + "/")) throw new NwsRefused("off_origin");
  const t0 = Date.now();
  let res: Response;
  try {
    res = await fetch(url, {
      headers: { "User-Agent": UA, Accept: "application/geo+json" },
      signal: AbortSignal.timeout(TIMEOUT_MS),
    });
  } catch (err) {
    logCall(kind, 0, t0);
    throw err;
  }
  logCall(kind, res.status, t0);
  if (res.status < 200 || res.status > 299) {
    await res.body?.cancel();
    return { status: res.status, json: null };
  }
  try {
    return { status: res.status, json: await res.json() };
  } catch {
    throw new NwsRefused("not_json");
  }
}

export interface Stored<T> { fetchedUtc: number; data: T }
export type Got<T> =
  | { kind: "ok"; fetchedUtc: number; data: T }
  | { kind: "stale"; fetchedUtc: number; data: T }
  | { kind: "none"; reason: "upstream_failed" | "breaker_open" };

async function load<T>(key: string): Promise<Stored<T> | null> {
  const hit = await caches.default.match(new Request(KEY_HOST + key));
  if (!hit) return null;
  try {
    const s = (await hit.json()) as Stored<T>;
    return typeof s?.fetchedUtc === "number" ? s : null;
  } catch {
    return null;
  }
}

async function save<T>(key: string, s: Stored<T>, ttlS: number): Promise<void> {
  await caches.default.put(new Request(KEY_HOST + key), new Response(JSON.stringify(s), {
    headers: { "Content-Type": "application/json", "Cache-Control": `max-age=${ttlS}` },
  }));
}

/** What a fetcher hands back: the data, and whether the breaker should hear about it. A /points 404 is an ANSWER
 *  (out of coverage), so it is stored and served but moves no breaker (S4.11.2, WS8). */
export interface Fetched<T> { data: T; countsForBreaker: boolean }

const inflight = new Map<string, Promise<Got<unknown>>>();

/** The hold-and-last-good read of one key. `holdS` is how long an entry is served without calling NWS. */
export async function held<T>(
  key: string,
  holdS: (data: T) => number,
  id: BreakerId,
  op: string,
  fetcher: () => Promise<Fetched<T>>,
): Promise<Got<T>> {
  const now = nowS();
  const prev = await load<T>(key);
  if (prev && now - prev.fetchedUtc < holdS(prev.data)) return { kind: "ok", fetchedUtc: prev.fetchedUtc, data: prev.data };
  if (!breakerAllows(id)) {
    logSkip(id, op, "breaker_open");
    return prev ? { kind: "stale", fetchedUtc: prev.fetchedUtc, data: prev.data } : { kind: "none", reason: "breaker_open" };
  }
  // Two boards asking for one key in the same moment share one fetch: the Cache API alone doesn't coalesce.
  let p = inflight.get(key) as Promise<Got<T>> | undefined;
  if (!p) {
    p = (async (): Promise<Got<T>> => {
      try {
        const f = await fetcher();
        if (f.countsForBreaker) breakerRecord(id, true);
        const s: Stored<T> = { fetchedUtc: now, data: f.data };
        await save(key, s, holdS(f.data) + LAST_GOOD_S);
        return { kind: "ok", fetchedUtc: s.fetchedUtc, data: s.data };
      } catch {
        breakerRecord(id, false);
        return prev ? { kind: "stale", fetchedUtc: prev.fetchedUtc, data: prev.data } : { kind: "none", reason: "upstream_failed" };
      }
    })().finally(() => inflight.delete(key));
    inflight.set(key, p as Promise<Got<unknown>>);
  }
  return p;
}
