import { afterEach, describe, expect, it, vi } from "vitest";
import * as q from "../src/analytics";
import type { Env } from "../src/types";

// vitest cannot run the SQL -- only the live engine can say what it accepts, and
// `npm run smoke:analytics` is that check. This file is the cheap half: it
// refuses the shapes the engine is KNOWN to reject, all found live on
// 2026-09-24, so they cannot come back through a green suite.
//
//   1. IF(cond, doubleN, 0): "the 2nd and 3rd arguments to IF() function must
//      have the same type but instead had Double and Integer" (422).
//   2. uniq(...) and uniqExact(...): both "unknown function call". The engine's
//      distinct count is count(DISTINCT x).
//   3. IF(cond, x, NULL): "must have the same type but instead had String and Null".

const base = { CF_ACCOUNT_ID: "acct", CF_API_TOKEN: "tok" } as Env;
const QUERIES = ["fleetRows", "fleetTotals", "firmwareSpread", "otaOutcomes", "enrichGaps", "usageRows", "seenDevices", "latestBoots", "otaNotOk", "upstreams"] as const;
// Statements that take something besides a window -- a device id or a route set.
const DEV = "0123456789abcdef";
const OTHER: ((e: Env) => Promise<unknown>)[] = [
  (e) => q.deviceSummary(e, DEV, 24),
  (e) => q.deviceFirmware(e, DEV),
  (e) => q.deviceBoots(e, DEV),
  (e) => q.deviceOta(e, DEV),
  (e) => q.usageRows(e, 24, DEV),
  (e) => q.firstRequestTimes(e, q.BLIPS_ROUTES),
  (e) => q.firstRequestTimes(e, q.PHOTO_ROUTES),
];

afterEach(() => vi.unstubAllGlobals());

async function everyStatement(): Promise<string[]> {
  const seen: string[] = [];
  vi.stubGlobal(
    "fetch",
    vi.fn(async (_i: RequestInfo | URL, init?: RequestInit) => {
      seen.push(String(init?.body ?? ""));
      return new Response(JSON.stringify({ data: [], rows: 0 }), { headers: { "Content-Type": "application/json" } });
    }),
  );
  for (const name of QUERIES) await (q[name] as (e: Env, h: number) => Promise<unknown>)(base, 24);
  for (const call of OTHER) await call(base);
  return seen;
}

// Integer literal else-branch after a double column: IF(..., doubleN, 0)
const MIXED_IF = /IF\([^;]*?,\s*double\d+\s*,\s*-?\d+\s*\)/;
const UNIQ_ANY = /\buniq(Exact)?\(/i;
const NULL_BRANCH = /IF\([^;]*?,\s*NULL\s*\)/i;

describe("SQL shapes the engine rejects", () => {
  it("covers every query the dashboard issues", async () => {
    expect(await everyStatement()).toHaveLength(QUERIES.length + OTHER.length);
  });

  it("no IF() pairs a double column with an integer literal", async () => {
    for (const sql of await everyStatement()) expect(sql, sql).not.toMatch(MIXED_IF);
  });

  it("no uniq() or uniqExact(); count(DISTINCT x) only", async () => {
    for (const sql of await everyStatement()) expect(sql, sql).not.toMatch(UNIQ_ANY);
  });

  it("no IF() with a NULL branch", async () => {
    for (const sql of await everyStatement()) expect(sql, sql).not.toMatch(NULL_BRANCH);
  });

  it("CONTROL: every pattern catches the exact SQL that failed live", () => {
    expect("SUM(IF(blob1 IN ('/api/v1/blipscope/photo', '/v1/photo'), double4, 0)) AS cards").toMatch(MIXED_IF);
    expect("SUM(IF(double1 >= 400, double4, 0)) AS errors").toMatch(MIXED_IF);
    expect("uniq(blob5) AS devices").toMatch(UNIQ_ANY);
    expect("uniqExact(blob5) AS devices").toMatch(UNIQ_ANY);
    expect("count(DISTINCT IF(blob5 != '', blob5, NULL)) AS n").toMatch(NULL_BRANCH);
    // ...and none fires on the forms that passed live
    expect("SUM(IF(double1 >= 400, double4, 0.0)) AS errors").not.toMatch(MIXED_IF);
    expect("count(DISTINCT blob5) AS devices").not.toMatch(UNIQ_ANY);
    expect("count(DISTINCT blob5) - MAX(IF(blob5 = '', 1, 0)) AS devices").not.toMatch(NULL_BRANCH);
    expect("count(DISTINCT blob5) - MAX(IF(blob5 = '', 1, 0)) AS devices").not.toMatch(MIXED_IF);
  });
});
