// TX1 (docs/test-identity.md, item 4): the production checks' TEST identity is not a unit. Every counter Daniel uses to
// estimate units leaves out the ids in TEST_DEVICE_IDS. The SQL API is stood in for by a minimal engine over a fixture
// of request points: it applies any `blobN NOT IN (...)` the query carries and answers each counter's shape, so the
// rows test what the exclusion DOES, not only that a string is present. EXAMPLE ids, built at run time (no 16-hex
// literal in the repo). S-TXNOEXCL, which removes the exclusion, must make TX1 fail with "Devices seen" up by one.
import { afterEach, describe, expect, it, vi } from "vitest";
import * as q from "../src/analytics";
import type { Env } from "../src/types";

const A = "a1".repeat(8), B = "b2".repeat(8), T = "c3".repeat(8); // two real devices, and the test identity
const POINTS = [A, A, B, T, T, T].map((dev, i) => ({ dev, fw: "18", model: "s3-128", ts: `2026-11-04 0${i}:00:00` }));

function engine(seenSql: string[]) {
  vi.stubGlobal("fetch", vi.fn(async (_u: RequestInfo | URL, init?: RequestInit) => {
    const sql = String(init?.body ?? "");
    seenSql.push(sql);
    const out = new Set<string>();
    for (const m of sql.matchAll(/blob[45] NOT IN \(([^)]*)\)/g)) for (const id of m[1]!.split(",")) out.add(id.trim().replace(/'/g, ""));
    const pts = POINTS.filter((p) => !out.has(p.dev));
    const devs = [...new Set(pts.map((p) => p.dev))];
    let data: unknown[] = [];
    if (/count\(DISTINCT blob5\) - MAX/.test(sql)) data = [{ devices: devs.length, requests: pts.length, errors: 0, cards: 0, unattributed: 0 }];
    else if (/SELECT blob6 AS fw/.test(sql)) data = [{ fw: "18", model: "s3-128", devices: devs.length }];
    else if (/MIN\(timestamp\) AS first/.test(sql)) data = devs.map((dev) => ({ dev, first: pts.find((p) => p.dev === dev)!.ts }));
    else if (/argMax\(blob4, timestamp\) AS model/.test(sql)) data = devs.map((dev) => ({ dev, model: "s3-128", fw: "18", requests: pts.filter((p) => p.dev === dev).length, last_seen: "x" }));
    else if (/SELECT blob5 AS dev/.test(sql)) data = devs.map((dev) => ({ dev }));
    return new Response(JSON.stringify({ data, rows: data.length }), { headers: { "Content-Type": "application/json" } });
  }));
}
const ledgerKv = (ids: string[]) => ({
  list: async () => ({ keys: ids.map((id) => ({ name: `enr:dev:${id}` })), list_complete: true, cursor: undefined }),
  get: async () => ({ firstAt: "2026-10-01T00:00:00Z", lastAt: "2026-10-01T00:00:00Z", enrollments: 1 }),
}) as unknown as KVNamespace;
const env = (testIds?: string) => ({ CF_ACCOUNT_ID: "acct", CF_API_TOKEN: "tok", ENRICH_KV: ledgerKv([A, B, T]), ...(testIds === undefined ? {} : { TEST_DEVICE_IDS: testIds }) }) as unknown as Env;

afterEach(() => vi.unstubAllGlobals());

describe("TX1, the test identity is not a unit", () => {
  it("TX1: every counter leaves the TEST_DEVICE_IDS id out; Devices seen is the fixture's two real devices", async () => {
    const sql: string[] = [];
    engine(sql);
    const e = env(T);
    expect((await q.fleetTotals(e, 24)).devices).toBe(2);
    expect((await q.fleetRows(e, 24)).map((r) => r.dev).sort()).toEqual([A, B]);
    expect((await q.firmwareSpread(e, 24))[0]!.devices).toBe(2);
    expect([...(await q.seenDevices(e, 24))].sort()).toEqual([A, B]);
    expect([...(await q.firstSeenTimes(e)).keys()].sort()).toEqual([A, B]);
    expect([...(await q.firstRequestTimes(e, ["/v1/blips"])).keys()].sort()).toEqual([A, B]);
    await q.usageRows(e, 24);
    expect(sql.at(-1)).toContain(`blob4 NOT IN ('${T}')`);
    expect((await q.readLedger(e)).map((r) => r.dev).sort()).toEqual([A, B]);
    const silent = await q.enrolledButSilent(e, new Set());
    expect(silent.enrolled).toBe(2);
  });

  it("TX1's control: with no TEST_DEVICE_IDS the fixture's test id IS counted (3), so TX1's 2 is the exclusion's", async () => {
    engine([]);
    expect((await q.fleetTotals(env(), 24)).devices).toBe(3);
    expect((await q.readLedger(env())).length).toBe(3);
  });

  it("a TEST_DEVICE_IDS entry out of the id shape is refused, never quoted into the SQL", async () => {
    engine([]);
    await expect(q.fleetTotals(env(`${T}, x') OR 1=1 --`), 24)).rejects.toThrow(/not a device id/);
  });
});
