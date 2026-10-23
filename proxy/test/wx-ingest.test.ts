// WS5 (docs/wx-server.md): the ingest is idempotent, against the emulator -- after proving the emulator
// honours the two conditions the ingest relies on.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import type { LatestRecord } from "../src/wx/handler";
import { readLatest } from "../src/wx/handler";
import { type Fetch, type IngestOpts, NoaaError, advanceLatest, epochOfKey, ingestOnce } from "../src/wx/ingest";
import { checkAll } from "../src/wx/ingest-worker";
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
  // Changed by #472 (docs/wx-ingest-noaa-blips.md N8, named before the code): T1 is listed beside T2, so the second
  // run now BACKFILLS T1 where it reported already-stored. The claim is unchanged: each frame is written once.
  it("(a) each frame is written once: the newest, then the hole, then nothing", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const noaa = fakeNoaa(() => [T1, T2], gz);
    const c = counted(env.WX!);
    const r1 = await ingestOnce(c.bucket, cref, NOW, noaa.doFetch);
    const r2 = await ingestOnce(c.bucket, cref, NOW, noaa.doFetch);
    const r3 = await ingestOnce(c.bucket, cref, NOW, noaa.doFetch);
    console.log(`WS5a ${r1.status}, ${r2.status}, ${r3.status}; frame writes ${c.frameWrites()}; NOAA GETs ${noaa.gets()}`);
    expect([r1.status, r2.status, r3.status]).toEqual(["stored", "backfilled", "already-stored"]);
    expect(c.frameWrites()).toBe(2);
    expect(noaa.gets()).toBe(2);
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

// #472 (docs/wx-ingest-noaa-blips.md): the ingest rides out NOAA blips. Short timeouts are injected so these run in ms.
const FAST: IngestOpts = { list: { timeoutMs: 60, attempts: 3, waitsMs: [5, 5] }, get: { timeoutMs: 60, attempts: 2, waitsMs: [5] } };
const T3 = keyAt("201436"), T4 = keyAt("201636"), T5 = keyAt("201836");
const NOW5 = (epochOfKey(cref, T5)! + 50) * 1000;

type Step = "ok" | "hang" | number;                    // a number is an HTTP status
/** NOAA scripted per request kind: each list / get request takes the next step (the last one repeats). A "hang"
 *  never answers, but honours the abort signal, as a real fetch does (unless honourSignal is false). */
function scripted(visible: string[], gz: Uint8Array, list: Step[], get: Step[] = ["ok"], honourSignal = true) {
  const n = { list: 0, get: 0 };
  const doFetch: Fetch = (u, init) => {
    const kind = u.includes("list-type=2") ? "list" : "get";
    const plan = kind === "list" ? list : get;
    const step = plan[Math.min(n[kind], plan.length - 1)]!;
    n[kind]++;
    if (step === "hang") {
      return new Promise<Response>((_, reject) => {
        if (honourSignal) init?.signal?.addEventListener("abort", () => reject(new Error("The operation was aborted due to timeout")));
        setTimeout(() => reject(new Error("hang released by the test")), 1500);   // never leak past the test
      });
    }
    if (typeof step === "number") return Promise.resolve(new Response("", { status: step }));
    if (kind === "list") {
      const xml = `<ListBucketResult>${visible.map((k) => `<Contents><Key>${k}</Key></Contents>`).join("")}</ListBucketResult>`;
      return Promise.resolve(new Response(xml));
    }
    return Promise.resolve(new Response(gz));
  };
  return { doFetch, n };
}

describe("#472: the ingest rides out NOAA blips", () => {
  it("N1 a listing that answers 522, then succeeds: stored, after 2 attempts", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const r = await ingestOnce(env.WX!, cref, NOW, scripted([T2], gz, [522, "ok"]).doFetch, FAST);
    expect(r.status).toBe("stored");
    expect(r.attempts).toEqual({ list: 2, get: 1 });
  });

  it("SABOTAGE N1: with one attempt, the same blip is fatal", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const one = { ...FAST, list: { ...FAST.list, attempts: 1 } };
    await expect(ingestOnce(env.WX!, cref, NOW, scripted([T2], gz, [522, "ok"]).doFetch, one)).rejects.toThrow("NOAA list 522 (1 attempt)");
  });

  it("N2 a listing attempt that never answers is aborted at its timeout, and the next one succeeds", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const t = Date.now();
    const r = await ingestOnce(env.WX!, cref, NOW, scripted([T2], gz, ["hang", "ok"]).doFetch, FAST);
    const ms = Date.now() - t;
    console.log(`N2 ${r.status} after ${ms} ms, attempts ${JSON.stringify(r.attempts)}`);
    expect(r.status).toBe("stored");
    expect(r.attempts!.list).toBe(2);
    expect(ms).toBeLessThan(FAST.list.timeoutMs + 5 + 1000);
  });

  it("SABOTAGE N2: without the abort signal, a hung attempt is never cut short", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const run = ingestOnce(env.WX!, cref, NOW, scripted([T2], gz, ["hang", "ok"], ["ok"], false).doFetch, FAST);
    const raced = await Promise.race([run.then(() => "finished", () => "finished"), new Promise((d) => setTimeout(() => d("still hung"), 400))]);
    expect(raced).toBe("still hung");
    await run.catch(() => undefined);                  // let the released hang settle before the next test
  });

  it("N3 every attempt fails: one NoaaError, saying 3 attempts", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const err = await ingestOnce(env.WX!, cref, NOW, scripted([T2], gz, [525]).doFetch, FAST).catch((e) => e);
    expect(err).toBeInstanceOf(NoaaError);
    expect((err as NoaaError).attempts).toBe(3);
    expect(String(err)).toContain("NOAA list 525 (3 attempts)");
  });

  it("N4 a 4xx is final: 1 attempt", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const err = await ingestOnce(env.WX!, cref, NOW, scripted([T2], gz, [403, "ok"]).doFetch, FAST).catch((e) => e);
    expect((err as NoaaError).attempts).toBe(1);
  });

  it("N5 a hole between two stored frames is backfilled; latest stays at the newest", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const all = [T1, T2, T3];
    await ingestOnce(env.WX!, cref, NOW5, scripted([T1], gz, ["ok"]).doFetch, FAST);
    await ingestOnce(env.WX!, cref, NOW5, scripted([T1, T3], gz, ["ok"]).doFetch, FAST);
    const r = await ingestOnce(env.WX!, cref, NOW5, scripted(all, gz, ["ok"]).doFetch, FAST);
    expect(r.status).toBe("backfilled");
    expect(r.epoch).toBe(epochOfKey(cref, T2));
    expect(await env.WX!.head(frameKey(cref, epochOfKey(cref, T2)!))).not.toBeNull();
    expect((await readLatest(env.WX!, cref))!.epoch).toBe(epochOfKey(cref, T3));
    const again = await ingestOnce(env.WX!, cref, NOW5, scripted(all, gz, ["ok"]).doFetch, FAST);
    expect(again.status).toBe("already-stored");
  });

  it("N6 one build per check: an empty bucket and 5 listed frames take one download per check, newest first", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const all = [T1, T2, T3, T4, T5];
    const a = scripted(all, gz, ["ok"]);
    const r1 = await ingestOnce(env.WX!, cref, NOW5, a.doFetch, FAST);
    expect([r1.status, r1.epoch, a.n.get]).toEqual(["stored", epochOfKey(cref, T5), 1]);
    const b = scripted(all, gz, ["ok"]);
    const r2 = await ingestOnce(env.WX!, cref, NOW5, b.doFetch, FAST);
    expect([r2.status, r2.epoch, b.n.get]).toEqual(["backfilled", epochOfKey(cref, T4), 1]);
  });

  it("N7 a download that answers 522, then succeeds: stored, after 2 get attempts", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const r = await ingestOnce(env.WX!, cref, NOW, scripted([T2], gz, ["ok"], [522, "ok"]).doFetch, FAST);
    expect(r.status).toBe("stored");
    expect(r.attempts).toEqual({ list: 1, get: 2 });
  });

  it("logging: a retried success logs wx_ingest with attempts; wx_ingest_error carries attempts = the maximum", async () => {
    const gz = await synthGribGz({ ...GRID, values });
    const lines: string[] = [];
    await checkAll({ WX: env.WX! }, ":00", scripted([T2], gz, [520, "ok"]).doFetch, (l) => lines.push(l), FAST);
    await checkAll({ WX: env.WX! }, ":30", scripted([T2], gz, [525]).doFetch, (l) => lines.push(l), FAST);
    const [ok, bad] = lines.map((l) => JSON.parse(l));
    expect([ok.evt, ok.attempts.list]).toEqual(["wx_ingest", 2]);
    expect([bad.evt, bad.attempts]).toEqual(["wx_ingest_error", 3]);
  });

  it("the worker's default fetch passes each attempt's abort signal (the production path)", async () => {
    const seen: (AbortSignal | undefined)[] = [];
    const real = globalThis.fetch;
    globalThis.fetch = (async (_u: RequestInfo | URL, init?: RequestInit) => {
      seen.push(init?.signal ?? undefined);
      return new Response("", { status: 404 });          // final, so one request is enough
    }) as typeof fetch;
    try {
      await checkAll({ WX: env.WX! }, ":00", undefined, () => undefined);
    } finally {
      globalThis.fetch = real;
    }
    expect(seen.length).toBeGreaterThan(0);
    expect(seen.every((s) => s instanceof AbortSignal)).toBe(true);
  });
});
