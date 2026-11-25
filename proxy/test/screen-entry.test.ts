// The screen service's class-shape checks (screen-registration-1.md, accepted 2026-11-25): the caller's class is
// STRUCTURAL. ScreenWebsite carries only free- and cloud-tier methods and takes no tier; ScreenApp carries every
// method and takes ctx.tier, fail-closed. Both are checked AGAINST THE ONE MAP (P-F4), method names read from each
// class's own prototype, so a method added to a class without a row, or a row without a method, is red.
// The pure gates are graded in plain Node by scripts/check-screen.mts; here they are only seen through the classes.
import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { RELEASE_URL, SCREEN_MAP } from "../src/screen/map";
import { __resetReleaseMemoForTests } from "../src/screen/release";
import worker, { ScreenApp, ScreenWebsite } from "../src/screen/worker";

const methodRows = SCREEN_MAP.filter((r) => r.kind === "method") as Array<{ method: string; tier: string }>;
const ownMethods = (cls: { prototype: object }): string[] =>
  Object.getOwnPropertyNames(cls.prototype).filter((n) => n !== "constructor").sort();

const RELEASE = new URL(RELEASE_URL);
/** The release's version.txt answers `body` once, via the same fetch the service uses. */
const releaseAnswers = (body: string) =>
  fetchMock.get(RELEASE.origin).intercept({ path: RELEASE.pathname }).reply(200, body);

type Answer = { ok: boolean; reason?: string };
const callWebsite = (name: string, ...args: unknown[]) =>
  (ScreenWebsite.prototype as unknown as Record<string, (...a: unknown[]) => Promise<Answer>>)[name].call({ env }, ...args);
const callApp = (name: string, ...args: unknown[]) =>
  (ScreenApp.prototype as unknown as Record<string, (...a: unknown[]) => Promise<Answer>>)[name].call({ env }, ...args);

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => __resetReleaseMemoForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

describe("P-T1 / P-F4: each class against the one map", () => {
  it("1 P-T1: ScreenWebsite's methods are exactly the map's free and cloud rows", () => {
    const want = methodRows.filter((r) => r.tier === "free" || r.tier === "cloud").map((r) => r.method).sort();
    expect(ownMethods(ScreenWebsite)).toEqual(want);
  });

  it("2 P-T1: no Plus-tier method exists on ScreenWebsite", () => {
    const plus = new Set(methodRows.filter((r) => r.tier === "plus").map((r) => r.method));
    expect(ownMethods(ScreenWebsite).filter((m) => plus.has(m))).toEqual([]);
  });

  it("3 P-F4: ScreenApp's methods are exactly ALL the map's method rows", () => {
    expect(ownMethods(ScreenApp)).toEqual(methodRows.map((r) => r.method).sort());
  });

  it("4 P-F4: every method row has a method on both classes", () => {
    const missing = methodRows.flatMap((r) => [
      ...(typeof (ScreenApp.prototype as unknown as Record<string, unknown>)[r.method] === "function" ? [] : [`app:${r.method}`]),
      ...(r.tier === "plus" || typeof (ScreenWebsite.prototype as unknown as Record<string, unknown>)[r.method] === "function" ? [] : [`website:${r.method}`]),
    ]);
    expect(missing).toEqual([]);
  });
});

describe("P-H1: the one public route", () => {
  it("5 GET /health answers the service, its commit, the release read and the map's counts; any other path is 404", async () => {
    releaseAnswers("19\n");
    const h = await worker.fetch(new Request("https://screen.example/health"), env);
    expect(h.status).toBe(200);
    const body = (await h.json()) as Record<string, unknown>;
    expect(body.service).toBe("blipscope-screen");
    expect(typeof body.commit).toBe("string");
    expect(body.release).toMatchObject({ version: 19 });
    expect(body.map).toEqual({ routes: 10, released: 4, planned: 3, excluded: 2, notRuled: 1 });
    const other = await worker.fetch(new Request("https://screen.example/v1/enrich/abc123"), env);
    expect(other.status).toBe(404);
  });
});

describe("the gates, seen through the classes (graded in full by check-screen.mts)", () => {
  it("6 ScreenApp with NO ctx is refused as bad_ctx (fail-closed)", async () => {
    expect(await callApp("snapshot")).toEqual({ ok: false, reason: "bad_ctx" });
  });

  it("7 ScreenApp with ctx cloud passes the gates and meets no handler yet: not_built", async () => {
    expect(await callApp("snapshot", { tier: "cloud" })).toEqual({ ok: false, reason: "not_built" });
  });

  it("8 a planned method (weather, waits for 20) is not_released while the release reads 19", async () => {
    releaseAnswers("19");
    expect(await callWebsite("weather", { product: "rain" })).toEqual({ ok: false, reason: "not_released" });
  });

  it("9 the same method passes the release gate at 20, and meets no handler yet: not_built", async () => {
    releaseAnswers("20");
    expect(await callWebsite("weather", { product: "rain" })).toEqual({ ok: false, reason: "not_built" });
  });

  it("10 an unscheduled method (airport) is not_released, and never asks the release (no interceptor is set)", async () => {
    expect(await callWebsite("airport", "PDX")).toEqual({ ok: false, reason: "not_released" });
  });
});
