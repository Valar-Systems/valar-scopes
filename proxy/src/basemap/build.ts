// The builder (docs/basemap-route.md §2.4; A1.7's ME, MB, MD rows): one BMPK v1 package from the store, for one
// request. Pure apart from the store it reads and Web APIs, so it runs the same over R2 in the Worker and over a
// directory or memory in Node (--check, the golden row, MD2).
//
// Reads, in order and only as needed (MW counts them): current; the manifest, whose sha256 must begin with current's
// store id (MW17); coverage; then, for vector layers, the index and the cells whose reach meets the square, one at a
// time; then, when a tile is needed, terrain-sources.bin and the tiles.
import { type Section, TYPE, encode } from "./format";
import { type Frame, type Pt, makeFrame } from "./frame";
import { type LayerKind, LayerSet, type Place, type Rivers, type Road, rankPeaks } from "./layers";
import { CAP_BYTES, type Dropped, EPS_STEP, fitBudget } from "./budget";
import { levelTiles, mosaicOf, summitElevation, terrainLevel, z9Pixel } from "./terrain";
import { type FileRef, type Layer, type ProvenanceInput, layerDigests, provenanceLines } from "./provenance";
import { type Box, CURRENT_KEY, type StoreIndex, boxMeets, covered, decodeCell, decodeTerrariumPng, storeKeys, tileBitsOf } from "./store";

export interface StoreReader { get(key: string): Promise<Uint8Array | null> }
export interface BuildQuery { lat0Micro: number; lon0Micro: number; rKm: number; epsMm: number; mask: number; levels: Array<{ radiusMm: number; n: number }> }
export const MASK = { water: 1, rivers: 2, roads: 4, peaks: 8, places: 16, terrain: 32 } as const;
export type BuildResult =
  | { ok: true; bytes: Uint8Array; storeId: string }
  | { ok: false; status: 404; code: "no_coverage" }
  | { ok: false; status: 503; code: "basemap_unavailable"; why: string }
  | { ok: false; status: 422; code: "too_large" };

const K = 111.0;
const unavailable = (why: string): BuildResult => ({ ok: false, status: 503, code: "basemap_unavailable", why });
const hex = (b: ArrayBuffer) => [...new Uint8Array(b)].map((x) => x.toString(16).padStart(2, "0")).join("");
const json = <T>(b: Uint8Array) => JSON.parse(new TextDecoder("utf-8", { fatal: true, ignoreBOM: false }).decode(b)) as T;

/** RD-P6: the square [-R, R]^2 as a latitude/longitude box, §2's metric inverted exactly (the reference's latlon_box). */
export function squareBox(f: Frame, rMm: number): Box {
  const r = rMm / 1e6;
  return [f.lon0 - r / (K * f.c0), f.lat0 - r / K, f.lon0 + r / (K * f.c0), f.lat0 + r / K];
}

interface Content { water?: Pt[][]; roads?: Road[]; rivers?: Rivers; collapsed: Partial<Record<Layer, number>> }

export async function build(store: StoreReader, q: BuildQuery, opts: { commit: string; capBytes?: number }): Promise<BuildResult> {
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
  const box = squareBox(f1, rMm);
  const read: Partial<Record<Layer, FileRef[]>> = {};
  const fileRef = (url: string): FileRef => { const m = byUrl.get(url); if (!m) throw new StoreGap(`a region names ${url}, which the manifest does not list`); return { url, sha256: m.sha256, vintage: m.vintage }; };

  // the vector layers: cells streamed one at a time, at ε and at 2ε in the same pass (§6 needs no second read)
  const kinds = (["water", "rivers", "roads", "peaks", "places"] as const).filter((k) => want(MASK[k]));
  const L1 = new LayerSet(f1), L2 = new LayerSet(f2);
  try {
    if (kinds.length) {
      const idxBytes = await store.get(keys.index);
      if (!idxBytes) return unavailable("no index");
      const index = json<StoreIndex>(idxBytes);
      for (const c of index.cells.filter((c) => boxMeets(c.reach, box)).sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0))) {
        const b = await store.get(keys.cell(c.key));
        if (!b) return unavailable(`no cell ${c.key}`);
        for (const ft of decodeCell(b)) {
          if (!kinds.includes(ft.kind as LayerKind)) continue;
          L1.add(ft.kind, ft);
          if (ft.kind === "water" || ft.kind === "rivers" || ft.kind === "roads") L2.add(ft.kind, ft);
        }
      }
      // RD-P6: a layer's files read are those of the regions whose box meets the square's
      const met = index.regions.filter((r) => boxMeets(r.box, box)).flatMap((r) => r.files);
      for (const k of ["water", "rivers", "roads", "peaks"] as const) {
        if (want(MASK[k])) read[k] = [...new Set(met)].filter((u) => byUrl.get(u)?.layer === k).sort().map(fileRef);
      }
      if (want(MASK.places)) for (const l of ["places", "population"] as const) read[l] = manifest.files.filter((f) => f.layer === l).map((f) => fileRef(f.url));
      for (const u of met) fileRef(u); // every region's file must be in the manifest, read or not
    }
  } catch (e) { if (e instanceof StoreGap) return unavailable(e.message); throw e; }

  // tiles: the summits' and the largest level's mosaic (plus its margin), each read once
  const tiles = new Map<string, Uint8Array>();
  const loadTile = async (x: number, y: number) => {
    const key = `${x}/${y}`;
    if (tiles.has(key)) return;
    const b = await store.get(keys.tile(x, y));
    if (!b) throw new StoreGap(`no tile ${key}`);
    tiles.set(key, await decodeTerrariumPng(b));
  };
  const cands = want(MASK.peaks) ? L1.summits() : [];
  let peaks: Array<{ q: Pt; elevM: number; name: string }> = [];
  const terrainSections: Section[] = [];
  try {
    for (const s of cands) { const [X, Y] = z9Pixel(s.lat, s.lon); await loadTile(X >> 8, Y >> 8); }
    const px = { rgb: (X: number, Y: number) => { const t = tiles.get(`${X >> 8}/${Y >> 8}`)!, o = ((Y & 255) * 256 + (X & 255)) * 3; return [t[o]!, t[o + 1]!, t[o + 2]!] as [number, number, number]; } };
    peaks = rankPeaks(f1, cands.map((s) => ({ ...s, elevM: summitElevation(px, s.lat, s.lon) })));
    if (levels.length && want(MASK.terrain)) {
      const [tx0, ty0, tx1, ty1] = levelTiles(f1, levels[levels.length - 1]!.radiusMm);
      for (let x = tx0; x <= tx1; x++) for (let y = ty0; y <= ty1; y++) await loadTile(x, y);
      const mosaic = mosaicOf(tiles, tx0, ty0, tx1, ty1);
      for (const lv of levels) terrainSections.push({ type: 6, levelRadiusMm: lv.radiusMm, n: lv.n, values: terrainLevel(f1, mosaic, lv.radiusMm, lv.n) });
    }
  } catch (e) { if (e instanceof StoreGap) return unavailable(e.message); throw e; }

  // the tiles read: their files (by RD17's /9/<x>/<y>.png tail) and their source bits (the credits)
  let tileInfo: ProvenanceInput["tiles"] = null;
  if (tiles.size) {
    const bitsBin = await store.get(keys.terrainSources);
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
    if (want(MASK.water)) { const w = L.waterRings(); c.water = w.rings; c.collapsed.water = w.collapsed; }
    if (want(MASK.rivers)) { c.rivers = L.selectRivers(); c.collapsed.rivers = c.rivers.collapsed; }
    if (want(MASK.roads)) { const r = L.keptRoads(); c.roads = r.roads; c.collapsed.roads = r.collapsed; }
    return c;
  };
  const at = { [q.epsMm]: content(L1), [EPS_STEP * q.epsMm]: content(L2) } as Record<number, Content>;
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
  const fitted = fitBudget(q.epsMm, opts.capBytes ?? CAP_BYTES, (tol) => at[tol]!, assemble);
  if (!fitted) return { ok: false, status: 422, code: "too_large" };
  const bytes = encode({ lat0Micro: q.lat0Micro, lon0Micro: q.lon0Micro, radiusMm: rMm, toleranceMm: fitted.toleranceMm, sections: fitted.sections });
  return { ok: true, bytes, storeId };
}

/** A store that names something it does not hold: answered as 503 basemap_unavailable, never as a partial package. */
class StoreGap extends Error {}
