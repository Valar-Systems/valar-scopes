// R2 of the UK data plan (docs/wx-opuk.md): K1-K10, K14 and K15, and LAEA against pyproj. Every check is a function of
// the thing it checks, and its sabotage hands the same function a broken variant -- so a sabotage that does not turn
// its row red shows up as a passing check, never as silence. K11 (CPU and memory) is scripts/wx-opuk-measure.ts; K12
// is scripts/wx-fixtures.ts's opuk crops; K13 the host render.
//
// Centres are public airports, named by code (test/fixtures/wx-airports-uk.txt); no coordinate is printed.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { VIEW, cropCellBox, quantise, viewDegrees } from "../src/wx/crop";
import { FrameBuilder, type FrameMeta, type GridGeom, frameMeta, objectBytes, readHeader, tileBytes, tileOffset, tiledCellAt } from "../src/wx/grid";
import { type LatestRecord, handleWx, readLatest } from "../src/wx/handler";
import { type Fetch, type IngestX, OPERA_BUCKET, OPERA_UA, ingestOnce } from "../src/wx/ingest";
import { WGS84, laea } from "../src/wx/laea";
import { HEAD_BYTES, NODATA, type OperaGeo, type RangeFetch, parseHead, readWindow, windowFor } from "../src/wx/opera";
import { OPUK_GRID, OPUK_LAND, OPUK_MARGIN, PRODUCTS, type Product, cropEtag, frameKey, latestKey, servedProduct } from "../src/wx/products";
import { type MaskOpts, OPUK_QI_MASK, mapFor, operaCode, prepareCentreOnly, resample } from "../src/wx/resample";
import dryBin from "./fixtures/wx/opera/20261029T0130.ranges.bin";
import dryExp from "./fixtures/wx/opera/20261029T0130.expected.json";
import dryRanges from "./fixtures/wx/opera/20261029T0130.ranges.json";
import wetBin from "./fixtures/wx/opera/20261028T0255.ranges.bin";
import wetExp from "./fixtures/wx/opera/20261028T0255.expected.json";
import wetRanges from "./fixtures/wx/opera/20261028T0255.ranges.json";
import pairs from "./fixtures/wx/opera/laea-pairs.json";
import airportsUk from "./fixtures/wx-airports-uk.txt";
import { apiRequest, call } from "./helpers";
import { CONUS } from "./wx-synth";

const cref = PRODUCTS.find((p) => p.id === "cref")!;
const opuk = PRODUCTS.find((p) => p.id === "opuk")!;
const SLOW = 180_000;

const AP = new Map(airportsUk.split(/\r?\n/).filter((l) => l.trim() && !l.startsWith("#")).map((l) => {
  const [id, lat, lon] = l.trim().split(/\s+/);
  return [id!, { lat: Number(lat), lon: Number(lon) }] as const;
}));
AP.set("KMCI", { lat: 39.30, lon: -94.71 });       // as in test/wx-endpoint.test.ts
const at = (id: string) => { const a = AP.get(id); if (!a) throw new Error(`no airport ${id}`); return a; };

// ---- the fixtures: exactly the byte ranges the reader requests (tools/wx/opera_fixture.py) -------------------------

interface Fx { name: string; ranges: { url: string; size: number; ranges: number[][] }; bin: ArrayBuffer;
               exp: { window: number[]; sha256_value_band: string; counts: Record<string, number>;
                      projection: { lat_0: number; lon_0: number; x_0: number; y_0: number } } }
const WET: Fx = { name: "wet 20261028T0255", ranges: wetRanges, bin: wetBin, exp: wetExp };
const DRY: Fx = { name: "dry 20261029T0130", ranges: dryRanges, bin: dryBin, exp: dryExp };

/** A RangeFetch over a fixture. Any range the fixture does not hold is an error: the reader asks for exactly these. */
function fixtureRanges(fx: Fx, asked?: string[]): RangeFetch {
  const all = new Uint8Array(fx.bin);
  const offs = new Map<string, number>();
  let o = 0;
  for (const [a, z] of fx.ranges.ranges) { offs.set(`${a}-${z}`, o); o += z! - a! + 1; }
  if (o !== all.length) throw new Error(`${fx.name}: ranges add up to ${o} B, the fixture holds ${all.length}`);
  return async (a, z) => {
    asked?.push(`${a}-${z}`);
    const s = offs.get(`${a}-${z}`);
    if (s === undefined) throw new Error(`${fx.name}: range ${a}-${z} is not in the fixture`);
    return all.slice(s, s + z - a + 1);
  };
}
const keyOf = (fx: Fx) => fx.ranges.url.slice(OPERA_BUCKET.length + 1);
const epochOf = (fx: Fx) => { const m = /@(\d{8})T(\d{4})@/.exec(fx.ranges.url)!;
  return Date.parse(`${m[1]!.slice(0, 4)}-${m[1]!.slice(4, 6)}-${m[1]!.slice(6, 8)}T${m[2]!.slice(0, 2)}:${m[2]!.slice(2, 4)}:00Z`) / 1000; };

interface Seen { url: string; headers: Record<string, string> }
/** OPERA's cache as the ingest sees it: a listing holding the fixture's key, and its byte ranges. */
function operaFetch(fx: Fx, seen?: Seen[]): Fetch {
  const rf = fixtureRanges(fx);
  return async (u, init) => {
    const headers = { ...(init?.headers ?? {}) };
    seen?.push({ url: u, headers });
    if (!u.startsWith(OPERA_BUCKET)) return new Response("", { status: 404 });     // cref's NOAA: not this test's
    if (u.includes("list-type=2")) {
      return new Response(`<ListBucketResult><Contents><Key>${keyOf(fx)}</Key></Contents></ListBucketResult>`);
    }
    const m = /^bytes=(\d+)-(\d+)$/.exec(headers.Range ?? "");
    if (!m || u !== fx.ranges.url) return new Response("", { status: 400 });
    return new Response(await rf(Number(m[1]), Number(m[2])), { status: 206 });
  };
}

async function hex(buf: ArrayBuffer | Uint8Array | Uint32Array) {
  const d = await crypto.subtle.digest("SHA-256", buf);
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}
/** The value band as the fixture tool hashes it: float32 LE, every NaN written as 0x7FC00000. */
async function bandSha(v: Float32Array) {
  const raw = new Uint32Array(v.buffer, v.byteOffset, v.length);
  const u = new Uint32Array(v.length);
  for (let i = 0; i < v.length; i++) u[i] = Number.isNaN(v[i]!) ? 0x7fc00000 : raw[i]!;
  return hex(u);
}

/** env.WX with every list() counted (K10). */
function countingLists() {
  let lists = 0;
  const WX = new Proxy(env.WX!, {
    get(t, prop) {
      const v = Reflect.get(t, prop);
      if (prop === "list") return (...a: unknown[]) => { lists++; return (v as (...x: unknown[]) => unknown).apply(t, a); };
      return typeof v === "function" ? v.bind(t) : v;
    },
  }) as R2Bucket;
  return { WX, lists: () => lists };
}

// ---- LAEA against pyproj -------------------------------------------------------------------------------------------
// Tolerances stated before the first run: forward within 1 mm, inverse within 1e-7 deg (about 1 cm). The pixels are
// 1 km; either bound is three orders of magnitude inside what the resampling can see.

function laeaErrors(invF: number) {
  const P = laea({ lat0: 55, lon0: 10, x0: 1950000, y0: -2100000, a: WGS84.a, invF });
  let fwd = 0, inv = 0;
  for (const [lon, lat, x, y] of pairs.forward as number[][]) {
    const [px, py] = P.forward(lon!, lat!);
    fwd = Math.max(fwd, Math.abs(px - x!), Math.abs(py - y!));
  }
  for (const [x, y, lon, lat] of pairs.inverse as number[][]) {
    const [plon, plat] = P.inverse(x!, y!);
    inv = Math.max(inv, Math.abs(plon - lon!), Math.abs(plat - lat!));
  }
  return { fwd, inv };
}

describe("LAEA: laea.ts against pyproj's PROJ", () => {
  it("CONTROL: the pairs are OPERA's projection, as the frames' own GeoKeys give it", () => {
    for (const fx of [WET, DRY]) {
      const p = fx.exp.projection;
      const got = Object.fromEntries([...pairs.proj.matchAll(/\+(lat_0|lon_0|x_0|y_0)=([-\d.]+)/g)].map((m) => [m[1], Number(m[2])]));
      expect(got).toEqual({ lat_0: p.lat_0, lon_0: p.lon_0, x_0: p.x_0, y_0: p.y_0 });
      expect(pairs.proj).toContain("+proj=laea");
      expect(pairs.proj).toContain("+ellps=WGS84");
    }
    expect(pairs.forward.length).toBeGreaterThan(500);
    expect(pairs.inverse.length).toBeGreaterThan(1000);
  });
  it("forward within 1 mm, inverse within 1e-7 deg", () => {
    const e = laeaErrors(WGS84.invF);
    console.log(`LAEA vs pyproj ${pairs.pyproj}: forward max ${e.fwd.toExponential(2)} m over ${pairs.forward.length}, ` +
                `inverse max ${e.inv.toExponential(2)} deg over ${pairs.inverse.length}`);
    expect(e.fwd).toBeLessThanOrEqual(1e-3);
    expect(e.inv).toBeLessThanOrEqual(1e-7);
  });
  it("SABOTAGE: a near-sphere (1/f = 1e9) misses by more than 1 m", () => {
    const e = laeaErrors(1e9);
    console.log(`LAEA sabotage (near-sphere): forward max ${e.fwd.toFixed(0)} m`);
    expect(e.fwd).toBeGreaterThan(1);
  });
});

// ---- K1: the COG reader on the wettest and driest frames ------------------------------------------------------------

async function k1(fx: Fx, band = 0) {
  const asked: string[] = [];
  const w = await readWindow(fixtureRanges(fx, asked), OPUK_GRID, band);
  const sha = await bandSha(w.value);
  const p = w.geo.proj, e = fx.exp.projection;
  const projOk = [[p.lat0, e.lat_0], [p.lon0, e.lon_0], [p.x0, e.x_0], [p.y0, e.y_0]].every(([a, b]) => Math.abs(a! - b!) <= 1e-9);
  return { sha, shaOk: sha === fx.exp.sha256_value_band, projOk, window: w.window, asked };
}

describe("K1: the COG reader equals each frame's own HDF5", () => {
  for (const fx of [WET, DRY]) {
    it(`${fx.name}: the window, its value band's sha and the projection numbers`, async () => {
      const r = await k1(fx);
      console.log(`K1 ${fx.name}: sha ${r.shaOk ? "equal" : "DIFFERENT"}, projection ${r.projOk ? "equal" : "DIFFERENT"}, ` +
                  `${r.asked.length} ranges asked, echo cells ${fx.exp.counts.echo_ge_15}`);
      expect(r.window).toEqual(fx.exp.window);
      expect(r.asked).toEqual(fx.ranges.ranges.map(([a, z]) => `${a}-${z}`));   // exactly the fixture's ranges
      expect(r.shaOk).toBe(true);
      expect(r.projOk).toBe(true);
    }, SLOW);
  }
  it("SABOTAGE: band 1 read for band 0 -- the shas differ", async () => {
    for (const fx of [WET, DRY]) expect((await k1(fx, 1)).shaOk, fx.name).toBe(false);
  }, SLOW);
  it("refuses a layout it does not know (a predictor set)", async () => {
    const head = new Uint8Array(DRY.bin.slice(0, HEAD_BYTES));
    expect(() => parseHead(head)).not.toThrow();
    // OPERA's IFD 0 carries Predictor (317) = 1 explicitly: plant 2 over it
    const bad = head.slice();
    const ifd = bad[4]! | (bad[5]! << 8) | (bad[6]! << 16) | (bad[7]! << 24);
    const n = bad[ifd]! | (bad[ifd + 1]! << 8);
    let hit = false;
    for (let i = 0; i < n; i++) {
      const e = ifd + 2 + 12 * i;
      if ((bad[e]! | (bad[e + 1]! << 8)) === 317) { expect(bad[e + 8], "CONTROL: Predictor is 1 before the plant").toBe(1); bad[e + 8] = 2; hit = true; }
    }
    expect(hit, "CONTROL: the Predictor tag was found to plant over").toBe(true);
    expect(() => parseHead(bad)).toThrow(/predictor/);
  });
});

// ---- K2: resampling keeps a one-pixel line unbroken -----------------------------------------------------------------

async function realGeo(): Promise<OperaGeo> { return parseHead(new Uint8Array(DRY.bin.slice(0, HEAD_BYTES))); }

/** K2's line: undetect everywhere, one 40 dBZ pixel per window row from the north-west corner to the south-east. */
function k2(geo: OperaGeo, centreOnly: boolean) {
  const g = OPUK_GRID;
  const win = windowFor(geo, g);
  const rows = win[1] - win[0] + 1, cols = win[3] - win[2] + 1;
  const value = new Float32Array(rows * cols).fill(NaN), quality = new Float32Array(rows * cols).fill(0.9);
  for (let r = 0; r < rows; r++) value[r * cols + Math.round((r * (cols - 1)) / (rows - 1))] = 40;
  const m = mapFor(geo, win, g);
  if (centreOnly) prepareCentreOnly(geo, win, g, m);
  const { codes } = resample(value, quality, m, g, { centreOnly });
  const has: boolean[] = [];
  for (let tr = 0; tr < g.nj; tr++) {
    let e = false;
    for (let tc = 0; tc < g.ni && !e; tc++) { const c = codes[tr * g.ni + tc]!; if (c >= 1 && c <= 13) e = true; }
    has.push(e);
  }
  const first = has.indexOf(true), last = has.lastIndexOf(true);
  let gaps = 0;
  for (let tr = first + 1; tr < last; tr++) if (!has[tr]) gaps++;
  return { first, last, gaps, rows, cols };
}

describe("K2: a one-pixel diagonal line survives the resampling", () => {
  it("0 rows between the line's first and last without an echo cell", async () => {
    const r = k2(await realGeo(), false);
    console.log(`K2 line over ${r.rows} x ${r.cols} source px: grid rows ${r.first}-${r.last}, gaps ${r.gaps}`);
    expect(r.first).toBeGreaterThanOrEqual(0);
    expect(r.last - r.first).toBeGreaterThan(1000);   // CONTROL: the line crosses most of the grid
    expect(r.gaps).toBe(0);
  }, SLOW);
  it("SABOTAGE: the centre rule alone (nearest neighbour) leaves gaps", async () => {
    const r = k2(await realGeo(), true);
    console.log(`K2 sabotage (centre rule alone): gaps ${r.gaps}`);
    expect(r.gaps).toBeGreaterThan(0);
  }, SLOW);
});

// ---- K3: the codes --------------------------------------------------------------------------------------------------

function k3(code: (v: number) => number): string[] {
  const bad: string[] = [];
  const want: Array<[number, number]> = [[NaN, 0], [NODATA, 14], [14.99, 0], [15.0, 1], [75.0, 13], [78.5, 13]];
  for (const [v, c] of want) if (code(v) !== c) bad.push(`${v} -> ${code(v)}, not ${c}`);
  for (let v = -40; v <= 120; v += 0.01) if (code(v) === 15) { bad.push(`${v.toFixed(2)} -> 15`); break; }
  return bad;
}
const unmasked = (v: number) => operaCode(v, 1);

describe("K3: OPERA values to WXC1 codes", () => {
  it("NaN 0, nodata 14, 14.99 0, 15 1, 75 13, 78.5 13, never 15", () => { expect(k3(unmasked)).toEqual([]); });
  it("SABOTAGE: 15 dBZ mapped to 0", () => {
    const r = k3((v) => (v <= 15 && !Number.isNaN(v) && v !== NODATA ? 0 : unmasked(v)));
    expect(r).toEqual(["15 -> 0, not 1"]);
  });
});

// ---- K4: WXG1 -------------------------------------------------------------------------------------------------------

const pattern = (r: number, c: number) => (r * 7 + c * 13) % 15;   // 0-14, every code

function buildPattern(g: GridGeom) {
  const m = frameMeta("opuk", 1, g);
  const fb = new FrameBuilder(m);
  const row = new Uint8Array(g.ni);
  for (let r = 0; r < g.nj; r++) { for (let c = 0; c < g.ni; c++) row[c] = pattern(r, c); fb.setRow(r, row); }
  return { m, bytes: fb.bytes };
}
/** Cells that differ after a round trip, reading the tiles with `meta` (the sabotage hands a swapped one). */
function roundTripDiffs(g: GridGeom, bytes: Uint8Array, meta: FrameMeta) {
  const tiles = new Map<string, Uint8Array>();
  for (let ty = 0; ty < meta.tilesY; ty++) for (let tx = 0; tx < meta.tilesX; tx++) {
    const o = tileOffset(meta, ty, tx);
    tiles.set(`${ty}-${tx}`, bytes.subarray(o, o + tileBytes(meta.tile)));
  }
  const cellAt = tiledCellAt(meta, tiles);
  let diffs = 0;
  for (let r = 0; r < g.nj; r++) for (let c = 0; c < g.ni; c++) if (cellAt(r, c) !== pattern(r, c)) diffs++;
  return diffs;
}

describe("K4: opuk as WXG1", () => {
  it("1,515 x 1,455 cells, 9 tiles, 1,179,712 B; a round trip identical cell for cell", () => {
    const { m, bytes } = buildPattern(OPUK_GRID);
    const h = readHeader(bytes);
    expect([h.nj, h.ni, h.tilesX * h.tilesY]).toEqual([1515, 1455, 9]);
    expect([bytes.length, objectBytes(m)]).toEqual([1179712, 1179712]);
    expect(roundTripDiffs(OPUK_GRID, bytes, h)).toBe(0);
  });
  it("SABOTAGE AS FROZEN (tilesY for tilesX) is BLIND on this grid: 3 = 3 (docs/wx-opuk.md; K4 reads FAIL until ruled)", () => {
    const { m, bytes } = buildPattern(OPUK_GRID);
    const swapped: FrameMeta = { ...m, tilesX: m.tilesY, tilesY: m.tilesX };
    const d = roundTripDiffs(OPUK_GRID, bytes, swapped);
    console.log(`K4 sabotage as frozen: tilesX ${m.tilesX}, tilesY ${m.tilesY}, cells differing ${d} -- BLIND`);
    // Pinned: if this ever differs, the grid changed and the record in docs/wx-opuk.md is stale.
    expect(d).toBe(0);
  });
});

// ---- K5, K7, K8: the dispatch, the ETag, the age limit, through the endpoint ------------------------------------------

const SMALL = { ni: 600, nj: 400, la1: 40.0, lo1: -100.0, di: 0.01, dj: 0.01 };   // cref's test grid: KMCI inside

async function storeFrame(p: Product, g: GridGeom, epoch: number, coveredShare?: number) {
  const m = frameMeta(p.id, epoch, g);
  const fb = new FrameBuilder(m);
  const row = new Uint8Array(g.ni);
  for (let r = 0; r < g.nj; r++) { for (let c = 0; c < g.ni; c++) row[c] = (r * 7 + c * 3) % 14; fb.setRow(r, row); }
  await env.WX!.put(frameKey(p, epoch), fb.bytes);
  const rec: LatestRecord = { product: p.id, epoch, key: frameKey(p, epoch), ingestedAt: Date.now(), ...g, tile: m.tile,
                              ...(coveredShare === undefined ? {} : { coveredShare }) };
  await env.WX!.put(latestKey(p), JSON.stringify(rec));
}
const q = (id: string, r = 161) => { const a = at(id); return `lat=${a.lat.toFixed(2)}&lon=${a.lon.toFixed(2)}&r=${r}`; };
const wxAt = (product: string, id: string, h: Record<string, string> = {}) =>
  call(apiRequest(`/api/v1/blipscope/wx/${product}?${q(id)}`, h));
const now = () => Math.floor(Date.now() / 1000);
/** One long run of code 14 covering every cell: 0xE0 then the LEB128 length (as test/wx-endpoint.test.ts). */
const allNoCoverage = (b: Uint8Array) => b[32] === 0xe0;

describe("K5: /wx/cref is served from opuk inside the UK land box, and only there", () => {
  const served = (id: string, byWindow = false) => { const a = at(id); const qq = quantise(a.lat, a.lon, 161); return servedProduct(cref, qq.lat, qq.lon, byWindow).id; };

  it("LHR opuk; CDG, KMCI, PHNL cref; the land box and CONUS do not overlap", () => {
    expect([served("LHR"), served("CDG"), served("KMCI"), served("PHNL")]).toEqual(["opuk", "cref", "cref", "cref"]);
    for (const id of ["MAN", "EDI", "GLA", "BFS", "ABZ", "CWL", "NQY"]) expect(served(id), id).toBe("opuk");
    const conusE = CONUS.lo1 + (CONUS.ni - 1) * CONUS.di, conusS = CONUS.la1 - (CONUS.nj - 1) * CONUS.dj;
    const overlap = !(OPUK_LAND.lonW > conusE || OPUK_LAND.lonE < CONUS.lo1 || OPUK_LAND.latS > CONUS.la1 || OPUK_LAND.latN < conusS);
    expect(overlap).toBe(false);
    // CONTROL: CDG is inside opuk's grid window -- the case the land box exists for
    const a = at("CDG"), g = OPUK_GRID;
    expect(a.lat <= g.la1 && a.lat >= g.la1 - (g.nj - 1) * g.dj && a.lon >= g.lo1 && a.lon <= g.lo1 + (g.ni - 1) * g.di).toBe(true);
  });

  it("through the endpoint: LHR's crop names opuk; CDG's is cref's all-14 crop; PHNL's too", async () => {
    const e = now() - 120;
    await storeFrame(cref, SMALL, e);
    await storeFrame(opuk, OPUK_GRID, e, 0.5);
    const lhr = await wxAt("cref", "LHR");
    expect(lhr.status).toBe(200);
    expect(lhr.headers.get("ETag")).toContain("-opuk-");
    for (const id of ["CDG", "PHNL"]) {
      const res = await wxAt("cref", id);
      const body = new Uint8Array(await res.arrayBuffer());
      expect(res.status, id).toBe(200);
      expect(res.headers.get("ETag"), id).toContain("-cref-");
      expect(allNoCoverage(body), id).toBe(true);
    }
    const kmci = await wxAt("cref", "KMCI");
    expect([kmci.status, kmci.headers.get("ETag")?.includes("-cref-")]).toEqual([200, true]);
  }, SLOW);

  it("SABOTAGE: dispatch by the grid's window -- CDG is served opuk", async () => {
    expect(served("CDG", true)).toBe("opuk");
    const e = now() - 120;
    await storeFrame(cref, SMALL, e);
    await storeFrame(opuk, OPUK_GRID, e, 0.5);
    const res = await handleWx(new Request(`https://proxy.test/api/v1/blipscope/wx/cref?${q("CDG")}`), env, "cref", true);
    expect(res.headers.get("ETag")).toContain("-opuk-");
  }, SLOW);
});

function k7(etag: (p: Product, e: number, la: string, lo: string, r: number) => string) {
  const a = quantise(at("LHR").lat, at("LHR").lon, 161);
  return etag(cref, 1, a.lat2, a.lon2, a.rKm) !== etag(opuk, 1, a.lat2, a.lon2, a.rKm);
}

describe("K7: the ETag names the served product", () => {
  it("the same query and epoch give different ETags for cref and opuk", () => { expect(k7(cropEtag)).toBe(true); });
  it("a device moved from LHR to KMCI never gets a 304", async () => {
    const e = now() - 120;
    await storeFrame(cref, SMALL, e);
    await storeFrame(opuk, OPUK_GRID, e, 0.5);
    const first = await wxAt("cref", "LHR");
    const tag = first.headers.get("ETag")!;
    const moved = await wxAt("cref", "KMCI", { "If-None-Match": tag });
    expect([first.status, moved.status]).toEqual([200, 200]);
    const again = await wxAt("cref", "LHR", { "If-None-Match": tag });
    expect(again.status, "CONTROL: the same query does get its 304").toBe(304);
  }, SLOW);
  it("SABOTAGE: an ETag without the product collides", () => {
    expect(k7((_p, e, la, lo, r) => `"wx1-${e}-${la}_${lo}_${r}"`)).toBe(false);
  });
});

/** Which ages a product's limit lets through (K8), with the limit taken from `limitOf`. */
function k8(limitOf: (p: Product) => number) {
  const ok = (p: Product, age: number) => age <= limitOf(p);
  return { opuk900: ok(opuk, 900), cref900: ok(cref, 900), opuk1801: ok(opuk, 1801) };
}

describe("K8: opuk's 1,800 s limit", () => {
  it("opuk at 900 s 200; cref at 900 s 503; opuk at 1,801 s 503 wx_stale", async () => {
    expect(k8((p) => p.maxAgeS)).toEqual({ opuk900: true, cref900: false, opuk1801: false });
    await storeFrame(opuk, OPUK_GRID, now() - 900, 0.5);
    await storeFrame(cref, SMALL, now() - 900);
    expect((await wxAt("cref", "LHR")).status).toBe(200);
    const c = await wxAt("cref", "KMCI");
    expect(c.status).toBe(503);
    expect(await c.json()).toMatchObject({ error: "wx_stale" });
    await storeFrame(opuk, OPUK_GRID, now() - 1801, 0.5);
    const o = await wxAt("cref", "LHR");
    expect(o.status).toBe(503);
    expect(await o.json()).toMatchObject({ error: "wx_stale" });
  }, SLOW);
  it("SABOTAGE: the limit read from cref -- opuk at 900 s is refused", () => {
    expect(k8(() => cref.maxAgeS).opuk900).toBe(false);
  });
});

// ---- K6: every view served from opuk lies inside its grid -----------------------------------------------------------

function gridFor(margin: number): GridGeom {
  const la1 = OPUK_LAND.latN + margin, lo1 = OPUK_LAND.lonW - margin;
  return { la1, lo1, di: 0.01, dj: 0.01,
           nj: Math.round((la1 - (OPUK_LAND.latS - margin)) / 0.01) + 1, ni: Math.round((OPUK_LAND.lonE + margin - lo1) / 0.01) + 1 };
}

function k6(g: GridGeom) {
  const north = g.la1 + g.dj / 2, south = g.la1 - (g.nj - 1) * g.dj - g.dj / 2;
  const west = g.lo1 - g.di / 2, east = g.lo1 + (g.ni - 1) * g.di + g.di / 2;
  const E = 1e-9;
  let cases = 0, views = 0, outside = 0, padOut = 0, notOpuk = 0;
  for (let i = 0; ; i++) {
    const lat = OPUK_LAND.latS + i * 0.05;
    if (lat > OPUK_LAND.latN + 1e-9) break;
    for (let j = 0; ; j++) {
      const lon = OPUK_LAND.lonW + j * 0.05;
      if (lon > OPUK_LAND.lonE + 1e-9) break;
      for (const r of [1, 25, 80, 160, 222, 300, 500]) {
        const qq = quantise(lat, lon, r);
        cases++;
        if (servedProduct(cref, qq.lat, qq.lon).id !== "opuk") { notOpuk++; continue; }
        const b = cropCellBox(g, qq);
        if (b.r0 < 0 || b.c0 < 0 || b.r0 + b.rows > g.nj || b.c0 + b.cols > g.ni) padOut++;
        for (const dLat of [-0.005, 0.005]) for (const dLon of [-0.005, 0.005]) for (const dR of [-0.5, 0.5]) {
          const pl = qq.lat + dLat, pn = qq.lon + dLon;
          const { radLat, radLon } = viewDegrees(pl, Math.max(0.5, qq.rKm + dR), VIEW);
          views++;
          if (pl + radLat > north + E || pl - radLat < south - E || pn - radLon < west - E || pn + radLon > east + E) outside++;
        }
      }
    }
  }
  return { cases, views, outside, padOut, notOpuk };
}

describe("K6: every view served from opuk lies inside its grid", () => {
  it("CONTROL: opuk's grid is the land box plus 2.02 deg at 0.01 deg", () => {
    const g = gridFor(OPUK_MARGIN);
    expect([g.ni, g.nj]).toEqual([OPUK_GRID.ni, OPUK_GRID.nj]);
    expect(Math.abs(g.la1 - OPUK_GRID.la1) < 1e-9 && Math.abs(g.lo1 - OPUK_GRID.lo1) < 1e-9).toBe(true);
  });
  it("0 views outside the grid; crops whose padding reaches outside printed", () => {
    const r = k6(OPUK_GRID);
    console.log(`K6 cases ${r.cases}, views ${r.views}, outside ${r.outside}, crops padded past the grid ${r.padOut}, ` +
                `not served opuk ${r.notOpuk}`);
    expect(r.notOpuk).toBe(0);
    expect(r.views).toBe(r.cases * 8);
    expect(r.outside).toBe(0);
  });
  it("SABOTAGE: a margin of 1.0 deg leaves views outside", () => {
    const r = k6(gridFor(1.0));
    console.log(`K6 sabotage (margin 1.0): outside ${r.outside} of ${r.views}`);
    expect(r.outside).toBeGreaterThan(0);
  });
});

// ---- K9, K10, K14: the ingest on a real frame ------------------------------------------------------------------------

describe("K9: the covered share", () => {
  it("the stored frame's record carries the window's share; /healthz prints it", async () => {
    const t = (epochOf(DRY) + 300) * 1000;
    const r = await ingestOnce(env.WX!, opuk, t, operaFetch(DRY));
    expect(r.status).toBe("stored");
    const rec = await readLatest(env.WX!, opuk);
    console.log(`K9 ${DRY.name}: covered share ${r.coveredShare?.toFixed(4)}, record ${rec?.coveredShare?.toFixed(4)}`);
    expect(r.coveredShare).toBeGreaterThan(0);
    expect(r.coveredShare).toBeLessThanOrEqual(1);
    expect(rec?.coveredShare).toBe(r.coveredShare);
    const h = await call(new Request("https://proxy.test/healthz"));
    const wx = ((await h.json()) as { wx: Record<string, { coveredShare?: number | null }> }).wx;
    expect(wx.opuk!.coveredShare).toBe(r.coveredShare);
    expect("coveredShare" in wx.cref!).toBe(false);     // cref's entry is unchanged
  }, SLOW);

  const allNodata = async (o: { coveredFixed?: number } = {}) => {
    const geo = await realGeo();
    const win = windowFor(geo, OPUK_GRID);
    const n = (win[1] - win[0] + 1) * (win[3] - win[2] + 1);
    return resample(new Float32Array(n).fill(NODATA), new Float32Array(n), mapFor(geo, win, OPUK_GRID), OPUK_GRID, o).covered;
  };
  it("an all-nodata frame reads 0", async () => { expect(await allNodata()).toBe(0); }, SLOW);
  it("SABOTAGE: the share fixed at 1 -- the all-nodata frame reads 1", async () => {
    expect(await allNodata({ coveredFixed: 1 })).toBe(1);
  }, SLOW);
});

describe("K10: opuk prunes only on the check that stores a frame", () => {
  const run = async (x: IngestX) => {
    const t = (epochOf(DRY) + 300) * 1000;
    const a = countingLists();
    const stored = await ingestOnce(a.WX, opuk, t, operaFetch(DRY), undefined, x);
    const b = countingLists();
    const again = await ingestOnce(b.WX, opuk, t + 30_000, operaFetch(DRY), undefined, x);
    return { stored: stored.status, storeLists: a.lists(), again: again.status, againLists: b.lists() };
  };
  it("a storing check prunes; a check that stores nothing makes 0 R2 lists", async () => {
    const r = await run({});
    console.log(`K10: ${r.stored} with ${r.storeLists} list(s), then ${r.again} with ${r.againLists}`);
    expect([r.stored, r.again]).toEqual(["stored", "already-stored"]);
    expect(r.storeLists).toBeGreaterThan(0);
    expect(r.againLists).toBe(0);
  }, SLOW);
  it("SABOTAGE: prune on every check -- the second check lists", async () => {
    expect((await run({ pruneAlways: true })).againLists).toBeGreaterThan(0);
  }, SLOW);
});

/** The request kinds that went out without K14's User-Agent. */
function k14(seen: Seen[]) {
  const kinds = new Map<string, number>();
  const missing = new Set<string>();
  for (const s of seen) {
    if (!s.url.startsWith(OPERA_BUCKET)) continue;
    const kind = s.url.includes("list-type=2") ? "list" : s.headers.Range === `bytes=0-${HEAD_BYTES - 1}` ? "head" : "tiles";
    kinds.set(kind, (kinds.get(kind) ?? 0) + 1);
    if (s.headers["User-Agent"] !== OPERA_UA) missing.add(kind);
  }
  return { kinds: Object.fromEntries(kinds), missing: [...missing].sort() };
}

describe("K14: every CloudFerro request names us", () => {
  const run = async (x: IngestX) => {
    const seen: Seen[] = [];
    const r = await ingestOnce(env.WX!, opuk, (epochOf(DRY) + 300) * 1000, operaFetch(DRY, seen), undefined, x);
    expect(r.status).toBe("stored");
    return k14(seen);
  };
  it("the listing, the header and every tile range carry the User-Agent", async () => {
    const r = await run({});
    console.log(`K14 requests ${JSON.stringify(r.kinds)}, missing the UA: ${JSON.stringify(r.missing)}`);
    expect(r.kinds).toEqual({ list: 1, head: 1, tiles: DRY.ranges.ranges.length - 1 });
    expect(r.missing).toEqual([]);
  }, SLOW);
  for (const kind of ["list", "head", "tiles"] as const) {
    it(`SABOTAGE: the header dropped from the ${kind} request`, async () => {
      expect((await run({ uaOff: kind })).missing).toEqual([kind]);
    }, SLOW);
  }
});

// ---- K15: the clutter mask, one switch ------------------------------------------------------------------------------

function k15(mask: MaskOpts) {
  const off: MaskOpts = { ...mask, on: false };
  return [operaCode(40, 0.4, mask), operaCode(40, 0.5, mask), operaCode(40, 0.9, mask), operaCode(40, 0.4, off)];
}

describe("K15: the mask switch", () => {
  it("on: (40, 0.4) 0, (40, 0.5) 6, (40, 0.9) 6; off: (40, 0.4) 6; default on at 0.5", () => {
    expect(OPUK_QI_MASK).toEqual({ on: true, threshold: 0.5 });
    expect(k15(OPUK_QI_MASK)).toEqual([0, 6, 6, 6]);
  });
  it("SABOTAGE: <= for < -- (40, 0.5) masks", () => {
    expect(k15({ ...OPUK_QI_MASK, cmp: "le" })).toEqual([0, 0, 6, 6]);
  });
});
