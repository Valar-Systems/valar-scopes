// The weather product registry: the ONE place a product is named (docs/wx-server.md, "The product
// parameter"). Every key, URL, ETag and cache key is built from a Product, never from a literal, so a
// second product -- Weatherscope's, say -- is a row here plus its code mapping and nothing else.

export interface Product {
  id: string;                    // ^[a-z0-9]{2,8}$ -- appears in R2 keys, the URL path and the ETag
  noaaPrefix: string;            // folder in the noaa-mrms-pds bucket, without the day
  noaaStem: string;              // file name up to the "_<YYYYMMDD>-<HHMMSS>.grib2.gz" suffix
  cadenceS: number;              // how often NOAA publishes a frame
  maxAgeS: number;               // older than this by the Worker's clock -> 503 wx_stale
  codeOf: (dbz: number) => number; // decoded value -> WXC1 code 0-15
}

export const PRODUCT_ID_RE = /^[a-z0-9]{2,8}$/;

/** MergedReflectivityQCComposite: A0's ruling (docs/wx-spike-2026-10.md). -99 is "no echo inside
 *  coverage", not missing; -999 is no coverage; code 15 is unused by this product. */
function codeOfComposite(v: number): number {
  if (v === -999) return 14;
  if (v < 15) return 0;
  const c = 1 + Math.floor((v - 15) / 5);
  return c > 13 ? 13 : c;
}

export const PRODUCTS: readonly Product[] = [
  {
    id: "cref",
    noaaPrefix: "CONUS/MergedReflectivityQCComposite_00.50",
    noaaStem: "MRMS_MergedReflectivityQCComposite_00.50",
    cadenceS: 120,
    maxAgeS: 600,
    codeOf: codeOfComposite,
  },
];

export function productById(id: string): Product | null {
  if (!PRODUCT_ID_RE.test(id)) return null;
  return PRODUCTS.find((p) => p.id === id) ?? null;
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
