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
  encodeCell, encodeFeature, featuresFrom, joinPopulation, pointsFrom, reachOf, readDelimited, storeIdOf, writeManifest,
} from "./basemap/ingest-lib";
import { readShapefile, shapefileFromZip } from "./basemap/shapefile";
import { dirname, join, resolve } from "node:path";
import { Readable } from "node:stream";
import { fileURLToPath } from "node:url";
import { pipeline } from "node:stream/promises";
import {
  FIXED_SOURCES, GNIS_LISTING_URL, type SourceFile, TIGER_DIRS, gnisFromListing, tigerFromListing, tigerListingUrl, vintageOf,
} from "./basemap/sources";

interface FetchRecord { bytes: number; sha256: string; lastModified: string | null; vintage: string }
type FetchLog = Record<string, FetchRecord>;

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
  const r = await fetch(url);
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

async function sha256File(path: string): Promise<string> {
  const h = createHash("sha256");
  await pipeline(createReadStream(path), async function* (src) { for await (const c of src) h.update(c as Buffer); });
  return h.digest("hex");
}

/** One download: streamed to a .part file while hashing, renamed only once complete and the length agrees. */
async function fetchOne(cache: string, f: SourceFile): Promise<FetchRecord> {
  const dest = cachePath(cache, f.url);
  mkdirSync(dirname(dest), { recursive: true });
  const r = await fetch(f.url);
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
  renameSync(part, dest);
  return { bytes, sha256: h.digest("hex"), lastModified, vintage: vintageOf(f, lastModified) };
}

async function fetchAll(cache: string): Promise<void> {
  const { files } = JSON.parse(readFileSync(join(cache, "plan.json"), "utf8")) as { files: SourceFile[] };
  const logPath = join(cache, "fetch-log.json");
  const log: FetchLog = existsSync(logPath) ? JSON.parse(readFileSync(logPath, "utf8")) : {};
  const conc = Number(arg("--concurrency") ?? 4);
  let next = 0, done = 0, fetched = 0, resumed = 0, failed = 0;
  const saveLog = () => writeFileSync(logPath, JSON.stringify(log, Object.keys(log).sort(), 1) + "\n");
  async function worker(): Promise<void> {
    for (;;) {
      const i = next++;
      if (i >= files.length) return;
      const f = files[i]!;
      const dest = cachePath(cache, f.url);
      const prior = log[f.url];
      // Resume only on proof: the file is there, its size matches the log, and its bytes hash to the logged sha.
      if (prior && existsSync(dest) && statSync(dest).size === prior.bytes && (await sha256File(dest)) === prior.sha256) { resumed++; done++; continue; }
      let lastErr = "";
      for (let attempt = 0; attempt < 4; attempt++) {
        try { log[f.url] = await fetchOne(cache, f); fetched++; lastErr = ""; break; }
        catch (e) { lastErr = String((e as Error).message); await new Promise((s) => setTimeout(s, 2000 * 2 ** attempt)); }
      }
      if (lastErr) { failed++; console.error(`FAILED ${f.url}: ${lastErr}`); }
      done++;
      if (done % 200 === 0) { saveLog(); console.log(`fetch: ${done}/${files.length} (${fetched} fetched, ${resumed} resumed, ${failed} failed)`); }
    }
  }
  await Promise.all(Array.from({ length: conc }, worker));
  saveLog();
  const total = Object.values(log).reduce((a, r) => a + r.bytes, 0);
  console.log(`fetch: done ${done}/${files.length}; fetched ${fetched}, resumed ${resumed}, FAILED ${failed}; ${(total / 1e9).toFixed(3)} GB logged`);
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
  const { files: planned } = JSON.parse(readFileSync(join(cache, "plan.json"), "utf8")) as { files: SourceFile[] };
  const log: FetchLog = JSON.parse(readFileSync(join(cache, "fetch-log.json"), "utf8"));
  const missing = planned.filter((f) => !log[f.url]);
  if (missing.length) throw new Error(`${missing.length} planned files not fetched (first: ${missing[0]!.url})`);
  const files: ManifestFile[] = planned
    .map((f) => ({ bytes: log[f.url]!.bytes, layer: f.layer, sha256: log[f.url]!.sha256, url: f.url, vintage: log[f.url]!.vintage }))
    .sort((a, b) => (a.url < b.url ? -1 : a.url > b.url ? 1 : 0));
  const index = new Map(files.map((f, i) => [f.url, i]));
  if (files.length > 0xffff) throw new Error("more files than a u16 index");

  const out = join(cache, "dry-run");
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
    const { shp, dbf } = shapefileFromZip(verifiedBytes(cache, f.url, log));
    const recs = readShapefile(shp, dbf);
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
  const gaz = readDelimited(new TextDecoder("utf-8").decode(unzipOne(verifiedBytes(cache, gazUrl, log), /\.txt$/i)), "\t");
  stats.places = { files: 1, records: gaz.length, kept: 0, vertices: 0, bytes: 0 };
  stats.population = { files: 1, records: pops.size, kept: 0, vertices: 0, bytes: 0 };
  add("places", pointsFrom("places", index.get(gazUrl)!, gaz, pops));
  flush();

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
  writeFileSync(join(out, "store", "manifest.json"), manifestText);
  writeFileSync(join(out, "store", "index.json"), indexText);
  storeBytes += Buffer.byteLength(manifestText) + Buffer.byteLength(indexText);

  const summary = {
    storeId, ingestCommit: commit, dirtyTree: dirty, files: files.length,
    layers: stats,
    cells: { count: entries.length, byLevel: levels, splitLevel0Cells: splitCells, largestBytes: largest, over24MB: oversize },
    manifestBytes: Buffer.byteLength(manifestText), indexBytes: Buffer.byteLength(indexText),
    vectorStoreBytes: storeBytes,
  };
  writeFileSync(join(out, "dry-run.json"), JSON.stringify(summary, null, 1) + "\n");
  console.log(JSON.stringify(summary, null, 1));
}

async function main(): Promise<void> {
  const cmd = process.argv[2];
  const cache = cacheDir();
  if (cmd === "plan") return plan(cache);
  if (cmd === "fetch") return fetchAll(cache);
  if (cmd === "dry-run") return dryRun(cache);
  throw new Error(`unknown command ${cmd ?? "(none)"}: plan | fetch | dry-run`);
}

main().catch((e) => { console.error(e); process.exitCode = 1; });
