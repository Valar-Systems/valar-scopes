// blipscope-wx-ingest-dev: Plan S's staging copy of the weather ingest (docs/wx-plan-s.md; proxy/wx-ingest-dev.wrangler.toml).
// Bound only to blipscope-wx-dev. NOTHING in production imports this file: scripts/check-wx-plant.mjs proves the
// production bundle (wx-ingest.wrangler.toml) carries no PLANT_MARKER, and its selftest proves that check can fail.
//
// Same shape as production's entry (ingest-worker.ts): a check at the cron firing and another 30 s in, in one
// invocation, so the per-invocation CPU it measures is the shape production runs. What each check does is MODE:
//   plant       a synthetic busy loop of PLANT_ITER rounds -- the positive control for the CPU-limit instrument (S1, S2).
//               It touches no bucket and no upstream.
//   cref, opuk  production's checkAll for that one product (S3, S4). Any other PRODUCTS id is the same for that row:
//               the M stage's opwe, opsn and opmt are production rows since the code stage (docs/wx-opera-code-stage.md),
//               and its opux was opuk grown, which opuk now is, so MODE opux is a mode error.
//   off         one line, nothing else (S0, S5; the toml also gives it no cron).
//   anything else  one error line, nothing else -- never a silent fall-back to a real check.

import { type Fetch, type IngestOpts, DEFAULT_OPTS } from "./ingest";
import { type IngestEnv, checkAll } from "./ingest-worker";
import { PRODUCTS, type Product } from "./products";

export interface DevEnv extends IngestEnv { MODE?: string }

/** The plant's log evt, and the string check-wx-plant.mjs looks for in a bundle. Defined once, here only. */
export const PLANT_MARKER = "wx_plant_synthetic_busy_loop";

/** Rounds per check. About 0.9 s of CPU per check on a 2026 laptop (local timing picks the count only; S1 measures
 *  it on Cloudflare's runtime). Two checks per invocation. */
export const PLANT_ITER = 800_000_000;

/** xorshift32, `iter` rounds. Bounded by count, not time: Date.now() does not advance during CPU in workerd. The
 *  result is logged so the loop cannot be optimised away. The guard names PLANT_MARKER so the marker travels WITH the
 *  loop: a bundle that pulls in plant() alone (no devCheck) still carries it. The first draft had the marker only in
 *  devCheck's log line, and check-wx-plant.mjs's PB1s found a bundle with the loop and no marker. */
export function plant(iter: number): number {
  if (!(iter >= 0)) throw new Error(PLANT_MARKER);
  let x = 2463534242 >>> 0;
  for (let i = 0; i < iter; i++) {
    x ^= x << 13; x >>>= 0;
    x ^= x >>> 17;
    x ^= x << 5; x >>>= 0;
  }
  return x;
}

/** Rows only the dev copy can reach. Empty since the code stage: the M stage's were promoted to PRODUCTS (DP2). */
export const DEV_PRODUCTS: readonly Product[] = [];

/** One check of the dev copy, by MODE. doFetch, log, opts and iter are injectable for the tests. */
export async function devCheck(env: DevEnv, label: string, doFetch?: Fetch,
                               log: (line: string) => void = (l) => console.log(l),
                               opts: IngestOpts = DEFAULT_OPTS, iter: number = PLANT_ITER): Promise<void> {
  const mode = env.MODE ?? "off";
  if (mode === "plant") {
    log(JSON.stringify({ evt: PLANT_MARKER, check: label, iter, result: plant(iter) }));
    return;
  }
  const p = PRODUCTS.find((q) => q.id === mode) ?? DEV_PRODUCTS.find((q) => q.id === mode);
  if (p) {
    await checkAll(env, label, doFetch, log, opts, [p]);
    return;
  }
  log(JSON.stringify({ evt: mode === "off" ? "wx_dev_off" : "wx_dev_mode_error", check: label, mode }));
}

export default {
  async scheduled(controller: ScheduledController, env: DevEnv): Promise<void> {
    await devCheck(env, ":00");
    const wait = 30_000 - (Date.now() - controller.scheduledTime);
    if (wait > 0) await new Promise((r) => setTimeout(r, wait));
    await devCheck(env, ":30");
  },
} satisfies ExportedHandler<DevEnv>;
