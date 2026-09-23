import { describe, expect, it } from "vitest";
import { putWithBackoff } from "../scripts/chunk-retry";

const BACKOFF_S = [5, 15, 45, 120]; // the route ingest's schedule

function rig(failures: number) {
  const calls: string[] = [];
  const slept: number[] = [];
  const exec = (cmd: string) => {
    calls.push(cmd);
    if (calls.length <= failures) throw Object.assign(new Error("boom"), { stdout: `524 on attempt ${calls.length}`, stderr: "" });
  };
  return { exec, calls, slept, sleep: (ms: number) => { slept.push(ms); }, log: () => {} };
}

describe("putWithBackoff (the route ingest's chunk retry)", () => {
  it("CONTROL: a first-time success is one attempt and no wait", () => {
    const r = rig(0);
    expect(putWithBackoff(r.exec, "put", BACKOFF_S, r.sleep, r.log)).toEqual({ ok: true, retries: 0, lastErr: "" });
    expect(r.calls.length).toBe(1);
    expect(r.slept).toEqual([]);
  });
  it("two failures: attempt 3 runs, after 5 s then 15 s", () => {
    const r = rig(2);
    const res = putWithBackoff(r.exec, "put", BACKOFF_S, r.sleep, r.log);
    expect(res).toMatchObject({ ok: true, retries: 2 });
    expect(r.calls.length).toBe(3);
    expect(r.slept).toEqual([5000, 15000]);
  });
  it("four failures: attempts 2 to 5 all run, with every backoff in order", () => {
    const r = rig(4);
    expect(putWithBackoff(r.exec, "put", BACKOFF_S, r.sleep, r.log).ok).toBe(true);
    expect(r.calls.length).toBe(5);
    expect(r.slept).toEqual([5000, 15000, 45000, 120000]);
  });
  it("five failures: gives up after the last attempt, with no wait after it, and returns the last output", () => {
    const r = rig(5);
    const res = putWithBackoff(r.exec, "put", BACKOFF_S, r.sleep, r.log);
    expect(res).toMatchObject({ ok: false, retries: 4 });
    expect(res.lastErr).toContain("524 on attempt 5");
    expect(r.calls.length).toBe(5);
    expect(r.slept).toEqual([5000, 15000, 45000, 120000]);
  });
});
