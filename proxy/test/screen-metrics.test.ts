// The screen service's unsampled counter and /health's logs field (phase 1, the logs part; its registration accepted
// 2026-11-25). The owner's ruling: "every request and every error is still counted unsampled (the metrics store)".
//   T1 P-M1   40 mixed calls -> exactly 40 request points, the outcomes equal to the calls' own answers, every blob from
//             a known set (method, caller, outcome): counts, never subjects.
//   T2 P-M1e  a planted throwing handler -> the call answers { ok: false, reason: "error" } (never a throw across RPC),
//             with exactly 1 request point (outcome "error") and 1 error point.
//   T3 P-H2   /health reports logs { rate, until } from the stamped defines; unstamped (this test env) both are null,
//             which the watch reads as BLIND.
// worker.ts is imported as a NAMESPACE, so a missing export reads undefined at run time instead of failing the file.
import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { RELEASE_URL, SCREEN_MAP } from "../src/screen/map";
import { __resetReleaseMemoForTests } from "../src/screen/release";
import * as W from "../src/screen/worker";

type Answer = { ok: boolean; reason?: string };
type Point = { blobs?: string[]; doubles?: number[]; indexes?: string[] };
const seams = W as unknown as { __setHandlerForTests?: (m: string, h: unknown) => void };

/** A recording stand-in for the Analytics Engine binding: every writeDataPoint is kept, none is sampled. */
const recorder = () => {
  const points: Point[] = [];
  return { points, binding: { writeDataPoint: (p: Point) => { points.push(p); } } };
};
const RELEASE = new URL(RELEASE_URL);
const call = (cls: { prototype: object }, self: object, name: string, ...args: unknown[]) =>
  (cls.prototype as Record<string, (...a: unknown[]) => Promise<Answer>>)[name].call(self, ...args);

const METHODS = new Set(SCREEN_MAP.filter((r) => r.kind === "method").map((r) => (r as { method: string }).method));
const OUTCOMES = new Set(["ok", "bad_ctx", "tier", "not_released", "not_built", "unknown_method", "error"]);

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => __resetReleaseMemoForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

describe("the unsampled counter (P-M1, P-M1e)", () => {
  it("T1 P-M1: 40 mixed calls write exactly 40 request points, outcomes equal to the answers, blobs from known sets", async () => {
    const rec = recorder();
    const self = { env: { ...env, SCREEN_METRICS: rec.binding } };
    fetchMock.get(RELEASE.origin).intercept({ path: RELEASE.pathname }).reply(200, "20"); // weather and basemap ask once (memo)
    const W_ = W.ScreenWebsite, A_ = W.ScreenApp;
    const calls: Array<() => Promise<Answer>> = [
      ...Array.from({ length: 5 }, () => () => call(W_, self, "snapshot")),
      ...Array.from({ length: 5 }, () => () => call(W_, self, "enrich", { hex: "abc123" })),
      ...Array.from({ length: 3 }, () => () => call(W_, self, "photo", "k")),
      ...Array.from({ length: 3 }, () => () => call(W_, self, "airports")),
      ...Array.from({ length: 4 }, () => () => call(W_, self, "weather", { product: "rain" })),
      ...Array.from({ length: 2 }, () => () => call(W_, self, "basemap")),
      ...Array.from({ length: 3 }, () => () => call(W_, self, "airport", "PDX")),
      ...Array.from({ length: 5 }, () => () => call(A_, self, "snapshot", { tier: "cloud" })),
      ...Array.from({ length: 4 }, () => () => call(A_, self, "snapshot")),
      ...Array.from({ length: 3 }, () => () => call(A_, self, "snapshot", { tier: "Plus" })),
      ...Array.from({ length: 3 }, () => () => call(A_, self, "weather", { tier: "plus" }, { product: "rain" })),
    ];
    expect(calls.length).toBe(40);
    const answers: Answer[] = [];
    for (const c of calls) answers.push(await c()); // in order: the memo is read once, by the first planned call
    const requestPoints = rec.points.filter((p) => p.indexes?.[0] !== "error");
    expect(requestPoints.length).toBe(40);
    const tally = (xs: string[]) => xs.reduce<Record<string, number>>((m, x) => ((m[x] = (m[x] ?? 0) + 1), m), {});
    expect(tally(requestPoints.map((p) => p.blobs?.[2] ?? "?"))).toEqual(tally(answers.map((a) => (a.ok ? "ok" : a.reason ?? "?"))));
    for (const p of requestPoints) {
      expect(METHODS.has(p.blobs?.[0] ?? "")).toBe(true);
      expect(["website", "app"]).toContain(p.blobs?.[1]);
      expect(OUTCOMES.has(p.blobs?.[2] ?? "")).toBe(true);
      expect(p.blobs?.length).toBe(3);
      expect(p.doubles?.[0]).toBe(1);
    }
  });

  it("T2 P-M1e: a throwing handler answers error, with exactly 1 request point and 1 error point", async () => {
    const rec = recorder();
    const self = { env: { ...env, SCREEN_METRICS: rec.binding } };
    seams.__setHandlerForTests!("snapshot", async () => { throw new Error("planted"); });
    try {
      const a = await call(W.ScreenWebsite, self, "snapshot");
      expect(a).toEqual({ ok: false, reason: "error" });
    } finally {
      seams.__setHandlerForTests!("snapshot", undefined);
    }
    const errors = rec.points.filter((p) => p.indexes?.[0] === "error");
    const requests = rec.points.filter((p) => p.indexes?.[0] !== "error");
    expect(requests.length).toBe(1);
    expect(requests[0].blobs).toEqual(["snapshot", "website", "error"]);
    expect(errors.length).toBe(1);
    expect(errors[0].blobs?.slice(0, 3)).toEqual(["error", "snapshot", "website"]);
  });
});

describe("/health's logs field (P-H2)", () => {
  it("T3 P-H2: unstamped, /health reports logs { rate: null, until: null }", async () => {
    fetchMock.get(RELEASE.origin).intercept({ path: RELEASE.pathname }).reply(200, "19");
    const h = await W.default.fetch(new Request("https://screen.example/health"), env);
    const body = (await h.json()) as Record<string, unknown>;
    expect(body.logs).toEqual({ rate: null, until: null });
  });
});
