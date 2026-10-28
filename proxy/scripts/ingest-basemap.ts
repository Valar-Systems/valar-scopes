/**
 * ingest-basemap.ts -- the basemap store's ingest (docs/basemap-route.md §2.2, A1.7 MI, §12 step 2).
 *
 *   npx tsx scripts/ingest-basemap.ts plan  --cache <dir>     # enumerate every source file from the publishers' listings
 *   npx tsx scripts/ingest-basemap.ts fetch --cache <dir>     # download them, sha256 as fetched, resumable
 *
 * The cache directory must lie OUTSIDE every repository: the sources are gigabytes and are not ours to commit.
 * Nothing here writes to Cloudflare. Output is counts and bytes only -- never a coordinate.
 */
import { createHash } from "node:crypto";
import { createWriteStream, existsSync, mkdirSync, readFileSync, renameSync, statSync, writeFileSync, createReadStream } from "node:fs";
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

async function main(): Promise<void> {
  const cmd = process.argv[2];
  const cache = cacheDir();
  if (cmd === "plan") return plan(cache);
  if (cmd === "fetch") return fetchAll(cache);
  throw new Error(`unknown command ${cmd ?? "(none)"}: plan | fetch`);
}

main().catch((e) => { console.error(e); process.exitCode = 1; });
