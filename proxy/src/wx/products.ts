// The weather product registry: the ONE place a product is named (docs/wx-server.md, "The product
// parameter"). Every key, URL, ETag and cache key is built from a Product, never from a literal, so a
// second product -- Weatherscope's, say -- is a row here plus its code mapping and nothing else.

import type { GridGeom } from "./grid";
import type { OverrunPx } from "./opera";

/** A rectangle in degrees, every bound inclusive: today's UK land box (Q-UKBOX) and each OPERA product's rule. */
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
  rule?: LandBox;                // OPERA: its rectangle in the region's ordered rule list, first match wins
                                 // (docs/wx-opera-europe.md §3). Read only by scripts/gen-wx-region.mjs, never per request
  overrunPx?: OverrunPx;         // OPERA: how far its window may pass the raster (docs/wx-opera-dev-measure.md)
  deviceMaxAgeS?: number;        // the age the device may draw this product's frames to, sent as X-Wx-Max-Age
                                 // (docs/wx-age-limit.md). Absent: none is sent and the device keeps its 600 s.
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

/** The UK land box (docs/uk-data.md §1.3, docs/wx-opuk.md), served by opuk whatever the polygons say (Q-UKBOX), so no
 *  UK point that got opuk before the region table loses it. The margin: the radar's largest view (2.0 deg, ProjectAt's
 *  clamp) and the crop's 2-cell pad. A product's grid is the box of its rule's served land grown by the margin. */
export const OPUK_LAND: LandBox = { latS: 49.8, latN: 60.9, lonW: -8.7, lonE: 1.8 };
export const OPUK_MARGIN = 2.02;
/** opuk's grid, the British Isles (docs/wx-opera-code-stage.md §1a): 1,585 rows x 1,954 cols, 16 WXG1 tiles. */
export const OPUK_GRID: GridGeom = { la1: 62.92, lo1: -15.71, di: 0.01, dj: 0.01, ni: 1954, nj: 1585 };
/** opuk's grid until the code stage, the land box plus the margin: 1,515 x 1,455, 9 tiles. The stored OPERA fixtures
 *  (test/fixtures/wx/opera/) hold only this grid's window's ranges, so the tests that read them build on it. */
export const OPUK_FIXTURE_GRID: GridGeom = { la1: 62.92, lo1: -10.72, di: 0.01, dj: 0.01, ni: 1455, nj: 1515 };

/** An OPERA product: opuk's row as ruled, but for its id, rule and grid (docs/wx-opera-europe.md §3), and the overrun
 *  its window was measured to need (docs/wx-opera-europe/eu_window_edge.py). A1 holds every row to the two ages. */
const opera = (id: string, rule: LandBox, g: Omit<GridGeom, "di" | "dj">, overrunPx?: OverrunPx): Product => ({
  id,
  source: "opera",
  noaaPrefix: "",
  noaaStem: "",
  cadenceS: 300,
  maxAgeS: 1800,               // Q4, ruled: above the device's 600 s, so a late frame is a 200 the device hides
  deviceMaxAgeS: 720,          // B, ruled 2026-11-04: R1's week blanks 0.69 % at 720 s against 5.51 % at 600 s
  codeOf: codeOfComposite,
  grid: { ...g, di: 0.01, dj: 0.01 },
  rule,
  ...(overrunPx ? { overrunPx } : {}),
});
const box = (lonW: number, latS: number, lonE: number, latN: number): LandBox => ({ latS, latN, lonW, lonE });

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
  // The OPERA products in rule order: a served point takes the first rule that holds it. The order is the region
  // table's product index (G1), so a row moved here is a regenerated table.
  opera("opuk", box(-15.0, 49.1, 1.8, 61.5), OPUK_GRID),                                                   // isles
  opera("opis", box(-25.0, 62.0, -7.0, 72.0), { la1: 73.2, lo1: -26.56, ni: 2065, nj: 1183 }, { n: 242 }),   // iceland
  opera("opib", box(-10.0, 35.1, 4.5, 43.8), { la1: 45.82, lo1: -11.52, ni: 1805, nj: 1268 }, { s: 107, w: 65 }), // iberia
  opera("opmt", box(14.0, 35.7, 15.0, 36.2), { la1: 38.1, lo1: 12.16, ni: 444, nj: 433 }, { s: 43 }),        // malta
  opera("opwe", box(-6.0, 41.3, 10.5, 53.7), { la1: 55.72, lo1: -7.15, ni: 1968, nj: 1638 }),               // west
  opera("opce", box(10.5, 47.0, 24.2, 55.1), { la1: 57.12, lo1: 8.48, ni: 1775, nj: 1215 }),                // central
  opera("opse", box(10.5, 41.8, 30.5, 48.6), { la1: 50.51, lo1: 8.48, ni: 2368, nj: 1069 }, { e: 32 }),      // southeast
  opera("opba", box(16.0, 53.5, 32.0, 63.0), { la1: 65.02, lo1: 13.98, ni: 1962, nj: 1316 }),               // baltic
  opera("opsw", box(4.0, 53.5, 16.0, 63.0), { la1: 65.02, lo1: 2.62, ni: 1541, nj: 1335 }),                 // scandisw
  opera("opsn", box(4.0, 63.0, 32.0, 71.3), { la1: 73.19, lo1: 5.16, ni: 2832, nj: 1222 }, { n: 54 }),       // scandin
];

export function productById(id: string): Product | null {
  if (!PRODUCT_ID_RE.test(id)) return null;
  return PRODUCTS.find((p) => p.id === id) ?? null;
}

/** The products /wx/cref may be answered from. (/healthz reports EVERY product, each marked routed or not from this
 *  set: docs/wx-healthz-products.md.) An OPERA product joins only after its
 *  ingest runs and its `latest` has been read (docs/wx-opera-europe.md §3, the rollout order): routed before that, a
 *  point that gets cref's 200 today would get a 503 wx_stale from a product with no frame. Each addition is a proxy
 *  deploy of its own. */
export const ROUTED: ReadonlySet<string> = new Set(["cref", "opuk"]);

// ---- every name derived from a product, in one place ---------------------------------------------

export const frameKey = (p: Product, epoch: number) => `wx/${p.id}/frames/${epoch}.wxg`;
export const framesPrefix = (p: Product) => `wx/${p.id}/frames/`;
export const latestKey = (p: Product) => `wx/${p.id}/latest`;
/** Stage 1c (docs/wx-opera-map-r2.md): an OPERA product's resample map, named by a hash of its geometry key. */
export const mapObjectKey = (p: Product, hash: string) => `wx/${p.id}/map/${hash}.wxm`;
/** The tile cache's key (off until Stage 2 decides from measurements; docs/wx-server.md). */
export const tileCacheKey = (p: Product, epoch: number, ty: number, tx: number) =>
  `https://wx-cache.internal/${p.id}/${epoch}/t/${ty}-${tx}`;
/** One ETag means one crop: product, frame, and the QUANTISED query the crop was computed from. */
export const cropEtag = (p: Product, epoch: number, lat2: string, lon2: string, rKm: number) =>
  `"wx1-${p.id}-${epoch}-${lat2}_${lon2}_${rKm}"`;
