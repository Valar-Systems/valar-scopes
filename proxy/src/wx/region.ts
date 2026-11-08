// Which product answers /wx/cref at a point (docs/wx-opera-europe.md §2; docs/wx-opera-code-stage.md §1c). The served
// region is a generated table (region.generated.ts, from scripts/gen-wx-region.mjs): per 0.01 deg lattice row, runs of
// columns and the OPERA product that serves them. No polygon and no projection runs per request -- a row, then a
// binary search of its runs -- and the request's point IS a lattice point, because the server answers from the
// quantised point alone (crop.ts quantise()).

import { type Product, PRODUCTS, ROUTED, productById } from "./products";
import { REGION_HEADER, REGION_ROW_INDEX, REGION_RUNS } from "./region.generated";

export interface RegionHeader {
  readonly rules: readonly { readonly id: string }[];
  readonly row0: number; readonly rows: number; readonly col0: number; readonly runs: number;
}

export interface RegionTable {
  row0: number;           // the northmost row, hundredths of a degree
  rows: number;
  col0: number;           // the westmost column, hundredths of a degree
  ids: readonly string[]; // a run's product index -> product id (the header's rules, in order)
  rowIndex: Uint32Array;  // rows + 1: row i's runs are [rowIndex[i], rowIndex[i + 1])
  runs: Uint16Array;      // (first col, last col, product index) triples, columns from col0
}

function fromBase64(s: string): Uint8Array {
  const bin = atob(s);
  const u = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
  return u;
}

/** The table, decoded and held to its header: a size that disagrees is a refused table, never a shorter one. */
export function decodeRegion(h: RegionHeader = REGION_HEADER, rowIndexB64 = REGION_ROW_INDEX,
                             runsB64 = REGION_RUNS): RegionTable {
  const ri = fromBase64(rowIndexB64), ru = fromBase64(runsB64);
  if (ri.length !== (h.rows + 1) * 4 || ru.length !== h.runs * 6) throw new Error("region table: its sizes disagree with its header");
  const dvi = new DataView(ri.buffer), rowIndex = new Uint32Array(h.rows + 1);
  for (let i = 0; i <= h.rows; i++) rowIndex[i] = dvi.getUint32(i * 4, true);
  const dvr = new DataView(ru.buffer), runs = new Uint16Array(h.runs * 3);
  for (let i = 0; i < h.runs * 3; i++) runs[i] = dvr.getUint16(i * 2, true);
  if (rowIndex[0] !== 0 || rowIndex[h.rows] !== h.runs) throw new Error("region table: its row index does not span its runs");
  return { row0: h.row0, rows: h.rows, col0: h.col0, ids: h.rules.map((r) => r.id), rowIndex, runs };
}

// Decoded on the first /wx/cref an isolate answers, not at module load: most proxy requests never ask.
let table: RegionTable | null = null;
export const regionTable = (): RegionTable => (table ??= decodeRegion());

/** The OPERA product id the table gives a QUANTISED point, or null where the region does not reach. */
export function regionProductId(lat: number, lon: number, t: RegionTable = regionTable()): string | null {
  const i = t.row0 - Math.round(lat * 100);
  if (!(i >= 0 && i < t.rows)) return null;
  const c = Math.round(lon * 100) - t.col0;
  let lo = t.rowIndex[i]!, hi = t.rowIndex[i + 1]! - 1;
  while (lo <= hi) {
    const m = (lo + hi) >> 1;
    if (c < t.runs[m * 3]!) hi = m - 1;
    else if (c > t.runs[m * 3 + 1]!) lo = m + 1;
    else return t.ids[t.runs[m * 3 + 2]!]!;
  }
  return null;
}

export interface DispatchOpts {
  /** D2s's and D3s-b's sabotage only: dispatch by a product's grid rectangle instead of the region. */
  byWindow?: boolean;
  /** The products a point may be answered from; production's is ROUTED. The D rows grade the table with all of them. */
  routed?: ReadonlySet<string>;
  /** A table other than the committed one (tests only). */
  table?: RegionTable;
}

const insideGrid = (p: Product, lat: number, lon: number) => {
  const g = p.grid!;
  return lat <= g.la1 && lat >= g.la1 - (g.nj - 1) * g.dj && lon >= g.lo1 && lon <= g.lo1 + (g.ni - 1) * g.di;
};

/** Q2 as ruled, by region: /wx/cref at a QUANTISED point inside the region is answered from the product the table
 *  gives it, when that product is routed; anywhere else it is cref's, as before. A request naming any other product
 *  is served by the product it names. */
export function servedProduct(requested: Product, lat: number, lon: number, o: DispatchOpts = {}): Product {
  if (requested.id !== "cref") return requested;
  const id = o.byWindow
    ? PRODUCTS.find((p) => p.source === "opera" && p.grid && insideGrid(p, lat, lon))?.id ?? null
    : regionProductId(lat, lon, o.table);
  if (id === null || !(o.routed ?? ROUTED).has(id)) return requested;
  return productById(id) ?? requested;
}
