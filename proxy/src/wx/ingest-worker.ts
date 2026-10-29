// blipscope-wx-ingest: the weather ingest Worker (proxy/wx-ingest.wrangler.toml; docs/wx-server.md).
// NOT DEPLOYED: Daniel creates the bucket and deploys it. It has no route; its only entry is the cron.
//
// (a'), ruled 2026-10-20: every product is checked at the cron firing and again 30 s into the minute,
// because NOAA lands files 17-28 s past the minute (A7). Every check is idempotent (ingest.ts), so an
// overlapping run or a retry can only find the work already done.

import { type Fetch, type IngestOpts, DEFAULT_OPTS, NoaaError, ingestOnce } from "./ingest";
import { PRODUCTS } from "./products";

export interface IngestEnv { WX: R2Bucket }

// The init carries each attempt's abort signal: dropping it (as `(u) => fetch(u)` did) would silently disable
// every timeout. test/wx-ingest.test.ts proves the default passes it.
const workerFetch: Fetch = (u, init) => fetch(u, init);

/** One check of every product, the products concurrently (docs/uk-data.md §1.3): opuk's listing and range reads
 *  overlap cref's NOAA fetch instead of queueing behind it. Each product's check is caught on its own, so one
 *  product's failure never stops another's. doFetch, log and opts are injectable for the tests. */
export async function checkAll(env: IngestEnv, label: string, doFetch: Fetch = workerFetch,
                               log: (line: string) => void = (l) => console.log(l),
                               opts: IngestOpts = DEFAULT_OPTS): Promise<void> {
  await Promise.all(PRODUCTS.map(async (p) => {
    const t0 = Date.now();
    try {
      const r = await ingestOnce(env.WX, p, t0, doFetch, opts);
      log(JSON.stringify({ evt: "wx_ingest", check: label, product: p.id, ...r,
        lagS: r.epoch ? Math.round(Date.now() / 1000 - r.epoch) : null, ms: Date.now() - t0 }));
    } catch (err) {
      // Logged only when every attempt at a NOAA request failed (NoaaError), or on a non-NOAA failure.
      const attempts = err instanceof NoaaError ? err.attempts : undefined;
      log(JSON.stringify({ evt: "wx_ingest_error", check: label, product: p.id, err: String(err), attempts,
        ms: Date.now() - t0 }));
    }
  }));
}

export default {
  async scheduled(controller: ScheduledController, env: IngestEnv): Promise<void> {
    await checkAll(env, ":00");
    const wait = 30_000 - (Date.now() - controller.scheduledTime);
    if (wait > 0) await new Promise((r) => setTimeout(r, wait));
    await checkAll(env, ":30");
  },
} satisfies ExportedHandler<IngestEnv>;
