// The basemap ingest's source list (docs/basemap-route.md §2.2, A1.11). PURE: no node:* imports, so the
// enumeration is testable in the workers pool. The CLI (scripts/ingest-basemap.ts) does the fetching.
//
// Every URL here is an exact fetch URL, because the manifest records it and PROVENANCE's source.<layer>
// and sha256.<layer> are computed from it (format doc §7 at 1efb660). A file's identity is its URL.

/** The eight package layer keys (format §7; "sea" since Q-W4 part 2, docs/basemap-dense-code.md) and the three ingest-only keys
 *  (F2, ruled 2026-10-28). */
export const PACKAGE_LAYERS = ["water", "sea", "rivers", "roads", "peaks", "places", "population", "terrain"] as const;
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
  // The sea layer (I1, confirmed after 04:2xZ; docs/basemap-dense-code.md L1449, L1697-1698): its pieces' files are the ocean and the
  // lakes, each ONE manifest entry. The ocean moved here from "naturalearth" (a manifest cannot list a url twice), and the tile-bits
  // step finds it under "sea". The admin-0 file stays "naturalearth": the sea ingest reads it for the US containment.
  { layer: "sea", url: "https://naciscdn.org/naturalearth/10m/physical/ne_10m_ocean.zip" },
  { layer: "sea", url: "https://naciscdn.org/naturalearth/10m/physical/ne_10m_lakes.zip" },
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

/** Six counties' TIGER2025 files stand in for their TIGER2024 files, which www2.census.gov and its ftp2 mirror refused
 *  this machine (the owner's ruling, 2026-11-15; docs/basemap-dense-code.md). EXACTLY these six: the listings above stay
 *  2024's, and a 2025 file enters a plan only through with2025. The note is the manifest's provenance for each. */
const TIGER25 = "https://www2.census.gov/geo/tiger/TIGER2025/";
const CENSUS_TIGER = /^https:\/\/[a-z0-9-]+\.census\.gov\/geo\/tiger\//;   // any census host: an ftp2 TIGER url is refused too
export const TIGER2025_NOTE =
  "fetched from www2.census.gov's TIGER2025 folder because www2.census.gov and its ftp2 mirror refused this machine this county's TIGER2024 file; a mixed-vintage store: these six files 2025, every other TIGER file 2024";
const SIX: ReadonlyArray<readonly ["water" | "rivers", string]> = [
  ["water", "38021"], ["water", "39167"], ["water", "44007"], ["water", "56029"], ["rivers", "17005"], ["rivers", "19123"],
];
const tigerUrl = (base: string, year: string, layer: "water" | "rivers", geoid: string) =>
  `${base}${TIGER_DIRS[layer].dir}/tl_${year}_${geoid}_${layer === "water" ? "areawater" : "linearwater"}.zip`;
export const TIGER2025_SIX: readonly SourceFile[] = SIX.map(([layer, g]) => ({ layer, url: tigerUrl(TIGER25, "2025", layer, g), note: TIGER2025_NOTE }));
export const TIGER2025_URLS: ReadonlySet<string> = new Set(TIGER2025_SIX.map((f) => f.url));

/** The plan with each of the six's TIGER2024 counterpart replaced IN PLACE by its TIGER2025 entry (so the order is kept).
 *  Refuses a counterpart that is missing or planned more than once: either would leave the six half-applied. */
export function with2025(files: readonly SourceFile[]): SourceFile[] {
  const out = [...files];
  for (const [k, [layer, g]] of SIX.entries()) {
    const u24 = tigerUrl(TIGER, "2024", layer, g);
    const at = out.flatMap((f, i) => (f.url === u24 && f.layer === layer ? [i] : []));
    if (at.length === 0) throw new Error(`with2025: ${u24}: counterpart missing`);
    if (at.length > 1) throw new Error(`with2025: ${u24}: counterpart appears ${at.length} times`);
    out[at[0]!] = { ...TIGER2025_SIX[k]! };
  }
  return out;
}

/** Every census TIGER url in a plan is under TIGER2024/, except exactly the six TIGER2025 urls, each planned once. */
export function checkTigerVintages(files: readonly SourceFile[]): void {
  const seen = new Map<string, number>();
  for (const f of files) {
    if (!CENSUS_TIGER.test(f.url)) continue;
    if (f.url.startsWith(TIGER)) continue;
    if (!TIGER2025_URLS.has(f.url)) throw new Error(`checkTigerVintages: ${f.url} is neither TIGER2024 nor one of the six TIGER2025 files`);
    seen.set(f.url, (seen.get(f.url) ?? 0) + 1);
  }
  for (const u of TIGER2025_URLS) {
    const n = seen.get(u) ?? 0;
    if (n !== 1) throw new Error(`checkTigerVintages: ${u} planned ${n} times, not once`);
  }
}
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
  if (/\/TIGER2025\//.test(f.url)) return "2025";   // BUILD-25: the dataset's year, not its Last-Modified
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
