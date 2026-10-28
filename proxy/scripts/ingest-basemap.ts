/**
 * ingest-basemap.ts -- the basemap store's ingest (docs/basemap-route.md §2.2, A1.7 MI, §12 step 2).
 *
 *   npx tsx scripts/ingest-basemap.ts plan  --cache <dir>     # enumerate every source file from the publishers' listings
 *   npx tsx scripts/ingest-basemap.ts fetch --cache <dir>     # download them, sha256 as fetched, resumable
 *
 * The cache directory must lie OUTSIDE every repository: the sources are gigabytes and are not ours to commit.
 * Nothing here writes to Cloudflare. Output is counts and bytes only -- never a coordinate.
 */
import { execSync } from "node:child_process";
import { createHash } from "node:crypto";
import { appendFileSync, createWriteStream, existsSync, mkdirSync, readFileSync, renameSync, rmSync, statSync, writeFileSync, createReadStream } from "node:fs";
import { unzipSync } from "fflate";
import {
  type Box, type Cell, type CellIndexEntry, type Feature, type ManifestFile, PREDICATES, assignCells, bboxOf, cellKey, decodeFeature,
  encodeCell, encodeFeature, featuresFrom, joinPopulation, pointsFrom, reachOf, readDelimited, sortKeys, storeIdOf, writeManifest,
} from "./basemap/ingest-lib";
import { readShapefile, shapefileFromZip } from "./basemap/shapefile";
import { coverageRows, encodeCoverage } from "./basemap/coverage";
import { tilesForCoverage } from "./basemap/terrain-plan";
import { dirname, join, resolve } from "node:path";
import { Readable } from "node:stream";
import { fileURLToPath } from "node:url";
import { pipeline } from "node:stream/promises";
import {
  FIXED_SOURCES, GNIS_LISTING_URL, type SourceFile, TERRITORY_FIPS, TIGER_DIRS, checkBody, gnisFromListing, terrariumUrl, tigerFromListing, tigerListingUrl, vintageOf,
} from "./basemap/sources";

interface FetchRecord { bytes: number; sha256: string; lastModified: string | null; vintage: string }
type FetchLog = Record<string, FetchRecord>;

/** Every request says who is asking (ruled 2026-10-28, after census.gov's firewall refused two files). */
const UA = { "User-Agent": "BlipscopeBasemapIngest/1 (+https://github.com/Valar-Systems/valar-scopes)" };

/** The fetch log, with the retry's own records merged in read-only (the retry writes only retry-fetched.json,
 *  so two processes never write one file). A merged record is used only where the main log has none. */
function loadLog(cache: string): FetchLog {
  const main = join(cache, "fetch-log.json"), retry = join(cache, "retry-fetched.json");
  const log: FetchLog = existsSync(main) ? JSON.parse(readFileSync(main, "utf8")) : {};
  if (existsSync(retry)) for (const [u, r] of Object.entries(JSON.parse(readFileSync(retry, "utf8")) as FetchLog)) log[u] ??= r;
  return log;
}

function arg(name: string): string | undefined {
  const i = process.argv.indexOf(name);
  return i > 0 ? process.argv[i + 1] : undefined;
}

function cacheDir(): string {
  const c = arg("--cache");
  if (!c) throw new Error("--cache <dir> is required");
  const abs = resolve(c);
  // Refuse a cache inside this repository: `git add -A` stages what you did not look at (CLAUDE.md).
  const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
  if ((abs + "/").startsWith(repoRoot + "/") || (abs + "\\").startsWith(repoRoot + "\\")) throw new Error(`--cache must lie outside the repository (${repoRoot})`);
  mkdirSync(abs, { recursive: true });
  return abs;
}

/** Where a URL's bytes live in the cache: host + path, so two layers can never collide. */
export function cachePath(cache: string, url: string): string {
  const u = new URL(url);
  return join(cache, "files", u.hostname, ...u.pathname.split("/").filter(Boolean));
}

async function getText(url: string): Promise<string> {
  const r = await fetch(url, { headers: UA });
  if (!r.ok) throw new Error(`${r.status} for ${url}`);
  return r.text();
}

async function plan(cache: string): Promise<void> {
  const files: SourceFile[] = [];
  for (const layer of Object.keys(TIGER_DIRS) as Array<keyof typeof TIGER_DIRS>) {
    const got = tigerFromListing(layer, await getText(tigerListingUrl(layer)));
    if (got.length === 0) throw new Error(`empty listing for ${layer}: refusing a plan with no ${layer}`);
    files.push(...got);
  }
  files.push(...gnisFromListing(await getText(GNIS_LISTING_URL)));
  files.push(...FIXED_SOURCES);
  writeFileSync(join(cache, "plan.json"), JSON.stringify({ files }, null, 1) + "\n");
  const by: Record<string, number> = {};
  for (const f of files) by[f.layer] = (by[f.layer] ?? 0) + 1;
  console.log(`plan: ${files.length} files`, JSON.stringify(by));
}

/** The first 16 bytes of a file, for checkBody. */
function readHead(path: string): Uint8Array {
  const b = readFileSync(path);
  return b.subarray(0, 16);
}

async function sha256File(path: string): Promise<string> {
  const h = createHash("sha256");
  await pipeline(createReadStream(path), async function* (src) { for await (const c of src) h.update(c as Buffer); });
  return h.digest("hex");
}

/** One download: streamed to a .part file while hashing, renamed only once complete and the length agrees. */
async function fetchOne(cache: string, f: SourceFile): Promise<FetchRecord> {
  const dest = cachePath(cache, f.url);
  mkdirSync(dirname(dest), { recursive: true });
  const r = await fetch(f.url, { headers: UA });
  if (!r.ok || !r.body) throw new Error(`${r.status} for ${f.url}`);
  const lastModified = r.headers.get("last-modified");
  const expect = Number(r.headers.get("content-length") ?? NaN);
  const h = createHash("sha256");
  let bytes = 0;
  const part = dest + ".part";
  await pipeline(Readable.fromWeb(r.body as never), async function* (src) {
    for await (const c of src) { h.update(c as Buffer); bytes += (c as Buffer).length; yield c; }
  }, createWriteStream(part));
  if (Number.isFinite(expect) && bytes !== expect) throw new Error(`short read for ${f.url}: ${bytes} of ${expect}`);
  const bad = checkBody(f.url, r.headers.get("content-type"), readHead(part));
  if (bad) { rmSync(part, { force: true }); throw new Error(`refused ${f.url}: ${bad}`); }
  renameSync(part, dest);
  return { bytes, sha256: h.digest("hex"), lastModified, vintage: vintageOf(f, lastModified) };
}

/** Every planned source: the publishers' listings (plan.json) and, once `coverage` has run, the tiles. */
function plannedFiles(cache: string): SourceFile[] {
  const { files } = JSON.parse(readFileSync(join(cache, "plan.json"), "utf8")) as { files: SourceFile[] };
  const tp = join(cache, "tiles.json");
  return existsSync(tp) ? [...files, ...(JSON.parse(readFileSync(tp, "utf8")) as { files: SourceFile[] }).files] : files;
}

async function fetchAll(cache: string): Promise<void> {
  // --layers a,b: fetch only those layers (the rest of the log is kept, untouched).
  const only = arg("--layers")?.split(",");
  const files = plannedFiles(cache).filter((f) => !only || only.includes(f.layer));
  const logPath = join(cache, "fetch-log.json");
  const log: FetchLog = loadLog(cache);
  const conc = Number(arg("--concurrency") ?? 4);
  let next = 0, done = 0, fetched = 0, resumed = 0, adopted = 0, failed = 0;
  // The log is re-read after every write and every record checked: the first version of this line wrote
  // 6,397 empty records and reported success (an array replacer drops nested keys).
  const saveLog = () => {
    writeFileSync(logPath, JSON.stringify(sortKeys(log), null, 1) + "\n");
    const back = JSON.parse(readFileSync(logPath, "utf8")) as FetchLog;
    for (const [u, r] of Object.entries(back)) {
      if (!(Number.isInteger(r.bytes) && /^[0-9a-f]{64}$/.test(r.sha256) && /^\d{4}$/.test(r.vintage))) throw new Error(`fetch log record unreadable after write: ${u}`);
    }
  };
  /** A file already in the cache with no log record: adopted only if a HEAD's Content-Length equals its size;
   *  its sha256 is computed from the bytes, and its Last-Modified (the vintage, RD-P3) taken from the HEAD. */
  async function adopt(f: SourceFile, dest: string): Promise<FetchRecord | null> {
    if (checkBody(f.url, null, readHead(dest))) return null; // the cached copy is not the file: fetch it again
    const h = await fetch(f.url, { method: "HEAD", headers: UA });
    // A HEAD answered with a rejection page says nothing about the file: never read it as "the file changed".
    if (/text\/html/i.test(h.headers.get("content-type") ?? "")) throw new Error(`HEAD rejected for ${f.url}`);
    const len = Number(h.headers.get("content-length") ?? NaN);
    if (!h.ok || !Number.isFinite(len) || len !== statSync(dest).size) return null;
    const lastModified = h.headers.get("last-modified");
    return { bytes: len, sha256: await sha256File(dest), lastModified, vintage: vintageOf(f, lastModified) };
  }
  async function worker(): Promise<void> {
    for (;;) {
      const i = next++;
      if (i >= files.length) return;
      const f = files[i]!;
      const dest = cachePath(cache, f.url);
      const prior = log[f.url];
      // Resume only on proof: the file is there, its size matches the log, and its bytes hash to the logged sha.
      if (prior && existsSync(dest) && !checkBody(f.url, null, readHead(dest)) && statSync(dest).size === prior.bytes && (await sha256File(dest)) === prior.sha256) { resumed++; done++; continue; }
      if (prior) delete log[f.url]; // a record whose file is gone, changed, or not the file: never kept
      if (!prior && existsSync(dest)) {
        let a: FetchRecord | null = null;
        try { a = await adopt(f, dest); }
        catch (e) { failed++; done++; console.error(String((e as Error).message)); continue; } // rejected: leave the cached copy alone
        if (a) { log[f.url] = a; adopted++; done++; continue; }
      }
      let lastErr = "";
      for (let attempt = 0; attempt < 4; attempt++) {
        try { log[f.url] = await fetchOne(cache, f); fetched++; lastErr = ""; break; }
        catch (e) { lastErr = String((e as Error).message); await new Promise((s) => setTimeout(s, 2000 * 2 ** attempt)); }
      }
      if (lastErr) { failed++; console.error(`FAILED ${f.url}: ${lastErr}`); }
      done++;
      if (done % 200 === 0) { saveLog(); console.log(`fetch: ${done}/${files.length} (${fetched} fetched, ${resumed} resumed, ${adopted} adopted, ${failed} failed)`); }
    }
  }
  await Promise.all(Array.from({ length: conc }, worker));
  saveLog();
  const back = JSON.parse(readFileSync(logPath, "utf8")) as FetchLog;
  const total = Object.values(back).reduce((a, r) => a + r.bytes, 0);
  console.log(`fetch: done ${done}/${files.length}; fetched ${fetched}, resumed ${resumed}, adopted ${adopted}, FAILED ${failed}; ${Object.keys(back).length} records, ${(total / 1e9).toFixed(3)} GB, read back from the log`);
  if (failed) process.exitCode = 1;
}

// ------------------------------------------------------------------------------------------------ dry-run (vectors)

const SPLIT_BYTES = 24 * 1024 * 1024; // §2.2's frozen rule: a cell over 24 MB is split into quarters

interface LayerStats { files: number; records: number; kept: number; vertices: number; bytes: number }

/** The verified bytes of a cached file: its sha256 must equal the fetch log's, or the run stops. */
function verifiedBytes(cache: string, url: string, log: FetchLog): Buffer {
  const rec = log[url];
  if (!rec) throw new Error(`not fetched: ${url}`);
  const b = readFileSync(cachePath(cache, url));
  const h = createHash("sha256").update(b).digest("hex");
  if (b.length !== rec.bytes || h !== rec.sha256) throw new Error(`cache does not match the fetch log: ${url}`);
  return b;
}

const unzipOne = (zip: Uint8Array, re: RegExp) => {
  const files = unzipSync(zip, { filter: (f) => re.test(f.name) });
  const names = Object.keys(files);
  if (names.length !== 1) throw new Error(`expected one ${re} in the zip, found ${names.length}`);
  return files[names[0]!]!;
};

async function dryRun(cache: string): Promise<void> {
  const planned = plannedFiles(cache);
  const log: FetchLog = loadLog(cache);
  // --partial: a MEASUREMENT with named files missing, never a store. It writes to its own directory, its
  // manifest is named PARTIAL, and its summary says so; without the flag a missing file stops the run.
  const partial = process.argv.includes("--partial");
  const missing = planned.filter((f) => !log[f.url]);
  if (missing.length && !partial) throw new Error(`${missing.length} planned files not fetched (first: ${missing[0]!.url})`);
  const files: ManifestFile[] = planned
    .filter((f) => log[f.url])
    .map((f) => ({ bytes: log[f.url]!.bytes, layer: f.layer, sha256: log[f.url]!.sha256, url: f.url, vintage: log[f.url]!.vintage }))
    .sort((a, b) => (a.url < b.url ? -1 : a.url > b.url ? 1 : 0));
  const index = new Map(files.map((f, i) => [f.url, i]));
  if (files.length > 0xffff) throw new Error("more files than a u16 index");

  const out = join(cache, partial ? "dry-run-partial" : "dry-run");
  rmSync(out, { recursive: true, force: true });
  const work = join(out, "work");
  mkdirSync(work, { recursive: true });
  const stats: Record<string, LayerStats> = {};
  const boxes = new Map<string, Box[]>();
  const pending = new Map<string, Uint8Array[]>();
  let pendingBytes = 0;
  const workPath = (k: string) => join(work, k.replace(/\//g, "_") + ".bin");
  const flush = () => {
    for (const [k, parts] of pending) appendFileSync(workPath(k), Buffer.concat(parts));
    pending.clear(); pendingBytes = 0;
  };
  const add = (layer: string, feats: Feature[]) => {
    const s = (stats[layer] ??= { files: 0, records: 0, kept: 0, vertices: 0, bytes: 0 });
    for (const f of feats) {
      const bytes = encodeFeature(f), box = bboxOf(f.xy), k = cellKey(0, box[0], box[1]);
      (pending.get(k) ?? pending.set(k, []).get(k)!).push(bytes);
      (boxes.get(k) ?? boxes.set(k, []).get(k)!).push(box);
      pendingBytes += bytes.length; s.kept++; s.vertices += f.xy.length / 2; s.bytes += bytes.length;
    }
    if (pendingBytes > 256 * 1024 * 1024) flush();
  };

  let n = 0;
  for (const f of files) {
    if (f.layer !== "water" && f.layer !== "rivers" && f.layer !== "roads") continue;
    let recs;
    try { const { shp, dbf } = shapefileFromZip(verifiedBytes(cache, f.url, log)); recs = readShapefile(shp, dbf); }
    catch (e) { throw new Error(`${f.url}: ${(e as Error).message}`); }
    const s = (stats[f.layer] ??= { files: 0, records: 0, kept: 0, vertices: 0, bytes: 0 });
    s.files++; s.records += recs.length;
    add(f.layer, featuresFrom(f.layer, index.get(f.url)!, recs));
    if (++n % 500 === 0) console.log(`dry-run: ${n} shapefiles read`);
  }
  for (const f of files.filter((x) => x.layer === "peaks")) {
    const rows = readDelimited(new TextDecoder("utf-8").decode(unzipOne(verifiedBytes(cache, f.url, log), /\.txt$/i)), "|");
    const s = (stats.peaks ??= { files: 0, records: 0, kept: 0, vertices: 0, bytes: 0 });
    s.files++; s.records += rows.length;
    add("peaks", pointsFrom("peaks", index.get(f.url)!, rows));
  }
  const popUrl = files.find((x) => x.layer === "population")!.url, gazUrl = files.find((x) => x.layer === "places")!.url;
  const pops = joinPopulation(new TextDecoder("latin1").decode(verifiedBytes(cache, popUrl, log)));
  // The 2025 Gazetteer is PIPE-delimited (older vintages used tabs).
  const gaz = readDelimited(new TextDecoder("utf-8").decode(unzipOne(verifiedBytes(cache, gazUrl, log), /\.txt$/i)), "|");
  stats.places = { files: 1, records: gaz.length, kept: 0, vertices: 0, bytes: 0 };
  stats.population = { files: 1, records: pops.size, kept: 0, vertices: 0, bytes: 0 };
  add("places", pointsFrom("places", index.get(gazUrl)!, gaz, pops));
  flush();
  // A filter that keeps nothing from a layer that has records is a parse failure until shown otherwise.
  for (const [layer, s] of Object.entries(stats)) {
    if (layer !== "population" && s.records > 0 && s.kept === 0) throw new Error(`${layer}: 0 kept of ${s.records} records -- refusing`);
  }

  // Cells: a level-0 cell over 24 MB is decoded and split by assignCells (MI6's rule, MI5's corner).
  const cellsDir = join(out, "store", "cells");
  const entries: CellIndexEntry[] = [];
  let storeBytes = 0, largest = 0, splitCells = 0, oversize = 0;
  const levels: Record<number, number> = {};
  for (const k of [...boxes.keys()].sort()) {
    const raw = readFileSync(workPath(k));
    const spans: Array<{ bytes: Uint8Array; box: Box }> = [];
    const bx = boxes.get(k)!;
    for (let o = 0, i = 0; o < raw.length; i++) { const { end } = decodeFeature(raw, o); spans.push({ bytes: raw.subarray(o, end), box: bx[i]! }); o = end; }
    if (spans.length !== bx.length) throw new Error(`cell ${entries.length}: ${spans.length} features decoded, ${bx.length} written`);
    const cells: Cell[] = raw.length > SPLIT_BYTES ? assignCells(spans, SPLIT_BYTES) : assignCells(spans, Infinity);
    if (cells.length > 1) splitCells++;
    for (const c of cells) {
      const obj = encodeCell(c.features);
      const p = join(cellsDir, ...c.key.split("/")) + ".bin";
      mkdirSync(dirname(p), { recursive: true });
      writeFileSync(p, obj);
      entries.push({ key: c.key, bytes: obj.length, count: c.features.length, reach: reachOf(c) });
      storeBytes += obj.length; largest = Math.max(largest, obj.length);
      levels[c.level] = (levels[c.level] ?? 0) + 1;
      if (obj.length > SPLIT_BYTES) oversize++;
    }
    rmSync(workPath(k));
  }

  let commit = "UNKNOWN", dirty = true;
  try {
    commit = execSync("git rev-parse HEAD", { encoding: "utf8" }).trim();
    dirty = execSync("git status --porcelain -- .", { encoding: "utf8" }).trim().length > 0;
  } catch { /* reported as UNKNOWN below */ }
  const manifestText = writeManifest({ builder: commit, files, predicates: [...PREDICATES] });
  const storeId = await storeIdOf(manifestText);
  const indexText = JSON.stringify({ cells: entries }) + "\n";
  writeFileSync(join(out, "store", partial ? "manifest.PARTIAL.json" : "manifest.json"), manifestText);
  writeFileSync(join(out, "store", "index.json"), indexText);
  storeBytes += Buffer.byteLength(manifestText) + Buffer.byteLength(indexText);

  const summary = {
    ...(partial ? { PARTIAL: true, missing: missing.map((f) => f.url), note: "a measurement with these files absent; not a store" } : {}),
    storeId: partial ? `PARTIAL-${storeId}` : storeId, ingestCommit: commit, dirtyTree: dirty, files: files.length,
    layers: stats,
    cells: { count: entries.length, byLevel: levels, splitLevel0Cells: splitCells, largestBytes: largest, over24MB: oversize },
    manifestBytes: Buffer.byteLength(manifestText), indexBytes: Buffer.byteLength(indexText),
    vectorStoreBytes: storeBytes,
  };
  writeFileSync(join(out, "dry-run.json"), JSON.stringify(summary, null, 1) + "\n");
  console.log(JSON.stringify(summary, null, 1));
}

// ------------------------------------------------------------------------------------------------ coverage + terrain plan

const TILE_REACH_KM = 250; // §3's largest level radius; R (at most 222) for the peaks is inside it

async function coverageAndTiles(cache: string): Promise<void> {
  const log: FetchLog = loadLog(cache);
  const { files: planned } = JSON.parse(readFileSync(join(cache, "plan.json"), "utf8")) as { files: SourceFile[] };
  const url = (layer: string) => planned.find((f) => f.layer === layer)!.url;
  const store = join(cache, "dry-run", "store");
  mkdirSync(store, { recursive: true });

  // states: coverage (50 + DC) and their extents
  const st = shapefileFromZip(verifiedBytes(cache, url("states"), log));
  const states = readShapefile(st.shp, st.dbf).filter((r) => !TERRITORY_FIPS.has(r.attrs["STATEFP"]!));
  if (states.length !== 51) throw new Error(`${states.length} states + DC, not 51`);
  const rows = coverageRows(states.map((r) => r.parts.map((xy) => ({ xy }))));
  const cov = encodeCoverage(rows);
  writeFileSync(join(store, "coverage.bin"), cov);
  let points = 0, intervals = 0;
  for (const iv of rows.values()) { intervals += iv.length; for (const [a, b] of iv) points += b - a + 1; }

  // RD-P6's extents: a county's or state's files are "read" when its polygon's bounding box meets the square's box
  const box = (parts: Float64Array[]) => {
    let a = Infinity, b = Infinity, c = -Infinity, d = -Infinity;
    for (const xy of parts) { const q = bboxOf(xy); a = Math.min(a, q[0]); b = Math.min(b, q[1]); c = Math.max(c, q[2]); d = Math.max(d, q[3]); }
    return [a, b, c, d];
  };
  const co = shapefileFromZip(verifiedBytes(cache, url("counties"), log));
  const counties = readShapefile(co.shp, co.dbf).filter((r) => !TERRITORY_FIPS.has(r.attrs["STATEFP"]!));
  const regions = {
    counties: Object.fromEntries(counties.map((r) => [r.attrs["GEOID"]!, box(r.parts)])),
    states: Object.fromEntries(states.map((r) => [r.attrs["STATEFP"]!, { box: box(r.parts), usps: r.attrs["STUSPS"]! }])),
  };
  const regionsText = JSON.stringify(sortKeys(regions)) + "\n";
  writeFileSync(join(store, "regions.json"), regionsText);

  // the terrain mirror: every tile within reach, measured by HEAD (bytes and Last-Modified), not downloaded
  const tiles = [...tilesForCoverage(rows, TILE_REACH_KM)].map((i) => terrariumUrl(Math.floor(i / 512), i % 512)).sort();
  // The tiles are planned sources like any other (layer terrain), so fetch, the manifest and store: cover them.
  writeFileSync(join(cache, "tiles.json"), JSON.stringify({ files: tiles.map((url) => ({ layer: "terrain", url })) }, null, 1) + "\n");
  const tlogPath = join(cache, "tile-log.json");
  const tlog: Record<string, { bytes: number; lastModified: string | null }> = existsSync(tlogPath) ? JSON.parse(readFileSync(tlogPath, "utf8")) : {};
  let next = 0, failed = 0;
  async function worker(): Promise<void> {
    for (;;) {
      const i = next++;
      if (i >= tiles.length) return;
      const u = tiles[i]!;
      if (tlog[u]) continue;
      for (let attempt = 0; attempt < 4; attempt++) {
        try {
          const h = await fetch(u, { method: "HEAD", headers: UA });
          const len = Number(h.headers.get("content-length") ?? NaN);
          if (!h.ok || !Number.isFinite(len)) throw new Error(String(h.status));
          tlog[u] = { bytes: len, lastModified: h.headers.get("last-modified") };
          break;
        } catch { if (attempt === 3) { failed++; console.error(`HEAD failed: ${u}`); } else await new Promise((s) => setTimeout(s, 1000 * 2 ** attempt)); }
      }
    }
  }
  await Promise.all(Array.from({ length: 16 }, worker));
  writeFileSync(tlogPath, JSON.stringify(sortKeys(tlog), null, 1) + "\n");
  const years: Record<string, number> = {};
  let tileBytes = 0;
  for (const u of tiles) { const r = tlog[u]; if (!r) continue; tileBytes += r.bytes; const y = r.lastModified ? String(new Date(Date.parse(r.lastModified)).getUTCFullYear()) : "none"; years[y] = (years[y] ?? 0) + 1; }
  const summary = {
    coverage: { rows: rows.size, intervals, latticePoints: points, bytes: cov.length },
    regions: { counties: counties.length, states: states.length, bytes: Buffer.byteLength(regionsText) },
    terrain: { reachKm: TILE_REACH_KM, tiles: tiles.length, headFailed: failed, bytes: tileBytes, lastModifiedYears: years },
  };
  writeFileSync(join(cache, "dry-run", "coverage-tiles.json"), JSON.stringify(summary, null, 1) + "\n");
  console.log(JSON.stringify(summary, null, 1));
  if (failed) process.exitCode = 1;
}

// ------------------------------------------------------------------------------------------------ retry (ruled 2026-10-28)

/** Files a firewall refused: retried gently, one at a time, on a schedule over about 24 hours, with the UA.
 *  Every attempt is appended to retry-attempts.jsonl; a success goes to retry-fetched.json, its own file,
 *  read back and checked, so this process never writes the log another fetch may be writing. */
async function retryBlocked(cache: string): Promise<void> {
  const urls = (arg("--urls") ?? "").split(",").filter(Boolean);
  if (!urls.length) throw new Error("--urls is required");
  const schedule = (arg("--schedule-min") ?? "0,60,180,360,720,1440").split(",").map(Number);
  const planned = plannedFiles(cache);
  const files = urls.map((u) => { const f = planned.find((p) => p.url === u); if (!f) throw new Error(`not a planned source: ${u}`); return f; });
  const outPath = join(cache, "retry-fetched.json"), attempts = join(cache, "retry-attempts.jsonl");
  const got: FetchLog = existsSync(outPath) ? JSON.parse(readFileSync(outPath, "utf8")) : {};
  const t0 = Date.now();
  for (const atMin of schedule) {
    const wait = t0 + atMin * 60_000 - Date.now();
    if (wait > 0) await new Promise((s) => setTimeout(s, wait));
    for (const f of files) {
      if (got[f.url]) continue;
      let outcome: string;
      try {
        got[f.url] = await fetchOne(cache, f);
        writeFileSync(outPath, JSON.stringify(sortKeys(got), null, 1) + "\n");
        const back = (JSON.parse(readFileSync(outPath, "utf8")) as FetchLog)[f.url];
        if (!back || !/^[0-9a-f]{64}$/.test(back.sha256)) throw new Error("retry record unreadable after write");
        outcome = "fetched";
      } catch (e) { outcome = `not fetched: ${(e as Error).message}`; }
      appendFileSync(attempts, JSON.stringify({ at: new Date().toISOString(), minute: atMin, url: f.url, outcome }) + "\n");
      console.log(`${new Date().toISOString()} ${f.url.split("/").pop()}: ${outcome}`);
      await new Promise((s) => setTimeout(s, 60_000)); // one at a time, a minute apart
    }
    if (files.every((f) => got[f.url])) break;
  }
  const left = files.filter((f) => !got[f.url]);
  console.log(left.length ? `retry: ${left.length} still not fetched after the schedule` : "retry: all fetched");
  if (left.length) process.exitCode = 1;
}

async function main(): Promise<void> {
  const cmd = process.argv[2];
  const cache = cacheDir();
  if (cmd === "retry") return retryBlocked(cache);
  if (cmd === "plan") return plan(cache);
  if (cmd === "fetch") return fetchAll(cache);
  if (cmd === "dry-run") return dryRun(cache);
  if (cmd === "coverage") return coverageAndTiles(cache);
  throw new Error(`unknown command ${cmd ?? "(none)"}: plan | fetch | dry-run | coverage`);
}

main().catch((e) => { console.error(e); process.exitCode = 1; });
