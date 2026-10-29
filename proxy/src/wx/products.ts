// The weather product registry: the ONE place a product is named (docs/wx-server.md, "The product
// parameter"). Every key, URL, ETag and cache key is built from a Product, never from a literal, so a
// second product -- Weatherscope's, say -- is a row here plus its code mapping and nothing else.

import type { GridGeom } from "./grid";

/** Where /wx/cref is answered from a regional product instead (docs/wx-opuk.md, K5): inclusive bounds, degrees. */
export interface LandBox { latS: number; latN: number; lonW: number; lonE: number }

export interface Product {
  id: string;                    // ^[a-z0-9]{2,8}$ -- appears in R2 keys, the URL path and the ETag
  source: "mrms" | "opera";      // which ingest path builds its frames
  noaaPrefix: string;            // MRMS: folder in the noaa-mrms-pds bucket, without the day ("" otherwise)
  noaaStem: string;              // MRMS: file name up to the "_<YYYYMMDD>-<HHMMSS>.grib2.gz" suffix ("" otherwise)
  cadenceS: number;              // how often the source publishes a frame
  maxAgeS: number;               // older than this by the Worker's clock -> 503 wx_stale
  codeOf: (dbz: number) => number; // decoded value -> WXC1 code 0-15
  grid?: GridGeom;               // OPERA: the stored 0.01 deg grid (MRMS frames carry their own)
  landBox?: LandBox;             // OPERA: where /wx/cref is served from this product
}

export const PRODUCT_ID_RE = /^[a-z0-9]{2,8}$/;

/** MergedReflectivityQCComposite: A0's ruling (docs/wx-spike-2026-10.md). -99 is "no echo inside
 *  coverage", not missing; -999 is no coverage; code 15 is unused by this product. OPERA's DBZH is the same kind of
 *  quantity and takes the same bins (resample.ts handles its own no-coverage and undetect values first). */
export function codeOfComposite(v: number): number {
  if (v === -999) return 14;
  if (v < 15) return 0;
  const c = 1 + Math.floor((v - 15) / 5);
  return c > 13 ? 13 : c;
}

/** opuk (docs/uk-data.md §1.3, docs/wx-opuk.md): the UK land box, and its grid: the box plus the radar's largest view
 *  (2.0 deg, ProjectAt's clamp) and the crop's 2-cell pad, at 0.01 deg. 1,515 rows x 1,455 cols, 9 WXG1 tiles. */
export const OPUK_LAND: LandBox = { latS: 49.8, latN: 60.9, lonW: -8.7, lonE: 1.8 };
export const OPUK_MARGIN = 2.02;
export const OPUK_GRID: GridGeom = { la1: 62.92, lo1: -10.72, di: 0.01, dj: 0.01, ni: 1455, nj: 1515 };

export const PRODUCTS: readonly Product[] = [
  {
    id: "cref",
    source: "mrms",
    noaaPrefix: "CONUS/MergedReflectivityQCComposite_00.50",
    noaaStem: "MRMS_MergedReflectivityQCComposite_00.50",
    cadenceS: 120,
    maxAgeS: 600,
    codeOf: codeOfComposite,
  },
  {
    id: "opuk",
    source: "opera",
    noaaPrefix: "",
    noaaStem: "",
    cadenceS: 300,
    maxAgeS: 1800,               // Q4, ruled: above the device's 600 s, so a late frame is a 200 the device hides
    codeOf: codeOfComposite,
    grid: OPUK_GRID,
    landBox: OPUK_LAND,
  },
];

export function productById(id: string): Product | null {
  if (!PRODUCT_ID_RE.test(id)) return null;
  return PRODUCTS.find((p) => p.id === id) ?? null;
}

export const inLandBox = (b: LandBox, lat: number, lon: number) =>
  lat >= b.latS && lat <= b.latN && lon >= b.lonW && lon <= b.lonE;

/** Q2 as ruled: /wx/cref is served from a regional product when the QUANTISED point lies in its land box; every other
 *  request is served by the product it names. `byWindow` exists for K5's sabotage only. */
export function servedProduct(requested: Product, lat: number, lon: number, byWindow = false): Product {
  if (requested.id !== "cref") return requested;
  for (const p of PRODUCTS) {
    if (!p.landBox || !p.grid) continue;
    const g = p.grid;
    const hit = byWindow
      ? lat <= g.la1 && lat >= g.la1 - (g.nj - 1) * g.dj && lon >= g.lo1 && lon <= g.lo1 + (g.ni - 1) * g.di
      : inLandBox(p.landBox, lat, lon);
    if (hit) return p;
  }
  return requested;
}

// ---- every name derived from a product, in one place ---------------------------------------------

export const frameKey = (p: Product, epoch: number) => `wx/${p.id}/frames/${epoch}.wxg`;
export const framesPrefix = (p: Product) => `wx/${p.id}/frames/`;
export const latestKey = (p: Product) => `wx/${p.id}/latest`;
/** The tile cache's key (off until Stage 2 decides from measurements; docs/wx-server.md). */
export const tileCacheKey = (p: Product, epoch: number, ty: number, tx: number) =>
  `https://wx-cache.internal/${p.id}/${epoch}/t/${ty}-${tx}`;
/** One ETag means one crop: product, frame, and the QUANTISED query the crop was computed from. */
export const cropEtag = (p: Product, epoch: number, lat2: string, lon2: string, rKm: number) =>
  `"wx1-${p.id}-${epoch}-${lat2}_${lon2}_${rKm}"`;
