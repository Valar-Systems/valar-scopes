// MQ (docs/basemap-route.md A1.7): query.ts against the frozen rows. Each test is named by its case id;
// scripts/sabotage-basemap.mjs grades its exact sets from them. Synthetic origins only (0.37, 0.59).
import { describe, expect, it } from "vitest";
import { MAX_R_KM, type Parsed, etagOf, parseQuery } from "../src/basemap/query";
import firmware from "../../src/AircraftManager.cpp?raw";

const Q0 = "lat=0.37&lon=0.59&r=97&eps=100584&layers=water,rivers,roads,peaks,places";
const parse = (qs: string): Parsed => parseQuery(new URL(`https://x.invalid/api/v1/blipscope/basemap?${qs}`));
const ok = (qs: string) => { const p = parse(qs); if (!p.ok) throw new Error(`refused: ${p.code}`); return p.q; };
const code = (qs: string) => { const p = parse(qs); return p.ok ? "ok" : p.code; };
const tag = (qs: string) => etagOf(ok(qs), 1, "abcdef012345");
const with_ = (k: string, v: string | null) => { const u = new URLSearchParams(Q0); if (v === null) u.delete(k); else u.set(k, v); return u.toString().replace(/%2C/g, ",").replace(/%3A/g, ":"); };

describe("MQ, the request", () => {
  it("MQ1: Q0 normalises as frozen", () => {
    expect(ok(Q0)).toMatchObject({ lat2: "0.37", lon2: "0.59", rKm: 97, epsMm: 100584, mask: 31, levels: [] });
  });

  it("MQ2: the layers' order does not change the ETag", async () => {
    expect(await tag(with_("layers", "places,peaks,roads,rivers,water"))).toBe(await tag(Q0));
  });

  it("MQ3: the origin is crop.ts's quantised point, in every case", () => {
    // mulberry32, seeded; 100,000 random points in [18, 72) x [-180, -64)
    let a = 0x6d71_3 >>> 0;
    const rand = () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
    const pts: Array<[number, number]> = [];
    for (let i = 0; i < 100_000; i++) pts.push([18 + rand() * 54, -180 + rand() * 116]);
    // every half-way value n + j/8, j odd, on each axis (the other axis held at a plain value)
    let halves = 0;
    for (let n = 18; n < 72; n++) for (const j of [1, 3, 5, 7]) { pts.push([n + j / 8, -100.25]); halves++; }
    for (let n = -180; n < -64; n++) for (const j of [1, 3, 5, 7]) { pts.push([40.25, n + j / 8]); halves++; }
    let diffs = 0;
    for (const [lat, lon] of pts) {
      const q = ok(`lat=${lat}&lon=${lon}&r=97&eps=100584&layers=water`);
      if (q.lat0Micro !== Math.round(lat * 100) * 10_000 || q.lon0Micro !== Math.round(lon * 100) * 10_000) diffs++;
    }
    console.log(`MQ3: ${pts.length - halves} random points and ${halves} half-way values; ${diffs} differences`);
    expect(halves).toBe(680);
    expect(diffs).toBe(0);
  });

  it("MQ4: r", () => {
    expect([ok(with_("r", "0.4")).rKm, ok(with_("r", "222.4")).rKm]).toEqual([1, 222]);
    expect(["222.5", "0", "-5", "x", null].map((v) => code(with_("r", v)))).toEqual(["bad_r", "bad_r", "bad_r", "bad_r", "bad_r"]);
  });

  it("MQ5: the bound is MAX_DEGREES x KM_PER_DEGREE, parsed from src/AircraftManager.cpp (else BLIND)", () => {
    const one = (re: RegExp) => { const m = [...firmware.matchAll(re)]; if (m.length !== 1) throw new Error(`BLIND: ${re} found ${m.length} times`); return Number(m[0]![1]); };
    const km = one(/constexpr double KM_PER_DEGREE = ([0-9.]+);/g), deg = one(/constexpr double MAX_DEGREES = ([0-9.]+);/g);
    expect(km * deg).toBe(222);
    expect(MAX_R_KM).toBe(km * deg);
  });

  it("MQ6: eps, integer millimetres", async () => {
    expect([ok(with_("eps", "100584")).epsMm, ok(with_("eps", "1000")).epsMm, ok(with_("eps", "10000000")).epsMm, ok(with_("eps", "05000")).epsMm]).toEqual([100584, 1000, 10000000, 5000]);
    expect(await tag(with_("eps", "05000"))).toBe(await tag(with_("eps", "5000")));
    expect(["999", "10000001", "100.6", "1e5", "+5000", ""].map((v) => code(with_("eps", v)))).toEqual(Array(6).fill("bad_eps"));
  });

  it("MQ7: layers", () => {
    expect(["", "water,water", "lakes", "water,", "WATER"].map((v) => code(with_("layers", v)))).toEqual(Array(5).fill("bad_layers"));
    expect(ok(with_("layers", "water")).mask).toBe(1);
    expect([code(with_("layers", "terrain")), code(with_("layers", "water,terrain"))]).toEqual(["bad_levels", "bad_levels"]);
  });

  it("MQ8: levels", async () => {
    const T = (levels: string | null) => { const u = with_("layers", "water,terrain"); return levels === null ? u : `${u}&levels=${levels}`; };
    const a = ok(T("24743664:246,49487328:246,98974656:246")), b = ok(T("98974656:246,24743664:246,49487328:246"));
    expect(a.levels).toEqual([{ radiusMm: 24743664, n: 246 }, { radiusMm: 49487328, n: 246 }, { radiusMm: 98974656, n: 246 }]);
    expect(b.levels).toEqual(a.levels);
    expect(await etagOf(b, 1, "abcdef012345")).toBe(await etagOf(a, 1, "abcdef012345"));
    expect(code(`${Q0}&levels=1000000:4`)).toBe("bad_levels"); // (c) levels without terrain
    const bad = ["1000000:4,2000000:4,3000000:4,4000000:4,5000000:4", "1000000:0", "1000000:1025", "999999:4", "250000001:4", "1000000:100,1000000:200", "1000000:1024,2000000:1024", "1000000", "1000000:4:5"];
    expect(bad.map((v) => code(T(v)))).toEqual(Array(bad.length).fill("bad_levels"));
    expect(ok(T("1000000:1024,2000000:1023")).levels.length).toBe(2); // (k) 2,095,121 B
    expect(ok(T("1000000:04")).levels).toEqual([{ radiusMm: 1000000, n: 4 }]); // (n)
  });

  it("MQ9: an unknown parameter is ignored", async () => {
    expect(await tag(`${Q0}&foo=1`)).toBe(await tag(Q0));
  });

  it("MQ10: the ETag's form; seven distinct; lat 0.3749 is Q0's", async () => {
    const t0 = await tag(Q0);
    expect(t0).toMatch(/^"bm1-[0-9]+-[0-9a-f]{12}-[0-9a-f]{24}"$/);
    const variants = [with_("lat", "0.38"), with_("lon", "0.60"), with_("r", "98"), with_("eps", "100585"), with_("layers", "water,rivers,roads,peaks"), `${with_("layers", "water,rivers,roads,peaks,places,terrain")}&levels=1000000:4`];
    const tags = new Set([t0, ...(await Promise.all(variants.map(tag)))]);
    expect(tags.size).toBe(7);
    expect(await tag(with_("lat", "0.3749"))).toBe(t0);
  });
});
