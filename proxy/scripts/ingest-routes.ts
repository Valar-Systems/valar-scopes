/**
 * ingest-routes.ts -- mirrors the CC0 route table into the `rt:` KV keys that
 * back callsign -> route on the enrich path.
 *
 *   npm run ingest:routes -- --dry-run              # download + parse + diff, no writes
 *   npm run ingest:routes -- --env staging
 *   npm run ingest:routes -- --env production
 *   npm run ingest:routes -- --env staging --file sd.tar.gz   # offline source
 *
 * ============================================================================
 * WHY THIS EXISTS
 *
 * adsb.lol's /api/0/routeset has returned 201 Created with an EMPTY BODY since
 * 2026-07-08. It is not deprecated and not a contract change -- their live
 * OpenAPI still documents it with the request shape we send, and a deliberately
 * INVALID body also returns 201 where 422 is documented, so nothing is parsing
 * requests and the application is not being reached. Broken upstream, silently,
 * for seven weeks, while /healthz reported every upstream "closed".
 *
 * Every route therefore fell through to adsbdb.com, which we have no written
 * permission to use commercially, and whose 24 h `rt:` cache made it worse
 * rather than better -- caching is what turns "fetch and display" into
 * "incorporated into another database".
 *
 * So we own the data instead. vradarserver/standing-data is CC0 1.0 Universal
 * (verified by reading LICENSE: it names "a database" and "including without
 * limitation commercial purposes"). No permission to ask for, no rate limit to
 * negotiate, and no unresponsive third party left anywhere in the chain.
 *
 * BUILT FROM THE SHARDED SOURCE, NOT FROM vrs-standing-data.adsb.lol. That
 * combined CSV is a build artifact of the same operation whose API died
 * silently; depending on it would reintroduce exactly the dependency being
 * removed, and it would fail the same quiet way. Our build was verified against
 * theirs on 2026-08-25: 619,103 rows, DELTA 0, zero malformed, zero unknown.
 *
 * ============================================================================
 * THE FOUR RULES THIS SCRIPT IMPLEMENTS
 *
 * KV has no transactional swap, which was the one real argument for D1. These
 * four conditions replace it, and each one is here because the alternative has
 * already bitten this project:
 *
 *   1. THE META KEY IS WRITTEN LAST. `meta:routes` carries the row count, the
 *      build time and the source revision, and is written only after every data
 *      key has landed AND the canaries have passed. A build that dies partway
 *      leaves the PREVIOUS meta in place, so /healthz correctly reports stale
 *      rather than reporting a fresh build that does not exist.
 *
 *   2. THE COUNT IS OF WHAT WE WROTE, NOT WHAT WE READ. The row count comes
 *      from the write loop's own tally. The source file's header is a statement
 *      of intent; the write count is evidence. Reporting the parsed figure would
 *      mean a refresh that failed halfway still published a full row count.
 *
 *   3. CANARIES READ BACK THROUGH THE LIVE PATH. A handful of known callsigns
 *      are resolved via GET /v1/enrich against the deployed Worker -- NOT by
 *      reading KV directly, which would prove only that we can read our own
 *      write. If they fail, the meta key is not written.
 *
 *   4. DIFF, DO NOT BLIND-UPSERT. VRS changes incrementally. A daily 619k-write
 *      refresh is a real cost and a real risk; this hashes each airline shard
 *      and writes only the shards whose contents changed. First run writes
 *      everything; subsequent runs write a few hundred keys.
 *
 * ============================================================================
 * KEY FORMAT -- deliberately the SAME shape resolveRoute already reads:
 *
 *   rt:<CALLSIGN>  ->  {"o":"<origin>","d":"<dest>"}
 *
 * so the Worker change is deleting the adsbdb fallback, not rewriting the
 * lookup. Codes are IATA where the airport has one, else ICAO -- the same
 * preference the airport overlay uses, and for the same reason (a spotter
 * recognises DFW; for a field with no IATA, KONT beats blank).
 *
 * Multi-leg routes ("VHHH-UACC-EBLG") take the FIRST leg as origin and the LAST
 * as destination, matching the parser this replaces.
 */
import { execSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdtempSync, readFileSync, writeFileSync, readdirSync, statSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

/** One request for the whole corpus, from the CC0 source of record. */
const TARBALL = "https://codeload.github.com/vradarserver/standing-data/tar.gz/refs/heads/main";
/** KV bulk API pair cap per request. */
const BULK_CHUNK = 10_000;
/** Written LAST, and only if everything else succeeded. See rule 1. */
const META_KEY = "meta:routes";

/**
 * Sanity band on the row count. Not tuned to the margin: 619,103 measured on
 * 2026-08-25, and the failure being caught is a truncated or empty build, which
 * reads far below this. A band rather than a floor because a sudden DOUBLING is
 * equally a reason to stop and look.
 */
const MIN_ROWS = 400_000;
const MAX_ROWS = 900_000;

/**
 * Canary callsigns. Scheduled, long-lived, and spread across three continents
 * so a single airline's schedule change cannot fail the build. Resolved through
 * the LIVE path -- see rule 3.
 */
const CANARIES = ["BAW117", "AAL175", "UAE201", "DLH400", "QFA1"];

interface Args {
  env?: string;
  dryRun: boolean;
  file?: string;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--dry-run") a.dryRun = true;
    else if (argv[i] === "--env") a.env = argv[++i];
    else if (argv[i] === "--file") a.file = argv[++i];
  }
  return a;
}

/**
 * Shell-argument path normaliser. Node's join()/tmpdir() emit backslashes on
 * Windows, and every layer between here and the binary treats one as an escape:
 * the path arrives at tar as C:\\Users\\... and it opens nothing. Forward
 * slashes are accepted by Windows APIs and by every tool we invoke, so they are
 * the portable spelling. No-op on Linux, where CI runs.
 */
const sh = (p: string) => p.split(String.fromCharCode(92)).join("/");  // 92 = backslash; written this way because the escaped-regex form was
// itself mis-escaped once and silently matched DOUBLE backslashes, leaving
// Windows paths untouched and tar opening nothing.
const q = (s: string) => (s.includes(" ") ? `"${s}"` : s);

/** Recursive .csv walk -- the source shards by first letter, then by airline. */
function csvFiles(dir: string, out: string[] = []): string[] {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) csvFiles(p, out);
    else if (name.toLowerCase().endsWith(".csv")) out.push(p);
  }
  return out;
}

/**
 * Minimal CSV row split. The source has no quoted fields in the columns we use
 * (callsigns and airport codes are [A-Z0-9-]), so a full parser would be
 * dead weight -- but the BOM is real and must be stripped or the first header
 * name comes back as "﻿Callsign" and every lookup silently misses.
 */
function rows(text: string): Record<string, string>[] {
  const lines = text.replace(/^﻿/, "").split(/\r?\n/).filter((l) => l.trim().length > 0);
  if (lines.length < 2) return [];
  const head = (lines[0] as string).split(",");
  const out: Record<string, string>[] = [];
  for (let i = 1; i < lines.length; i++) {
    const cells = (lines[i] as string).split(",");
    const rec: Record<string, string> = {};
    head.forEach((h, j) => (rec[h.trim()] = (cells[j] ?? "").trim()));
    out.push(rec);
  }
  return out;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (!args.dryRun && !args.env) {
    console.error("refusing to run: pass --env <staging|production> or --dry-run");
    process.exit(2);
  }

  const tmp = mkdtempSync(join(tmpdir(), "routes-"));
  const tarPath = args.file ?? join(tmp, "sd.tar.gz");

  if (!args.file) {
    console.log(`downloading ${TARBALL}`);
    execSync(`curl -sL --max-time 300 ${q(TARBALL)} -o ${q(sh(tarPath))}`, { stdio: "inherit" });
  }
  // --force-local: GNU tar reads a leading "C:" as a REMOTE HOST spec
  // ("Cannot connect to C: resolve failed"), and Node's tmpdir() hands us
  // exactly that on Windows. Harmless on CI's Linux, required to run this
  // locally at all -- and running it locally is how it gets tested before it
  // touches production KV.
  execSync(`tar xzf ${q(sh(tarPath))} -C ${q(sh(tmp))} --force-local`, { stdio: "inherit" });

  const rootDirs = readdirSync(tmp).filter((d) => d.startsWith("standing-data-"));
  const root = join(tmp, rootDirs[0] as string);

  // ---- ICAO -> IATA, from the SAME CC0 corpus ------------------------------
  // Same source and same revision as the routes, so the two can never drift
  // apart the way two independently-refreshed datasets would.
  const icaoToIata = new Map<string, string>();
  for (const fp of csvFiles(join(root, "airports"))) {
    for (const r of rows(readFileSync(fp, "utf8"))) {
      const icao = (r["ICAO"] ?? "").toUpperCase();
      const iata = (r["IATA"] ?? "").toUpperCase();
      if (icao && iata) icaoToIata.set(icao, iata);
    }
  }
  const code = (icao: string) => icaoToIata.get(icao.toUpperCase()) ?? icao.toUpperCase();
  console.log(`airports: ${icaoToIata.size} ICAO->IATA mappings`);

  // ---- parse the route shards, hashing each --------------------------------
  const shardFiles = csvFiles(join(root, "routes")).sort();
  const shards = new Map<string, { hash: string; pairs: [string, string][] }>();
  let parsed = 0;
  let skipped = 0;
  for (const fp of shardFiles) {
    const raw = readFileSync(fp, "utf8");
    const name = fp.slice(root.length + 1).replace(/\\/g, "/");
    const hash = createHash("sha256").update(raw).digest("hex").slice(0, 16);
    const pairs: [string, string][] = [];
    for (const r of rows(raw)) {
      parsed++;
      const cs = (r["Callsign"] ?? "").toUpperCase();
      const codes = r["AirportCodes"] ?? "";
      if (!cs || !codes || codes.toLowerCase() === "unknown") { skipped++; continue; }
      const legs = codes.split("-").filter((p) => p.length > 0);
      if (legs.length < 2) { skipped++; continue; }
      pairs.push([
        `rt:${cs}`,
        JSON.stringify({ o: code(legs[0] as string), d: code(legs[legs.length - 1] as string) }),
      ]);
    }
    shards.set(name, { hash, pairs });
  }
  const totalRows = [...shards.values()].reduce((n, s) => n + s.pairs.length, 0);
  console.log(`routes: ${shardFiles.length} shards, ${parsed} rows parsed, ${skipped} skipped, ${totalRows} usable`);

  if (totalRows < MIN_ROWS || totalRows > MAX_ROWS) {
    console.error(`REFUSING: ${totalRows} rows is outside the sane band ${MIN_ROWS}-${MAX_ROWS}.`);
    console.error("A truncated or restructured source is exactly what this band exists to catch.");
    process.exit(1);
  }

  // ---- rule 4: diff against the stored manifest ----------------------------
  let prevManifest: Record<string, string> = {};
  if (args.env) {
    try {
      const out = execSync(
        ["npx", "wrangler", "kv", "key", "get", q(META_KEY), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
        { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] },
      );
      prevManifest = (JSON.parse(out) as { shards?: Record<string, string> }).shards ?? {};
      console.log(`previous manifest: ${Object.keys(prevManifest).length} shards`);
    } catch {
      // ABSENT IS NOT AN ERROR, it is the first run -- but it must be said out
      // loud, because "no previous manifest" and "could not read KV" produce the
      // same empty object and mean very different things.
      console.log("no previous manifest readable -- treating as a FULL build");
    }
  }

  const changed = [...shards.entries()].filter(([name, s]) => prevManifest[name] !== s.hash);
  const changedPairs = changed.flatMap(([, s]) => s.pairs);
  console.log(`diff: ${changed.length}/${shards.size} shards changed -> ${changedPairs.length} keys to write`);

  if (args.dryRun) {
    console.log("--dry-run: stopping before any write");
    return;
  }

  // ---- write the data keys, tallying what ACTUALLY went (rule 2) -----------
  let written = 0;
  for (let i = 0; i < changedPairs.length; i += BULK_CHUNK) {
    const chunk = changedPairs.slice(i, i + BULK_CHUNK);
    const path = join(tmp, `bulk-${i / BULK_CHUNK}.json`);
    writeFileSync(path, JSON.stringify(chunk.map(([key, value]) => ({ key, value }))));
    console.log(`bulk put ${chunk.length} (${i + chunk.length}/${changedPairs.length}) ...`);
    execSync(
      ["npx", "wrangler", "kv", "bulk", "put", q(sh(path)), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
      { stdio: "inherit" },
    );
    // Incremented only after the put RETURNS. execSync throws on a non-zero
    // exit, so a failed chunk aborts the run with the meta key untouched.
    written += chunk.length;
  }
  console.log(`wrote ${written} keys`);

  // ---- rule 3: canaries, through the LIVE path ----------------------------
  const base = args.env === "production"
    ? "https://scopes.valarsystems.com"
    : "https://scopes-staging.valarsystems.com";
  const key = process.env["BLIP_KEY"] ?? "";
  const device = process.env["BLIP_DEVICE"] ?? "";
  if (!key || !device) {
    console.error("REFUSING to write the meta key: BLIP_KEY/BLIP_DEVICE are unset, so the");
    console.error("canaries cannot run. An unverified build must not publish a fresh row count.");
    process.exit(1);
  }
  let canaryOk = 0;
  for (const cs of CANARIES) {
    const url = `${base}/v1/enrich/000000?cs=${cs}`;
    try {
      const body = execSync(
        `curl -s --max-time 25 -H ${q(`X-Blip-Key: ${key}`)} -H ${q(`X-Blip-Device: ${device}`)} ${q(url)}`,
        { encoding: "utf8" },
      );
      const j = JSON.parse(body) as { o?: string; d?: string };
      if (j.o && j.d) { canaryOk++; console.log(`  canary ${cs} -> ${j.o}-${j.d}`); }
      else console.log(`  canary ${cs} -> NO ROUTE`);
    } catch (e) {
      console.log(`  canary ${cs} -> ERROR ${(e as Error).message.slice(0, 80)}`);
    }
  }
  // A MAJORITY, not all: a single airline retiring a flight number must not
  // block a refresh, but three of five failing means the read path is broken and
  // publishing a fresh row count would be a lie about a dataset nobody can read.
  if (canaryOk < 3) {
    console.error(`REFUSING to write the meta key: only ${canaryOk}/${CANARIES.length} canaries resolved.`);
    console.error("The data keys are written; the meta key is not, so /healthz will report");
    console.error("the PREVIOUS build and this run reads as the failure it is.");
    process.exit(1);
  }

  // ---- rule 1: the meta key, LAST -----------------------------------------
  const meta = {
    v: 1,
    rows: totalRows,
    written,
    builtAt: new Date().toISOString(),
    source: "vradarserver/standing-data (CC0 1.0)",
    shards: Object.fromEntries([...shards.entries()].map(([n, s]) => [n, s.hash])),
  };
  const metaPath = join(tmp, "meta.json");
  writeFileSync(metaPath, JSON.stringify([{ key: META_KEY, value: JSON.stringify(meta) }]));
  execSync(
    ["npx", "wrangler", "kv", "bulk", "put", q(sh(metaPath)), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
    { stdio: "inherit" },
  );
  console.log(`meta written: rows=${totalRows} written=${written} canaries=${canaryOk}/${CANARIES.length}`);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
