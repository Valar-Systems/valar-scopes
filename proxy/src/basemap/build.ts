// The builder (docs/basemap-route.md §2.4; A1.7's ME, MB, MD rows): one BMPK v1 package from the store, for one
// request. Pure apart from the store it reads and Web APIs, so it runs the same over R2 in the Worker and over a
// directory or memory in Node (--check, the golden row, MD2).
//
// Reads, in order and only as needed (MW counts them): current; the manifest, whose sha256 must begin with current's
// store id (MW17); coverage; then, for vector layers, the index and the cells whose reach meets the square; then, when a
// tile is needed, the tiles and terrain-sources.bin. The cells, and the tiles with terrain-sources.bin, are read up to
// READ_AHEAD at once and taken in order (C1, MX1), so the package is the same bytes as reading one at a time.
//
// Q-W4 (docs/basemap-dense-code.md): the cells are read once into four layer sets -- ε, ε × 2, rivers and roads at ε × 4, water and the
// sea at ε × 8 -- for budget.ts's ladder; past the ladder, D2 builds the whole package again at r × 0.8, × 0.64, ... (six steps, 129.6 to
// 42.47 km from 162), and past the last a 422. The sea (store kind 6) is read whenever the store has it and water is asked for. A sea
// repair that cannot finish stops the build with a 500 and its code (KB4 (B')), never a partial package. Each build's report (R2-R5)
// carries D2's radii, the refusals and the sea's counts for bm_build, and the run harness's extras, which nothing logs.
import { type Section, TYPE, encode } from "./format";
import { type Frame, type Pt, makeFrame } from "./frame";
import { type LayerKind, LayerSet, type Place, type Rivers, type Road, type WaterRings, rankPeaks } from "./layers";
import { CAP_BYTES, type Dropped, EPS_STEP, type Ladder, fitBudget, newLadder } from "./budget";
import { levelTiles, mosaicOf, summitElevation, terrainLevel, z9Pixel } from "./terrain";
import { type FileRef, type Layer, type ProvenanceInput, layerDigests, provenanceLines } from "./provenance";
import { type Box, CURRENT_KEY, type StoreIndex, boxMeets, covered, decodeCell, decodeTerrariumPng, storeKeys, tileBitsOf } from "./store";
import { type RepairCounts, type SeaCounts, SeaRepairStop, newSeaCounts, ringBytes } from "./sea";

export interface StoreReader { get(key: string): Promise<Uint8Array | null> }
export interface BuildQuery { lat0Micro: number; lon0Micro: number; rKm: number; epsMm: number; mask: number; levels: Array<{ radiusMm: number; n: number }> }
export const MASK = { water: 1, rivers: 2, roads: 4, peaks: 8, places: 16, terrain: 32 } as const;
/** QW4's memory instrument's hook: a named phase per build stage ("cell", "tile", "tiles", "terrain", "layers", "ladder", "d2"). It is
 *  null in production, so each site is an optional call that does nothing; only the memory harness sets it. It changes no byte. */
export const buildHooks: { phase: ((phase: string) => void) | null } = { phase: null };

/** The sea as the package draws it (R5), plus the run harness's extras (never logged, never in the package). */
export interface SeaReport extends SeaCounts {
  /** pieces the ladder dropped from the drawn set (seaDropped) */
  dropped: number;
  /** seaLost = refused + collapsed + dropped: the sea and lake pieces this package does not draw */
  lost: number;
  /** the harness's: the sea rings' bytes, their positions in the water section, and every piece's fate */
  bytes: number; ringIdx: number[]; fates: Array<{ file: number; record: number; fate: string; q?: { outer: Pt[]; holes: Pt[][] } }>;
}
/** What one build call did, for bm_build (R2-R5). */
export interface BuildReport {
  /** D2's radii in order, km: [r] when the first build fits; steps = radii.length (R3) */
  radii: number[];
  /** every backstop refusal in every water set of every D2 step, TIGER's and the sea's step 3 (R4) */
  refusedBuilt: number;
  /** the drawn set's refusals (R2) and the drawn set's sea (R5): a built package only */
  refused?: number; sea?: SeaReport;
}
export type BuildResult =
  | { ok: true; bytes: Uint8Array; storeId: string; ladder: Ladder; report: BuildReport }
  | { ok: false; status: 404; code: "no_coverage" }
  | { ok: false; status: 503; code: "basemap_unavailable"; why: string }
  | { ok: false; status: 422; code: "too_large"; ladder: Ladder; report: BuildReport }
  | { ok: false; status: 500; code: "sea_repair_failed" | "sea_repair_limit" | "sea_repair_state"; why: string; repair: RepairCounts | null };

const K = 111.0;
const unavailable = (why: string): BuildResult => ({ ok: false, status: 503, code: "basemap_unavailable", why });

/** C1 (docs/basemap-a1.md, MX1): the most reads in flight at once, for the cells and for the tiles. */
export const READ_AHEAD = 6;
/** The keys' reads, up to READ_AHEAD at once, yielded IN THE KEYS' ORDER: what is built from them, and the first
 *  missing object named, are the same as reading one at a time. A read-ahead left unread when the caller stops early
 *  settles in the background; its rejection is caught here so it never surfaces unhandled.
 *  Each read's slot is cleared as the read is handed over, so a consumed read is the caller's alone to keep or drop
 *  (docs/basemap-release.md). Holding every read to the loop's end put all of KLGA's cells, 61 MiB, in the build's peak. */
async function* readInOrder(store: StoreReader, keys: string[]): AsyncGenerator<Uint8Array | null> {
  const pending: Array<Promise<Uint8Array | null> | undefined> = [];
  let next = 0;
  for (let i = 0; i < keys.length; i++) {
    while (next < keys.length && next < i + READ_AHEAD) {
      const p = store.get(keys[next++]!);
      p.catch(() => undefined);
      pending.push(p);
    }
    const v = await pending[i]!;
    pending[i] = undefined;
    yield v;
  }
}
const hex = (b: ArrayBuffer) => [...new Uint8Array(b)].map((x) => x.toString(16).padStart(2, "0")).join("");
const json = <T>(b: Uint8Array) => JSON.parse(new TextDecoder("utf-8", { fatal: true, ignoreBOM: false }).decode(b)) as T;

/** RD-P6: the square [-R, R]^2 as a latitude/longitude box, §2's metric inverted exactly (the reference's latlon_box). */
export function squareBox(f: Frame, rMm: number): Box {
  const r = rMm / 1e6;
  return [f.lon0 - r / (K * f.c0), f.lat0 - r / K, f.lon0 + r / (K * f.c0), f.lat0 + r / K];
}

/** A layer set's content for the ladder: its drawn items, and its water set's own counts ("(C)": they travel with its rings). */
interface Content { water?: Pt[][]; roads?: Road[]; rivers?: Rivers; collapsed: Partial<Record<Layer, number>>; refusedW?: number; seaW?: WaterRings["sea"] }

/** One build at one radius; build() runs D2 over it. `refusedBuilt` gains this build's refusals across its four sets. */
async function buildOnce(store: StoreReader, q: BuildQuery, opts: { commit: string; capBytes?: number }, acc: { refusedBuilt: number }): Promise<BuildResult> {
  if (!/^[0-9a-f]{40}$/.test(opts.commit)) throw new Error("the builder refuses to build unstamped (BUILD_COMMIT is not a commit)");
  if (q.lat0Micro % 10_000 !== 0 || q.lon0Micro % 10_000 !== 0) throw new Error("the origin is not a two-decimal point");
  const want = (bit: number) => (q.mask & bit) !== 0;
  const levels = [...q.levels].sort((a, b) => a.radiusMm - b.radiusMm); // ME7: the builder orders them itself

  // current, then the manifest it names, which must hash to that name (MW17)
  const cur = await store.get(CURRENT_KEY);
  if (!cur) return unavailable("no current");
  const storeId = json<{ storeId?: unknown }>(cur).storeId;
  if (typeof storeId !== "string" || !/^[0-9a-f]{12}$/.test(storeId)) return unavailable("current names no store");
  const keys = storeKeys(storeId);
  const manifestBytes = await store.get(keys.manifest);
  if (!manifestBytes) return unavailable("no manifest");
  if (hex(await crypto.subtle.digest("SHA-256", manifestBytes)).slice(0, 12) !== storeId) return unavailable("the manifest does not hash to current's store id");
  const manifest = json<{ files: Array<FileRef & { layer: string }> }>(manifestBytes);
  const byUrl = new Map(manifest.files.map((f) => [f.url, f]));

  // coverage: the origin's own bit, before any other read
  const cov = await store.get(keys.coverage);
  if (!cov) return unavailable("no coverage");
  if (!covered(cov, q.lat0Micro / 10_000, q.lon0Micro / 10_000)) return { ok: false, status: 404, code: "no_coverage" };

  const rMm = q.rKm * 1_000_000;
  const f1 = makeFrame(q.lat0Micro, q.lon0Micro, rMm, q.epsMm), f2 = makeFrame(q.lat0Micro, q.lon0Micro, rMm, EPS_STEP * q.epsMm);
  const MEM = buildHooks.phase;
  const box = squareBox(f1, rMm);
  const read: Partial<Record<Layer, FileRef[]>> = {};
  const fileRef = (url: string): FileRef => { const m = byUrl.get(url); if (!m) throw new StoreGap(`a region names ${url}, which the manifest does not list`); return { url, sha256: m.sha256, vintage: m.vintage }; };

  // the vector layers: cells streamed one at a time, at ε and at 2ε in the same pass (§6 needs no second read)
  const kinds: LayerKind[] = [...(["water", "rivers", "roads", "peaks", "places"] as const).filter((k) => want(MASK[k])), ...(want(MASK.water) ? ["sea" as const] : [])];
  const L1 = new LayerSet(f1), L2 = new LayerSet(f2);
  const L4 = new LayerSet(makeFrame(q.lat0Micro, q.lon0Micro, rMm, 4 * q.epsMm)), L8 = new LayerSet(makeFrame(q.lat0Micro, q.lon0Micro, rMm, 8 * q.epsMm));
  try {
    if (kinds.length) {
      const idxBytes = await store.get(keys.index);
      if (!idxBytes) return unavailable("no index");
      const index = json<StoreIndex>(idxBytes);
      const cells = index.cells.filter((c) => boxMeets(c.reach, box)).sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));
      let ci = 0;
      for await (const b of readInOrder(store, cells.map((c) => keys.cell(c.key)))) {
        const c = cells[ci++]!;
        if (!b) return unavailable(`no cell ${c.key}`);
        for (const ft of decodeCell(b)) {
          if (!kinds.includes(ft.kind)) continue;
          L1.add(ft.kind, ft);
          if (ft.kind === "water" || ft.kind === "sea" || ft.kind === "rivers" || ft.kind === "roads") L2.add(ft.kind, ft);
          if (ft.kind === "rivers" || ft.kind === "roads") L4.add(ft.kind, ft);
          if (ft.kind === "water" || ft.kind === "sea") L8.add(ft.kind, ft);
        }
        MEM?.("cell");
      }
      // RD-P6: a layer's files read are those of the regions whose box meets the square's
      const met = index.regions.filter((r) => boxMeets(r.box, box)).flatMap((r) => r.files);
      for (const k of ["water", "rivers", "roads", "peaks"] as const) {
        if (want(MASK[k])) read[k] = [...new Set(met)].filter((u) => byUrl.get(u)?.layer === k).sort().map(fileRef);
      }
      if (want(MASK.water)) { const s = [...new Set(met)].filter((u) => byUrl.get(u)?.layer === "sea").sort().map(fileRef); if (s.length) read.sea = s; } // RD-P6: the sea's files, only when one is met
      if (want(MASK.places)) for (const l of ["places", "population"] as const) read[l] = manifest.files.filter((f) => f.layer === l).map((f) => fileRef(f.url));
      for (const u of met) fileRef(u); // every region's file must be in the manifest, read or not
    }
  } catch (e) { if (e instanceof StoreGap) return unavailable(e.message); throw e; }

  // tiles: the summits' and the largest level's mosaic (plus its margin), each read once. Their keys are worked out first,
  // in the order they were always loaded (the summits', then the mosaic's), and read ahead with terrain-sources.bin after
  // them (C1); each is decoded and kept in that order, so `tiles` iterates exactly as before.
  const cands = want(MASK.peaks) ? L1.summits() : [];
  const tileOrder: string[] = [];
  const addTile = (x: number, y: number) => { const k = `${x}/${y}`; if (!tileOrder.includes(k)) tileOrder.push(k); };
  for (const s of cands) { const [X, Y] = z9Pixel(s.lat, s.lon); addTile(X >> 8, Y >> 8); }
  const levelBox = levels.length && want(MASK.terrain) ? levelTiles(f1, levels[levels.length - 1]!.radiusMm) : null;
  if (levelBox) for (let x = levelBox[0]; x <= levelBox[2]; x++) for (let y = levelBox[1]; y <= levelBox[3]; y++) addTile(x, y);
  const tiles = new Map<string, Uint8Array>();
  let bitsBin: Uint8Array | null = null;
  if (tileOrder.length) {
    const tileKeys = tileOrder.map((k) => { const [x, y] = k.split("/").map(Number) as [number, number]; return keys.tile(x, y); });
    let ti = 0;
    for await (const b of readInOrder(store, [...tileKeys, keys.terrainSources])) {
      if (ti < tileOrder.length) {
        const k = tileOrder[ti++]!;
        if (!b) return unavailable(`no tile ${k}`);
        tiles.set(k, await decodeTerrariumPng(b));
      } else bitsBin = b;
      MEM?.("tile");
    }
  }
  MEM?.("tiles");
  let peaks: Array<{ q: Pt; elevM: number; name: string }> = [];
  const terrainSections: Section[] = [];
  try {
    const px = { rgb: (X: number, Y: number) => { const t = tiles.get(`${X >> 8}/${Y >> 8}`)!, o = ((Y & 255) * 256 + (X & 255)) * 3; return [t[o]!, t[o + 1]!, t[o + 2]!] as [number, number, number]; } };
    peaks = rankPeaks(f1, cands.map((s) => ({ ...s, elevM: summitElevation(px, s.lat, s.lon) })));
    if (levelBox) {
      const [tx0, ty0, tx1, ty1] = levelBox;
      const mosaic = mosaicOf(tiles, tx0, ty0, tx1, ty1);
      for (const lv of levels) terrainSections.push({ type: 6, levelRadiusMm: lv.radiusMm, n: lv.n, values: terrainLevel(f1, mosaic, lv.radiusMm, lv.n) });
    }
  } catch (e) { if (e instanceof StoreGap) return unavailable(e.message); throw e; }

  MEM?.("terrain");
  // the tiles read: their files (by RD17's /9/<x>/<y>.png tail) and their source bits (the credits)
  let tileInfo: ProvenanceInput["tiles"] = null;
  if (tiles.size) {
    if (!bitsBin) return unavailable("no terrain-sources.bin");
    const bits = tileBitsOf(bitsBin);
    const byTile = new Map<string, FileRef>();
    for (const f of manifest.files) { const m = f.layer === "terrain" ? /\/9\/(\d+)\/(\d+)\.png$/.exec(f.url) : null; if (m) byTile.set(`${m[1]}/${m[2]}`, { url: f.url, sha256: f.sha256, vintage: f.vintage }); }
    let creditBits = 0;
    const refs: FileRef[] = [];
    for (const t of tiles.keys()) {
      const b = bits.get(t), ref = byTile.get(t);
      if (b === undefined || !ref) return unavailable(`tile ${t} has no source bits or no manifest entry`);
      creditBits |= b; refs.push(ref);
    }
    read.terrain = refs.sort((a, b) => (a.url < b.url ? -1 : a.url > b.url ? 1 : 0));
    tileInfo = { levels: terrainSections.length > 0, peaks: cands.length > 0, creditBits };
  }

  const places: Place[] = want(MASK.places) ? L1.keptPlaces() : [];
  const content = (L: LayerSet): Content => {
    const c: Content = { collapsed: {} };
    if (want(MASK.water)) { const w = L.waterRings(); c.water = w.rings; c.collapsed.water = w.collapsed; c.refusedW = w.refused; c.seaW = w.sea; if (w.sea.fates.length) c.collapsed.sea = w.sea.n.collapsed; }
    if (want(MASK.rivers)) { c.rivers = L.selectRivers(); c.collapsed.rivers = c.rivers.collapsed; }
    if (want(MASK.roads)) { const r = L.keptRoads(); c.roads = r.roads; c.collapsed.roads = r.collapsed; }
    return c;
  };
  let at: Record<number, Content>;
  try { // a sea repair that cannot finish stops the build here (KB4 (B'))
    at = { [q.epsMm]: content(L1), [EPS_STEP * q.epsMm]: content(L2) } as Record<number, Content>;
    at[4 * q.epsMm] = content(L4); at[8 * q.epsMm] = content(L8);
  } catch (e) { if (e instanceof SeaRepairStop) return { ok: false, status: 500, code: e.code, why: e.message, repair: e.counts }; throw e; }
  for (const c of Object.values(at)) acc.refusedBuilt += (c.refusedW ?? 0) + (c.seaW?.n.refused3 ?? 0); // R4: every set's refusals
  MEM?.("layers");
  const digests = await layerDigests(read);
  const assemble = (c: Content, d: Dropped, _tol: number): Section[] => {
    const s: Section[] = [];
    if (want(MASK.water)) s.push({ type: 1, rings: c.water! });
    if (want(MASK.rivers)) s.push({ type: 2, names: c.rivers!.names, lines: c.rivers!.lines });
    if (want(MASK.roads)) s.push({ type: 3, roads: c.roads! });
    if (want(MASK.peaks)) s.push({ type: 4, peaks });
    if (want(MASK.places)) s.push({ type: 5, places });
    s.push(...terrainSections);
    const p = provenanceLines({ commit: opts.commit, storeId, files: read, tiles: tileInfo, epsilonMm: q.epsMm, dropped: { water: d.water, roads: d.roads }, collapsed: c.collapsed }, digests);
    s.push({ type: TYPE.PROVENANCE, text: new TextEncoder().encode(p.text), lines: p.lines });
    return s;
  };
  const ladder = newLadder(); // step 0: what the ladder measured, for the builder's bm_build line
  const fitted = fitBudget(q.epsMm, opts.capBytes ?? CAP_BYTES, (tol) => at[tol]!, assemble, ladder);
  MEM?.("ladder");
  if (!fitted) return { ok: false, status: 422, code: "too_large", ladder, report: { radii: [q.rKm], refusedBuilt: acc.refusedBuilt } };
  // the drawn set's sea (R5), the pieces the ladder dropped, and the harness's extras: the sea's bytes, every piece's fate, the sea rings'
  // positions in the package's water section
  const sw = fitted.content.seaW, set: Set<Pt[]> = sw?.set ?? new Set(), fin: Pt[][] = fitted.content.water ?? [];
  const present = new Set(fin.filter((r) => set.has(r)));
  const fates = (sw?.fates ?? []).map((p) => ({ file: p.file, record: p.record, fate: p.fate === "drawn" && p.ring !== null && !present.has(p.ring) ? "dropped" : p.fate, q: p.q }));
  const n: SeaCounts = sw?.n ?? newSeaCounts();
  const dropped = fates.filter((x) => x.fate === "dropped").length;
  const sea: SeaReport = { ...n, dropped, lost: n.refused + n.collapsed + dropped, bytes: [...present].reduce((a, r) => a + ringBytes(r), 0),
    ringIdx: fin.flatMap((r, i) => (set.has(r) ? [i] : [])), fates };
  const bytes = encode({ lat0Micro: q.lat0Micro, lon0Micro: q.lon0Micro, radiusMm: rMm, toleranceMm: fitted.toleranceMm, sections: fitted.sections });
  return { ok: true, bytes, storeId, ladder, report: { radii: [q.rKm], refusedBuilt: acc.refusedBuilt, refused: fitted.content.refusedW ?? 0, sea } };
}

/** A store that names something it does not hold: answered as 503 basemap_unavailable, never as a partial package. */
class StoreGap extends Error {}

/** D2 inside the builder (Q-DP3). While the build answers 422, the whole build again at r x 0.8^k (k = 1..6: 129.6 .. 42.47 km from
 *  162), each radius taken as Q-DR3 took it (Math.round(r_mm x 0.8^k) / 1e6). Past the last, the 422 stands. The result's report lists
 *  every radius built, in order (R3), and the refusals of every step (R4). */
export async function build(store: StoreReader, q: BuildQuery, opts: { commit: string; capBytes?: number }): Promise<BuildResult> {
  const acc = { refusedBuilt: 0 }, radii = [q.rKm];
  let r = await buildOnce(store, q, opts, acc);
  for (let k = 1; k <= 6 && !r.ok && r.status === 422; k++) {
    buildHooks.phase?.("d2"); // a D2 step begins
    const rKm = Math.round(q.rKm * 1e6 * 0.8 ** k) / 1e6;
    radii.push(rKm);
    r = await buildOnce(store, { ...q, rKm }, opts, acc);
  }
  if (r.ok || r.status === 422) r.report = { ...r.report, radii, refusedBuilt: acc.refusedBuilt };
  return r;
}
