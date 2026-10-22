// blipscope-wx-ingest: the weather ingest Worker (proxy/wx-ingest.wrangler.toml; docs/wx-server.md).
// NOT DEPLOYED: Daniel creates the bucket and deploys it. It has no route; its only entry is the cron.
//
// (a'), ruled 2026-10-20: every product is checked at the cron firing and again 30 s into the minute,
// because NOAA lands files 17-28 s past the minute (A7). Every check is idempotent (ingest.ts), so an
// overlapping run or a retry can only find the work already done.

import { ingestOnce } from "./ingest";
import { PRODUCTS } from "./products";

export interface IngestEnv { WX: R2Bucket }

async function checkAll(env: IngestEnv, label: string): Promise<void> {
  for (const p of PRODUCTS) {
    const t0 = Date.now();
    try {
      const r = await ingestOnce(env.WX, p, t0, (u) => fetch(u));
      console.log(JSON.stringify({ evt: "wx_ingest", check: label, product: p.id, ...r,
        lagS: r.epoch ? Math.round(Date.now() / 1000 - r.epoch) : null, ms: Date.now() - t0 }));
    } catch (err) {
      console.log(JSON.stringify({ evt: "wx_ingest_error", check: label, product: p.id, err: String(err), ms: Date.now() - t0 }));
    }
  }
}

export default {
  async scheduled(controller: ScheduledController, env: IngestEnv): Promise<void> {
    await checkAll(env, ":00");
    const wait = 30_000 - (Date.now() - controller.scheduledTime);
    if (wait > 0) await new Promise((r) => setTimeout(r, wait));
    await checkAll(env, ":30");
  },
} satisfies ExportedHandler<IngestEnv>;
