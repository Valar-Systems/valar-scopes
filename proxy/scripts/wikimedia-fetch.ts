/**
 * wikimedia-fetch.ts -- fetch from Wikimedia with ONE retry on HTTP 429.
 *
 * Wikimedia rate-limits per IP, across commons.wikimedia.org and
 * upload.wikimedia.org alike. In the photo dashboard a search pulls ~25
 * thumbnails into the browser from the same IP, so the dashboard's own next
 * call (a preview, a replace) can land inside that burst and get a 429 -- seen
 * twice on 2026-09-23, each time fine when repeated seconds later.
 *
 * So: retry once, after Retry-After (or a short default), and if it is still
 * 429 throw RateLimited, which says so plainly. A 429 is not a verdict on the
 * photo and must never be reported as one -- the dashboard used to show it as
 * "Rejected:", the same word the license gate uses.
 *
 * No Node imports, so vitest's Workers pool can run it (test/wikimedia-fetch.test.ts).
 */

export class RateLimited extends Error {}

export const RETRY_DEFAULT_MS = 3_000;
export const RETRY_MAX_MS = 15_000; // asked to wait longer than this: report, don't hang the UI

export function retryAfterMs(h: string | null, now = Date.now()): number {
  if (!h) return RETRY_DEFAULT_MS;
  const secs = Number(h);
  if (Number.isFinite(secs)) return Math.max(0, secs * 1000);
  const at = Date.parse(h);
  return Number.isFinite(at) ? Math.max(0, at - now) : RETRY_DEFAULT_MS;
}

export interface WikimediaFetchOpts {
  fetchImpl?: typeof fetch;
  sleep?: (ms: number) => Promise<void>;
  log?: (msg: string) => void;
}

export async function wikimediaFetch(
  url: string,
  userAgent: string,
  opts: WikimediaFetchOpts = {},
): Promise<Response> {
  const doFetch = opts.fetchImpl ?? fetch;
  const sleep = opts.sleep ?? ((ms: number) => new Promise<void>((r) => setTimeout(r, ms)));
  const host = new URL(url).host;
  for (let attempt = 0; ; attempt++) {
    const res = await doFetch(url, { headers: { "User-Agent": userAgent } });
    if (res.status !== 429) {
      if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
      return res;
    }
    await res.body?.cancel();
    const wait = retryAfterMs(res.headers.get("retry-after"));
    if (attempt >= 1 || wait > RETRY_MAX_MS) {
      const why = attempt >= 1 ? "still 429 after one retry" : `it asks for ${Math.round(wait / 1000)} s`;
      throw new RateLimited(
        `Wikimedia (${host}) is rate-limiting this machine: ${why}. Wait a minute and try again.`,
      );
    }
    opts.log?.(`[wikimedia] 429 from ${host}; retrying once in ${wait} ms`);
    await sleep(wait);
  }
}
