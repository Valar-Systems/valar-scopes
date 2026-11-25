// The screen service, phase 1, the ENRICH part (its registration, accepted 2026-11-25 with C1, A1, D3' and the
// P-E2b ruling). The joined `snapshot({ place?, model? })` and `enrich({ hex, model? })`, run as D3' builds them:
// the Worker checks shape with 0 reads and keeps the per-location shield; everything else is ONE RPC per call to the
// one ScreenDetailsDO (admit, the site feed, the place gate, the blocked list, the class, the store, the cap).
//
// HOW THE DO IS DRIVEN. The test pool builds its Worker from the boards' wrangler.toml, which cannot bind a screen DO
// (P-B2 keeps the screen out of the boards' bundle). So the DO is run IN PROCESS: one object made from
// ScreenDetailsDO's prototype, as test/screen-entry.test.ts drives the entrypoints, behind a fake namespace that
// counts requests and structured-clones each request and answer as RPC would. That needs two things of the code:
// its state is created lazily on `this` (no constructor runs here), and background resolutions go to
// `this.ctx.waitUntil` (which keeps a real DO in memory, and lets this test wait for them).
//
// WHAT IS REAL AND WHAT IS FAKE.
//   real   the boards' handleEnrich, their ENRICH_KV (isolated per test) under a recording wrapper, the photo
//          pointers, the counter (src/screen/metrics.ts) and dispatch.
//   fake   the site feed's binding (its answer is set per test; its circle is centred on a PUBLIC AIRPORT at 2 dp,
//          never the site feed's own -- A1); the upstreams, through fetchMock, each counted; the per-location
//          shield; the clock (an injected seam).
// Every hex below is SYNTHETIC (a repeating pattern), outside the non-ICAO block icaoalloc.ts short-circuits. None is
// a real aircraft's. The military fixture sits inside the FIRMWARE's MIL_RANGES (src/SpecialAircraft.cpp, US block
// 0xADF7C8-0xAFFFFF); scripts/check-screen.ts's P-E3 proves the runtime table equals that file.
//
// EVERY HEX THE SNAPSHOT CAN RESOLVE IS MOCKED ONCE, in beforeAll. A background resolution of an unmocked hex would
// fail upstream and could open the adsb.lol breaker for the rest of the file; the breakers are also reset per test.
//
// COUNTS, NAMED ONCE. "0 resolutions, 0 store reads" (D3') is read as: 0 upstream hex fetches and 0 KV reads other
// than the two cfg: keys. A call that reaches the DO costs exactly 1 DO request; Worker-side refusals cost 0.
//
// Rows (predicted first, in screen-enrich-red.md): P-S1 P-S2 P-S3 P-E1 P-E2 (a-f) P-C1 P-K1 P-R1 P-G1 P-L1 (strict,
// two locations) P-L1b P-T1 P-V1 P-D1 P-J1 P-G2 P-G3 P-M1 (the 8 new outcomes) P-H3 (no per-outcome counts exposed).
import { env, fetchMock } from "cloudflare:test";
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it, vi } from "vitest";
import { pointerKey } from "../src/photolicense";
import { RELEASE_URL } from "../src/screen/map";
import { __resetReleaseMemoForTests } from "../src/screen/release";
import * as W from "../src/screen/worker";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call as deviceCall, hexBody, makeAc } from "./helpers";

type Answer = { ok: true; value: unknown } | { ok: false; reason: string };
type Point = { blobs?: string[]; doubles?: number[]; indexes?: string[] };
type DOProto = { serve(req: unknown): Promise<unknown>; stats(): Promise<{ kvWritesDropped: number }> | { kvWritesDropped: number } };
// worker.ts as a NAMESPACE: an export the code has not written yet reads undefined, so each test goes red on its own.
const X = W as unknown as {
  ScreenDetailsDO?: { prototype: DOProto };
  __setClockForTests?: (f: (() => number) | undefined) => void;
  __tapEnrichRequestForTests?: (f: ((r: Request) => void | Promise<void>) | undefined) => void;
};

const LOL = "https://api.adsb.lol";
const CENTRE = { lat: 51.47, lon: -0.45 }; // a public airport's reference point, 2 dp (A1)
const RADIUS_KM = 40;
const T0 = Date.UTC(2026, 10, 25, 12, 0, 0);

// The 8 outcomes this phase adds, and the counter's full closed set (metrics.ts OUTCOMES).
const NEW_OUTCOMES = ["not_in_snapshot", "stale", "private", "blocked", "blocked_unknown", "place_not_allowed", "bad_model", "limited"];
const ALL_OUTCOMES = new Set(["ok", "bad_ctx", "tier", "not_released", "not_built", "unknown_method", "error", ...NEW_OUTCOMES]);

// Synthetic fixtures. cs: an airline-shaped callsign matches ^[A-Z]{3}[0-9][0-9A-Z]{0,3}$, a non-airline one does not.
const H = {
  PUB: "a0b1c2", NULLDB: "a0b1c3", GA: "a0b1c4", FLAG4: "a1a1a1", FLAG8: "a1a1a8", MILD: "a2a2a2", BLK: "a3a3a3",
  ABSENT: "a4a4a4", COLD: "a5a5a5", C1: "a6a6a6", R1: "a7a7a1", R2: "a7a7a2", R3: "a7a7a3", MILR: "ae0f00", PRIV: "c0ffee",
  V1: "a8a8a1", GAP: "a9a9a1", GAP2: "a9a9a2", S3: "a0a0a3", E: "a0a0ae", T1: "a0a0a1",
} as const;

function ac(hex: string, cs: string, extra: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    hex, cs, lat: 51.5, lon: -0.4, alt_ft: 30000, gs_kt: 400, track_deg: 90, vrate_fpm: 0, category: 3, seen_s: 1,
    t: "A320", desc: "AIRBUS A-320", dbFlags: null, ...extra,
  };
}
const FIXTURE = [
  ac(H.PUB, "TST101"), ac(H.NULLDB, "TST105", { dbFlags: null }), ac(H.GA, "N123AB"), ac(H.FLAG4, "TST104", { dbFlags: 4 }),
  ac(H.FLAG8, "TST108", { dbFlags: 8 }), ac(H.MILD, "TOPCAT2", { dbFlags: 1 }), ac(H.BLK, "TST103"), ac(H.COLD, "TST106"),
  ac(H.C1, "TST107"), ac(H.R1, "TST111"), ac(H.R2, "TST112"), ac(H.R3, "TST113"), ac(H.MILR, "TOPCAT1"), ac(H.PRIV, "TOPCAT3"),
  ac(H.V1, "TST121"), ac(H.GAP, "TST122", { t: "ZZ9Z", desc: null }), ac(H.GAP2, "TST126", { t: "ZZ9Z", desc: null }),
  ac(H.S3, "TST123", { lat: 51.53, lon: -0.31, track_deg: 270 }), ac(H.E, "TST124"),
];
const T1_AC = ac(H.T1, "TST125"); // P-T1's own: its upstream answer is delayed, so it is in no other test's snapshot
const CLASS_WANT: Record<string, "public" | "private"> = {
  [H.PUB]: "public", [H.NULLDB]: "public", [H.MILR]: "public", [H.MILD]: "public",
  [H.PRIV]: "private", [H.GA]: "private", [H.FLAG4]: "private", [H.FLAG8]: "private",
};
// What each hex's upstream answers with, beyond the defaults: ZZ9Z is a type no name table knows (an enrichment gap).
const MOCK_EXTRA: Record<string, Record<string, unknown>> = { [H.GAP]: { t: "ZZ9Z" }, [H.GAP2]: { t: "ZZ9Z" } };

function feedAnswer(aircraft: unknown[], stale = false): Record<string, unknown> {
  return {
    available: true, v: 2, centre: { ...CENTRE }, radius_km: RADIUS_KM, t: Math.floor(T0 / 1000) - 3, age_s: stale ? 200 : 3, stale,
    source: "adsb.lol", attribution: "fixture", attribution_url: "https://example.test/", aircraft: stale ? [] : aircraft,
  };
}

/** A recording wrapper over the real (per-test isolated) ENRICH_KV. Reads and writes are kept by key. */
function recordingKv(kv: KVNamespace, opts: { throwOn?: string } = {}) {
  const c = { reads: [] as string[], writes: [] as string[] };
  const read = (k: string) => {
    c.reads.push(k);
    if (opts.throwOn === k) throw new Error("planted KV read failure");
  };
  const w = {
    get: (k: string, o?: unknown) => { read(k); return (kv.get as (k: string, o?: unknown) => Promise<unknown>).call(kv, k, o); },
    getWithMetadata: (k: string, o?: unknown) => {
      read(k);
      return (kv.getWithMetadata as (k: string, o?: unknown) => Promise<unknown>).call(kv, k, o);
    },
    list: (o?: KVNamespaceListOptions) => { c.reads.push(`list:${o?.prefix ?? ""}`); return kv.list(o); },
    put: (k: string, v: string, o?: KVNamespacePutOptions) => { c.writes.push(k); return kv.put(k, v, o); },
    delete: (k: string) => { c.writes.push(`delete:${k}`); return kv.delete(k); },
  };
  return { c, kv: w as unknown as KVNamespace };
}
const isCfg = (k: string) => k.startsWith("cfg:screen-");
const dataReads = (c: { reads: string[] }) => c.reads.filter((k) => !isCfg(k)).length;
const cfgReads = (c: { reads: string[] }, key: string) => c.reads.filter((k) => k === key).length;

const recorder = () => {
  const points: Point[] = [];
  return { points, binding: { writeDataPoint: (p: Point) => { points.push(p); } } };
};

/** A per-location shield (the Workers rate-limit binding's shape): `limit` per class key, for the whole test. */
function shield(limit: number) {
  const n: Record<string, number> = {};
  return { limit: async ({ key }: { key: string }) => { n[key] = (n[key] ?? 0) + 1; return { success: n[key] <= limit }; } };
}

// Upstream hex lookups, counted per hex and zeroed per test. Routeset POSTs are counted file-wide (one path for every
// callsign), zeroed per test.
const hexFetches: Record<string, number> = {};
const hexBodyFor = (hex: string, extra: Record<string, unknown> = {}) =>
  hexBody([makeAc({ hex, flight: "TST000  ", r: "X-SYNTH", t: "A320", desc: "AIRBUS A-320", ownOp: "Synthetic Air", lat: 51.5, lon: -0.4, ...extra })]);
function mockHex(hex: string, extra: Record<string, unknown> = {}) {
  hexFetches[hex] = 0;
  fetchMock.get(LOL).intercept({ path: `/v2/hex/${hex}` }).reply(200, () => { hexFetches[hex]++; return hexBodyFor(hex, extra); }).persist();
}
const sumFetches = () => Object.values(hexFetches).reduce((s, n) => s + n, 0);
let routesetPosts = 0;

// File-wide P-K1 tally: every KV write any screen harness saw, and every write its read-only wrapper dropped.
let screenKvWrites = 0;
let droppedSeen = 0;

let clock = T0;
function harness(opts: { vars?: Record<string, string>; feed?: Record<string, unknown>; kvThrowOn?: string } = {}) {
  const DO = X.ScreenDetailsDO!.prototype; // red until the export exists: TypeError, per test
  const feed = { reads: 0, answer: opts.feed ?? feedAnswer(FIXTURE), snapshot: async () => { feed.reads++; return structuredClone(feed.answer); } };
  const kvRec = recordingKv(env.ENRICH_KV, { throwOn: opts.kvThrowOn });
  const gaps = recorder();
  const counter = recorder();
  const pending: Promise<unknown>[] = [];
  const doEnv: Record<string, unknown> = {
    ...env, ENRICH_KV: kvRec.kv, METRICS: gaps.binding, SITE_FEED: feed,
    SCREEN_LIMIT_WEBSITE_PER_MIN: "1000", SCREEN_LIMIT_APP_PER_MIN: "1000", SCREEN_RESOLVE_PER_MIN: "1000", ...opts.vars,
  };
  const doSelf = Object.create(DO) as Record<string, unknown>;
  doSelf.env = doEnv;
  doSelf.ctx = { id: { name: "screen", toString: () => "screen" }, waitUntil: (p: Promise<unknown>) => { pending.push(p); } };
  const doCalls = { n: 0, names: new Set<string>() };
  const stub = (name: string) => {
    doCalls.names.add(name);
    return { serve: async (req: unknown) => { doCalls.n++; return structuredClone(await DO.serve.call(doSelf, structuredClone(req))); } };
  };
  const ns = { idFromName: (n: string) => ({ name: n, toString: () => n }), get: (id: { name: string }) => stub(id.name), getByName: (n: string) => stub(n) };
  const location = (shieldLimit?: number) => ({
    env: { ...doEnv, SCREEN_METRICS: counter.binding, SCREEN_DETAILS: ns, ...(shieldLimit ? { RL_SCREEN: shield(shieldLimit) } : {}) },
  });
  const settle = async () => { while (pending.length) await Promise.allSettled(pending.splice(0)); };
  const stats = async () => (await DO.stats.call(doSelf)).kvWritesDropped;
  return { feed, kvRec, gaps, counter, pending, doCalls, loc: location(), location, settle, stats };
}
type Self = { env: object };
const site = (self: Self, name: "snapshot" | "enrich", args?: unknown) =>
  (W.ScreenWebsite.prototype as unknown as Record<string, (a?: unknown) => Promise<Answer>>)[name].call(self, args);
const app = (self: Self, name: "snapshot" | "enrich", args?: unknown) =>
  (W.ScreenApp.prototype as unknown as Record<string, (c: unknown, a?: unknown) => Promise<Answer>>)[name].call(self, { tier: "cloud" }, args);
const reason = (a: Answer) => (a.ok ? "ok" : a.reason);
const aircraftOf = (a: Answer) => ((a as { value: { aircraft: Array<Record<string, unknown>> } }).value.aircraft);
const outcomes = (rec: { points: Point[] }) => rec.points.filter((p) => p.indexes?.[0] !== "error").map((p) => p.blobs?.[2] ?? "?");
const tally = (h: { kvRec: { c: { writes: string[] } } }) => { screenKvWrites += h.kvRec.c.writes.length; };

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
  fetchMock.get(LOL).intercept({ path: "/api/0/routeset", method: "POST" }).reply(200, () => { routesetPosts++; return "[]"; }).persist();
  for (const a of FIXTURE) mockHex(a.hex as string, MOCK_EXTRA[a.hex as string]);
  mockHex(H.T1);
});
beforeEach(() => {
  routesetPosts = 0;
  for (const k of Object.keys(hexFetches)) hexFetches[k] = 0;
  clock = T0;
  X.__setClockForTests?.(() => clock);
  __resetReleaseMemoForTests();
  __resetBreakersForTests();
});
afterEach(() => {
  X.__setClockForTests?.(undefined);
  X.__tapEnrichRequestForTests?.(undefined);
  vi.restoreAllMocks();
});
afterAll(() => {
  // P-K1, file-wide: the service wrote NOTHING to KV, and its read-only wrapper is provably in the path (it dropped).
  console.log(`P-K1: screen KV writes ${screenKvWrites}; writes dropped by the read-only wrapper ${droppedSeen}`);
  expect(screenKvWrites).toBe(0);
  expect(droppedSeen).toBeGreaterThan(0);
});

describe("membership, staleness, the snapshot's own position (P-S1, P-S2, P-S3)", () => {
  it("P-S1: a hex not in the snapshot -> not_in_snapshot, 0 resolutions, 0 store reads; a malformed hex costs 0 DO requests", async () => {
    const h = harness();
    expect(await site(h.loc, "enrich", { hex: H.ABSENT })).toEqual({ ok: false, reason: "not_in_snapshot" });
    expect(dataReads(h.kvRec.c)).toBe(0);
    expect(h.doCalls.n).toBe(1);
    for (const bad of ["zzzzzz", "a0b1c", "a0b1c2d", "", 123, null, "~a0b1c2"]) {
      expect(await site(h.loc, "enrich", { hex: bad }), `hex ${JSON.stringify(bad)}`).toEqual({ ok: false, reason: "not_in_snapshot" });
    }
    expect(h.doCalls.n).toBe(1); // the malformed ones never reached the DO
    expect(h.feed.reads).toBe(1);
    expect(sumFetches()).toBe(0);
    tally(h);
  });

  it("P-S2: a stale snapshot -> enrich refuses stale; snapshot() answers no aircraft", async () => {
    const h = harness({ feed: feedAnswer(FIXTURE, true) });
    expect(await site(h.loc, "enrich", { hex: H.PUB })).toEqual({ ok: false, reason: "stale" });
    const s = await site(h.loc, "snapshot");
    expect(s.ok).toBe(true);
    expect(aircraftOf(s)).toEqual([]);
    expect(hexFetches[H.PUB]).toBe(0);
    tally(h);
  });

  it("P-S3: enrich takes no place; the route check gets the SNAPSHOT's lat, lon and trk", async () => {
    const h = harness();
    const seen: string[] = [];
    X.__tapEnrichRequestForTests!((r) => { seen.push(r.url); });
    const a = await site(h.loc, "enrich", { hex: H.S3, lat: 10, lon: 10, trk: 0, place: { lat: 10, lon: 10, r: 10 } });
    expect(a.ok).toBe(true);
    expect(seen.length).toBe(1);
    const u = new URL(seen[0]);
    expect(["lat", "lon", "trk"].map((k) => Number(u.searchParams.get(k)))).toEqual([51.53, -0.31, 270]);
    tally(h);
  });
});

describe("the class rules (P-E1, P-E2a-f, P-J1)", () => {
  it("P-E1: dbFlags 4 or 8 -> private, 0 resolutions, 0 store reads; an omitted dbFlags on an airliner is answered", async () => {
    const h = harness();
    for (const hex of [H.FLAG4, H.FLAG8]) expect(await site(h.loc, "enrich", { hex })).toEqual({ ok: false, reason: "private" });
    expect(dataReads(h.kvRec.c)).toBe(0);
    expect(hexFetches[H.FLAG4] + hexFetches[H.FLAG8]).toBe(0);
    const a = await site(h.loc, "enrich", { hex: H.NULLDB });
    expect(a.ok).toBe(true);
    expect(hexFetches[H.NULLDB]).toBe(1);
    tally(h);
  });

  it("P-E2: the PRIVATE rule, row by row, as snapshot() classes each aircraft", async () => {
    const h = harness();
    const s = await site(h.loc, "snapshot");
    expect(s.ok).toBe(true);
    const got = Object.fromEntries(aircraftOf(s).map((a) => [a.hex as string, a.class]));
    for (const [hex, want] of Object.entries(CLASS_WANT)) expect(got[hex], hex).toBe(want);
    for (const a of aircraftOf(s)) expect(["public", "private"]).toContain(a.class);
    await h.settle();
    tally(h);
  });

  it("P-E2a: cfg:screen-blocked absent -> an empty list; a public aircraft is answered", async () => {
    const h = harness();
    expect((await site(h.loc, "enrich", { hex: H.E })).ok).toBe(true);
    tally(h);
  });

  it("P-E2b: a blocked hex answers EXACTLY as an absent one, with the same I/O; the counter alone records blocked", async () => {
    await env.ENRICH_KV.put("cfg:screen-blocked", JSON.stringify([H.BLK]));
    const h = harness();
    await site(h.loc, "snapshot"); // warms the list (one read), as any live service is
    await h.settle(); // and lets its background resolutions finish, so they cannot land inside the measurement
    const now = () => ({ data: dataReads(h.kvRec.c), cfg: cfgReads(h.kvRec.c, "cfg:screen-blocked"), feed: h.feed.reads, dos: h.doCalls.n });
    const io = async (hex: string) => {
      const b = now();
      const a = await site(h.loc, "enrich", { hex });
      const e = now();
      return { a, d: { data: e.data - b.data, cfg: e.cfg - b.cfg, feed: e.feed - b.feed, dos: e.dos - b.dos } };
    };
    const blocked = await io(H.BLK);
    const absent = await io(H.ABSENT);
    expect(JSON.stringify(blocked.a)).toBe(JSON.stringify(absent.a));
    expect(blocked.a).toEqual({ ok: false, reason: "not_in_snapshot" });
    expect(blocked.d).toEqual(absent.d);
    expect(blocked.d).toEqual({ data: 0, cfg: 0, feed: 1, dos: 1 });
    expect(hexFetches[H.BLK]).toBe(0);
    expect(outcomes(h.counter).slice(-2)).toEqual(["blocked", "not_in_snapshot"]);
    const s = await site(h.loc, "snapshot");
    expect(aircraftOf(s).map((a) => a.hex)).not.toContain(H.BLK);
    await h.settle();
    tally(h);
  });

  it("P-E2c: a malformed list FAILS CLOSED -> snapshot and enrich refuse blocked_unknown", async () => {
    for (const bad of ["not json", "{}", "\"a3a3a3\"", "[123]", "[\"a3a3a3x\"]", "[\"a3a3\"]", "null"]) {
      await env.ENRICH_KV.put("cfg:screen-blocked", bad);
      const h = harness();
      expect(await site(h.loc, "snapshot"), bad).toEqual({ ok: false, reason: "blocked_unknown" });
      expect(await site(h.loc, "enrich", { hex: H.PUB }), bad).toEqual({ ok: false, reason: "blocked_unknown" });
      tally(h);
    }
    await env.ENRICH_KV.put("cfg:screen-blocked", JSON.stringify(["A3A3A3"])); // a hex in either case is a hex
    const h = harness();
    expect(aircraftOf(await site(h.loc, "snapshot")).map((a) => a.hex)).not.toContain(H.BLK);
    await h.settle();
    tally(h);
  });

  it("P-E2d: the list's read throws -> as P-E2c", async () => {
    const h = harness({ kvThrowOn: "cfg:screen-blocked" });
    expect(await site(h.loc, "snapshot")).toEqual({ ok: false, reason: "blocked_unknown" });
    expect(await site(h.loc, "enrich", { hex: H.PUB })).toEqual({ ok: false, reason: "blocked_unknown" });
    tally(h);
  });

  it("P-E2e: a changed list is in force within 60 s, at 1 KV read per 60 s", async () => {
    const h = harness();
    expect((await site(h.loc, "enrich", { hex: H.PUB })).ok).toBe(true);
    await env.ENRICH_KV.put("cfg:screen-blocked", JSON.stringify([H.PUB]));
    clock = T0 + 30_000;
    expect((await site(h.loc, "enrich", { hex: H.PUB })).ok).toBe(true);
    expect(cfgReads(h.kvRec.c, "cfg:screen-blocked")).toBe(1);
    clock = T0 + 61_000;
    expect(await site(h.loc, "enrich", { hex: H.PUB })).toEqual({ ok: false, reason: "not_in_snapshot" });
    expect(cfgReads(h.kvRec.c, "cfg:screen-blocked")).toBe(2);
    tally(h);
  });

  it("P-E2f: no counter point and no log line carries a hex", async () => {
    await env.ENRICH_KV.put("cfg:screen-blocked", JSON.stringify([H.BLK]));
    const lines: string[] = [];
    for (const m of ["log", "info", "warn", "error", "debug"] as const) {
      vi.spyOn(console, m).mockImplementation((...a: unknown[]) => { lines.push(a.map(String).join(" ")); });
    }
    const h = harness();
    await site(h.loc, "enrich", { hex: H.BLK });
    await site(h.loc, "enrich", { hex: H.ABSENT });
    await site(h.loc, "enrich", { hex: H.PRIV });
    const texts = [...lines, ...h.counter.points.map((p) => JSON.stringify(p))];
    expect(h.counter.points.length).toBe(3); // CONTROL: there is something to search
    for (const hex of [H.BLK, H.ABSENT, H.PRIV]) for (const t of texts) expect(t.toLowerCase()).not.toContain(hex);
    tally(h);
  });

  it("P-J1: the joined snapshot -- no blocked aircraft, a class on each, details only on public ones, a cold one fills in", async () => {
    await env.ENRICH_KV.put("cfg:screen-blocked", JSON.stringify([H.BLK]));
    const h = harness();
    const r1 = await site(h.loc, "snapshot");
    expect(aircraftOf(r1).map((a) => a.hex)).not.toContain(H.BLK);
    expect(aircraftOf(r1).find((a) => a.hex === H.COLD)?.details).toBeUndefined(); // read 1 never waits
    await h.settle();
    const r2 = await site(h.loc, "snapshot");
    for (const a of aircraftOf(r2)) {
      expect(["public", "private"]).toContain(a.class);
      if (a.class === "private") expect(a.details, `${a.hex as string}`).toBeUndefined();
    }
    expect(aircraftOf(r2).find((a) => a.hex === H.COLD)?.details).toBeDefined();
    for (const hex of Object.keys(CLASS_WANT).filter((x) => CLASS_WANT[x] === "private")) expect(hexFetches[hex], hex).toBe(0);
    expect(hexFetches[H.BLK]).toBe(0);
    await h.settle();
    droppedSeen += await h.stats();
    tally(h);
  });
});

describe("one resolution per hex, read-only, no POSTs, no gaps (P-C1, P-K1, P-R1, P-G1)", () => {
  it("P-C1: 50 concurrent first asks -> exactly 1 upstream fetch; the next 100 -> 0 more", async () => {
    const h = harness();
    const first = await Promise.all(Array.from({ length: 50 }, () => site(h.loc, "enrich", { hex: H.C1 })));
    expect(first.every((a) => a.ok)).toBe(true);
    expect(new Set(first.map((a) => JSON.stringify(a))).size).toBe(1);
    expect(hexFetches[H.C1]).toBe(1);
    const next = await Promise.all(Array.from({ length: 100 }, () => site(h.loc, "enrich", { hex: H.C1 })));
    expect(next.every((a) => a.ok)).toBe(true);
    expect(hexFetches[H.C1]).toBe(1);
    droppedSeen += await h.stats();
    tally(h);
  });

  it("P-K1 CONTROL + P-R1 + P-G1: the device route writes ac:, POSTs once and records a gap; the service does none", async () => {
    const dev = recordingKv(env.ENRICH_KV);
    const devGaps = recorder();
    const res = await deviceCall(apiRequest(`/v1/enrich/${H.GAP}?cs=TST122&lat=51.5&lon=-0.4&trk=90`), { ENRICH_KV: dev.kv, METRICS: devGaps.binding } as never);
    expect(res.status).toBe(200);
    expect(dev.c.writes.some((k) => k.startsWith("ac:"))).toBe(true);
    expect(routesetPosts).toBe(1);
    expect(devGaps.points.filter((p) => p.indexes?.[0] === "enrich_gap").length).toBeGreaterThan(0);

    routesetPosts = 0;
    const h = harness();
    expect((await site(h.loc, "enrich", { hex: H.GAP2 })).ok).toBe(true);
    expect(hexFetches[H.GAP2]).toBe(1); // CONTROL: a real resolution happened, of a type with a gap
    expect(routesetPosts).toBe(0);
    expect(h.gaps.points.filter((p) => p.indexes?.[0] === "enrich_gap").length).toBe(0);
    expect(h.kvRec.c.writes).toEqual([]);
    droppedSeen += await h.stats();
    tally(h);
  });
});

describe("the limits (P-L1 strict, P-L1b, P-T1)", () => {
  it("P-L1: two locations, one object -- limit + 1 calls in total -> exactly 1 limited; the other class is untouched", async () => {
    const h = harness({ vars: { SCREEN_LIMIT_APP_PER_MIN: "3" } });
    const a = h.location(3), b = h.location(3); // each location's own shield lets 3 through
    const answers = [await app(a, "snapshot"), await app(a, "snapshot"), await app(a, "snapshot"), await app(b, "snapshot")];
    expect(answers.map(reason)).toEqual(["ok", "ok", "ok", "limited"]);
    expect(h.doCalls.names.size).toBe(1); // ONE object
    expect(reason(await site(a, "snapshot"))).toBe("ok");
    clock = T0 + 61_000;
    expect(reason(await app(b, "snapshot"))).toBe("ok"); // the next window
    await h.settle();
    tally(h);
  });

  it("P-L1b: the resolution cap + 1 new hexes -> the extra is deferred (limited), with 0 upstream", async () => {
    const h = harness({ vars: { SCREEN_RESOLVE_PER_MIN: "2" } });
    const got: string[] = [];
    for (const hex of [H.R1, H.R2, H.R3]) got.push(reason(await site(h.loc, "enrich", { hex })));
    expect(got).toEqual(["ok", "ok", "limited"]);
    expect([hexFetches[H.R1], hexFetches[H.R2], hexFetches[H.R3]]).toEqual([1, 1, 0]);
    clock = T0 + 61_000;
    expect(reason(await site(h.loc, "enrich", { hex: H.R3 }))).toBe("ok");
    expect(hexFetches[H.R3]).toBe(1);
    tally(h);
  });

  it("P-T1: while a resolution's upstream fetch is pending, 20 other calls into the object all answer first", async () => {
    // H.T1's resolution is HELD at the seam immediately before handleEnrich (whose first act is the upstream hex
    // fetch) and released only after the 20 have answered: no timer, so only the code's own ordering can make the 20
    // wait. Two ways of holding the fetch itself were tried and are unusable in this pool: fetchMock's delay() ended
    // every delayed reply as an AbortError (the chain retried it, 3 fetches), and a spy on globalThis.fetch never saw
    // the chain's call (green 1 and green 3, screen-enrich-red.md).
    let release!: () => void;
    const held = new Promise<void>((r) => { release = r; });
    let reached!: () => void;
    const reachedHold = new Promise<void>((r) => { reached = r; });
    let heldCalls = 0;
    X.__tapEnrichRequestForTests!((r) => {
      if (!r.url.includes(`/v1/enrich/${H.T1}`)) return;
      heldCalls++;
      reached();
      return held;
    });
    const timers: Array<ReturnType<typeof setTimeout>> = [];
    const within = <T>(p: Promise<T>, ms: number) =>
      Promise.race([p.then(() => "done" as const), new Promise<"waited">((r) => { timers.push(setTimeout(() => r("waited"), ms)); })]);
    const h = harness({ feed: feedAnswer([T1_AC, ...FIXTURE.filter((a) => a.hex === H.PRIV)]) });
    let coldDone = false;
    const cold = site(h.loc, "enrich", { hex: H.T1 }).then((a) => { coldDone = true; return a; });
    let othersP: Promise<Answer[]> | undefined;
    try {
      // FIRST the precondition: the resolution is in flight, held before its upstream fetch.
      expect(await within(reachedHold, 2000)).toBe("done");
      othersP = Promise.all([
        ...Array.from({ length: 10 }, () => site(h.loc, "snapshot")),
        ...Array.from({ length: 5 }, () => site(h.loc, "enrich", { hex: H.ABSENT })),
        ...Array.from({ length: 5 }, () => site(h.loc, "enrich", { hex: H.PRIV })),
      ]);
      // The 20 get 2 s. A code that made them wait for the held resolution would wait forever: the bound turns that
      // into a fast, legible failure, and the finally releases it either way so nothing outlives the test.
      const answeredFirst = (await within(othersP, 2000)) === "done";
      const coldDoneFirst = coldDone; // read BEFORE the release, which lets the cold call finish
      const heldFirst = heldCalls;
      const fetchesFirst = hexFetches[H.T1];
      release();
      const others = await othersP;
      expect(answeredFirst).toBe(true);
      expect(heldFirst).toBe(1); // CONTROL: one resolution, held -- the snapshots joined it rather than starting another
      expect(fetchesFirst).toBe(0); // ... before its upstream fetch
      expect(coldDoneFirst).toBe(false);
      expect(others.map(reason)).toEqual([...Array(10).fill("ok"), ...Array(5).fill("not_in_snapshot"), ...Array(5).fill("private")]);
      const a = await cold;
      expect(a.ok).toBe(true);
      expect((a as { value: { t?: string } }).value.t).toBe("A320");
      expect(hexFetches[H.T1]).toBe(1);
    } finally {
      timers.forEach(clearTimeout);
      release();
      await Promise.allSettled([othersP ?? Promise.resolve([]), cold]);
      await h.settle();
      tally(h);
    }
  });
});

describe("model and place (P-V1, P-D1, P-G2, P-G3)", () => {
  it("P-V1: for each model, details.p equals the device route's square for that model at fw 7; never the rectangle", async () => {
    await env.ENRICH_KV.put(pointerKey("type", "A320"), "photo:A320-0000aaaa");
    const SQ: Record<string, number> = { "s3-128": 240, "s3-146": 412, "s3-21": 480 };
    for (const s of Object.values(SQ)) await env.ENRICH_KV.put(pointerKey("type", "A320", s), `photo:A320-0000a${s}`);
    const h = harness();
    for (const m of Object.keys(SQ)) {
      const dev = await deviceCall(apiRequest(`/v1/enrich/${H.V1}?lat=51.5&lon=-0.4&trk=90`, { "X-Blip-Model": m, "X-Blip-FW": "7" }));
      const devP = ((await dev.json()) as { p?: string }).p;
      const a = await site(h.loc, "enrich", { hex: H.V1, model: m });
      const p = (a as { value?: { p?: string } }).value?.p;
      expect(p, m).toBe(devP);
      expect(p, m).toContain(`0000a${SQ[m]}`);
      expect(p, m).not.toContain("0000aaaa");
    }
    tally(h);
  });

  it("P-D1: a model outside the enum -> bad_model with 0 DO requests; absent means s3-128", async () => {
    const h = harness();
    for (const m of ["s3-999", "", "S3-128", " s3-128", 128, null]) {
      expect(await site(h.loc, "enrich", { hex: H.PUB, model: m }), JSON.stringify(m)).toEqual({ ok: false, reason: "bad_model" });
      expect(await site(h.loc, "snapshot", { model: m }), JSON.stringify(m)).toEqual({ ok: false, reason: "bad_model" });
    }
    expect(h.doCalls.n).toBe(0);
    expect(h.feed.reads).toBe(0);
    expect((await site(h.loc, "snapshot", {})).ok).toBe(true);
    await h.settle();
    tally(h);
  });

  it("P-G2: under circle (absent, \"circle\", malformed), a place one tile off -> place_not_allowed, 1 site-feed read, 0 upstream", async () => {
    const OFF = { lat: 51.52, lon: -0.45, r: RADIUS_KM }; // quantizeTile: 51.50 vs the circle's 51.45
    const ON = { lat: 51.46, lon: -0.44, r: 35 }; // the same tile, the same radius bucket
    for (const v of [undefined, "circle", "ANY", "any ", "{\"mode\":\"any\"}", ""]) {
      if (v !== undefined) await env.ENRICH_KV.put("cfg:screen-places", v);
      const h = harness();
      const f0 = sumFetches(); // a delta: the previous iteration's ON snapshot resolved in the background
      expect(await site(h.loc, "snapshot", { place: OFF }), String(v)).toEqual({ ok: false, reason: "place_not_allowed" });
      expect(h.feed.reads, String(v)).toBe(1);
      expect(dataReads(h.kvRec.c), String(v)).toBe(0);
      expect(sumFetches() - f0, String(v)).toBe(0);
      expect((await site(h.loc, "snapshot", { place: ON })).ok, String(v)).toBe(true);
      await h.settle();
      tally(h);
    }
    const h = harness();
    for (const bad of [{ lat: "51.47", lon: -0.45, r: 40 }, { lat: 51.47, lon: -0.45 }, "here", { lat: NaN, lon: -0.45, r: 40 }]) {
      expect(await site(h.loc, "snapshot", { place: bad }), JSON.stringify(bad)).toEqual({ ok: false, reason: "place_not_allowed" });
    }
    expect(h.doCalls.n).toBe(0); // a place that is not numbers is refused before the DO
    tally(h);
  });

  it("P-G3: flip to any -> the same call is not_built; flip back -> place_not_allowed within 60 s", async () => {
    const OFF = { lat: 51.52, lon: -0.45, r: RADIUS_KM };
    await env.ENRICH_KV.put("cfg:screen-places", "any");
    const h = harness();
    expect(await site(h.loc, "snapshot", { place: OFF })).toEqual({ ok: false, reason: "not_built" });
    await env.ENRICH_KV.put("cfg:screen-places", "circle");
    clock = T0 + 30_000;
    expect(await site(h.loc, "snapshot", { place: OFF })).toEqual({ ok: false, reason: "not_built" });
    clock = T0 + 61_000;
    expect(await site(h.loc, "snapshot", { place: OFF })).toEqual({ ok: false, reason: "place_not_allowed" });
    expect(cfgReads(h.kvRec.c, "cfg:screen-places")).toBe(2);
    tally(h);
  });
});

describe("the counter and what is exposed (P-M1, P-H3)", () => {
  it("P-M1: each of the 8 new outcomes writes exactly one point, outcome equal to the answer's (blocked: the counter's own)", async () => {
    await env.ENRICH_KV.put("cfg:screen-blocked", JSON.stringify([H.BLK]));
    const h = harness({ vars: { SCREEN_LIMIT_APP_PER_MIN: "1" } });
    const calls: Array<[string, () => Promise<Answer>]> = [
      ["not_in_snapshot", () => site(h.loc, "enrich", { hex: H.ABSENT })],
      ["private", () => site(h.loc, "enrich", { hex: H.PRIV })],
      ["blocked", () => site(h.loc, "enrich", { hex: H.BLK })],
      ["place_not_allowed", () => site(h.loc, "snapshot", { place: { lat: 51.52, lon: -0.45, r: RADIUS_KM } })],
      ["bad_model", () => site(h.loc, "enrich", { hex: H.PUB, model: "s3-999" })],
      ["ok", () => app(h.loc, "snapshot")],
      ["limited", () => app(h.loc, "snapshot")],
    ];
    for (const [, c] of calls) await c();
    await h.settle();
    h.feed.answer = feedAnswer(FIXTURE, true);
    await site(h.loc, "enrich", { hex: H.PUB }); // stale
    await env.ENRICH_KV.put("cfg:screen-blocked", "not json");
    clock = T0 + 61_000;
    h.feed.answer = feedAnswer(FIXTURE);
    await site(h.loc, "enrich", { hex: H.PUB }); // blocked_unknown
    const got = outcomes(h.counter);
    expect(got).toEqual([...calls.map(([o]) => o), "stale", "blocked_unknown"]);
    expect(new Set(got.filter((o) => o !== "ok"))).toEqual(new Set(NEW_OUTCOMES));
    for (const p of h.counter.points) expect(ALL_OUTCOMES.has(p.blobs?.[2] ?? "")).toBe(true);
    tally(h);
  });

  it("P-H3: /health and every answer carry no per-outcome count (the blocked total is internal)", async () => {
    const R = new URL(RELEASE_URL);
    fetchMock.get(R.origin).intercept({ path: R.pathname }).reply(200, "19");
    const health = (await (await W.default.fetch(new Request("https://screen.example/health"), env)).json()) as unknown;
    // A per-outcome count is a NUMBER under a key named for an outcome (the site feed's `stale` flag is a boolean).
    const counts = (o: unknown, out: string[] = []): string[] => {
      if (o && typeof o === "object") {
        for (const [k, v] of Object.entries(o)) {
          if (ALL_OUTCOMES.has(k) && typeof v === "number") out.push(k);
          counts(v, out);
        }
      }
      return out;
    };
    const keysIn = (o: unknown): number => (o && typeof o === "object" ? Object.entries(o).reduce((n, [, v]) => n + 1 + keysIn(v), 0) : 0);
    expect(keysIn(health)).toBeGreaterThan(0); // CONTROL: there is a body to search
    expect(counts(health)).toEqual([]);
    expect(counts({ x: { blocked: 3 } })).toEqual(["blocked"]); // CONTROL: the search finds one when there is one
    const h = harness();
    const answers = [await site(h.loc, "snapshot"), await site(h.loc, "enrich", { hex: H.ABSENT })];
    for (const a of answers) expect(counts(a)).toEqual([]);
    await h.settle();
    tally(h);
  });
});
