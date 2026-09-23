import { describe, expect, it } from "vitest";
import { BACKOFF_MS, execWithRetry } from "../scripts/exec-retry";

// An exec that fails its first `failures` calls, and a sleep that records what
// it was asked to wait. The retry is only real if attempts 2..N are SEEN to run.
function rig(failures: number) {
  const calls: string[] = [];
  const slept: number[] = [];
  const exec = (cmd: string) => {
    calls.push(cmd);
    if (calls.length <= failures) throw new Error(`fail #${calls.length}`);
  };
  return { exec, calls, slept, sleep: (ms: number) => { slept.push(ms); }, log: () => {} };
}

describe("execWithRetry", () => {
  it("CONTROL: a first-time success is one call and no wait", () => {
    const r = rig(0);
    execWithRetry(r.exec, "put k", "put k", r);
    expect(r.calls.length).toBe(1);
    expect(r.slept).toEqual([]);
  });

  it("one failure: attempt 2 runs, after the first backoff", () => {
    const r = rig(1);
    execWithRetry(r.exec, "put k", "put k", r);
    expect(r.calls.length).toBe(2);
    expect(r.slept).toEqual([BACKOFF_MS[0]]);
  });

  it("three failures: attempts 2, 3 and 4 all run, with every backoff in order", () => {
    const r = rig(3);
    execWithRetry(r.exec, "put k", "put k", r);
    expect(r.calls.length).toBe(4);
    expect(r.slept).toEqual([1500, 4000, 9000]);
  });

  it("four failures: all four attempts run, then the LAST error is thrown", () => {
    const r = rig(4);
    expect(() => execWithRetry(r.exec, "put k", "put k", r)).toThrow("fail #4");
    expect(r.calls.length).toBe(4);
    expect(r.slept).toEqual([1500, 4000, 9000]);
  });
});

// sleepSync itself is NOT tested here: workerd forbids Atomics.wait on its main
// thread, and Node (where the ingest runs) allows it. It is proven under Node by
// the rehearsal that makes a real wrangler put fail -- see the PR description.
