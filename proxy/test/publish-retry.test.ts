import { describe, expect, it } from "vitest";
import { updateMainRef } from "../scripts/github-api";
import { REF_RETRY_BACKOFF_MS, RefUpdateRejected, RetryCeiling, withRefRetry } from "../scripts/publish-retry";

// GitHub's ACTUAL response to the racing ref update in acceptance A5,
// 2026-09-23 -- the body the first version failed to recognise.
const GITHUB_422 = JSON.stringify({
  message: "Reference cannot be updated",
  documentation_url: "https://docs.github.com/rest/git/refs#update-a-reference",
  status: "422",
});

// A fetch that answers the ref PATCH with a scripted sequence and counts calls.
function ghWith(...responses: Array<{ status: number; body: string }>) {
  let calls = 0;
  const fetchImpl = (async () => {
    const r = responses[Math.min(calls, responses.length - 1)]!;
    calls++;
    return new Response(r.body, { status: r.status });
  }) as unknown as typeof fetch;
  return { gh: { repo: "o/r", token: "t", fetchImpl }, calls: () => calls };
}

const noSleep = () => { const slept: number[] = []; return { slept, sleep: async (ms: number) => { slept.push(ms); } }; };

describe("updateMainRef: every non-2xx is a RefUpdateRejected, whatever the body", () => {
  it("CONTROL: a 200 moves the ref and throws nothing", async () => {
    const { gh } = ghWith({ status: 200, body: "{}" });
    await expect(updateMainRef(gh, "abc")).resolves.toBeUndefined();
  });
  it("GitHub's real 422 'Reference cannot be updated' is a rejection", async () => {
    const { gh } = ghWith({ status: 422, body: GITHUB_422 });
    const err = await updateMainRef(gh, "abc").catch((e) => e);
    expect(err).toBeInstanceOf(RefUpdateRejected);
    expect(err.status).toBe(422);
    expect(err.message).toContain("Reference cannot be updated");
  });
  it("a generic 500 with an EMPTY body is a rejection too", async () => {
    const { gh } = ghWith({ status: 500, body: "" });
    const err = await updateMainRef(gh, "abc").catch((e) => e);
    expect(err).toBeInstanceOf(RefUpdateRejected);
    expect(err.message).toContain("(empty body)");
  });
});

describe("withRefRetry: bounded, and only for ref rejections", () => {
  it("CONTROL: success first time is one attempt, no wait", async () => {
    const s = noSleep();
    const r = await withRefRetry(async () => "sha", { sleep: s.sleep });
    expect(r).toEqual({ value: "sha", attempts: 1 });
    expect(s.slept).toEqual([]);
  });

  it("A5's race: a real 422 then success lands on attempt 2", async () => {
    const { gh, calls } = ghWith({ status: 422, body: GITHUB_422 }, { status: 200, body: "{}" });
    const s = noSleep();
    const r = await withRefRetry(async () => { await updateMainRef(gh, "abc"); return "sha"; }, { sleep: s.sleep });
    expect(r.attempts).toBe(2);
    expect(calls()).toBe(2);
    expect(s.slept).toEqual([REF_RETRY_BACKOFF_MS[0]]);
  });

  it("an empty-body 500 is retried the same way", async () => {
    const { gh } = ghWith({ status: 500, body: "" }, { status: 200, body: "{}" });
    const s = noSleep();
    expect((await withRefRetry(async () => { await updateMainRef(gh, "abc"); return 1; }, { sleep: s.sleep })).attempts).toBe(2);
  });

  it("a revoked token (401 every time) stops after exactly 3 attempts and names the ceiling and last error", async () => {
    const { gh, calls } = ghWith({ status: 401, body: '{"message":"Bad credentials"}' });
    const s = noSleep();
    const err = await withRefRetry(async () => { await updateMainRef(gh, "abc"); return 1; }, { sleep: s.sleep }).catch((e) => e);
    expect(err).toBeInstanceOf(RetryCeiling);
    expect(err.message).toContain("3 attempts");
    expect(err.message).toContain("HTTP 401");
    expect(err.message).toContain("Bad credentials");
    expect(calls()).toBe(3);
    expect(s.slept).toEqual([1000, 3000]);
  });

  it("a failure in any other step is NOT retried", async () => {
    const s = noSleep();
    let n = 0;
    const err = await withRefRetry(async () => { n++; throw new Error("GitHub POST /git/blobs: HTTP 500"); }, { sleep: s.sleep }).catch((e) => e);
    expect(err).not.toBeInstanceOf(RetryCeiling);
    expect(String(err.message)).toContain("/git/blobs");
    expect(n).toBe(1);
    expect(s.slept).toEqual([]);
  });
});
