// The layer rules (docs/basemap-package-format.md §5; §13's RD2-RD4, RD8-RD12, RD26, RD29, RD31), applied to the
// features the ingest stored. Pure; nothing here logs, and nothing returns a coordinate but quantised ones.
//
// The store filters by attribute (ingest-lib's PREDICATES). This module re-applies every rule whose column the store
// keeps -- AWATER, FULLNAME, MTFCC -- so no rule for those lives only in the ingest, and the Bus rule lives ONLY here
// (MI1, S-IBUS). Peaks (feature_class) and places (LSAD) are filtered by the ingest alone: the store does not keep
// those columns, so there is nothing here to re-apply them to.
//
// ONE PATH. LayerSet does the per-feature work as features stream in, cell by cell, keeping only quantised output and
// each feature's source-order key (so a 222 km square never holds its raw vertices); its finishing methods put
// everything in RD2's order. The list functions the ML rows bind to (keepWater, selectRivers, ...) are LayerSet over
// a list, so the rows test the code build.ts runs.
import {
  type Frame, type Pt, clipLine, clipRing, dedupe, inDiscLengthM, isOuter, openRing, planarDist, quantise, simplify,
  simplifyRing, toFrameKm, within3km,
} from "./frame";

/** A stored feature, as store.ts's decodeFeature returns it: its source-order key, attributes and lon/lat pairs. */
export interface SrcFeature { file: number; record: number; part: number; attrs: Record<string, string | number>; xy: Float64Array }
export type LayerKind = "water" | "rivers" | "roads" | "peaks" | "places";
interface Key { file: number; record: number; part: number }

/** RD2, RD4: files by URL ascending (the manifest's order, so `file`), then record, then part. */
export const bySource = (a: Key, b: Key) => a.file - b.file || a.record - b.record || a.part - b.part;
const keyOf = (ft: SrcFeature): Key => ({ file: ft.file, record: ft.record, part: ft.part });

const lonLat = (xy: Float64Array): Pt[] => {
  const out: Pt[] = [];
  for (let i = 0; i + 1 < xy.length; i += 2) out.push([xy[i]!, xy[i + 1]!]);
  return out;
};

const te = new TextEncoder();
/** Byte order of the UTF-8 forms (RD8's tie-break), which JS's UTF-16 comparison is not above U+FFFF. */
function byteCmp(a: string, b: string): number {
  const x = te.encode(a), y = te.encode(b);
  for (let i = 0; i < Math.min(x.length, y.length); i++) if (x[i] !== y[i]) return x[i]! - y[i]!;
  return x.length - y.length;
}

/** RD9 + RD31: within R of the origin, inclusive, sqrt(E*E + S*S) <= R in km. The one disc test (summits, places). */
export const withinMm = (p: Pt, rMm: number) => planarDist(p[0], p[1]) <= rMm / 1e6;

export const WATER_MIN_M2 = 1_500_000;
export const isRiverName = (fullname: string) => fullname.endsWith(" Riv");

const BUS = /(^|[^A-Za-z0-9])Bus($|[^A-Za-z0-9])/;
/** §5.3: "Bus" as a whole word, case-sensitive. A missing FULLNAME is not a business route. */
export const isBusRoute = (fullname: string | undefined) => fullname !== undefined && BUS.test(fullname);
/** §5.3: S1100 is class 1, S1200 class 2; anything else is not a road here. */
export const roadClass = (mtfcc: string): 1 | 2 | null => (mtfcc === "S1100" ? 1 : mtfcc === "S1200" ? 2 : null);

export interface Rivers { names: string[]; lines: Array<{ name: number; pts: Pt[] }>; collapsed: number; lengthsM: number[] }
export interface Summit { fid: number; name: string; p: Pt; lat: number; lon: number }
export type Road = { cls: 1 | 2; pts: Pt[] };
export type Place = { q: Pt; pop: number; name: string };

export class LayerSet {
  private water: Array<Key & { ring: Pt[] | null }> = [];
  private rivers: Array<Key & { name: string; lengthM: number; items: Pt[][]; collapsed: number }> = [];
  private roads: Array<Key & { cls: 1 | 2; items: Pt[][]; collapsed: number }> = [];
  private peaks: Array<Key & Summit> = [];
  private places: Array<Key & Place> = [];
  droppedBus = 0;
  constructor(readonly f: Frame) {}

  /** One part as a line: its quantised in-square runs (RD5), and how many collapsed under 2 vertices (§2). */
  private lineItems(km: Pt[]): { items: Pt[][]; collapsed: number } {
    const items: Pt[][] = [];
    let collapsed = 0;
    for (const run of clipLine(km, this.f.rKm)) {
      const q = dedupe(simplify(run, this.f.epsM).map((p) => quantise(p, this.f.rMm)));
      if (q.length < 2) collapsed++;
      else items.push(q);
    }
    return { items, collapsed };
  }
  private km(ft: SrcFeature): Pt[] { return lonLat(ft.xy).map(([x, y]) => toFrameKm(this.f, x, y)); }

  add(kind: LayerKind, ft: SrcFeature): void {
    const f = this.f;
    switch (kind) {
      case "water": {
        // §5.1: AWATER >= 1,500,000 m²; OUTER parts only (RD26), so holes are dropped (RD3). A part that clips to
        // nothing is outside, not collapsed; one that clips to something and quantises under 3 vertices is.
        if (!(Number(ft.attrs["AWATER"]) >= WATER_MIN_M2)) return;
        const part = lonLat(ft.xy);
        if (!isOuter(part)) return;
        const clipped = clipRing(openRing(part).map(([x, y]) => toFrameKm(f, x, y)), f.rKm);
        if (!clipped.length) return;
        const q = dedupe(simplifyRing(clipped, f.epsM).map((p) => quantise(p, f.rMm)), true);
        this.water.push({ ...keyOf(ft), ring: q.length < 3 ? null : q });
        return;
      }
      case "rivers": {
        const name = String(ft.attrs["FULLNAME"] ?? "");
        if (!isRiverName(name)) return;
        const km = this.km(ft);
        const lengthM = inDiscLengthM(km, this.f.rKm);
        this.rivers.push({ ...keyOf(ft), name, lengthM, ...this.lineItems(km) });
        return;
      }
      case "roads": {
        const cls = roadClass(String(ft.attrs["MTFCC"] ?? ""));
        if (cls === null) return;
        if (isBusRoute(ft.attrs["FULLNAME"] === undefined ? undefined : String(ft.attrs["FULLNAME"]))) { this.droppedBus++; return; }
        this.roads.push({ ...keyOf(ft), cls, ...this.lineItems(this.km(ft)) });
        return;
      }
      case "peaks": {
        const lon = ft.xy[0]!, lat = ft.xy[1]!, p = toFrameKm(f, lon, lat);
        if (!withinMm(p, f.rMm)) return;
        this.peaks.push({ ...keyOf(ft), fid: Number(ft.attrs["feature_id"]), name: String(ft.attrs["feature_name"] ?? ""), p, lat, lon });
        return;
      }
      case "places": {
        const p = toFrameKm(f, ft.xy[0]!, ft.xy[1]!);
        if (!withinMm(p, f.rMm)) return;
        this.places.push({ ...keyOf(ft), q: quantise(p, f.rMm), pop: Number(ft.attrs["pop"] ?? 0), name: String(ft.attrs["NAME"] ?? "") });
        return;
      }
    }
  }

  /** §5.1, RD2: the rings in source order, and how many collapsed. */
  waterRings(): { rings: Pt[][]; collapsed: number } {
    const recs = [...this.water].sort(bySource);
    return { rings: recs.flatMap((r) => (r.ring ? [r.ring] : [])), collapsed: recs.filter((r) => !r.ring).length };
  }

  /** §5.2, RD8, RD29: groups by FULLNAME; each group's length is the sum of its parts' raw lengths inside the disc,
   *  in source order; a group with 0 m is not a candidate; the 5 longest, ties by FULLNAME byte order; all their parts. */
  selectRivers(): Rivers {
    const src = [...this.rivers].sort(bySource);
    const length = new Map<string, number>();
    for (const r of src) length.set(r.name, (length.get(r.name) ?? 0) + r.lengthM);
    const ranked = [...length].filter(([, m]) => m > 0).sort((a, b) => b[1] - a[1] || byteCmp(a[0], b[0])).slice(0, 5);
    const names = ranked.map(([n]) => n);
    const index = new Map(names.map((n, i) => [n, i]));
    const lines: Rivers["lines"] = [];
    let collapsed = 0;
    for (const r of src) {
      const i = index.get(r.name);
      if (i === undefined) continue;
      collapsed += r.collapsed;
      for (const pts of r.items) lines.push({ name: i, pts });
    }
    return { names, lines, collapsed, lengthsM: ranked.map(([, m]) => m) };
  }

  /** §5.3, RD4: the kept roads in source order, each in-square run its own item (RD5). */
  keptRoads(): { roads: Road[]; collapsed: number; droppedBus: number } {
    const src = [...this.roads].sort(bySource);
    return { roads: src.flatMap((r) => r.items.map((pts) => ({ cls: r.cls, pts }))), collapsed: src.reduce((a, r) => a + r.collapsed, 0), droppedBus: this.droppedBus };
  }

  /** §5.4, RD9, RD11, RD12: summits inside the disc, one per feature_id (the first inside it, in source order). */
  summits(): Summit[] {
    const out: Summit[] = [];
    const seen = new Set<number>();
    for (const s of [...this.peaks].sort(bySource)) {
      if (seen.has(s.fid)) continue;
      seen.add(s.fid);
      out.push({ fid: s.fid, name: s.name, p: s.p, lat: s.lat, lon: s.lon });
    }
    return out;
  }

  /** §5.5: places within R (inclusive), in the file's order, with the population the ingest joined (0 = none). */
  keptPlaces(): Place[] {
    const places = [...this.places].sort(bySource).map(({ q, pop, name }) => ({ q, pop, name }));
    return places;
  }
}

const over = (f: Frame, kind: LayerKind, feats: SrcFeature[]) => { const L = new LayerSet(f); for (const ft of feats) L.add(kind, ft); return L; };

// The list forms, which the ML and MR rows bind to by their frozen names.
export const keepWater = (f: Frame, feats: SrcFeature[]) => over(f, "water", feats).waterRings();
export const selectRivers = (f: Frame, feats: SrcFeature[]) => over(f, "rivers", feats).selectRivers();
export const keepRoads = (f: Frame, feats: SrcFeature[]) => over(f, "roads", feats).keptRoads();
export const summits = (f: Frame, feats: SrcFeature[]) => over(f, "peaks", feats).summits();
export const keepPlaces = (f: Frame, feats: SrcFeature[]) => over(f, "places", feats).keptPlaces();

export const PEAKS_KEPT = 16;

/** §5.4: ranked by elevation descending, then feature_id ascending; a summit within 3.0 km (RD9: <=) of a
 *  higher-ranked KEPT summit is dropped; the first 16 survivors. */
export function rankPeaks(f: Frame, cands: Array<Summit & { elevM: number }>): Array<{ q: Pt; elevM: number; name: string }> {
  const ranked = [...cands].sort((a, b) => b.elevM - a.elevM || a.fid - b.fid);
  const kept: Array<Summit & { elevM: number }> = [];
  for (const c of ranked) {
    if (kept.some((k) => within3km(c.p[0] - k.p[0], c.p[1] - k.p[1]))) continue;
    kept.push(c);
    if (kept.length === PEAKS_KEPT) break;
  }
  return kept.map((k) => ({ q: quantise(k.p, f.rMm), elevM: k.elevM, name: k.name }));
}
