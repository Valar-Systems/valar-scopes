import { env } from "cloudflare:test";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { BootRow, LedgerRow, OtaRow } from "../src/analytics";
import { driftState, type DriftStatus } from "../src/drift";
import { computeFunnel, median, sortUpstreams } from "../src/funnel";
import worker from "../src/index";
import { devLink, triageBody } from "../src/render";
import { EXPECTED_BOOT_REASONS, FLAGGED_BOOT_REASONS, OTA_TRIAGE_HOURS, computeTriage, isFlaggedBoot, triageCount, type TriageInput } from "../src/triage";
import type { Env } from "../src/types";

// Allowlisted fake ids only (scripts/device-id-allowlist.txt).
const A = "1111222233334444", B = "aaaabbbbccccdddd", C = "dead0000beef1111";
const ledger = (...ids: string[]): LedgerRow[] => ids.map((dev) => ({ dev, firstEnrolled: "2026-09-01T00:00:00Z", lastEnrolled: "2026-09-01T00:00:00Z", enrollments: 1 }));
const CLEAN: DriftStatus = { state: "CLEAN", detail: "(a) 0 · (b) 0", at: "2026-09-24T03:24:07Z" };
const ota = (dev: string, result: string): OtaRow => ({ dev, model: "s3-128", result, fwFrom: 11, fwTo: 14, when: "2026-09-20 11:00:00" });
const boot = (dev: string, reason: string): BootRow => ({ dev, reason, at: "2026-09-20 11:00:17" });

// A healthy fleet: every item empty. Each test below changes ONE input.
const healthy = (): TriageInput => ({
  ledger: ledger(A, B, C),
  seen7d: new Set([A, B, C]),
  seenRetention: new Set([A, B, C]),
  latestBoots: [boot(A, "SW"), boot(B, "POWERON"), boot(C, "USB")],
  otaNotOk: [],
  drift: CLEAN,
  nowMs: Date.parse("2026-09-24T09:24:07Z"), // 6 h after the CLEAN run: fresh, pinned
});
const item = (t: TriageInput, key: string) => computeTriage(t).find((i) => i.key === key)!;

describe("triage: each item has a fixture that fills it and a control that keeps it empty", () => {
  it("CONTROL: a healthy fleet has all five items at zero, rendered as 'nothing'", () => {
    const items = computeTriage(healthy());
    expect(items.map(triageCount)).toEqual([0, 0, 0, 0, 0]);
    // four plain "nothing"s, and the drift line's "nothing (CLEAN · last run …)"
    expect(triageBody(items).match(/>nothing</g)?.length).toBe(4);
    expect(triageBody(items)).toContain(">nothing (CLEAN · last run 6 h ago)<");
  });

  it("1. enrolled, no request in 7 days -- by last request, not the ledger", () => {
    const t = { ...healthy(), seen7d: new Set([A, B]) };
    expect(item(t, "silent7").devices).toEqual([C]);
    expect(item(healthy(), "silent7").devices).toEqual([]);
  });

  it("2. last boot a crash/watchdog/brownout", () => {
    const t = { ...healthy(), latestBoots: [boot(A, "PANIC"), boot(B, "POWERON"), boot(C, "SW")] };
    expect(item(t, "crashBoot").devices).toEqual([A]);
    expect(item(t, "crashBoot").detail[0]).toContain("PANIC");
    expect(item(healthy(), "crashBoot").devices).toEqual([]);
  });

  it("3. OTA attempts that did not succeed", () => {
    const t = { ...healthy(), otaNotOk: [ota(B, "incomplete")] };
    expect(item(t, "otaFail").devices).toEqual([B]);
    expect(item(healthy(), "otaFail").devices).toEqual([]);
  });

  it("4. enrolled, never made a request", () => {
    const t = { ...healthy(), seen7d: new Set([A, B]), seenRetention: new Set([A, B]) };
    expect(item(t, "neverSeen").devices).toEqual([C]);
    expect(item(healthy(), "neverSeen").devices).toEqual([]);
  });

  it("5. render drift not CLEAN -- including a run that could not be read", () => {
    for (const state of ["DRIFT", "FAILED", "UNREADABLE", "RUNNING", "NONE"] as const) {
      expect(triageCount(item({ ...healthy(), drift: { state, detail: "x", at: "" } }, "drift")), state).toBe(1);
    }
    expect(triageCount(item(healthy(), "drift"))).toBe(0);
  });
});

describe("triage item 5: the drift run's age, amber when stale, red when failed", () => {
  const at = CLEAN.at;
  const hAfter = (h: number) => Date.parse(at) + h * 3600000;
  it("CONTROL: a fresh CLEAN run is 'nothing', and the line still shows its age", () => {
    const i = item({ ...healthy(), nowMs: hAfter(6) }, "drift");
    expect(triageCount(i)).toBe(0);
    expect(i.detail[0]).toBe("CLEAN · last run 6 h ago");
    expect(triageBody(computeTriage({ ...healthy(), nowMs: hAfter(6) }))).toContain("nothing (CLEAN · last run 6 h ago)");
  });
  it("a CLEAN run over 26 h old is amber, and says it is stale", () => {
    const i = item({ ...healthy(), nowMs: hAfter(26.5) }, "drift");
    expect([triageCount(i), i.level]).toEqual([1, "amber"]);
    expect(i.note).toContain("stale: over 26 h");
    expect(item({ ...healthy(), nowMs: hAfter(25.5) }, "drift").level).toBe(undefined);
  });
  it("a failed or unreadable job is red, whatever its age", () => {
    for (const state of ["FAILED", "UNREADABLE"] as const) {
      expect(item({ ...healthy(), drift: { ...CLEAN, state }, nowMs: hAfter(1) }, "drift").level, state).toBe("red");
    }
  });
});

describe("triage item 3: a fixed 7-day window", () => {
  it("says so on its line, independent of the page window", () => {
    expect(OTA_TRIAGE_HOURS).toBe(168);
    expect(item(healthy(), "otaFail").window).toBe("7 days, fixed -- whatever window the page is showing");
  });
});

describe("boot-reason classes come from the firmware's strings", () => {
  it("the expected reasons are not flagged; every other known reason is", () => {
    for (const r of EXPECTED_BOOT_REASONS) expect(isFlaggedBoot(r), r).toBe(false);
    for (const r of FLAGGED_BOOT_REASONS) expect(isFlaggedBoot(r), r).toBe(true);
  });
  it("an unknown or future reason is flagged, never ignored", () => {
    expect(isFlaggedBoot("UNKNOWN_16")).toBe(true);
    expect(isFlaggedBoot("SOMETHING_NEW")).toBe(true);
  });
});

describe("drift: the photo dashboard's rule -- only a posted report is a verdict", () => {
  const run = { status: "completed", conclusion: "success", head_sha: "x", created_at: "2026-09-24T03:22:44Z", updated_at: "2026-09-24T03:24:10Z" };
  it("CONTROL: a posted CLEAN report is CLEAN with its counts", () => {
    const s = driftState(run, JSON.stringify({ verdict: "CLEAN", a: { count: 0 }, b: { count: 0 }, finishedAt: "2026-09-24T03:24:07Z" }));
    expect([s.state, s.detail]).toEqual(["CLEAN", "(a) 0 · (b) 0"]);
  });
  it("a green run that posted no report is FAILED, not CLEAN", () => {
    expect(driftState(run, null).state).toBe("FAILED");
  });
  it("no run yet is NONE; an unfinished run is RUNNING", () => {
    expect(driftState(null, null).state).toBe("NONE");
    expect(driftState({ ...run, status: "in_progress", conclusion: null }, null).state).toBe("RUNNING");
  });
});

describe("funnel", () => {
  const now = Date.parse("2026-09-24T00:00:00Z");
  const retentionStart = now - 90 * 24 * 3600000;
  const led: LedgerRow[] = [
    { dev: A, firstEnrolled: "2026-09-10T00:00:00Z", lastEnrolled: "", enrollments: 1 },
    { dev: B, firstEnrolled: "2026-09-10T00:00:00Z", lastEnrolled: "", enrollments: 1 },
    { dev: C, firstEnrolled: "2026-09-10T00:00:00Z", lastEnrolled: "", enrollments: 1 },
  ];
  it("stages, gaps in hours, and who is stuck where", () => {
    const f = computeFunnel(led, new Map([[A, "2026-09-10 02:00:00"], [B, "2026-09-10 04:00:00"]]), new Map([[A, "2026-09-10 03:30:00"]]), retentionStart);
    expect(f.rows.map((r) => [r.dev, r.stage])).toEqual([[A, "card"], [B, "blips"], [C, "enrolled"]]);
    expect(f.rows[0]!.gapEnrolToBlipsH).toBe(2);
    expect(f.rows[0]!.gapBlipsToCardH).toBe(1.5);
    expect(f.stuck).toEqual({ enrolled: [C], blips: [B] });
    expect(f.medianEnrolToBlipsH).toBe(3); // (2 + 4) / 2
    expect(f.medianBlipsToCardH).toBe(1.5);
  });
  it("a device enrolled before retention, or requesting before it enrolled, is shown but not measured", () => {
    const old = [{ ...led[0]!, firstEnrolled: "2026-05-01T00:00:00Z" }, { ...led[1]! }];
    const f = computeFunnel(old, new Map([[A, "2026-09-10 02:00:00"], [B, "2026-09-01 00:00:00"]]), new Map(), retentionStart);
    expect(f.rows.map((r) => r.flag)).toEqual(["enrolled before retention", "requested before enrolment"]);
    expect(f.rows.every((r) => r.gapEnrolToBlipsH === null)).toBe(true);
    expect(f.medianEnrolToBlipsH).toBe(null); // nothing measurable -> no median, not 0
  });
  it("median of an odd and an even list", () => {
    expect(median([5, 1, 3])).toBe(3);
    expect(median([])).toBe(null);
  });
});

describe("upstreams: worst first", () => {
  it("highest error share first, then slowest p95", () => {
    const rows = sortUpstreams([
      { upstream: "fast_clean", requests: 100, errors: 0, p50: 100, p95: 200 },
      { upstream: "erroring", requests: 100, errors: 10, p50: 100, p95: 150 },
      { upstream: "slow_clean", requests: 100, errors: 0, p50: 300, p95: 900 },
    ]);
    expect(rows.map((r) => r.upstream)).toEqual(["erroring", "slow_clean", "fast_clean"]);
  });
});

describe("device links and the /device page", () => {
  const E = env as unknown as Env;
  afterEach(() => vi.unstubAllGlobals());

  it("every id renders as a link to its page; anything else stays plain text", () => {
    expect(devLink(A)).toContain(`href="/device/${A}"`);
    expect(devLink("<script>")).not.toContain("href");
    expect(devLink("<script>")).toContain("&lt;script&gt;");
  });

  it("/device/<id> is behind Access like every other page, and spends nothing", async () => {
    const spent = vi.fn();
    vi.stubGlobal("fetch", spent);
    for (const path of [`/device/${A}`, "/funnel", "/upstreams"]) {
      expect((await worker.fetch(new Request(`https://fleet.example${path}`), E)).status, path).toBe(403);
    }
    expect(spent).not.toHaveBeenCalled();
  });
});
