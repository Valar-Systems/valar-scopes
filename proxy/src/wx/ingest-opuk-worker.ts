// blipscope-wx-ingest-opuk: the opuk ingest, in its own Worker (proxy/wx-ingest-opuk.wrangler.toml;
// docs/wx-opuk-split.md). R3 rolled opuk back because an invocation that built a cref frame AND an opuk frame ran
// 5.4-6.1 s of CPU; here each invocation builds at most opuk's frames, with its own cpu_ms, deploys and rollbacks.
//
// Production's shape (ingest-worker.ts): a check at the cron firing and another 30 s in. WX_INGEST is the stop:
// "on" checks opuk; anything else (the toml's [env.inert] sets "off") logs one line per check and touches nothing,
// so stopping opuk is a version change -- immediate -- not a cron removal, which takes minutes to propagate.

import { type IngestEnv, checkAll } from "./ingest-worker";
import { type Product, PRODUCTS } from "./products";

export interface OpukEnv extends IngestEnv { WX_INGEST?: string }

/** What this Worker checks: opuk only. FT2 and FX (test/wx-ingest-split.test.ts) hold this to the tomls. */
export const OPUK_WORKER: readonly Product[] = PRODUCTS.filter((p) => p.id === "opuk");

async function check(env: OpukEnv, label: string): Promise<void> {
  if (env.WX_INGEST !== "on") {
    console.log(JSON.stringify({ evt: "wx_ingest_off", check: label, value: env.WX_INGEST ?? null }));
    return;
  }
  await checkAll(env, label, undefined, undefined, undefined, OPUK_WORKER);
}

export default {
  async scheduled(controller: ScheduledController, env: OpukEnv): Promise<void> {
    await check(env, ":00");
    const wait = 30_000 - (Date.now() - controller.scheduledTime);
    if (wait > 0) await new Promise((r) => setTimeout(r, wait));
    await check(env, ":30");
  },
} satisfies ExportedHandler<OpukEnv>;
