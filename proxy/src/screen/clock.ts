// The screen service's clock: Date.now(), unless a test injected one (worker.ts re-exports the seam as
// __setClockForTests; no RPC method reaches it). The limiter's windows, the cfg cache and the details store all read
// time here, so a test can step across a 60 s window without waiting for one.
let injected: (() => number) | undefined;

export const now = (): number => (injected ? injected() : Date.now());

/** TEST SEAM ONLY: set a clock, or restore Date.now() with undefined. */
export function __setClock(f: (() => number) | undefined): void {
  injected = f;
}
