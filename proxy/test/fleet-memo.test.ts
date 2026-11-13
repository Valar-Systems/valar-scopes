// The fw: read's isolate memo (docs/proxy-bill.md, step 4a: C1-C3, and the pin from the cloud reviewer's item 1).
//
// Each test drives makeFleetRecorder() against an in-memory KV that counts its fw: gets, with a clock the test owns,
// and settles every request's waitUntil before the next -- so "how many reads" is a number, not a timing accident.
// C1-red (the memo planted out must turn C1 red with exactly 22 reads) is scripts/sabotage-fleet-memo.mjs.
import { describe, expect, it } from "vitest";
import srcTexts from "virtual:proxy-src-texts";
import { FLEET_MEMO_MAX, REFRESH_MS, makeFleetRecorder, type FleetRecord, type FleetRecorder } from "../src/fleet";
import type { RequestMetric } from "../src/metrics";
import type { Env } from "../src/types";

const DEV = "0123456789abcdef"; // the allowlisted example id (scripts/device-id-allowlist.txt), never a real device
const T0 = Date.UTC(2026, 10, 13, 0, 0, 0);

function fakeKv() {
  const store = new Map<string, string>();
  const kv = {
    gets: 0,
    failNext: false,
    async get(key: string, _type?: string): Promise<unknown> {
      if (key.startsWith("fw:")) kv.gets++;
      if (kv.failNext) {
        kv.failNext = false;
        throw new Error("planted KV failure");
      }
      const v = store.get(key);
      return v === undefined ? null : JSON.parse(v);
    },
    async put(key: string, value: string): Promise<void> {
      store.set(key, value);
    },
    row(dev = DEV): FleetRecord | null {
      const v = store.get(`fw:${dev}`);
      return v === undefined ? null : (JSON.parse(v) as FleetRecord);
    },
  };
  return kv;
}

type Kv = ReturnType<typeof fakeKv>;

/** One request through a recorder: returns its fwKvRead once its waitUntil has settled. */
async function request(rec: FleetRecorder, kv: Kv, fw: string, dev = DEV): Promise<number | undefined> {
  const pending: Promise<unknown>[] = [];
  const ctx = { waitUntil: (p: Promise<unknown>) => void pending.push(p), passThroughOnException: () => {} };
  const meta: RequestMetric = { ep: "/api/v1/blipscope/blips", status: 200, ms: 1, model: "s3-128", colo: "SEA", dev, fw };
  rec.record({ ENRICH_KV: kv } as unknown as Env, ctx as unknown as ExecutionContext, meta);
  await Promise.all(pending);
  return meta.fwKvRead;
}

describe("the fw: memo (docs/proxy-bill.md step 4a)", () => {
  it("C1: the counter equals the reads, and the memo answers the rest -- 3 reads over 22 requests", async () => {
    const kv = fakeKv();
    let t = T0;
    const rec = makeFleetRecorder(() => t);
    let counted = 0;
    for (let i = 0; i < 20; i++) {
      counted += (await request(rec, kv, "19")) ?? 0; // one firmware, inside the hour
      t += 1_000;
    }
    counted += (await request(rec, kv, "20")) ?? 0; // a firmware change: must read
    t += REFRESH_MS;
    counted += (await request(rec, kv, "20")) ?? 0; // the stored refresh falls due: must read
    expect(kv.gets).toBe(3);
    expect(counted).toBe(kv.gets);
    expect(kv.row()?.fw).toBe("20");
    expect(kv.row()?.changes.map((c) => `${c.from}->${c.to}`)).toEqual(["19->20"]);
  });

  it("C2: a return to a firmware an isolate still remembers is recorded LATE, never lost", async () => {
    const kv = fakeKv();
    let t = T0;
    const A = makeFleetRecorder(() => t);
    const B = makeFleetRecorder(() => t);
    await request(A, kv, "19"); // A: no row, writes v19 at T0, remembers (19, T0)
    t = T0 + 60_000;
    await request(B, kv, "20"); // B: reads v19, records 19->20
    t = T0 + 120_000;
    expect(await request(A, kv, "19")).toBe(0); // A's memo still says 19: no read, so not yet recorded
    expect(kv.row()?.fw).toBe("20"); // the table is behind the device: the cost the doc names
    t = T0 + REFRESH_MS; // A's memo expires exactly when its remembered lastSeen's refresh falls due
    expect(await request(A, kv, "19")).toBe(1);
    const ch = kv.row()?.changes ?? [];
    expect(ch.map((c) => `${c.from}->${c.to}`)).toEqual(["19->20", "20->19"]);
    expect(ch[1]?.at).toBeLessThanOrEqual(T0 + REFRESH_MS); // late by at most REFRESH_MS...
    expect(ch[1]?.at).toBeGreaterThan(T0 + 120_000); // ...and genuinely late: the bound is real, not zero
  });

  it("C3: the memo is bounded, and it remembers at all", async () => {
    const kv = fakeKv();
    const rec = makeFleetRecorder(() => T0);
    for (let i = 0; i <= FLEET_MEMO_MAX; i++) await request(rec, kv, "19", `dev-${i}`);
    expect(rec.memoSize()).toBeLessThanOrEqual(FLEET_MEMO_MAX);
    expect(rec.memoSize()).toBeGreaterThan(0); // control: a memo that never stores would pass the bound
  });

  it("no decision without both headers: no read, and no counter value", async () => {
    const kv = fakeKv();
    const rec = makeFleetRecorder(() => T0);
    expect(await request(rec, kv, "")).toBeUndefined();
    expect(kv.gets).toBe(0);
  });

  it("a failed read is not remembered: the next request reads again", async () => {
    const kv = fakeKv();
    const rec = makeFleetRecorder(() => T0);
    kv.failNext = true;
    expect(await request(rec, kv, "19")).toBe(1);
    expect(await request(rec, kv, "19")).toBe(1);
    expect(kv.gets).toBe(2);
    expect(await request(rec, kv, "19")).toBe(0); // and once a read succeeds, the memo answers
  });
});

// The cloud reviewer's item 1 (docs/proxy-bill.md): the update offer is GitHub's (src/OtaUpdater.cpp OTA_RELEASE_BASE),
// and nothing in proxy/src but fleet.ts may reach the fw: table -- or, through it, the memo. A future offer path that
// did would trip this the day it is written.
const FW_REF = /fleetKey\(|[`"']fw:/;
function fwReaders(files: Record<string, string>): string[] {
  return Object.entries(files)
    .filter(([f, text]) => f !== "fleet.ts" && FW_REF.test(text))
    .map(([f]) => f)
    .sort();
}

describe("only fleet.ts reaches a fw: key", () => {
  it("P: no other file in proxy/src references fleetKey( or a fw: key literal", () => {
    expect(Object.keys(srcTexts).length).toBeGreaterThan(30); // control: the texts arrived
    expect(srcTexts["fleet.ts"]).toMatch(FW_REF); // control: the pattern sees fleet.ts's own key
    expect(fwReaders(srcTexts)).toEqual([]);
  });

  it("P-red: a reference planted in a scratch copy is named", () => {
    const copy = { ...srcTexts, "config.ts": `${srcTexts["config.ts"] ?? ""}\nconst k = \`fw:\${dev}\`;\n` };
    expect(fwReaders(copy)).toEqual(["config.ts"]);
  });
});
