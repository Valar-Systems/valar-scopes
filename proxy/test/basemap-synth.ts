// Synthetic stores for the builder's rows (docs/basemap-route.md A1.7: MB, ME2, ME3, ME7, MD1, MD2), committed BEFORE
// any builder code, as A1.7 requires of ME2's generator. Not a test file: build.test.ts and the MD2 script import it.
// It uses the store's shared formats and RD21's tile formula, and no builder code. Synthetic origins only.
import { crc32 } from "../src/basemap/format";
import { CURRENT_KEY, type Box, type FeatureKind, type StoreIndex, encodeCell, encodeFeature, storeKeys } from "../src/basemap/store";
import { z9X, z9Y } from "../src/basemap/terrain";
import { PREDICATES, assignCells, bboxOf, reachOf, storeIdOf, writeManifest } from "../scripts/basemap/ingest-lib";
import { encodeCoverage } from "../scripts/basemap/coverage";
import { encodeTileBits } from "../scripts/basemap/tile-bits";

export type Pt = [number, number];
export const COMMIT = "1".repeat(40);

/** mulberry32: a small seeded PRNG, the same sequence in workerd and in Node (MD2). */
export function rng(seed: number): () => number {
  let a = seed >>> 0;
  return () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

// ------------------------------------------------------------------------------------------------ PNG tiles

async function deflateZlib(data: Uint8Array): Promise<Uint8Array> {
  const stream = new Blob([data]).stream().pipeThrough(new CompressionStream("deflate"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}
function chunk(type: string, data: Uint8Array): Uint8Array {
  const out = new Uint8Array(12 + data.length), dv = new DataView(out.buffer);
  dv.setUint32(0, data.length, false);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  dv.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)), false);
  return out;
}
/** 256 x 256 RGB, filter 0 on every row. */
export async function encodePng(rgb: Uint8Array): Promise<Uint8Array> {
  const raw = new Uint8Array(256 * (1 + 768));
  for (let y = 0; y < 256; y++) raw.set(rgb.subarray(y * 768, (y + 1) * 768), y * 769 + 1);
  const ihdr = new Uint8Array(13), dv = new DataView(ihdr.buffer);
  dv.setUint32(0, 256, false); dv.setUint32(4, 256, false); ihdr[8] = 8; ihdr[9] = 2;
  const parts = [Uint8Array.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk("IHDR", ihdr), chunk("IDAT", await deflateZlib(raw)), chunk("IEND", new Uint8Array(0))];
  const out = new Uint8Array(parts.reduce((a, p) => a + p.length, 0));
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}
/** A tile of one elevation, in metres, as Terrarium RGB. */
export function flatTileRgb(elevM: number): Uint8Array {
  const v = elevM + 32768, r = Math.floor(v / 256), g = Math.floor(v) % 256, b = Math.floor((v - Math.floor(v)) * 256);
  const t = new Uint8Array(256 * 256 * 3);
  for (let i = 0; i < 65536; i++) { t[3 * i] = r; t[3 * i + 1] = g; t[3 * i + 2] = b; }
  return t;
}

// ------------------------------------------------------------------------------------------------ the store

/** A store in memory, counting reads (MW's read counts; MD's determinism needs only the bytes). */
export class MemStore {
  reads: string[] = [];
  constructor(readonly objects: Map<string, Uint8Array>) {}
  async get(key: string): Promise<Uint8Array | null> { this.reads.push(key); return this.objects.get(key) ?? null; }
}

export interface SynthFile { layer: string; url: string; sha256: string; vintage: string }
export interface SynthFeature { kind: FeatureKind; url: string; record: number; part: number; attrs: Record<string, string | number>; lonlat: Pt[] }
export interface SynthDef {
  files: SynthFile[];
  features: SynthFeature[];
  regions: Array<{ box: Box; files: string[] }>;
  /** covered lattice points, in hundredths: [lat, lon] */
  covered: Array<[number, number]>;
  /** tile "x/y" -> its PNG and source bits */
  tiles?: Map<string, { png: Uint8Array; bits: number }>;
}

/** The store objects for a definition, sealed with `current`. Features go to cells as the ingest assigns them. */
export async function writeSynthStore(d: SynthDef): Promise<{ store: MemStore; storeId: string }> {
  const manifestText = writeManifest({ builder: COMMIT, predicates: [...PREDICATES], files: d.files.map((f) => ({ bytes: 1000, ...f })) });
  const order = [...d.files].map((f) => f.url).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  const id = await storeIdOf(manifestText), k = storeKeys(id), te = new TextEncoder();
  const feats = d.features.map((f) => {
    const file = order.indexOf(f.url);
    if (file < 0) throw new Error(`synthetic feature names an unlisted file ${f.url}`);
    const xy = Float64Array.from(f.lonlat.flat());
    return { bytes: encodeFeature({ kind: f.kind, file, record: f.record, part: f.part, attrs: f.attrs, xy }), box: bboxOf(xy) };
  });
  const objects = new Map<string, Uint8Array>();
  const cells = assignCells(feats, 24 * 1024 * 1024);
  for (const c of cells) objects.set(k.cell(c.key), encodeCell(c.features));
  const index: StoreIndex = { cells: cells.map((c) => ({ key: c.key, bytes: encodeCell(c.features).length, count: c.features.length, reach: reachOf(c) })), regions: d.regions };
  objects.set(k.manifest, te.encode(manifestText));
  objects.set(k.index, te.encode(JSON.stringify(index) + "\n"));
  const rows = new Map<number, Array<[number, number]>>();
  for (const [h, c] of d.covered) (rows.get(h) ?? rows.set(h, []).get(h)!).push([c, c]);
  for (const iv of rows.values()) iv.sort((a, b) => a[0] - b[0]);
  objects.set(k.coverage, encodeCoverage(rows));
  if (d.tiles) {
    const bits: Array<{ x: number; y: number; bits: number }> = [];
    for (const [xy, t] of d.tiles) { const [x, y] = xy.split("/").map(Number) as [number, number]; objects.set(k.tile(x, y), t.png); bits.push({ x, y, bits: t.bits }); }
    objects.set(k.terrainSources, encodeTileBits(bits));
  }
  objects.set(CURRENT_KEY, te.encode(JSON.stringify({ storeId: id })));
  return { store: new MemStore(objects), storeId: id };
}

/** A1.7's convention: (E, S) km to (lon, lat) about an origin. */
export function lonLatOf(lat0: number, lon0: number, e: number, s: number): Pt {
  const c0 = Math.max(Math.cos(lat0 * (Math.PI / 180)), 0.01);
  return [lon0 + e / (111 * c0), lat0 - s / 111];
}
const boxOfKm = (lat0: number, lon0: number, half: number): Box => {
  const [a, b] = lonLatOf(lat0, lon0, -half, half), [c, d] = lonLatOf(lat0, lon0, half, -half);
  return [a, b, c, d];
};

// ------------------------------------------------------------------------------------------------ MB (A1.7)

export interface BuildQuery { lat0Micro: number; lon0Micro: number; rKm: number; epsMm: number; mask: number; levels: Array<{ radiusMm: number; n: number }> }
export const MASK = { water: 1, rivers: 2, roads: 4, peaks: 8, places: 16, terrain: 32 } as const;

const W_URL = "https://example.invalid/water/w.zip", R_URL = "https://example.invalid/roads/r.zip";
const tileUrl = (x: number, y: number) => `https://example.invalid/terrain/9/${x}/${y}.png`;

/** MB's fixtures A, B and A8 at origin (0, 0): R 97 km, ε 100,584 mm, layers water and roads (A8 adds terrain). */
export async function mbStore(which: "A" | "B" | "A8") {
  const ll = (pts: Pt[]) => pts.map(([e, s]) => lonLatOf(0, 0, e, s));
  const features: SynthFeature[] = [];
  for (let i = 1; i <= 9; i++) {
    const e0 = 25 + 5 * (i - 1), s0 = 30, side = 1.0 + 0.1 * i;
    const ring: Pt[] = [[e0, s0], [e0 + side, s0], [e0 + side, s0 + side], [e0, s0 + side], [e0, s0]];
    features.push({ kind: "water", url: W_URL, record: i - 1, part: 0, attrs: { AWATER: 2_000_000 }, lonlat: ll(ring) });
  }
  const roads: Array<[string, number]> = [["S1100", 10], ["S1100", 11], ["S1100", 12], ["S1200", 3], ["S1200", 4], ["S1200", 5], ["S1200", 6]];
  roads.forEach(([mtfcc, len], j) => {
    const s = -30 - 2 * (j + 1);
    features.push({ kind: "roads", url: R_URL, record: j, part: 0, attrs: { MTFCC: mtfcc, FULLNAME: `Road ${j + 1}` }, lonlat: ll([[-70, s], [-70 + len, s]]) });
  });
  if (which === "B") {
    features.push({ kind: "roads", url: R_URL, record: 7, part: 0, attrs: { MTFCC: "S1100", FULLNAME: "Z1" }, lonlat: ll([[-70, -46], [-60, -46.15], [-50, -46]]) });
    features.push({ kind: "roads", url: R_URL, record: 8, part: 0, attrs: { MTFCC: "S1100", FULLNAME: "Z2" }, lonlat: ll([[-70, -48], [-60, -48.3], [-50, -48]]) });
  }
  const files: SynthFile[] = [{ layer: "water", url: W_URL, sha256: "3".repeat(64), vintage: "2024" }, { layer: "roads", url: R_URL, sha256: "4".repeat(64), vintage: "2024" }];
  let tiles: SynthDef["tiles"];
  if (which === "A8") {
    tiles = new Map();
    const png = await encodePng(flatTileRgb(1000));
    for (let x = 254; x <= 257; x++) for (let y = 254; y <= 257; y++) {
      files.push({ layer: "terrain", url: tileUrl(x, y), sha256: "5".repeat(64), vintage: "2016" });
      tiles.set(`${x}/${y}`, { png, bits: 1 });
    }
  }
  const def: SynthDef = {
    files, features, tiles, covered: [[0, 0]],
    regions: [{ box: boxOfKm(0, 0, 100), files: [W_URL] }, { box: boxOfKm(0, 0, 100), files: [R_URL] }],
  };
  const query: BuildQuery = { lat0Micro: 0, lon0Micro: 0, rKm: 97, epsMm: 100_584, mask: MASK.water | MASK.roads | (which === "A8" ? MASK.terrain : 0), levels: which === "A8" ? [{ radiusMm: 1_000_000, n: 4 }] : [] };
  return { ...(await writeSynthStore(def)), query };
}

// ------------------------------------------------------------------------------------------------ ME3, ME7

/** ME3: a primary road from the origin to (E, S) = (R, -R). */
export async function me3Store() {
  const r = 97;
  const def: SynthDef = {
    files: [{ layer: "roads", url: R_URL, sha256: "4".repeat(64), vintage: "2024" }],
    features: [{ kind: "roads", url: R_URL, record: 0, part: 0, attrs: { MTFCC: "S1100", FULLNAME: "Edge Rd" }, lonlat: [lonLatOf(0, 0, 0, 0), lonLatOf(0, 0, r, -r)] }],
    regions: [{ box: boxOfKm(0, 0, 100), files: [R_URL] }], covered: [[0, 0]],
  };
  const query: BuildQuery = { lat0Micro: 0, lon0Micro: 0, rKm: r, epsMm: 100_584, mask: MASK.roads, levels: [] };
  return { ...(await writeSynthStore(def)), query };
}

/** ME7: every layer, at origin (0, 0), with A8's tiles; the levels passed unsorted (2,000,000 then 1,000,000). */
export async function me7Store() {
  const ll = (pts: Pt[]) => pts.map(([e, s]) => lonLatOf(0, 0, e, s));
  const RIV = "https://example.invalid/rivers/v.zip", GN = "https://example.invalid/peaks/g.zip", GZ = "https://example.invalid/places/z.txt", PO = "https://example.invalid/population/p.csv";
  const files: SynthFile[] = [
    { layer: "water", url: W_URL, sha256: "3".repeat(64), vintage: "2024" }, { layer: "roads", url: R_URL, sha256: "4".repeat(64), vintage: "2024" },
    { layer: "rivers", url: RIV, sha256: "6".repeat(64), vintage: "2024" }, { layer: "peaks", url: GN, sha256: "7".repeat(64), vintage: "2025" },
    { layer: "places", url: GZ, sha256: "8".repeat(64), vintage: "2025" }, { layer: "population", url: PO, sha256: "9".repeat(64), vintage: "2024" },
  ];
  const tiles = new Map<string, { png: Uint8Array; bits: number }>();
  const png = await encodePng(flatTileRgb(1000));
  for (let x = 253; x <= 258; x++) for (let y = 253; y <= 258; y++) { files.push({ layer: "terrain", url: tileUrl(x, y), sha256: "5".repeat(64), vintage: "2016" }); tiles.set(`${x}/${y}`, { png, bits: 1 }); }
  const def: SynthDef = {
    files, tiles, covered: [[0, 0]],
    features: [
      { kind: "water", url: W_URL, record: 0, part: 0, attrs: { AWATER: 2_000_000 }, lonlat: ll([[1, 1], [2, 1], [2, 2], [1, 2], [1, 1]]) },
      { kind: "rivers", url: RIV, record: 0, part: 0, attrs: { FULLNAME: "Test Riv" }, lonlat: ll([[-5, 3], [5, 3]]) },
      { kind: "roads", url: R_URL, record: 0, part: 0, attrs: { MTFCC: "S1100", FULLNAME: "Test Rd" }, lonlat: ll([[-5, -3], [5, -3]]) },
      { kind: "peaks", url: GN, record: 0, part: 0, attrs: { feature_id: 42, feature_name: "Test Peak" }, lonlat: ll([[0.5, 0.5]]) },
      { kind: "places", url: GZ, record: 0, part: 0, attrs: { GEOID: "0000001", NAME: "Testville", pop: 1234 }, lonlat: ll([[-0.5, -0.5]]) },
    ],
    regions: [{ box: boxOfKm(0, 0, 100), files: [W_URL, RIV] }, { box: boxOfKm(0, 0, 100), files: [R_URL, GN] }],
  };
  const query: BuildQuery = { lat0Micro: 0, lon0Micro: 0, rKm: 97, epsMm: 100_584, mask: 63, levels: [{ radiusMm: 2_000_000, n: 4 }, { radiusMm: 1_000_000, n: 4 }] };
  return { ...(await writeSynthStore(def)), query };
}

// ------------------------------------------------------------------------------------------------ ME2's generator

const RIVER_NAMES = ["A Riv", "B Riv", "Crooked Riv", "Deschutes Riv", "Little Deschutes Riv", "Mud Riv", "Ñandú Riv", "Riverside Dr", "Big Creek"];
const ROAD_NAMES = ["US Hwy 97", "OR 126", "I- 84", "", "Main St", "President George Bush Tpke", "Busby Rd"]; // none with "Bus" as a word
let pngs: Uint8Array[] | null = null;

/**
 * ME2's synthetic store number i (A1.7): 0-40 features per layer; lines of 2-200 vertices; rings of 3-100; R 1-222 km;
 * ε 1,000-2,000,000 mm; a random mask; 0-2 levels with N 1-64; no road name holding "Bus" as a word. The level radius,
 * which A1.7 leaves open, is drawn from 1-10 km so 1,000 builds stay quick; features fall in the square and up to 20%
 * beyond it. Two counties (one far outside the square, whose files must not be read) and one state.
 */
export async function me2Store(i: number) {
  const rand = rng(0x6d65_32 + i), int = (a: number, b: number) => a + Math.floor(rand() * (b - a + 1));
  pngs ??= await Promise.all([600, 1400, 2200, 3000].map((e) => encodePng(flatTileRgb(e))));
  const lat0Micro = int(2500, 4800) * 10_000, lon0Micro = -int(6800, 12400) * 10_000;
  const lat0 = lat0Micro / 1e6, lon0 = lon0Micro / 1e6, rKm = int(1, 222), epsMm = int(1_000, 2_000_000);
  let mask = int(1, 63);
  const levels = mask & MASK.terrain ? Array.from({ length: int(1, 2) }, () => ({ radiusMm: int(1_000, 10_000) * 1_000, n: int(1, 64) })) : [];
  if (levels.length === 2 && levels[0]!.radiusMm === levels[1]!.radiusMm) levels.pop();
  const u = (m: number) => (rand() * 2 - 1) * m, reach = rKm * 1.2;
  const U = (layer: string, n: string) => `https://example.invalid/${layer}/${n}`;
  const WA = U("water", "c1.zip"), WB = U("water", "c2.zip"), RA = U("rivers", "c1.zip"), RB = U("rivers", "c2.zip"), RD = U("roads", "s1.zip"), PK = U("peaks", "s1.zip"), GZ = U("places", "gaz.txt"), PO = U("population", "pop.csv");
  const files: SynthFile[] = [WA, WB, RA, RB, RD, PK, GZ, PO].map((url) => ({ layer: url.split("/")[3]!, url, sha256: (url.length % 10).toString().repeat(64), vintage: "2024" }));
  const features: SynthFeature[] = [];
  const ll = (pts: Pt[]) => pts.map(([e, s]) => lonLatOf(lat0, lon0, e, s));
  const walk = (n: number): Pt[] => { const p: Pt[] = [[u(reach), u(reach)]]; for (let k = 1; k < n; k++) p.push([p[k - 1]![0] + u(5), p[k - 1]![1] + u(5)]); return p; };
  for (let r = 0, n = int(0, 40); r < n; r++) {
    const c: Pt = [u(reach), u(reach)], rad = 0.2 + rand() * 20, k = int(3, 100), hole = rand() < 0.2;
    const ang = Array.from({ length: k }, () => rand() * 2 * Math.PI).sort((a, b) => (hole ? b - a : a - b));
    const ring = ang.map((t) => [c[0] + rad * (0.3 + rand()) * Math.cos(t), c[1] + rad * (0.3 + rand()) * Math.sin(t)] as Pt);
    features.push({ kind: "water", url: WA, record: r, part: 0, attrs: { AWATER: int(1_000_000, 5_000_000) }, lonlat: ll([...ring, ring[0]!]) });
  }
  for (let r = 0, n = int(0, 40); r < n; r++) features.push({ kind: "rivers", url: RA, record: r, part: 0, attrs: { FULLNAME: RIVER_NAMES[int(0, RIVER_NAMES.length - 1)]! }, lonlat: ll(walk(int(2, 200))) });
  for (let r = 0, n = int(0, 40); r < n; r++) features.push({ kind: "roads", url: RD, record: r, part: 0, attrs: { MTFCC: ["S1100", "S1200", "S1400"][int(0, 2)]!, FULLNAME: ROAD_NAMES[int(0, ROAD_NAMES.length - 1)]! }, lonlat: ll(walk(int(2, 200))) });
  for (let r = 0, n = int(0, 40); r < n; r++) features.push({ kind: "peaks", url: PK, record: r, part: 0, attrs: { feature_id: int(1, 60), feature_name: `Peak ${r}` }, lonlat: ll([[u(reach), u(reach)]]) });
  for (let r = 0, n = int(0, 40); r < n; r++) features.push({ kind: "places", url: GZ, record: r, part: 0, attrs: { GEOID: String(1_000_000 + r), NAME: `Town ${r}`, pop: int(0, 200_000) }, lonlat: ll([[u(reach), u(reach)]]) });
  // tiles: every z9 tile over the larger of the square and the levels, plus two; their PNGs one of four flat elevations
  const half = Math.max(rKm, ...levels.map((l) => l.radiusMm / 1e6)) * 1.25;
  const [a, b] = lonLatOf(lat0, lon0, -half, half), [c, d] = lonLatOf(lat0, lon0, half, -half);
  const tx0 = Math.floor(z9X(a) / 256) - 2, tx1 = Math.floor(z9X(c) / 256) + 2, ty0 = Math.floor(z9Y(d) / 256) - 2, ty1 = Math.floor(z9Y(b) / 256) + 2;
  const tiles = new Map<string, { png: Uint8Array; bits: number }>();
  for (let x = tx0; x <= tx1; x++) for (let y = ty0; y <= ty1; y++) {
    tiles.set(`${x}/${y}`, { png: pngs[(x + y) & 3]!, bits: int(1, 15) });
    files.push({ layer: "terrain", url: tileUrl(x, y), sha256: "5".repeat(64), vintage: "2016" });
  }
  const def: SynthDef = {
    files, features, tiles, covered: [[lat0Micro / 10_000, lon0Micro / 10_000]],
    regions: [
      { box: boxOfKm(lat0, lon0, reach + 30), files: [WA, RA] },
      { box: boxOfKm(lat0 + 10, lon0, 5), files: [WB, RB] }, // far outside the square: never read
      { box: boxOfKm(lat0, lon0, reach + 30), files: [RD, PK] },
    ],
  };
  if (!levels.length) mask &= ~MASK.terrain;
  const query: BuildQuery = { lat0Micro, lon0Micro, rKm, epsMm, mask, levels };
  return { ...(await writeSynthStore(def)), query };
}
