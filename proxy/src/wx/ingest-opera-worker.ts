// blipscope-wx-ingest-<id>: one OPERA product's ingest per Worker (proxy/wx-ingest-opera.wrangler.toml, an [env.<id>]
// each; docs/wx-opera-code-stage.md §1d, Q-WORKERS). R3 rolled opuk back because an invocation that built a cref frame
// AND an opuk frame ran 5.4-6.1 s of CPU (docs/wx-opuk-split.md); here each invocation builds at most its own product's
// frames, with its own cpu_ms, deploys and rollbacks. opuk's Worker keeps its name, blipscope-wx-ingest-opuk.
//
// Production's shape (ingest-worker.ts): a check at the cron firing and another 30 s in. Two vars decide a check:
//   WX_INGEST  the stop: "on" checks PRODUCT; anything else (each [env.<id>-inert] sets "off") logs one line per check
//              and touches nothing, so stopping a product is a version change -- immediate -- not a cron removal,
//              which takes minutes to propagate.
//   PRODUCT    the OPERA product this Worker builds. Anything that is not an OPERA product's id logs one
//              wx_ingest_error line per check and builds nothing: never a fall-back to some other product.

import { type IngestEnv, checkAll } from "./ingest-worker";
import { type Product, productById } from "./products";

export interface OperaEnv extends IngestEnv { PRODUCT?: string; WX_INGEST?: string }

/** What a PRODUCT var names: an OPERA row, or nothing. FT2 and FX (test/wx-ingest-split.test.ts) hold it to the toml. */
export function operaProduct(id: string | undefined): Product | null {
  const p = id === undefined ? null : productById(id);
  return p && p.source === "opera" ? p : null;
}

async function check(env: OperaEnv, label: string): Promise<void> {
  if (env.WX_INGEST !== "on") {
    console.log(JSON.stringify({ evt: "wx_ingest_off", check: label, value: env.WX_INGEST ?? null }));
    return;
  }
  const p = operaProduct(env.PRODUCT);
  if (!p) {
    console.log(JSON.stringify({ evt: "wx_ingest_error", check: label, product: env.PRODUCT ?? null,
                                 err: "PRODUCT names no OPERA product" }));
    return;
  }
  await checkAll(env, label, undefined, undefined, undefined, [p]);
}

export default {
  async scheduled(controller: ScheduledController, env: OperaEnv): Promise<void> {
    await check(env, ":00");
    const wait = 30_000 - (Date.now() - controller.scheduledTime);
    if (wait > 0) await new Promise((r) => setTimeout(r, wait));
    await check(env, ":30");
  },
} satisfies ExportedHandler<OperaEnv>;
