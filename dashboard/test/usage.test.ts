import { env } from "cloudflare:test";
import { afterEach, describe, expect, it, vi } from "vitest";
import {
  REQUEST_POINTS,
  SEEN_CAP,
  SILENT_SHOWN,
  enrolledButSilent,
  firmwareSpread,
  fleetRows,
  fleetTotals,
  seenDevices,
  usageRows,
} from "../src/analytics";
import worker from "../src/index";
import { usageBody } from "../src/render";
import type { Env } from "../src/types";

const base = { CF_ACCOUNT_ID: "acct", CF_API_TOKEN: "tok" } as Env;
const E = env as unknown as Env;

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

// Four point families were added to the dataset after this dashboard, and they
// put a family name in blob1 where a request point puts its route.
describe("request queries read request points only", () => {
  it("fleet rows, fleet totals and the firmware spread all carry the filter", async () => {
    const seen = captureSql();
    await fleetRows(base, 24);
    await fleetTotals(base, 24);
    await firmwareSpread(base, 24);
    expect(seen).toHaveLength(3);
    for (const sql of seen) expect(sql).toContain(REQUEST_POINTS);
  });

  it("the filter selects routes and rejects every family name the device Worker writes", () => {
    // The SQL LIKE '/%' is a prefix test; the families are the literal blob1
    // values in proxy/src/metrics.ts.
    const like = (v: string) => v.startsWith("/");
    expect(REQUEST_POINTS).toBe("blob1 LIKE '/%'");
    for (const route of ["/api/v1/blipscope/photo", "/v1/enrich", "/other", "/blipscope/leaderboard/:id"]) {
      expect(like(route), route).toBe(true);
    }
    for (const family of ["usage", "boot", "ota", "enrich_gap"]) expect(like(family), family).toBe(false);
  });
});

describe("usage rows: three kinds of number, one of them summed", () => {
  it("sums the six counter deltas, weighted by the query-time sample interval", async () => {
    const seen = captureSql();
    await usageRows(base, 720);
    const sql = seen[0] as string;
    expect(sql).toContain("blob1 = 'usage'");
    for (const d of ["double1", "double2", "double3", "double4", "double5", "double6"]) {
      expect(sql).toContain(`SUM(_sample_interval * ${d})`);
    }
    expect(sql).toContain("SUM(_sample_interval) AS reports");
  });

  it("takes follow-enabled and uptime at the latest report and NEVER sums them", async () => {
    const seen = captureSql();
    await usageRows(base, 720);
    const sql = seen[0] as string;
    expect(sql).toContain("argMax(double7, timestamp) AS follow_enabled");
    expect(sql).toContain("argMax(double8, timestamp) AS uptime_hours");
    expect(sql).not.toMatch(/SUM\([^)]*double7/);
    expect(sql).not.toMatch(/SUM\([^)]*double8/);
  });

  it("reads the device from blob4 and the window from the caller", async () => {
    const seen = captureSql();
    await usageRows(base, 168);
    expect(seen[0]).toContain("blob4 AS dev");
    expect(seen[0]).toContain("INTERVAL '168' HOUR");
  });

  it("coerces the API's stringy numbers and the follow flag", async () => {
    captureSql([{
      dev: "0123456789abcdef", model: "s3-128", fw: "14", reports: "30.0", card_opens: "12",
      radar: "5", list: "2", stats: "1", follow: "0", claims: "3", follow_enabled: "1",
      uptime_hours: "71", last_report: "2026-09-24 03:00:00",
    }]);
    const [r] = await usageRows(base, 168);
    expect(r).toMatchObject({ reports: 30, cardOpens: 12, claims: 3, followEnabled: true, uptimeHours: 71 });
  });
});

describe("enrolled but silent", () => {
  // Allowlisted fakes (scripts/device-id-allowlist.txt), in sorted order.
  const ids = ["1111222233334444", "aaaabbbbccccdddd", "dead0000beef1111"];

  async function seedLedger() {
    for (const id of ids) {
      await E.ENRICH_KV.put(`enr:dev:${id}`, JSON.stringify({
        id, firstAt: "2026-08-01T00:00:00Z", lastAt: "2026-08-02T00:00:00Z", enrollments: 1,
        lastCountry: "XX", lastAsn: "AS0",
      }));
    }
    await E.ENRICH_KV.put("enr:dev:NOT-AN-ID", "{}");
    await E.ENRICH_KV.put("enr:day:2026-08-01", "3");
  }

  it("is the ledger MINUS the devices that made a request, not an age test on the ledger", async () => {
    await seedLedger();
    const out = await enrolledButSilent(E, new Set([ids[1] as string]));
    expect(out.rows.map((r) => r.dev)).toEqual([ids[0], ids[2]]);
    expect(out.total).toBe(2);
    expect(out.enrolled).toBe(3); // the malformed key and the day counter are not devices
  });

  it("CONTROL: when every enrolled device was heard from, the list is empty", async () => {
    await seedLedger();
    const out = await enrolledButSilent(E, new Set(ids));
    expect(out.rows).toEqual([]);
    expect(out.total).toBe(0);
  });

  it("carries the enrolment dates under their own name and nothing about location", async () => {
    await seedLedger();
    const out = await enrolledButSilent(E, new Set());
    expect(out.rows[0]).toEqual({
      dev: ids[0], firstEnrolled: "2026-08-01T00:00:00Z", lastEnrolled: "2026-08-02T00:00:00Z", enrollments: 1,
    });
    expect(JSON.stringify(out)).not.toContain("XX");
    expect(JSON.stringify(out)).not.toContain("AS0");
  });

  it("the seen set refuses to answer when it hits its cap rather than truncate", async () => {
    captureSql(Array.from({ length: SEEN_CAP }, (_, i) => ({ dev: i.toString(16).padStart(16, "0") })));
    await expect(seenDevices(base, 720)).rejects.toThrow(/cap/);
  });

  it("the seen set counts request points only", async () => {
    const seen = captureSql([{ dev: "0123456789abcdef" }]);
    const got = await seenDevices(base, 720);
    expect(got.has("0123456789abcdef")).toBe(true);
    expect(seen[0]).toContain(REQUEST_POINTS);
  });
});

describe("usage page rendering", () => {
  const row = {
    dev: "0123456789abcdef", model: "s3-128", fw: "14", reports: 30, cardOpens: 12, radar: 5, list: 2,
    stats: 1, follow: 0, claims: 3, followEnabled: true, uptimeHours: 71, lastReport: new Date().toISOString(),
  };

  it("keeps the framing: card opens is the interaction number, reports are not attention", () => {
    const html = usageBody([row], { rows: [], total: 0, enrolled: 1 }, 720);
    expect(html).toContain("Card opens</b> is the interaction number");
    expect(html).toContain("Requests are not attention");
    expect(html).toContain("last 30 days");
  });

  it("says when the silent list is cut, with the true total", () => {
    const rows = Array.from({ length: SILENT_SHOWN }, (_, i) => ({
      dev: i.toString(16).padStart(16, "0"), firstEnrolled: "", lastEnrolled: "", enrollments: 1,
    }));
    const html = usageBody([], { rows, total: SILENT_SHOWN + 5, enrolled: 400 }, 168);
    expect(html).toContain(`${SILENT_SHOWN + 5}</b> of 400`);
    expect(html).toContain(`showing the first ${SILENT_SHOWN}`);
  });

  it("CONTROL: an uncut list does not claim to be cut", () => {
    const html = usageBody([], { rows: [], total: 0, enrolled: 3 }, 168);
    expect(html).not.toContain("showing the first");
  });

  it("a silent list that could not be computed says so, instead of reading as none silent", () => {
    const html = usageBody([row], { error: "seen-device list hit its cap" }, 168);
    expect(html).toContain("Not shown: seen-device list hit its cap");
    expect(html).not.toContain("Every enrolled device made a request");
  });
});

describe("the front door covers the new pages", () => {
  it("/usage and /usage.json are a flat 403 without Access, and spend nothing", async () => {
    const spent = vi.fn();
    vi.stubGlobal("fetch", spent);
    for (const path of ["/usage", "/usage.json", "/usage?hours=720"]) {
      const res = await worker.fetch(new Request(`https://fleet.example${path}`), E);
      expect(res.status, path).toBe(403);
    }
    expect(spent).not.toHaveBeenCalled();
  });
});
