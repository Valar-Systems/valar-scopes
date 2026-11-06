// The basemap ingest's source list (docs/basemap-route.md §2.2, A1.11). PURE: no node:* imports, so the
// enumeration is testable in the workers pool. The CLI (scripts/ingest-basemap.ts) does the fetching.
//
// Every URL here is an exact fetch URL, because the manifest records it and PROVENANCE's source.<layer>
// and sha256.<layer> are computed from it (format doc §7 at 1efb660). A file's identity is its URL.

/** The seven package layer keys (format §7) and the three ingest-only keys (F2, ruled 2026-10-28). */
export const PACKAGE_LAYERS = ["water", "rivers", "roads", "peaks", "places", "population", "terrain"] as const;
export const STORE_ONLY_LAYERS = ["states", "counties", "naturalearth"] as const;
export type Layer = (typeof PACKAGE_LAYERS)[number] | (typeof STORE_ONLY_LAYERS)[number];

/** `note`: why this file's URL is not the usual one (a mirror, ruled 2026-10-28). It becomes the manifest's
 *  "<url>: <note>" -- taken from the PLAN, never from a run, so a re-ingest reproduces it and no clock enters. */
export interface SourceFile { layer: Layer; url: string; note?: string }

/** v1 coverage is the 50 states + DC (BQ4): every state FIPS code except the territories'. */
export const TERRITORY_FIPS = new Set(["60", "66", "69", "72", "78"]);
/** GNIS names its per-state files by postal code; these are the territories' (BQ4). */
export const TERRITORY_POSTAL = new Set(["AS", "GU", "MP", "PR", "VI"]);

const TIGER = "https://www2.census.gov/geo/tiger/TIGER2024/";
const GNIS = "https://prd-tnm.s3.amazonaws.com/StagedProducts/GeographicNames/DomesticNames/";

export const FIXED_SOURCES: readonly SourceFile[] = [
  { layer: "places", url: "https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip" },
  { layer: "population", url: "https://www2.census.gov/programs-surveys/popest/datasets/2020-2024/cities/totals/sub-est2024.csv" },
  { layer: "states", url: `${TIGER}STATE/tl_2024_us_state.zip` },
  { layer: "counties", url: `${TIGER}COUNTY/tl_2024_us_county.zip` },
  { layer: "naturalearth", url: "https://naciscdn.org/naturalearth/10m/physical/ne_10m_land.zip" },
  { layer: "naturalearth", url: "https://naciscdn.org/naturalearth/10m/physical/ne_10m_ocean.zip" },
  { layer: "naturalearth", url: "https://naciscdn.org/naturalearth/10m/cultural/ne_10m_admin_0_countries.zip" },
];

/** The TIGER directory a layer is published in, and the file-name pattern its listing uses. */
export const TIGER_DIRS = {
  water: { dir: "AREAWATER", re: /href="(tl_2024_(\d{2})\d{3}_areawater\.zip)"/g },
  rivers: { dir: "LINEARWATER", re: /href="(tl_2024_(\d{2})\d{3}_linearwater\.zip)"/g },
  roads: { dir: "PRISECROADS", re: /href="(tl_2024_(\d{2})_prisecroads\.zip)"/g },
} as const;

/** File URLs from a TIGER directory listing (Apache HTML), territories dropped, sorted by URL (RD2's order). */
export function tigerFromListing(layer: keyof typeof TIGER_DIRS, html: string): SourceFile[] {
  const { dir, re } = TIGER_DIRS[layer];
  const seen = new Set<string>();
  for (const m of html.matchAll(re)) if (!TERRITORY_FIPS.has(m[2]!)) seen.add(m[1]!);
  return [...seen].sort().map((f) => ({ layer, url: `${TIGER}${dir}/${f}` }));
}

export const tigerListingUrl = (layer: keyof typeof TIGER_DIRS) => `${TIGER}${TIGER_DIRS[layer].dir}/`;
export const GNIS_LISTING_URL = `https://prd-tnm.s3.amazonaws.com/?prefix=StagedProducts/GeographicNames/DomesticNames/&max-keys=1000`;

/** GNIS per-state text files from the bucket's ListBucketResult XML, territories dropped, sorted by URL.
 *  Refuses a truncated listing: a partial list would silently drop states. */
export function gnisFromListing(xml: string): SourceFile[] {
  if (/<IsTruncated>true<\/IsTruncated>/.test(xml)) throw new Error("gnis listing truncated: refusing a partial state list");
  const out: SourceFile[] = [];
  for (const block of xml.split("<Contents>").slice(1)) {
    const key = /<Key>([^<]+)<\/Key>/.exec(block)?.[1];
    const m = key && /\/DomesticNames_([A-Z]{2})_Text\.zip$/.exec(key);
    if (m && !TERRITORY_POSTAL.has(m[1]!)) out.push({ layer: "peaks", url: `${GNIS}DomesticNames_${m[1]}_Text.zip` });
  }
  return out.sort((a, b) => (a.url < b.url ? -1 : a.url > b.url ? 1 : 0));
}

/** A file's vintage (RD-P3, ruled): the year in its dataset's name where it has one, otherwise the year of
 *  its Last-Modified as fetched. A missing Last-Modified is refused, never defaulted: a guessed year
 *  would be a plausible lie in PROVENANCE. */
export function vintageOf(f: SourceFile, lastModified: string | null): string {
  if (/\/TIGER2024\//.test(f.url)) return "2024";
  if (/\/2025_Gazetteer\//.test(f.url)) return "2025";
  if (/sub-est2024\.csv$/.test(f.url)) return "2024";
  const t = lastModified ? Date.parse(lastModified) : NaN;
  if (!Number.isFinite(t)) throw new Error(`no usable Last-Modified for ${f.url}`);
  return String(new Date(t).getUTCFullYear());
}

/** Whether a response body is the file it claims to be, by its own first bytes. census.gov's firewall
 *  answers a rejected request with a 247-byte HTML page under status 200 -- it once overwrote two good zips,
 *  logged with a size and a sha256 as if it were a new version. So status and length prove nothing; the
 *  magic does. Returns null when good, or why not. */
export function checkBody(url: string, contentType: string | null, head: Uint8Array): string | null {
  if (contentType && /text\/html/i.test(contentType)) return `served as ${contentType} (a rejection page?)`;
  const starts = (...m: number[]) => m.every((v, i) => head[i] === v);
  if (/\.zip$/i.test(url)) return starts(0x50, 0x4b, 0x03, 0x04) || starts(0x50, 0x4b, 0x05, 0x06) ? null : "not a zip (no PK header)";
  if (/\.png$/i.test(url)) return starts(0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a) ? null : "not a PNG";
  if (/\.csv$/i.test(url)) return head.length > 0 && head[0] !== 0x3c ? null : "a CSV starting with '<' (HTML?)";
  return `no magic check for ${url}`;
}

/** The URL of one Terrarium z9 tile, in RD17's form. */
export const terrariumUrl = (x: number, y: number) => `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/9/${x}/${y}.png`;
