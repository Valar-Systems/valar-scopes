// WS5 (docs/wx-server.md): the ingest is idempotent, against the emulator -- after proving the emulator
// honours the two conditions the ingest relies on.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import type { LatestRecord } from "../src/wx/handler";
import { readLatest } from "../src/wx/handler";
import { type Fetch, advanceLatest, epochOfKey, ingestOnce } from "../src/wx/ingest";
import { PRODUCTS, frameKey, framesPrefix, latestKey } from "../src/wx/products";
import { synthGribGz } from "./wx-grib";

const cref = PRODUCTS[0]!;
const DAY = "20261020";
const keyAt = (hhmmss: string) =>
  `${cref.noaaPrefix}/${DAY}/${cref.noaaStem}_${DAY}-${hhmmss}.grib2.gz`;
const T1 = keyAt("201036"), T2 = keyAt("201236");
const e1 = epochOfKey(cref, T1)!, e2 = epochOfKey(cref, T2)!;
const NOW = (e2 + 50) * 1000;    // 50 s after T2's frame time

const GRID = { ni: 40, nj: 20, la1: 40.0, lo1: -100.0, di: 0.01, dj: 0.01 };
const values = Array.from({ length: GRID.ni * GRID.nj }, (_, i) => (i % 7 === 0 ? -999 : i % 5 === 0 ? -99 : 15 + (i % 13) * 5));

/** NOAA as the ingest sees it: a listing of `visible` keys, and a body for each. */
function fakeNoaa(visible: () => string[], gz: Uint8Array): { doFetch: Fetch; gets: () => number } {
  let gets = 0;
  const doFetch: Fetch = async (u) => {
    if (u.includes("list-type=2")) {
      const xml = `<ListBucketResult>${visible().map((k) => `<Contents><Key>${k}</Key><LastModified>2026-10-20T20:13:00.000Z</LastModified></Contents>`).join("")}</ListBucketResult>`;
      return new Response(xml);
    }
    gets++;
    return new Response(gz);
  };
  return { doFetch, gets: () => gets };
}

/** env.WX with every successful frame write counted, and optionally the create-only condition removed
 *  (the sabotage: as if ingest.ts had no If-None-Match). */
function counted(bucket: R2Bucket, stripCondition = false) {
  let frameWrites = 0;
  const wrapped = new Proxy(bucket, {
    get(target, prop) {
      if (prop === "put") return async (k: string, v: unknown, o?: R2PutOptions) => {
        const opts = stripCondition && k.startsWith(framesPrefix(cref)) ? { ...o, onlyIf: undefined } : o;
        const r = await target.put(k, v as ArrayBuffer, opts);
        if (r && k.startsWith(framesPrefix(cref))) frameWrites++;
        return r;
      };
      const v = Reflect.get(target, prop);
      return typeof v === "function" ? v.bind(target) : v;
    },
  });
  return { bucket: wrapped as R2Bucket, frameWrites: () => frameWrites };
}

describe("WS5 controls: the emulator honours the conditions (else WS5 is BLIND)", () => {
  it("an unconditional put overwrites an existing key", async () => {
    await env.WX!.put("probe/x", "one");
    expect(await env.WX!.put("probe/x", "two")).not.toBeNull();
    expect(await (await env.WX!.get("probe/x"))!.text()).toBe("two");
  });
  it("If-None-Match: * on an existing key returns null and leaves it", async () => {
    await env.WX!.put("probe/y", "one");
    expect(await env.WX!.put("probe/y", "two", { onlyIf: new Headers({ "If-None-Match": "*" }) })).toBeNull();
    expect(await (await env.WX!.get("probe/y"))!.text()).toBe("one");
  });
  it("etagMatches with a stale etag returns null", async () => {
    const a = await env.WX!.put("probe/z", "one");
    await env.WX!.put("probe/z", "two");
    expect(await env.WX!.put("probe/z", "three", { onlyIf: { etagMatches: a!.etag } })).toBeNull();
  });
});

describe("WS5: idempotent ingest", () => {
  it("(a) the same frame twice is written once; the second run reports already-stored", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const noaa = fakeNoaa(() => [T1, T2], gz);
    const c = counted(env.WX!);
    const r1 = await ingestOnce(c.bucket, cref, NOW, noaa.doFetch);
    const r2 = await ingestOnce(c.bucket, cref, NOW, noaa.doFetch);
    console.log(`WS5a ${r1.status} then ${r2.status}; frame writes ${c.frameWrites()}; NOAA GETs ${noaa.gets()}`);
    expect([r1.status, r2.status]).toEqual(["stored", "already-stored"]);
    expect(c.frameWrites()).toBe(1);
    expect(noaa.gets()).toBe(1);
    expect((await readLatest(env.WX!, cref))!.epoch).toBe(e2);
  });

  it("(b) newest first, then an older frame: latest stays at the newest", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    let vis = [T2];
    const noaa = fakeNoaa(() => vis, gz);
    await ingestOnce(env.WX!, cref, NOW, noaa.doFetch);
    vis = [T1];                                   // a listing that (somehow) offers only the older frame
    const r = await ingestOnce(env.WX!, cref, NOW, noaa.doFetch);
    console.log(`WS5b second run: ${r.status}, latest ${r.latest}`);
    expect(r.latest).toBe("kept-newer");
    expect((await readLatest(env.WX!, cref))!.epoch).toBe(e2);
  });

  it("SABOTAGE (b): without the 'only if newer' test, latest moves backwards", async () => {
    const rec = (e: number): LatestRecord => ({ product: "cref", epoch: e, key: frameKey(cref, e), ingestedAt: 0, ...GRID, tile: 512 });
    await advanceLatest(env.WX!, cref, rec(e2));
    // advanceLatest with the epoch comparison removed: the etag guard alone, as a careless version would do
    const cur = await env.WX!.head(latestKey(cref));     // head: the etag without a body to leave unread
    await env.WX!.put(latestKey(cref), JSON.stringify(rec(e1)), { onlyIf: { etagMatches: cur!.etag } });
    const now = (await readLatest(env.WX!, cref))!.epoch;
    console.log(`WS5b sabotage: latest is now ${now === e1 ? "the OLDER frame" : "unchanged"}`);
    expect(now).toBe(e1);
  });

  it("(c) two ingests of the same frame, started together: exactly one frame write", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const noaa = fakeNoaa(() => [T2], gz);
    const c = counted(env.WX!);
    const rs = await Promise.all([ingestOnce(c.bucket, cref, NOW, noaa.doFetch), ingestOnce(c.bucket, cref, NOW, noaa.doFetch)]);
    console.log(`WS5c ${rs.map((r) => r.status).join(" + ")}; frame writes ${c.frameWrites()}`);
    expect(c.frameWrites()).toBe(1);
  });

  it("SABOTAGE (c): without the create-only condition, two writes land", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const noaa = fakeNoaa(() => [T2], gz);
    const c = counted(env.WX!, true);
    await Promise.all([ingestOnce(c.bucket, cref, NOW, noaa.doFetch), ingestOnce(c.bucket, cref, NOW, noaa.doFetch)]);
    console.log(`WS5c sabotage frame writes ${c.frameWrites()}`);
    expect(c.frameWrites()).toBe(2);
  });

  it("(d) frames older than 1 h are deleted; newer ones and latest's frame are kept", async () => {
    const old1 = e2 - 3700, old2 = e2 - 3601, young = e2 - 3500;
    for (const e of [old1, old2, young]) await env.WX!.put(frameKey(cref, e), "x");
    const gz = await synthGribGz({ ...GRID, values });
    const r = await ingestOnce(env.WX!, cref, NOW, fakeNoaa(() => [T2], gz).doFetch);
    const left = (await env.WX!.list({ prefix: framesPrefix(cref) })).objects.map((o) => o.key).sort();
    console.log(`WS5d pruned ${r.pruned}; left ${left.length}`);
    expect(r.pruned).toBe(2);
    expect(left).toEqual([frameKey(cref, young), frameKey(cref, e2)].sort());
  });
});
