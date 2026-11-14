// RH1-RH8 (docs/route-mirror-age.md, section 2): /healthz's `routes` field, through the proxy's own dispatch. The
// mirror's age comes from `meta:routes` (written last by scripts/ingest-routes.ts); a missing, unparsable, future or
// unreadable value reads null with stale true and a reason, never a made-up age. Each test is named by its row id.
import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { call } from "./helpers";

// By glob, not by import: on a tree without src/routeshealth.ts the glob is {} and every row is graded red one by
// one, rather than the file failing to load (the registration's "red on today's code" is a per-row prediction).
const mods = import.meta.glob<{ resetRoutesHealth?: () => void }>("../src/routeshealth.ts", { eager: true });
const resetRoutesHealth = () => Object.values(mods)[0]?.resetRoutesHealth?.();

const DAY = 86_400_000;
type Routes = { builtAt: string | null; ageDays: number | null; rows: number | null; stale: boolean; maxAgeDays: number; why?: string };
const body = async (overrides: Parameters<typeof call>[1] = {}) => {
  const res = await call(new Request("https://proxy.test/healthz"), overrides);
  expect(res.status).toBe(200);
  return await res.json() as { commit?: unknown; wx?: unknown; basemap?: unknown; routes?: Routes };
};
const routes = async (overrides: Parameters<typeof call>[1] = {}) => (await body(overrides)).routes;
const plant = (builtAtMs: number, rows = 619103) =>
  env.ENRICH_KV.put("meta:routes", JSON.stringify({ v: 1, ruleRev: 3, rows, builtAt: new Date(builtAtMs).toISOString(), shards: { "routes/ZZ-all.csv": "0:1" } }));

describe("RH, /healthz's routes field", () => {
  beforeEach(async () => {
    resetRoutesHealth();
    await env.ENRICH_KV.delete("meta:routes");
  });

  it("RH1: a 3-day-old build is fresh", async () => {
    const t = Date.now() - 3 * DAY;
    await plant(t);
    expect(await routes()).toEqual({ builtAt: new Date(t).toISOString(), ageDays: 3, rows: 619103, stale: false, maxAgeDays: 10 });
  });

  it("RH2: a 20-day-old build is stale", async () => {
    await plant(Date.now() - 20 * DAY);
    const r = await routes();
    expect(r?.ageDays).toBe(20);
    expect(r?.stale).toBe(true);
    expect(r?.why).toBeUndefined();
  });

  it("RH3: the boundary is 10 days in milliseconds, not floored days", async () => {
    await plant(Date.now() - 10 * DAY + 60_000);
    let r = await routes();
    expect([r?.ageDays, r?.stale]).toEqual([9, false]);
    resetRoutesHealth();
    await plant(Date.now() - 10 * DAY - 60_000);
    r = await routes();
    expect([r?.ageDays, r?.stale]).toEqual([10, true]);
  });

  it("RH4: no meta key reads null, stale, absent", async () => {
    expect(await routes()).toEqual({ builtAt: null, ageDays: null, rows: null, stale: true, maxAgeDays: 10, why: "absent" });
  });

  it("RH5: an unparsable value, or one with no builtAt, reads null, stale, unparsable", async () => {
    const want = { builtAt: null, ageDays: null, rows: null, stale: true, maxAgeDays: 10, why: "unparsable" };
    await env.ENRICH_KV.put("meta:routes", "not json");
    expect(await routes()).toEqual(want);
    resetRoutesHealth();
    await env.ENRICH_KV.put("meta:routes", JSON.stringify({ rows: 5 }));
    expect(await routes()).toEqual(want);
  });

  it("RH6: a builtAt in the future is never clamped to age 0", async () => {
    await plant(Date.now() + DAY);
    expect(await routes()).toEqual({ builtAt: null, ageDays: null, rows: null, stale: true, maxAgeDays: 10, why: "future" });
  });

  it("RH7: a throwing KV reads unreadable and is not cached; a good read IS cached until reset", async () => {
    const throwing = { get: async () => { throw new Error("kv down"); } } as unknown as KVNamespace;
    expect(await routes({ ENRICH_KV: throwing })).toEqual({ builtAt: null, ageDays: null, rows: null, stale: true, maxAgeDays: 10, why: "unreadable" });
    const t = Date.now() - 3 * DAY;
    await plant(t);
    expect((await routes())?.builtAt).toBe(new Date(t).toISOString()); // no reset: the throw was not cached
    await plant(Date.now() - 20 * DAY);
    expect((await routes())?.builtAt).toBe(new Date(t).toISOString()); // cached for the isolate's 10 minutes
    resetRoutesHealth();
    expect((await routes())?.ageDays).toBe(20);
  });

  it("RH8 (control): commit, wx and basemap are still in the body", async () => {
    await plant(Date.now() - 3 * DAY);
    const b = await body();
    expect(typeof b.commit).toBe("string");
    expect(b).toHaveProperty("wx");
    expect(b).toHaveProperty("basemap");
  });

  // W13 (docs/route-weekly-code.md, section 2c): the SOURCE's own commit date, informational, so an upstream that
  // stops changing stays visible even while a verified re-seal keeps builtAt fresh. Never invented, never an alert.
  const plantWith = (sourceCommittedAt: unknown) => {
    const t = Date.now() - 3 * DAY;
    return env.ENRICH_KV.put("meta:routes", JSON.stringify({ v: 1, ruleRev: 3, rows: 619103, builtAt: new Date(t).toISOString(), sourceCommittedAt, shards: { "routes/ZZ-all.csv": "0:1" } }));
  };

  it("RH9: meta's sourceCommittedAt is shown byte-identical", async () => {
    await plantWith("2026-11-12T10:00:00Z");
    const r = await routes() as Routes & { sourceCommittedAt?: unknown };
    expect(r?.sourceCommittedAt).toBe("2026-11-12T10:00:00Z");
    expect(r?.stale).toBe(false);
  });

  it("RH9c (control): meta without sourceCommittedAt shows no such field", async () => {
    await plant(Date.now() - 3 * DAY);
    const r = await routes();
    expect(r).toBeDefined();
    expect(Object.keys(r as object)).not.toContain("sourceCommittedAt");
  });

  it("RH9s: a non-ISO sourceCommittedAt is omitted, not shown", async () => {
    for (const bad of ["yesterday", 1731400000, "2026-11-12", "2026-11-12T10:00:00+01:00"]) {
      resetRoutesHealth();
      await plantWith(bad);
      const r = await routes();
      expect(r?.builtAt).not.toBeNull(); // the rest of the field still reads
      expect(Object.keys(r as object)).not.toContain("sourceCommittedAt");
    }
  });
});
