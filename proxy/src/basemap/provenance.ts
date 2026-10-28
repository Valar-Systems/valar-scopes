// PROVENANCE by reference (docs/basemap-package-format.md §7; the route doc's A1.5; §13's RD13, RD15, RD17, RD-P1 to
// RD-P6). Pure apart from SHA-256 (Web Crypto). Lines joined by "\n", with no trailing newline.
//
// Which files were READ is the caller's (RD-P6: the counties and states whose box meets the square, the tiles of the
// mosaics and the summits); this module only writes what it is given, in the fixed order.

export const LAYER_ORDER = ["water", "rivers", "roads", "peaks", "places", "population", "terrain"] as const;
export type Layer = (typeof LAYER_ORDER)[number];
export interface FileRef { url: string; sha256: string; vintage: string }

export const BUILDER_NAME = "blipscope-basemap";

/** The tile-source bits (the store's terrain-sources.bin) and RD-P5's credit lines, in RD-P5's order. */
export const CREDIT_BITS: ReadonlyArray<readonly [number, string]> = [
  [1, "SRTM data courtesy of the U.S. Geological Survey"],
  [2, "GMTED2010 data courtesy of the U.S. Geological Survey"],
  [4, "ETOPO1: DOC/NOAA/NESDIS/NCEI > National Centers for Environmental Information, NESDIS, NOAA, U.S. Department of Commerce"],
  [8, "Contains information licensed under the Open Government Licence – Canada. https://open.canada.ca/en/open-government-licence-canada"],
];

const LEVELS_TEXT = "Terrarium z9 elevations; hillshade from 315 deg at 45 deg with 2x vertical exaggeration; 4x4 mean of z9 pixels; " +
  "each level pixel takes the z9 pixel containing its centre; shade quantised to 8 bits";
const PEAKS_TEXT = "summit elevations taken from the z9 pixel containing the summit, rounded to whole metres";

/** §7's processing line for what the tiles were read for. */
export function processingText(levels: boolean, peaks: boolean): string {
  if (levels && peaks) return `${LEVELS_TEXT}; ${PEAKS_TEXT}`;
  if (levels) return LEVELS_TEXT;
  if (peaks) return `Terrarium z9 elevations; ${PEAKS_TEXT}`;
  throw new Error("a processing line with no tiles read");
}

const te = new TextEncoder();
function byteCmp(a: Uint8Array, b: Uint8Array): number {
  for (let i = 0; i < Math.min(a.length, b.length); i++) if (a[i] !== b[i]) return a[i]! - b[i]!;
  return a.length - b.length;
}
const hex = (b: ArrayBuffer) => [...new Uint8Array(b)].map((x) => x.toString(16).padStart(2, "0")).join("");

/** §7: lowercase hex SHA-256 over one `<url> <sha256>\n` line per file read, the lines in byte order. */
export async function layerDigest(files: FileRef[]): Promise<string> {
  const lines = files.map((f) => te.encode(`${f.url} ${f.sha256}\n`)).sort(byteCmp);
  const all = new Uint8Array(lines.reduce((a, l) => a + l.length, 0));
  let o = 0;
  for (const l of lines) { all.set(l, o); o += l.length; }
  return hex(await crypto.subtle.digest("SHA-256", all));
}

/** §7: the longest common prefix of the URLs, cut back to its last "/". */
export function commonDir(urls: string[]): string {
  let pre = urls[0] ?? "";
  for (const u of urls) { let i = 0; while (i < pre.length && i < u.length && pre[i] === u[i]) i++; pre = pre.slice(0, i); }
  return pre.slice(0, pre.lastIndexOf("/") + 1);
}

export interface ProvenanceInput {
  commit: string;
  storeId: string;
  /** Per layer READ: its files. A layer absent here was not read, and writes no line. */
  files: Partial<Record<Layer, FileRef[]>>;
  /** Whenever any tile was read: what for, and the OR of its tiles' source bits. */
  tiles: { levels: boolean; peaks: boolean; creditBits: number } | null;
  /** RD13: the ε REQUESTED. */
  epsilonMm: number;
  dropped: Partial<Record<Layer, number>>;
  collapsed: Partial<Record<Layer, number>>;
}

/** §7's lines, in §7's order (A1.5). */
export async function provenanceText(p: ProvenanceInput): Promise<{ text: string; lines: number }> {
  if (!/^[0-9a-f]{40}$/.test(p.commit)) throw new Error("builder commit is not 40 hex");
  if (!/^[0-9a-f]{12}$/.test(p.storeId)) throw new Error("store id is not 12 hex");
  if (Boolean(p.files.terrain?.length) !== Boolean(p.tiles)) throw new Error("terrain files and tiles read disagree");
  const read = LAYER_ORDER.filter((l) => p.files[l] !== undefined);
  const out = ["format: BMPK 1", `builder: ${BUILDER_NAME} ${p.commit}`, `store: ${p.storeId}`];
  for (const l of read) {
    const fs = p.files[l]!;
    if (!fs.length) throw new Error(`layer ${l} read with no files`);
    out.push(`source.${l}: ${commonDir(fs.map((f) => f.url))}`);
    out.push(`vintage.${l}: ${[...new Set(fs.map((f) => f.vintage))].sort().join(" ")}`);
    out.push(`files.${l}: ${fs.length}`);
    out.push(`sha256.${l}: ${await layerDigest(fs)}`);
  }
  if (p.tiles) {
    if (!p.tiles.creditBits) throw new Error("tiles read with no source bit");
    for (const [bit, text] of CREDIT_BITS) if (p.tiles.creditBits & bit) out.push(`credit.terrain: ${text}`);
    out.push(`processing.terrain: ${processingText(p.tiles.levels, p.tiles.peaks)}`);
  }
  out.push(`epsilon.m: ${Math.floor(p.epsilonMm / 1000)}.${String(p.epsilonMm % 1000).padStart(3, "0")}`);
  for (const l of read) { out.push(`dropped.${l}: ${p.dropped[l] ?? 0}`); out.push(`collapsed.${l}: ${p.collapsed[l] ?? 0}`); }
  return { text: out.join("\n"), lines: out.length };
}
