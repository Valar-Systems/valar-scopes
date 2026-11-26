// RR: the builder's store reader retries R2's transient internal error once (src/basemap/worker.ts r2Reader). On 2026-11-23
// a staging build died after 7 s of CPU on "get: We encountered an internal error. Please try again." in one of its ~124
// store reads, and the next ask had to build again. Each test is named by its case id; RR1 is the control.
// The store is the endpoint file's MW fixture (test/basemap-endpoint.test.ts mwStore), COPIED here so the two files share
// nothing: origin (0.37, 0.59), one water ring and one primary road in one cell.
import { describe, expect, it } from "vitest";
import { build } from "../src/basemap/build";
import { parseQuery } from "../src/basemap/query";
import { type ReadStat, r2Reader } from "../src/basemap/worker";
import { type SynthDef, COMMIT, lonLatOf, writeSynthStore } from "./basemap-synth";

const QW = "lat=0.37&lon=0.59&r=97&eps=100584&layers=water,roads";

async function mwStore() {
  const W = "https://example.invalid/water/w.zip", R = "https://example.invalid/roads/r.zip";
  const ll = (pts: Array<[number, number]>) => pts.map(([e, s]) => lonLatOf(0.37, 0.59, e, s));
  const covered: Array<[number, number]> = [];
  for (let h = 30; h <= 40; h++) for (let c = 50; c <= 60; c++) covered.push([h, c]);
  const def: SynthDef = {
    files: [{ layer: "water", url: W, sha256: "3".repeat(64), vintage: "2024" }, { layer: "roads", url: R, sha256: "4".repeat(64), vintage: "2024" }],
    features: [
      { kind: "water", url: W, record: 0, part: 0, attrs: { AWATER: 2_000_000 }, lonlat: ll([[1, 1], [3, 1], [3, 3], [1, 3], [1, 1]]) },
      { kind: "roads", url: R, record: 0, part: 0, attrs: { MTFCC: "S1100", FULLNAME: "Main Hwy" }, lonlat: ll([[-5, -2], [5, -2]]) },
    ],
    regions: [{ box: [0.3, 0.3, 0.9, 0.45], files: [W] }, { box: [0.3, 0.3, 0.9, 0.45], files: [R] }],
    covered,
  };
  return writeSynthStore(def);
}

const buildQ = () => {
  const p = parseQuery(new URL(`https://x/?${QW}`));
  if (!p.ok) throw new Error(p.code);
  const q = p.q;
  return { lat0Micro: q.lat0Micro, lon0Micro: q.lon0Micro, rKm: q.rKm, epsMm: q.epsMm, mask: q.mask, levels: q.levels };
};
const freshStat = (): ReadStat => ({ reads: 0, bytes: 0, readMs: 0, maxReadMs: 0 });
const INTERNAL = () => new Error("get: We encountered an internal error. Please try again. (10001)");

/** An R2Bucket over the store's objects, counting get() calls; `fail(n, key)` returns an Error to throw for that call, or null. */
function bucket(objects: Map<string, Uint8Array>, fail: (n: number, key: string) => Error | null = () => null) {
  const calls = { n: 0 };
  const b = {
    async get(key: string) {
      const e = fail(++calls.n, key);
      if (e) throw e;
      const v = objects.get(key);
      return v ? { arrayBuffer: async () => v.slice().buffer } : null;
    },
  } as unknown as R2Bucket;
  return { b, calls };
}

describe("RR, the reader's one retry on R2's internal error", () => {
  it("RR1 CONTROL: a clean reader builds, reading N > 0 times", async () => {
    const s = await mwStore();
    const stat = freshStat(), { b, calls } = bucket(s.store.objects);
    const r = await build(r2Reader(b, stat), buildQ(), { commit: COMMIT });
    expect(r.ok).toBe(true);
    expect(stat.reads).toBeGreaterThan(0);
    expect(calls.n).toBe(stat.reads);
  });

  it("RR2: one internal error on the 3rd read: the build succeeds with the SAME bytes, reading N + 1 times", async () => {
    const s = await mwStore();
    const st0 = freshStat(), clean = bucket(s.store.objects);
    const r0 = await build(r2Reader(clean.b, st0), buildQ(), { commit: COMMIT });
    const st1 = freshStat(), flaky = bucket(s.store.objects, (n) => (n === 3 ? INTERNAL() : null));
    const r1 = await build(r2Reader(flaky.b, st1), buildQ(), { commit: COMMIT });
    if (!r0.ok || !r1.ok) throw new Error(`RR2: a build failed (clean ${r0.ok}, flaky ${r1.ok})`);
    expect(r1.bytes).toEqual(r0.bytes);
    expect([st1.reads, flaky.calls.n]).toEqual([st0.reads + 1, clean.calls.n + 1]);
  });

  it("RR3: a missing key is null after exactly one call (never retried)", async () => {
    const s = await mwStore();
    const { b, calls } = bucket(s.store.objects);
    expect(await r2Reader(b).get("bm/v1/no-such-key")).toBeNull();
    expect(calls.n).toBe(1);
  });

  it("RR4: any other error is thrown at once, after exactly one call", async () => {
    const s = await mwStore();
    for (const err of [new Error("get: The specified bucket does not exist. (10006)"), new TypeError("boom")]) {
      const { b, calls } = bucket(s.store.objects, () => err);
      await expect(r2Reader(b).get("bm/v1/current")).rejects.toBe(err);
      expect(calls.n).toBe(1);
    }
  });

  it("RR5: two internal errors on one key throw after exactly two calls (one retry only)", async () => {
    const s = await mwStore();
    const { b, calls } = bucket(s.store.objects, () => INTERNAL());
    await expect(r2Reader(b).get("bm/v1/current")).rejects.toThrow(/We encountered an internal error/);
    expect(calls.n).toBe(2);
  });
});
