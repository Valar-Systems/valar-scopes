// blipscope-wx-ingest-<id>: one MRMS product's ingest per Worker, beside cref's own (W4: docs/weatherscope-stage4.md
// S4.12.3). An [env.<id>] in wx-ingest.wrangler.toml points its `main` here; cref's top-level Worker (ingest-worker.ts)
// is untouched. Each invocation builds at most its own product's frames, with its own cpu_ms, deploys and rollbacks, the
// shape the OPERA Workers already have (ingest-opera-worker.ts).
//
// Two vars decide a check, as in the OPERA entry:
//   WX_INGEST  the stop: "on" checks PRODUCT; anything else ([env.<id>-inert] sets "off") logs one wx_ingest_off line per
//              check and touches nothing, so stopping a product is a version change -- immediate -- not a cron removal.
//   PRODUCT    the MRMS product this Worker builds. Anything that is not an MRMS product's id logs one wx_ingest_error
//              line per check and builds nothing: never a fall-back to some other product.

import { type IngestEnv, checkAll } from "./ingest-worker";
import { type Product, productById } from "./products";

export interface MrmsEnv extends IngestEnv { PRODUCT?: string; WX_INGEST?: string }

/** What a PRODUCT var names: an MRMS row, or nothing. X7 and FX (test/wx-shsr.test.ts, test/wx-ingest-split.test.ts)
 *  hold it to the toml. */
export function mrmsProduct(id: string | undefined): Product | null {
  const p = id === undefined ? null : productById(id);
  return p && p.source === "mrms" ? p : null;
}

async function check(env: MrmsEnv, label: string): Promise<void> {
  if (env.WX_INGEST !== "on") {
    console.log(JSON.stringify({ evt: "wx_ingest_off", check: label, value: env.WX_INGEST ?? null }));
    return;
  }
  const p = mrmsProduct(env.PRODUCT);
  if (!p) {
    console.log(JSON.stringify({ evt: "wx_ingest_error", check: label, product: env.PRODUCT ?? null,
                                 err: "PRODUCT names no MRMS product" }));
    return;
  }
  await checkAll(env, label, undefined, undefined, undefined, [p]);
}

export default {
  async scheduled(controller: ScheduledController, env: MrmsEnv): Promise<void> {
    await check(env, ":00");
    const wait = 30_000 - (Date.now() - controller.scheduledTime);
    if (wait > 0) await new Promise((r) => setTimeout(r, wait));
    await check(env, ":30");
  },
} satisfies ExportedHandler<MrmsEnv>;
