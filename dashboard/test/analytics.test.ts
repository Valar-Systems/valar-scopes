import { afterEach, describe, expect, it, vi } from "vitest";
import { clampHours, fleetRows, fleetTotals, otaOutcomes, runSql } from "../src/analytics";
import type { Env } from "../src/types";

const base = { CF_ACCOUNT_ID: "acct", CF_API_TOKEN: "tok" } as Env;

function captureSql(rows: unknown[] = []) {
  const seen: string[] = [];
  vi.stubGlobal(
    "fetch",
    vi.fn(async (_input: RequestInfo | URL, init?: RequestInit) => {
      seen.push(String(init?.body ?? ""));
      return new Response(JSON.stringify({ data: rows, rows: rows.length }), {
        headers: { "Content-Type": "application/json" },
      });
    }),
  );
  return seen;
}

afterEach(() => vi.unstubAllGlobals());

// The device Worker samples successful cache HIT/STALE points 1:10 and carries
// the correction in double4. Counting rows instead would under-report the
// busiest devices by 10x -- precisely the ones worth looking at -- so the rule
// is asserted rather than left to a comment.
describe("the sampling correction", () => {
  it("counts with SUM(double4), never count(*)", async () => {
    const seen = captureSql();
    await fleetRows(base, 24);
    await fleetTotals(base, 24);
    for (const sql of seen) {
      expect(sql).toContain("SUM(double4)");
      expect(sql.toLowerCase()).not.toMatch(/\bcount\(\*\)/);
    }
  });

  it("derives every per-device counter from the weighted sum", async () => {
    const seen = captureSql();
    await fleetRows(base, 24);
    const sql = seen[0] as string;
    for (const col of ["errors", "cards", "enriches", "stale_served"]) {
      // `[^)]*` for the predicate would be wrong: since #142 the endpoint columns
      // match BOTH path families with an IN list, whose own closing paren the
      // negated class cannot cross. Match lazily up to the weighted-sum tail
      // instead -- what this test is about is `double4` (the sampling weight)
      // rather than the shape of the predicate in front of it.
      expect(sql).toMatch(new RegExp(`SUM\\(IF\\(.*?, double4, 0\\)\\) AS ${col}`));
    }
  });
});

describe("query construction", () => {
  it("only counts requests it can attribute to a device", async () => {
    const seen = captureSql();
    await fleetRows(base, 24);
    expect(seen[0]).toContain("blob5 != ''");
  });

  it("counts a card open across BOTH path families, not just the current one", async () => {
    const seen = captureSql();
    await fleetRows(base, 24);
    const sql = seen[0] as string;
    // Two separate properties, and the second is the one #142 added.
    //
    // 1. TEMPLATES, not prefix matches on raw paths -- the device Worker collapses
    //    /v1/photo/<key> to its template before the point is written.
    // 2. BOTH the namespaced and the legacy template. Analytics Engine points are
    //    retained rather than rewritten, so any window spanning the #142 cutover
    //    holds both shapes by design, and a device on old firmware keeps producing
    //    the legacy one long afterwards. Matching only the current path reads 0
    //    for cards and enriches -- which looks exactly like a fleet that stopped
    //    fetching photos rather than a query that stopped matching.
    //
    // This test previously asserted the single-path form and had been failing on
    // main since the #142 merge: the query was corrected, the test was not.
    for (const surface of ["photo", "enrich"]) {
      expect(sql, `${surface}: namespaced template`).toContain(`'/api/v1/blipscope/${surface}'`);
      expect(sql, `${surface}: legacy template`).toContain(`'/v1/${surface}'`);
    }
  });

  it("reads the OTA device from blob4, where recordOtaMem appends it", async () => {
    const seen = captureSql();
    await otaOutcomes(base, 24);
    expect(seen[0]).toContain("blob4 AS dev");
    expect(seen[0]).toContain("blob1 = 'ota'");
  });

  it("refuses a dataset name that is not a bare identifier", async () => {
    captureSql();
    await expect(fleetRows({ ...base, AE_DATASET: "x; DROP TABLE y" } as Env, 24)).rejects.toThrow(
      /bad dataset/,
    );
    await expect(fleetRows({ ...base, AE_DATASET: "1bad" } as Env, 24)).rejects.toThrow(/bad dataset/);
  });

  it("refuses to query at all when the API token is absent", async () => {
    captureSql();
    await expect(runSql({ ENRICH_KV: {} } as unknown as Env, "SELECT 1")).rejects.toThrow(
      /not configured/,
    );
  });

  it("surfaces an upstream failure instead of rendering an empty fleet", async () => {
    vi.stubGlobal("fetch", vi.fn(async () => new Response("nope", { status: 403 })));
    await expect(fleetRows(base, 24)).rejects.toThrow(/analytics query failed \(403\)/);
  });
});

// A window is interpolated into the SQL, so it is a number or it is nothing.
describe("clampHours", () => {
  it("defaults to 24 and bounds the range", () => {
    expect(clampHours(null)).toBe(24);
    expect(clampHours("6")).toBe(6);
    expect(clampHours("0")).toBe(1);
    expect(clampHours("-5")).toBe(1);
    expect(clampHours("100000")).toBe(720);
  });

  it("never returns anything but an integer, whatever the URL says", () => {
    for (const junk of ["abc", "", "1e999", "NaN", "1; DROP TABLE x", "24.7"]) {
      const h = clampHours(junk);
      expect(Number.isInteger(h)).toBe(true);
      expect(h).toBeGreaterThanOrEqual(1);
      expect(h).toBeLessThanOrEqual(720);
    }
  });
});

describe("row coercion", () => {
  it("turns the API's stringy numbers into numbers", async () => {
    captureSql([
      {
        dev: "2aeea64cb4b760b8", model: "s3-128", fw: "5",
        requests: "1234.0", errors: "2", cards: "17", enriches: "40",
        stale_served: "3", last_seen: "2026-08-02 09:00:00",
      },
    ]);
    const rows = await fleetRows(base, 24);
    expect(rows[0]).toMatchObject({ requests: 1234, errors: 2, cards: 17, revoked: false });
  });
});
