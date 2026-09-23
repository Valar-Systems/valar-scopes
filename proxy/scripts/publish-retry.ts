/**
 * publish-retry.ts -- when a dashboard publish may retry, and when it must stop.
 *
 * Pure (no Node, no network): test/publish-retry.test.ts drives it.
 *
 * WHAT IS RETRIED: a rejection of the one step that moves main -- the ref
 * update -- and nothing else. Two publishes racing both build a commit on the
 * main they read; the second to move the ref is rejected. Re-reading main,
 * re-planning (which re-checks for conflicts) and trying again is safe because
 * the ref update is fast-forward only: a retry can never overwrite a commit.
 *
 * ANY REJECTION, NOT A PHRASE. The first version recognised a race only when
 * GitHub's 422 said "fast forward". On 2026-09-23 (acceptance A5) the real
 * race came back as 422 "Reference cannot be updated", matched nothing, and a
 * racing publish failed instead of retrying. The status code and wording are
 * GitHub's to change; "the ref update did not happen" is the fact that
 * matters, so every non-2xx from that step is a RefUpdateRejected.
 *
 * BOUNDED. Three attempts with backoff, then RetryCeiling, naming the ceiling
 * and the last rejection. A revoked token also makes the ref update fail, and
 * that must end in a clear error, never an endless retry.
 */

export class RefUpdateRejected extends Error {
  constructor(
    public readonly status: number,
    public readonly body: string,
  ) {
    super(`main's ref update was rejected: HTTP ${status} ${body.trim().slice(0, 200) || "(empty body)"}`);
  }
}

export class RetryCeiling extends Error {}

export const REF_RETRY_ATTEMPTS = 3;
export const REF_RETRY_BACKOFF_MS = [1000, 3000] as const;

export interface RefRetryOpts {
  attempts?: number;
  backoffMs?: readonly number[];
  sleep?: (ms: number) => Promise<void>;
  log?: (msg: string) => void;
}

export async function withRefRetry<T>(
  attempt: (n: number) => Promise<T>,
  opts: RefRetryOpts = {},
): Promise<{ value: T; attempts: number }> {
  const n = opts.attempts ?? REF_RETRY_ATTEMPTS;
  const backoff = opts.backoffMs ?? REF_RETRY_BACKOFF_MS;
  const sleep = opts.sleep ?? ((ms: number) => new Promise<void>((r) => setTimeout(r, ms)));
  let last: RefUpdateRejected | undefined;
  for (let i = 1; i <= n; i++) {
    try {
      return { value: await attempt(i), attempts: i };
    } catch (err) {
      if (!(err instanceof RefUpdateRejected)) throw err; // not a race: fail now
      last = err;
      if (i < n) {
        const ms = backoff[Math.min(i - 1, backoff.length - 1)]!;
        opts.log?.(`[publish] attempt ${i}/${n}: ${err.message}; re-reading main in ${ms} ms`);
        await sleep(ms);
      }
    }
  }
  throw new RetryCeiling(`gave up after ${n} attempts to move main; last rejection: ${last!.message}`);
}
