/**
 * exec-retry.ts -- run a command, retrying with backoff, SYNCHRONOUSLY.
 *
 * Used by ingest-photos.ts for every KV write. A single write occasionally fails
 * transiently (a 429, a network blip) partway through a run, and re-running a
 * write is a no-op flip on a content-addressed blob, so the write is retried.
 *
 * THE WAIT IS NATIVE, and that is the fix, not a tidy-up. It used to be a
 * shell-out -- `powershell -Command "Start-Sleep ..."` on Windows, `sleep` elsewhere
 * -- which put a second process spawn INSIDE the failure path. On 2026-09-23 an
 * ingest died on pptr:t:B738 with the retry never observed to retry. Whatever
 * exactly threw, a wait that can fail is a retry that can fail, and the one
 * place a retry must not fail is between its attempts. Atomics.wait blocks this
 * thread for `ms` and cannot throw for any reason a shell can.
 *
 * Synchronous on purpose: the ingest writes blob-then-pointer in a strict order,
 * and an async retry would invite someone to overlap them.
 *
 * No Node imports: `exec` is passed in, so vitest's Workers pool can drive it
 * (test/exec-retry.test.ts) with a fake that fails on demand.
 */

export const BACKOFF_MS = [1500, 4000, 9000] as const;

export function sleepSync(ms: number): void {
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
}

export interface ExecRetryOpts {
  attempts?: number;
  sleep?: (ms: number) => void;
  log?: (msg: string) => void;
}

export function execWithRetry(
  exec: (cmd: string) => void,
  cmd: string,
  label: string,
  opts: ExecRetryOpts = {},
): void {
  const attempts = opts.attempts ?? 4;
  const sleep = opts.sleep ?? sleepSync;
  const log = opts.log ?? ((m: string) => console.error(m));
  for (let i = 0; i < attempts; i++) {
    try {
      exec(cmd);
      if (i > 0) log(`  ${label}: succeeded on attempt ${i + 1}/${attempts}`);
      return;
    } catch (err) {
      if (i === attempts - 1) {
        log(`  ${label}: write failed (attempt ${i + 1}/${attempts}); giving up`);
        throw err;
      }
      const wait = BACKOFF_MS[Math.min(i, BACKOFF_MS.length - 1)]!;
      log(`  ${label}: write failed (attempt ${i + 1}/${attempts}); retrying in ${wait / 1000}s ...`);
      sleep(wait);
    }
  }
}
