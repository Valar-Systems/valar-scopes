import { describe, expect, it } from "vitest";
import {
  FEW_MAX,
  combine,
  driftPanel,
  exitCodeFor,
  formatReport,
  interpretA,
  rowsFor,
  titleFor,
  type DryRunStatus,
  type VerifyStatus,
} from "../scripts/photo-drift";
import type { ManifestEntry } from "../src/photolicense";

const a = (changedRows: string[], total = 237, publishedNotInRepo: string[] = []): DryRunStatus => ({
  mode: "dry-run", verdict: "READ", reason: "", total, changed: changedRows.length, changedRows, publishedNotInRepo, finishedAt: "",
});
const b = (rowsDisagreeing: string[] = []): VerifyStatus => ({
  mode: "verify", verdict: rowsDisagreeing.length ? "FAIL" : "PASS", reason: "", rows: 237, pointersChecked: 948, rowsDisagreeing, finishedAt: "",
});
const rows = (n: number) => Array.from({ length: n }, (_, i) => `type:T${i}`);

describe("render drift: two numbers, each with its meaning", () => {
  it("CONTROL: 0 and 0 is CLEAN, exit 0", () => {
    const r = combine(a([]), b());
    expect([r.verdict, r.a.count, r.b.count, exitCodeFor(r)]).toEqual(["CLEAN", 0, 0, 0]);
    expect(r.a.meaning).toMatch(/^clean/);
  });

  it("one edited row is exactly 1 in (a), named, and 0 in (b)", () => {
    const r = combine(a(["type:E75L"]), b());
    expect([r.verdict, r.a.count, r.b.count, exitCodeFor(r)]).toEqual(["DRIFT", 1, 0, 1]);
    expect(r.a.rows).toEqual(["type:E75L"]);
    expect(r.a.meaning).toContain("type:E75L");
  });

  it("ALL rows changed points at the renderer and package-lock, not the photos", () => {
    const h = interpretA(a(rows(237)));
    expect(h.count).toBe(237);
    expect(h.meaning).toContain("RENDERER");
    expect(h.meaning).toContain("package-lock.json");
  });

  it("between a few and all is OUTSIDE THE RULE -- it does not guess", () => {
    const h = interpretA(a(rows(FEW_MAX + 1)));
    expect(h.meaning).toMatch(/^OUTSIDE THE RULE/);
    expect(h.meaning).not.toContain("package-lock");
    // and the boundary: FEW_MAX is still a few
    expect(interpretA(a(rows(FEW_MAX))).meaning).not.toMatch(/OUTSIDE/);
  });

  it("published rows no longer in the repo count as drift in (a)", () => {
    const r = combine(a([], 236, ["type:GONE"]), b());
    expect([r.verdict, r.a.count]).toEqual(["DRIFT", 1]);
    expect(r.a.meaning).toContain("type:GONE");
  });

  it("(b) names the rows whose live keys disagree", () => {
    const r = combine(a([]), b(["type:B738"]));
    expect([r.verdict, r.a.count, r.b.count]).toEqual(["DRIFT", 0, 1]);
    expect(r.b.meaning).toContain("type:B738");
  });
});

describe("an unreadable half is FAILED, never zero, never ALL", () => {
  it("unreadable manifest in (a): count is null, verdict FAILED, exit 3", () => {
    const r = combine({ ...a([]), verdict: "UNREADABLE", reason: "could not read photo:manifest" }, b());
    expect([r.verdict, r.a.count, exitCodeFor(r)]).toEqual(["FAILED", null, 3]);
    expect(r.a.meaning).not.toContain("RENDERER");
  });

  it("a missing half (the step never wrote a status) is FAILED", () => {
    expect(combine(null, b()).verdict).toBe("FAILED");
    expect(combine(a([]), null).verdict).toBe("FAILED");
  });

  it("UNTRUSTWORTHY in (b) is FAILED even when (a) is clean", () => {
    const r = combine(a([]), { ...b(), verdict: "UNTRUSTWORTHY", reason: "HTTP 401" });
    expect([r.verdict, r.b.count]).toEqual(["FAILED", null]);
  });

  it("the title and report print FAILED, not 0, for an unreadable half", () => {
    const r = combine(null, b());
    expect(titleFor(r)).toBe("FAILED: (a) FAILED · (b) 0");
    expect(formatReport(r)).toContain("(a) render vs published manifest: FAILED");
    expect(formatReport(r)).not.toMatch(/render vs published manifest: 0/);
  });

  it("the report carries the interpretation rule, not just the numbers", () => {
    const text = formatReport(combine(a([]), b()));
    expect(text).toContain("0 = clean");
    expect(text).toContain("ALL = the renderer or sharp changed");
    expect(text).toContain("package-lock.json");
  });
});

describe("rowsFor: pointer and blob keys back to rows", () => {
  const m = [{
    kind: "type", target: "B738", blobKey: "photo:B738-aaaaaaaa",
    squareKeys: { "240": "photo:B738-1aaaaaaa", "412": "photo:B738-2aaaaaaa", "480": "photo:B738-3aaaaaaa" },
  }] as unknown as ManifestEntry[];
  it("a square pointer, a legacy pointer and a blob of one row are one row", () => {
    expect(rowsFor(m, ["pptr:t:B738:s412", "pptr:t:B738", "photo:B738-3aaaaaaa"])).toEqual(["type:B738"]);
  });
  it("CONTROL: a key no row owns is kept as-is, not dropped", () => {
    expect(rowsFor(m, ["pptr:t:ZZZZ"])).toEqual(["pptr:t:ZZZZ"]);
  });
});

describe("driftPanel: a failed run shows as failed, not as 0", () => {
  const done = { status: "completed", conclusion: "success", updatedAt: "2026-09-23T06:20:00Z" };
  it("CONTROL: a posted CLEAN report shows 0 and 0 with its time", () => {
    const p = driftPanel(done, JSON.stringify(combine(a([]), b(), "2026-09-23T06:19:00Z")));
    expect([p.state, p.a, p.b, p.at]).toEqual(["CLEAN", 0, 0, "2026-09-23T06:19:00Z"]);
  });
  it("a posted FAILED report keeps the unreadable half as null", () => {
    const p = driftPanel({ ...done, conclusion: "failure" }, JSON.stringify(combine(null, b())));
    expect([p.state, p.a, p.b]).toEqual(["FAILED", null, 0]);
  });
  it("a completed run that posted NO report is FAILED with no counts", () => {
    const p = driftPanel({ ...done, conclusion: "failure" }, null);
    expect([p.state, p.a, p.b]).toEqual(["FAILED", null, null]);
    expect(p.meaningA).toContain("without posting a report");
  });
  it("even a GREEN run with no report is FAILED -- only a report can produce a count", () => {
    expect(driftPanel(done, null).state).toBe("FAILED");
    expect(driftPanel(done, "not json").state).toBe("FAILED");
  });
  it("an in-progress run with no report yet is RUNNING; no run at all is NONE", () => {
    expect(driftPanel({ ...done, status: "in_progress", conclusion: null }, null).state).toBe("RUNNING");
    expect(driftPanel(null, null).state).toBe("NONE");
  });
});
