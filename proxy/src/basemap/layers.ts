// The layer rules (docs/basemap-package-format.md §5; §13's RD2-RD4, RD8-RD12, RD26, RD29, RD31), applied to the
// features the ingest stored. Pure; nothing here logs, and nothing returns a coordinate but quantised ones.
//
// The store filters by attribute (ingest-lib's PREDICATES). This module re-applies every rule whose column the store
// keeps -- AWATER, FULLNAME, MTFCC -- so no rule for those lives only in the ingest, and the Bus rule lives ONLY here
// (MI1, S-IBUS). Peaks (feature_class) and places (LSAD) are filtered by the ingest alone: the store does not keep
// those columns, so there is nothing here to re-apply them to.
import {
  type Frame, type Pt, clipLine, clipRing, dedupe, inDiscLengthM, isOuter, openRing, planarDist, quantise, simplify,
  simplifyRing, toFrameKm, within3km,
} from "./frame";

/** A stored feature, as ingest-lib's decodeFeature returns it: its source-order key, attributes and lon/lat pairs. */
export interface SrcFeature { file: number; record: number; part: number; attrs: Record<string, string | number>; xy: Float64Array }

/** RD2, RD4: files by URL ascending (the manifest's order, so `file`), then record, then part. */
export const bySource = (a: SrcFeature, b: SrcFeature) => a.file - b.file || a.record - b.record || a.part - b.part;

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

/** One source part as a line: its quantised in-square runs (RD5), and how many collapsed under 2 vertices (§2). */
function lineItems(f: Frame, part: Pt[]): { items: Pt[][]; collapsed: number } {
  const km = part.map(([x, y]) => toFrameKm(f, x, y));
  const items: Pt[][] = [];
  let collapsed = 0;
  for (const run of clipLine(km, f.rKm)) {
    const q = dedupe(simplify(run, f.epsM).map((p) => quantise(p, f.rMm)));
    if (q.length < 2) collapsed++;
    else items.push(q);
  }
  return { items, collapsed };
}

// ------------------------------------------------------------------------------------------------ water (§5.1)

export const WATER_MIN_M2 = 1_500_000;

/** §5.1: AWATER >= 1,500,000 m²; each OUTER part a ring (RD26), holes dropped (RD3); RD2's order. A part that
 *  clips to nothing is outside, not collapsed; one that clips to something and quantises under 3 vertices is. */
export function keepWater(f: Frame, feats: SrcFeature[]): { rings: Pt[][]; collapsed: number } {
  const rings: Pt[][] = [];
  let collapsed = 0;
  for (const ft of [...feats].sort(bySource)) {
    if (!(Number(ft.attrs["AWATER"]) >= WATER_MIN_M2)) continue;
    const part = lonLat(ft.xy);
    if (!isOuter(part)) continue;
    const clipped = clipRing(openRing(part).map(([x, y]) => toFrameKm(f, x, y)), f.rKm);
    if (!clipped.length) continue;
    const q = dedupe(simplifyRing(clipped, f.epsM).map((p) => quantise(p, f.rMm)), true);
    if (q.length < 3) collapsed++;
    else rings.push(q);
  }
  return { rings, collapsed };
}

// ------------------------------------------------------------------------------------------------ rivers (§5.2)

export const isRiverName = (fullname: string) => fullname.endsWith(" Riv");

export interface Rivers { names: string[]; lines: Array<{ name: number; pts: Pt[] }>; collapsed: number; lengthsM: number[] }

/** §5.2, RD8, RD29: groups by FULLNAME; each group's length is the sum of its parts' raw lengths inside the disc, in
 *  source order; a group with 0 m is not a candidate; the 5 longest, ties by FULLNAME byte order; all their parts. */
export function selectRivers(f: Frame, feats: SrcFeature[]): Rivers {
  const src = feats.filter((ft) => isRiverName(String(ft.attrs["FULLNAME"] ?? ""))).sort(bySource);
  const length = new Map<string, number>();
  for (const ft of src) {
    const name = String(ft.attrs["FULLNAME"]);
    const km = lonLat(ft.xy).map(([x, y]) => toFrameKm(f, x, y));
    length.set(name, (length.get(name) ?? 0) + inDiscLengthM(km, f.rKm));
  }
  const ranked = [...length].filter(([, m]) => m > 0).sort((a, b) => b[1] - a[1] || byteCmp(a[0], b[0])).slice(0, 5);
  const names = ranked.map(([n]) => n);
  const index = new Map(names.map((n, i) => [n, i]));
  const lines: Rivers["lines"] = [];
  let collapsed = 0;
  for (const ft of src) {
    const i = index.get(String(ft.attrs["FULLNAME"]));
    if (i === undefined) continue;
    const r = lineItems(f, lonLat(ft.xy));
    collapsed += r.collapsed;
    for (const pts of r.items) lines.push({ name: i, pts });
  }
  return { names, lines, collapsed, lengthsM: ranked.map(([, m]) => m) };
}

// ------------------------------------------------------------------------------------------------ roads (§5.3)

const BUS = /(^|[^A-Za-z0-9])Bus($|[^A-Za-z0-9])/;

/** §5.3: "Bus" as a whole word, case-sensitive. A missing FULLNAME is not a business route. */
export const isBusRoute = (fullname: string | undefined) => fullname !== undefined && BUS.test(fullname);

/** §5.3: S1100 is class 1, S1200 class 2; anything else is not a road here. */
export const roadClass = (mtfcc: string): 1 | 2 | null => (mtfcc === "S1100" ? 1 : mtfcc === "S1200" ? 2 : null);

/** §5.3, RD4: the kept roads in source order, their in-square runs as separate items (RD5). */
export function keepRoads(f: Frame, feats: SrcFeature[]): { roads: Array<{ cls: 1 | 2; pts: Pt[] }>; collapsed: number; droppedBus: number } {
  const roads: Array<{ cls: 1 | 2; pts: Pt[] }> = [];
  let collapsed = 0, droppedBus = 0;
  for (const ft of [...feats].sort(bySource)) {
    const cls = roadClass(String(ft.attrs["MTFCC"] ?? ""));
    if (cls === null) continue;
    if (isBusRoute(ft.attrs["FULLNAME"] === undefined ? undefined : String(ft.attrs["FULLNAME"]))) { droppedBus++; continue; }
    const r = lineItems(f, lonLat(ft.xy));
    collapsed += r.collapsed;
    for (const pts of r.items) roads.push({ cls, pts });
  }
  return { roads, collapsed, droppedBus };
}

// ------------------------------------------------------------------------------------------------ peaks (§5.4)

export interface Summit { fid: number; name: string; p: Pt; lat: number; lon: number }

/** §5.4, RD9, RD11, RD12: GNIS summits inside the disc, one per feature_id (the first inside it, in source order). */
export function summits(f: Frame, feats: SrcFeature[]): Summit[] {
  const out: Summit[] = [];
  const seen = new Set<number>();
  for (const ft of [...feats].sort(bySource)) {
    const fid = Number(ft.attrs["feature_id"]);
    if (seen.has(fid)) continue;
    const lon = ft.xy[0]!, lat = ft.xy[1]!;
    const p = toFrameKm(f, lon, lat);
    if (!withinMm(p, f.rMm)) continue;
    seen.add(fid);
    out.push({ fid, name: String(ft.attrs["feature_name"] ?? ""), p, lat, lon });
  }
  return out;
}

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

// ------------------------------------------------------------------------------------------------ places (§5.5)

/** §5.5: places within R (inclusive), in the file's order, with the population the ingest joined (0 = none). */
export function keepPlaces(f: Frame, feats: SrcFeature[]): Array<{ q: Pt; pop: number; name: string }> {
  const places: Array<{ q: Pt; pop: number; name: string }> = [];
  for (const ft of [...feats].sort(bySource)) {
    const p = toFrameKm(f, ft.xy[0]!, ft.xy[1]!);
    if (!withinMm(p, f.rMm)) continue;
    places.push({ q: quantise(p, f.rMm), pop: Number(ft.attrs["pop"] ?? 0), name: String(ft.attrs["NAME"] ?? "") });
  }
  return places;
}
