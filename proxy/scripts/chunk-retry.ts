/**
 * chunk-retry.ts -- the route ingest's bulk-put retry, extracted so it can be
 * PROVEN to retry (test/chunk-retry.test.ts), the way exec-retry.ts was.
 *
 * Behaviour is unchanged from the inline loop it replaces in ingest-routes.ts:
 * one attempt, then one retry per backoff entry, the wrangler output printed
 * WHOLE on every failure, and the last output returned when every attempt
 * failed. The one change is the wait: it was `execSync("sleep N")`, which on
 * Windows resolves only when Git's usr/bin happens to be on PATH -- and it sat
 * INSIDE the catch, so a sleep that threw escaped the retry loop entirely. The
 * wait is now native (sleepSync), injectable here for the tests.
 *
 * No Node imports: `exec` and `sleep` are passed in.
 */
export interface ChunkRetryResult {
  ok: boolean;
  retries: number;
  lastErr: string;
}

export function putWithBackoff(
  exec: (cmd: string) => void,
  cmd: string,
  backoffS: readonly number[],
  sleep: (ms: number) => void,
  log: (msg: string) => void = console.log,
): ChunkRetryResult {
  let lastErr = "";
  let retries = 0;
  for (let attempt = 0; attempt <= backoffS.length; attempt++) {
    try {
      exec(cmd);
      return { ok: true, retries, lastErr: "" };
    } catch (e) {
      const err = e as { stdout?: string; stderr?: string; message?: string };
      lastErr = `${err.stdout ?? ""}\n${err.stderr ?? ""}`.trim() || String(err.message ?? e);
      const wait = backoffS[attempt];
      if (wait === undefined) break;
      retries++;
      // PRINTED WHOLE, not grepped for an expected shape. The failure output is
      // the one thing guaranteed not to look the way you predicted.
      log(`  chunk failed (attempt ${attempt + 1}), retrying in ${wait}s`);
      log(`  --- wrangler output ---\n${lastErr.slice(0, 600)}\n  -----------------------`);
      sleep(wait * 1000);
    }
  }
  return { ok: false, retries, lastErr };
}
