import type { Env, RateLimit } from "./types";
import { clientIp, errorResponse } from "./util";

// Workers Rate Limiting bindings are per-colo token buckets (see wrangler.toml
// for the budgets and their reasoning). Both bindings are optional: absent in
// `wrangler dev`/tests, enforced wherever they're bound. The binding doesn't
// expose time-to-refill, so Retry-After is a fixed, honest-enough hint.
const RETRY_AFTER_S = "10";

async function check(rl: RateLimit | undefined, key: string): Promise<Response | null> {
  if (!rl) return null;
  const { success } = await rl.limit({ key });
  return success ? null : errorResponse(429, "rate_limited", { "Retry-After": RETRY_AFTER_S });
}

// Per-IP limit runs BEFORE auth so key-guessing floods are throttled too.
export function limitByIp(env: Env, request: Request): Promise<Response | null> {
  return check(env.RL_IP, `ip:${clientIp(request)}`);
}

// Per-key limit runs after auth; the bucket id (never the key itself) is a
// shared-key index (`key:N`) or a per-device id (`dev:<id>`), so per-device
// keys get their own rate-limit bucket instead of sharing the fleet's.
export function limitByKey(env: Env, bucket: string): Promise<Response | null> {
  return check(env.RL_KEY, bucket);
}
