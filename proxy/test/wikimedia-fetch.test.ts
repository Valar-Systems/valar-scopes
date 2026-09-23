import { describe, expect, it } from "vitest";
import {
  RateLimited,
  RETRY_DEFAULT_MS,
  retryAfterMs,
  wikimediaFetch,
} from "../scripts/wikimedia-fetch";

// A fetch that serves a scripted sequence of responses and counts calls, and a
// sleep that records what it was asked to wait instead of waiting.
function rig(...script: Array<{ status: number; retryAfter?: string }>) {
  let calls = 0;
  const slept: number[] = [];
  const fetchImpl = (async () => {
    const s = script[Math.min(calls, script.length - 1)]!;
    calls++;
    const headers = new Headers();
    if (s.retryAfter !== undefined) headers.set("retry-after", s.retryAfter);
    return new Response(s.status === 200 ? "ok" : "no", { status: s.status, headers });
  }) as unknown as typeof fetch;
  const sleep = async (ms: number) => { slept.push(ms); };
  return { fetchImpl, sleep, slept, calls: () => calls };
}

const URL_ = "https://upload.wikimedia.org/wikipedia/commons/x.jpg";

describe("wikimediaFetch", () => {
  it("CONTROL: a 200 is one fetch and no wait", async () => {
    const r = rig({ status: 200 });
    const res = await wikimediaFetch(URL_, "ua", r);
    expect(res.status).toBe(200);
    expect(r.calls()).toBe(1);
    expect(r.slept).toEqual([]);
  });

  it("a 429 then 200 retries once, after the default wait", async () => {
    const r = rig({ status: 429 }, { status: 200 });
    const res = await wikimediaFetch(URL_, "ua", r);
    expect(res.status).toBe(200);
    expect(r.calls()).toBe(2);
    expect(r.slept).toEqual([RETRY_DEFAULT_MS]);
  });

  it("honours Retry-After in seconds", async () => {
    const r = rig({ status: 429, retryAfter: "2" }, { status: 200 });
    await wikimediaFetch(URL_, "ua", r);
    expect(r.slept).toEqual([2000]);
  });

  it("a second 429 throws RateLimited after exactly two fetches", async () => {
    const r = rig({ status: 429 }, { status: 429 }, { status: 200 });
    const err = await wikimediaFetch(URL_, "ua", r).catch((e) => e);
    expect(err).toBeInstanceOf(RateLimited);
    expect(String(err.message)).toContain("still 429 after one retry");
    expect(r.calls()).toBe(2);
  });

  it("a Retry-After too long to wait on throws at once, without sleeping", async () => {
    const r = rig({ status: 429, retryAfter: "60" }, { status: 200 });
    const err = await wikimediaFetch(URL_, "ua", r).catch((e) => e);
    expect(err).toBeInstanceOf(RateLimited);
    expect(r.calls()).toBe(1);
    expect(r.slept).toEqual([]);
  });

  it("any other failure is a plain error, NOT RateLimited, and is not retried", async () => {
    const r = rig({ status: 500 }, { status: 200 });
    const err = await wikimediaFetch(URL_, "ua", r).catch((e) => e);
    expect(err).toBeInstanceOf(Error);
    expect(err).not.toBeInstanceOf(RateLimited);
    expect(r.calls()).toBe(1);
  });
});

describe("retryAfterMs", () => {
  it("reads seconds, an HTTP date, and falls back on junk or absence", () => {
    const now = Date.parse("2026-09-23T12:00:00Z");
    expect(retryAfterMs("5", now)).toBe(5000);
    expect(retryAfterMs("Wed, 23 Sep 2026 12:00:07 GMT", now)).toBe(7000);
    expect(retryAfterMs("soon", now)).toBe(RETRY_DEFAULT_MS);
    expect(retryAfterMs(null, now)).toBe(RETRY_DEFAULT_MS);
  });
});
