import { afterEach, describe, expect, it, vi } from "vitest";
import { clampHours, fleetRows, fleetTotals, otaOutcomes, runSql, unknownAirframes } from "../src/analytics";
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
      expect(sql).toMatch(new RegExp(`SUM\\(IF\\([^)]*, double4, 0\\)\\) AS ${col}`));
    }
  });
});

describe("query construction", () => {
  it("only counts requests it can attribute to a device", async () => {
    const seen = captureSql();
    await fleetRows(base, 24);
    expect(seen[0]).toContain("blob5 != ''");
  });

  it("counts a card open as a /v1/photo request, matching the templated route", async () => {
    const seen = captureSql();
    await fleetRows(base, 24);
    // Must be the TEMPLATE, not a prefix match on a raw path -- the device
    // Worker collapses /v1/photo/<key> before the point is written.
    expect(seen[0]).toContain("blob1 = '/v1/photo'");
    expect(seen[0]).toContain("blob1 = '/v1/enrich'");
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

// The per-hex work list. The gap SUMMARY groups by gap+type, which cannot answer
// "which airframes" for a `type` gap -- by definition nothing resolved a type, so
// blob3 is empty and every unknown aircraft collapses into one "(none)" row. The
// hex was always recorded in blob4; this is the query that reads it.
describe("unknownAirframes", () => {
  it("selects the hex and filters to the requested gap", async () => {
    const seen = captureSql([{ hex: "a1b2c3", lookups: "40" }]);
    const out = await unknownAirframes(base, 24, "type");
    expect(seen[0]).toContain("blob4 AS hex");
    expect(seen[0]).toContain("blob1 = 'enrich_gap'");
    expect(seen[0]).toContain("blob2 = 'type'");
    expect(out).toEqual([{ hex: "a1b2c3", lookups: 40 }]);
  });

  it("binds the gap to a whitelist rather than interpolating it", async () => {
    // blob2 is set by recordEnrichGap and is never user input today, but it
    // reaches a SQL API and this route takes it from ?gap=. "It can only ever be
    // one of three values" is exactly the assumption that stops being true later.
    const seen = captureSql();
    await unknownAirframes(base, 24, "'; DROP TABLE x; --");
    expect(seen[0]).toContain("blob2 = 'type'"); // fell back, did not interpolate
    expect(seen[0]).not.toContain("DROP TABLE");
  });

  it("never attributes a gap to a device", async () => {
    // The set of hexes one scope asks about IS that scope's location at aircraft
    // range. The README promises location never leaves the device, and a join
    // that reconstructs it here breaks that just as completely as sending
    // coordinates would. Asserted so a future "which devices?" column has to
    // delete this test on purpose.
    const seen = captureSql();
    await unknownAirframes(base, 24, "type");
    expect(seen[0]).not.toContain("blob5");
    expect(seen[0].toLowerCase()).not.toContain("distinct");
  });

  it("drops points that carry no hex", async () => {
    // Points written before blob4 existed have nothing to chase; a blank-code row
    // in a work list is worse than a shorter list.
    captureSql([{ hex: "", lookups: "5" }, { hex: "abc123", lookups: "2" }]);
    const out = await unknownAirframes(base, 24, "name");
    expect(out).toEqual([{ hex: "abc123", lookups: 2 }]);
  });
});
