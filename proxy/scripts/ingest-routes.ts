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
 *   1. THE META KEY IS WRITTEN LAST. `meta:routes` carries the row count, the build time and per-shard hashes (no
 *      source commit: docs/route-sources.md s.6 pinned it by those hashes), written only after every data key has
 *      landed AND the canaries have passed. A build that dies partway leaves the PREVIOUS meta in place, so
 *      /healthz's `routes` (src/routeshealth.ts; no health surface read it before 2026-11) reports the previous
 *      build's age, stale past 10 days, rather than a fresh build that does not exist.
 *
 *   2. THE COUNT IS OF WHAT WE WROTE, NOT WHAT WE READ. The row count comes
 *      from the write loop's own tally. The source file's header is a statement
 *      of intent; the write count is evidence. Reporting the parsed figure would
 *      mean a refresh that failed halfway still published a full row count.
 *
 *   3. READ BACK THROUGH THE LIVE PATH, WITH A CHECK THAT CAN ACTUALLY FAIL.
 *      GET /v1/enrich against the deployed Worker -- not a direct KV read, which
 *      would prove only that we can read our own write.
 *
 *      The first draft used real airline callsigns for this and was WORTHLESS:
 *      resolveRoute() falls back to adsbdb on a KV miss, so BAW117 resolves
 *      whether our mirror landed or not. The green light stayed green against a
 *      bulk put that wrote nothing.
 *
 *      So the check is a per-run SENTINEL callsign, probed absent before the
 *      write and present after, carrying a value no upstream could invent. The
 *      airline canaries are kept but demoted to a liveness check. If either the
 *      sentinel or a majority of canaries fails, the meta key is not written.
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
 * Multi-leg handling lives in routerule.ts (shared with the verifier so the two
 * cannot drift): through service A-B-C renders A-C, but a ROTATION A-B-A renders
 * the outbound leg A-B rather than the self-loop A-A the inherited parser
 * produced. RULE_REV is folded into the shard hash so a change to that rule
 * re-diffs every shard instead of silently writing nothing.
 */
import { execFileSync, execSync } from "node:child_process";
import { createHash, randomBytes } from "node:crypto";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync, readdirSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { putWithBackoff } from "./chunk-retry";
import { sleepSync } from "./exec-retry";
import { RULE_REV, routeEndpoints } from "./routerule";
import { type BackupPayload, backupPathRefusal, writeBackup } from "./routes-backup";
import { fk1Refusal } from "./routes-fk1";
import { bulkGet } from "./routes-kvread";
import { sweepAtStart, tmpDir } from "./routes-tmp";
import * as G from "./routes-guards";
import { q, sh } from "./shquote";
import { TokenFileError, resolveTestIdentity } from "./token-file";

/** One request for the whole corpus, from the CC0 source of record. */
const TARBALL = "https://codeload.github.com/vradarserver/standing-data/tar.gz/refs/heads/main";
/** KV bulk API pair cap per request. */
const BULK_CHUNK = 10_000;
/** Written LAST, and only if everything else succeeded. See rule 1. */
const META_KEY = "meta:routes";
/**
 * Every KV call goes through the helper (docs/wrangler-run.md). ROUTES_KV_CLI exists for TESTS ONLY: it names
 * scripts/routes-fakekv.mjs, a JSON-file stand-in, so the writer can be graded red-first with no network
 * (docs/route-weekly-code.md). check_routes_workflow.py refuses any workflow that sets it.
 */
const KV_CLI = process.env.ROUTES_KV_CLI ?? "scripts/wrangler-run.mjs";


/**
 * Sanity band on the row count. Not tuned to the margin: 619,103 measured on
 * 2026-08-25, and the failure being caught is a truncated or empty build, which
 * reads far below this. A band rather than a floor because a sudden DOUBLING is
 * equally a reason to stop and look.
 */
const MIN_ROWS = 400_000;
const MAX_ROWS = 900_000;

/**
 * Liveness canaries. Scheduled, long-lived, spread across three continents so a
 * single airline's schedule change cannot fail the build.
 *
 * THESE DO NOT PROVE THE MIRROR WORKS, and the earlier version of this file said
 * they did. The Worker's resolveRoute falls back to adsbdb on a KV miss, so a
 * real callsign resolving through /v1/enrich is equally consistent with "our
 * 619,103 keys landed", "adsbdb answered", and "a leftover 24 h cache entry
 * answered". Provenance is the sentinel's job (see rule 3, part 1); this list
 * only shows the endpoint is alive.
 */
const CANARIES = ["BAW117", "AAL175", "UAE201", "DLH400", "QFA1"];

interface Args {
  env?: string;
  dryRun: boolean;
  file?: string;
  forceShards: string[];
  // the key-level mode (docs/route-weekly-code.md, W2)
  keyLevel: boolean;
  prev?: string;
  prevAlso?: string;
  prevManifest?: string;
  noSeal: boolean;
  sealOnly: boolean;
  receipt?: string;
  readback: "on" | "off";
  maxDeletes?: number;
  allowShards: string[];
  allowRemoved: string[];
  outWritten?: string;
  outDeleted?: string;
  sourceDate?: string;
  replay: boolean;
  backup?: string;    // 2q: the file a writing run saves every touched key's pre-run value to, before any write
  restore?: string;   // 2q: the undo, from such a file
}

/** B2 (the review): an override is a non-negative integer or it is refused. Number("1,000") is NaN, and every guard
 *  comparison against NaN is false, so a NaN cap would silently pass any count. Never coerced, never defaulted. */
function countArg(flag: string, v: string | undefined): number {
  if (v === undefined || !/^\d+$/.test(v)) {
    console.error(`REFUSING (OVR): ${flag} must be a non-negative integer such as 1000 (no commas, signs or units). Nothing done.`);
    process.exit(2);
  }
  return Number(v);
}

/** D8 (docs/route-weekly-code.md 2j): --allow-removed names a SHARD FILE, as G4 prints it. The shape fits 1,591 of 1,591
 *  names in standing-data 24ce5424; a count such as "5" (the old form) or "5x" is refused loudly, never read as a name.
 *  Whether the named shard is actually removed is G4's check, on the proven snapshots. */
const SHARD_NAME = /^routes\/schema-\d{2}\/[A-Z0-9]\/[A-Z0-9]+-[A-Za-z0-9]+\.csv$/;
function shardNameArg(flag: string, v: string | undefined): string {
  if (v === undefined || !SHARD_NAME.test(v)) {
    console.error(`REFUSING (OVR): ${flag} takes a shard file as G4 prints it, such as routes/schema-01/B/BES-all.csv, once per shard (never a count). Nothing done.`);
    process.exit(2);
  }
  return v;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false, forceShards: [], keyLevel: false, noSeal: false, sealOnly: false, readback: "on", allowShards: [], allowRemoved: [], replay: false };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--dry-run") a.dryRun = true;
    else if (argv[i] === "--env") a.env = argv[++i];
    else if (argv[i] === "--file") a.file = argv[++i];
    else if (argv[i] === "--force-shard") a.forceShards.push(argv[++i] as string);
    else if (argv[i] === "--key-level") a.keyLevel = true;
    else if (argv[i] === "--prev") a.prev = argv[++i];
    else if (argv[i] === "--prev-also") a.prevAlso = argv[++i];
    else if (argv[i] === "--prev-manifest") a.prevManifest = argv[++i];
    else if (argv[i] === "--no-seal") a.noSeal = true;
    else if (argv[i] === "--seal-only") a.sealOnly = true;
    else if (argv[i] === "--receipt") a.receipt = argv[++i];
    else if (argv[i] === "--readback") a.readback = argv[++i] === "off" ? "off" : "on";
    else if (argv[i] === "--max-deletes") a.maxDeletes = countArg("--max-deletes", argv[++i]);
    else if (argv[i] === "--allow-shard") a.allowShards.push(argv[++i] as string);
    else if (argv[i] === "--allow-removed") a.allowRemoved.push(shardNameArg("--allow-removed", argv[++i]));
    else if (argv[i] === "--out-written") a.outWritten = argv[++i];
    else if (argv[i] === "--out-deleted") a.outDeleted = argv[++i];
    else if (argv[i] === "--source-date") a.sourceDate = argv[++i];
    else if (argv[i] === "--replay") a.replay = true;
    else if (argv[i] === "--backup") a.backup = argv[++i];
    else if (argv[i] === "--restore") a.restore = argv[++i];
    else {
      // N7 (the re-review, 2q): an argument the writer does not know is REFUSED, never ignored -- a hand-typed --dryrun
      // would otherwise run for real. Only a flag-shaped token is named; anything else could be a pasted value.
      const t = argv[i] as string;
      console.error(`REFUSING (UF): unknown argument ${/^--[a-z][a-z-]*$/.test(t) ? t : "(not flag-shaped, not shown)"}. A flag the writer does not know is refused, never ignored. Nothing done.`);
      process.exit(2);
    }
  }
  return a;
}


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
 * Split one CSV line, honouring double-quoted fields.
 *
 * THIS WAS A PLAIN .split(",") AND IT LOST 22 AIRPORTS.
 *
 * The reasoning for the naive version was that the columns we read are
 * [A-Z0-9-] and therefore never quoted -- true of those columns, and irrelevant,
 * because the airports file is Code,Name,ICAO,IATA,Location,... and both `Name`
 * and `Location` are free text. "Trondheim Airport, Værnes" is one quoted field
 * containing a comma, so a comma-split shifts every column after it: ICAO picked
 * up a fragment of the name and IATA picked up the ICAO.
 *
 * MEASURED, and the measurement matters because the first estimate was five
 * times too big. On the 2026-08-25 corpus 142 airport rows carry a quoted comma
 * -- but in 120 of them the comma is in `Location`, which sits AFTER the two
 * columns we read, so those rows parsed correctly by luck. Only the 22 with a
 * comma in `Name` actually shifted:
 *
 *     15 Norwegian ("X Airport, Y" is their house style) -- Trondheim TRD,
 *        Stavanger SVG, Bergen BGO, Sandefjord TRF, Svalbard LYR, ...
 *      7 others -- Hyderabad HYD, Amritsar ATQ, Baton Rouge BTR, Old Town OLD,
 *        Ambon AMQ, Labuha LAH, Mangole MAL
 *
 * 7,383 of the 619,103 routes (1.19%) begin or end at one of those, and every
 * one would have rendered the raw four-letter ICAO on the card -- the exact
 * outcome the IATA preference exists to avoid.
 *
 * The route shards are unaffected and always were: zero of their 619,103 rows
 * contain a quote character at all. The original comment was right about routes
 * and wrong about airports, which is why the assumption survived.
 *
 * Nothing about the row COUNT changes when this breaks -- 619,103 either way --
 * so no count-based check could ever have seen it. What surfaced it was
 * validating the SHAPE of each parsed code, which is the cheaper habit: assert
 * on the field you are about to use, not on the size of the batch it came in.
 */
function splitCsvLine(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  let inQuotes = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i] as string;
    if (inQuotes) {
      if (c === '"') {
        if (line[i + 1] === '"') { cur += '"'; i++; } // RFC 4180 escaped quote
        else inQuotes = false;
      } else cur += c;
    } else if (c === '"') inQuotes = true;
    else if (c === ",") { out.push(cur); cur = ""; }
    else cur += c;
  }
  out.push(cur);
  return out;
}

/**
 * Parse a CSV shard into records keyed by header name.
 *
 * The BOM is real and must be stripped or the first header name comes back as
 * "﻿Callsign" and every lookup silently misses.
 */
function rows(text: string): Record<string, string>[] {
  const lines = text.replace(/^﻿/, "").split(/\r?\n/).filter((l) => l.trim().length > 0);
  if (lines.length < 2) return [];
  const head = splitCsvLine(lines[0] as string);
  const out: Record<string, string>[] = [];
  for (let i = 1; i < lines.length; i++) {
    const cells = splitCsvLine(lines[i] as string);
    const rec: Record<string, string> = {};
    head.forEach((h, j) => (rec[h.trim()] = (cells[j] ?? "").trim()));
    out.push(rec);
  }
  return out;
}

/** A parsed source: the writer's ONE parse of a standing-data tarball, called by the full mode and the key-level mode
 * alike, so the two can never disagree about a row (docs/route-weekly-code.md, W2). `commit` is read from the archive's
 * own root directory, `standing-data-<sha>`. */
interface ParsedSource {
  root: string;
  commit: string;
  shards: Map<string, { hash: string; plainHash: string; pairs: [string, string][] }>;
  totalRows: number;
}

function parseSource(tarPath: string, tmp: string, label = ""): ParsedSource {
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
  //
  // SHAPE-CHECKED, because the CSV split here is naive on purpose and the
  // airports file is the one place that assumption is exposed: its columns are
  // Code,Name,ICAO,IATA,... and Name is free text. One unescaped comma in an
  // airport name shifts every later column, so ICAO would silently pick up a
  // fragment of a city and IATA a country code -- a corruption that changes no
  // row COUNT and would sail past a count-based check. AA.csv was sampled clean
  // (zero quote characters), but a sample is not the corpus, so the invariant is
  // enforced on every row and the rejects are counted rather than assumed zero.
  const icaoToIata = new Map<string, string>();
  let airportSkipped = 0;
  for (const fp of csvFiles(join(root, "airports"))) {
    for (const r of rows(readFileSync(fp, "utf8"))) {
      const icao = (r["ICAO"] ?? "").toUpperCase();
      const iata = (r["IATA"] ?? "").toUpperCase();
      if (!icao || !iata) continue;
      if (!/^[A-Z0-9]{4}$/.test(icao) || !/^[A-Z0-9]{3}$/.test(iata)) { airportSkipped++; continue; }
      icaoToIata.set(icao, iata);
    }
  }
  const code = (icao: string) => icaoToIata.get(icao.toUpperCase()) ?? icao.toUpperCase();
  console.log(`${label}airports: ${icaoToIata.size} ICAO->IATA mappings, ${airportSkipped} malformed`);
  // A handful of odd rows is the source's business. A flood means the columns
  // moved under us, and every route code downstream would be quietly wrong.
  if (airportSkipped > icaoToIata.size / 100) {
    console.error(`REFUSING: ${airportSkipped} malformed airport rows is over 1% -- the CSV columns`);
    console.error("have almost certainly shifted, which would corrupt every code silently.");
    process.exit(1);
  }

  // ---- parse the route shards, hashing each --------------------------------
  const shardFiles = csvFiles(join(root, "routes")).sort();
  const shards = new Map<string, { hash: string; plainHash: string; pairs: [string, string][] }>();
  let parsed = 0;
  let skipped = 0;
  let rotations = 0;
  for (const fp of shardFiles) {
    const raw = readFileSync(fp, "utf8");
    const name = fp.slice(root.length + 1).replace(/\\/g, "/");
    // RULE_REV is part of the hash, not just the bytes: a rule change must
    // re-diff every shard, or the fix silently writes nothing.
    const hash = createHash("sha256").update("rule" + RULE_REV).update(raw).digest("hex").slice(0, 16);
    // The pre-rule hash, sha256(raw) alone: the scheme of manifests written before RULE_REV was folded in (staging's
    // 08-25 build). Used only to PROVE such a manifest against its snapshot (--prev-also); the diff never uses it.
    const plainHash = createHash("sha256").update(raw).digest("hex").slice(0, 16);
    const pairs: [string, string][] = [];
    for (const r of rows(raw)) {
      parsed++;
      const cs = (r["Callsign"] ?? "").toUpperCase();
      const codes = r["AirportCodes"] ?? "";
      if (!cs || !codes || codes.toLowerCase() === "unknown") { skipped++; continue; }
      const legs = codes.split("-").filter((p) => p.length > 0);
      if (legs.length < 2) { skipped++; continue; }
      // See routeEndpoints: a rotation must not render as DFW-DFW.
      const [oIcao, dIcao] = routeEndpoints(legs);
      if (legs.length > 2 && legs[0] === legs[legs.length - 1]) rotations++;
      pairs.push([`rt:${cs}`, JSON.stringify({ o: code(oIcao), d: code(dIcao) })]);
    }
    shards.set(name, { hash, plainHash, pairs });
  }
  const totalRows = [...shards.values()].reduce((n, s) => n + s.pairs.length, 0);
  console.log(`${label}routes: ${shardFiles.length} shards, ${parsed} rows parsed, ${skipped} skipped, ${totalRows} usable`);
  console.log(`${label}  rule rev ${RULE_REV}: ${rotations} rotations rendered as their outbound leg (would have been X-X)`);
  return { root, commit: (rootDirs[0] as string).slice("standing-data-".length), shards, totalRows };
}

type Meta = Record<string, unknown> & { shards: Record<string, string> };
/**
 * Read meta:routes through the helper. "Absent" and "cannot read" never collapse into one branch: a clean not-found is
 * returned as absent, and anything else -- auth, network, config, an unparsable value -- refuses here, printing why.
 * Shared by the full mode and the key-level mode (moved unchanged out of main(), 2026-11-13).
 */
function readMeta(env: string): { kind: "absent" } | { kind: "ok"; meta: Meta; raw: string } {
  const cmd = ["node", KV_CLI, "kv", "key", "get", q(META_KEY),
    "--binding=ENRICH_KV", `--env=${env}`, "--remote"].join(" ");
  let raw: string;
  try {
    raw = execSync(cmd, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
  } catch (e) {
    const err = e as { status?: number; stdout?: string; stderr?: string };
    const blob = `${err.stdout ?? ""}\n${err.stderr ?? ""}`;
    // wrangler's not-found wording, matched loosely because it has changed
    // between releases. Everything else -- auth, network, config -- is fatal.
    const notFound = /not found|does not exist|key .* not/i.test(blob);
    if (!notFound) {
      console.error(`REFUSING: could not read ${META_KEY} from ${env}, and this is NOT a`);
      console.error("clean not-found. Treating it as a first run would blind-write every key.");
      console.error(`exit status: ${err.status ?? "?"}`);
      console.error(`--- wrangler output ---\n${blob.trim()}\n-----------------------`);
      process.exit(1);
    }
    return { kind: "absent" };
  }
  // A successful exit still has to yield the shape we expect. wrangler has
  // been known to put a banner on stdout ahead of the value; a parse failure
  // here is a reason to stop, not a reason to assume a first run.
  try {
    const parsed = JSON.parse(raw) as Meta;
    if (!parsed || typeof parsed !== "object" || !parsed.shards) {
      throw new Error("no `shards` field in the meta value");
    }
    return { kind: "ok", meta: parsed, raw };
  } catch (e) {
    console.error(`REFUSING: ${META_KEY} read back but did not parse as a manifest.`);
    console.error(`reason: ${(e as Error).message}`);
    console.error(`--- first 200 bytes ---\n${raw.slice(0, 200)}\n-----------------------`);
    process.exit(1);
  }
}

function refuse(code: number, ...lines: string[]): never {
  for (const l of lines) console.error(l);
  process.exit(code);
}

// TEMP DIRS: tmpDir() and the start-of-run sweep live in routes-tmp.ts, shared with the verify (2026-11-14: 371 dirs,
// 4.7 GB, left behind before it existed). Every dir this tool makes goes through tmpDir(), and is removed on exit.
const TEMP_PREFIXES = ["routes-", "routes-new-", "routes-prev-", "routes-also-", "routes-kl-", "routes-seal-", "routes-meta-",
  "routes-rp-new-", "routes-rp-prev-", "routes-rs-"];

type Enrich = (cs: string) => { o?: string; d?: string } | null;
/**
 * The live read path, as the TEST identity: the base URL and a GET /v1/enrich probe. Shared by the full mode and the
 * key-level mode (moved unchanged out of main(), 2026-11-14), so there is one implementation of it, not two.
 */
function liveReader(env: string): { base: string; enrich: Enrich } {
  const base = env === "production"
    ? "https://scopes.valarsystems.com"
    : "https://scopes-staging.valarsystems.com";
  // The TEST identity (docs/test-identity.md, Q-T3), never a board's pair: resolveTestIdentity refuses BLIP_KEY/
  // BLIP_DEVICE and records-device. Never printed, and never on a command line (the read-back below sends it on stdin).
  let key = "", device = "", why = "";
  try {
    ({ key, device } = resolveTestIdentity(env === "production" ? "production" : "staging"));
  } catch (e) {
    if (!(e instanceof TokenFileError)) throw e;
    why = e.message;
  }
  if (!key || !device) {
    console.error("REFUSING: no test identity, so nothing can be read back through");
    if (why) console.error("  (the token file: " + why + ")");
    console.error("the live path. An unverified build must not publish a fresh row count.");
    process.exit(1);
  }
  // curl reads both headers from STDIN (-K -): the key is never a process argument (docs/test-identity.md, item 7)
  const authConfig = `header = "X-Blip-Key: ${key}"\nheader = "X-Blip-Device: ${device}"\n`;

  /** GET /v1/enrich for one callsign. Returns null when the request itself failed. */
  const enrich: Enrich = (cs) => {
    const url = `${base}/v1/enrich/000000?cs=${cs}`;
    try {
      const body = execFileSync("curl", ["-s", "--max-time", "25", "-K", "-", url], { encoding: "utf8", input: authConfig });
      return JSON.parse(body) as { o?: string; d?: string };
    } catch (e) {
      // DO NOT PRINT THE ERROR MESSAGE. execSync puts the whole failed command in
      // e.message, and the command carries `-H "X-Blip-Key: <secret>"` about forty
      // characters in -- so any slice long enough to be useful also leaks the
      // device key into the log, and CI logs outlive the run. Report the status
      // only; the key is never the interesting part of the failure anyway.
      const status = (e as { status?: number }).status;
      console.log(`  ! ${cs} request failed (curl exit ${status ?? "?"})`);
      return null;
    }
  };
  return { base, enrich };
}

/** PFX1 (the owner, 2026-11-13): the only keys a route run may write or delete. Checked for EVERY key, before any write. */
const SENTINEL_KEY = /^rt:ZZ[0-9A-F]{5}S$/;
/** D1 (docs/route-weekly-code.md 2n): the write-ahead journal of keys written or deleted by runs that did not seal. */
const META_PENDING_KEY = "meta:routes-pending";
const pfxOk = (k: string) => G.MIRROR_KEY.test(k) || SENTINEL_KEY.test(k) || k === META_KEY || k === META_PENDING_KEY;

type Pending = { kind: "absent" } | { kind: "stale"; raw: string } | { kind: "current"; keys: string[]; raw: string };
/** J7 (2n): the sha256 of the journal as stored, or null with none. The receipt carries it; the seal compares it. */
const journalSha = (p: Pending): string | null => (p.kind === "absent" ? null : sha256Text(p.raw));
/** D1: the journal, judged against the meta just read. A journal whose `since` is not this meta's sha256 was written
 *  against an earlier meta (a seal happened after it, and its clear failed): it is STALE and ignored. A read that is
 *  neither a clean not-found nor a parseable journal is a refusal, never "absent" -- an unreadable journal assumed empty
 *  would re-open exactly the hole it closes. */
function readPending(env: string, metaRaw: string): Pending {
  const cmd = ["node", KV_CLI, "kv", "key", "get", q(META_PENDING_KEY), "--binding=ENRICH_KV", `--env=${env}`, "--remote"].join(" ");
  let raw: string;
  try {
    raw = execSync(cmd, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
  } catch (e) {
    const err = e as { status?: number; stdout?: string; stderr?: string };
    if (/not found|does not exist|key .* not/i.test(`${err.stdout ?? ""}\n${err.stderr ?? ""}`)) return { kind: "absent" };
    refuse(1, `REFUSING (JN2): could not read ${META_PENDING_KEY} (exit ${err.status ?? "?"}), and this is not a clean not-found. Nothing written.`);
  }
  let j: { v?: number; since?: string; keys?: unknown };
  try { j = JSON.parse(raw) as typeof j; } catch { refuse(1, `REFUSING (JN2): ${META_PENDING_KEY} does not parse. Nothing written.`); }
  if (j.v !== 1 || typeof j.since !== "string" || !Array.isArray(j.keys) || !j.keys.every((k) => typeof k === "string")) {
    refuse(1, `REFUSING (JN2): ${META_PENDING_KEY} is not a v1 journal. Nothing written.`);
  }
  return j.since === sha256Text(metaRaw) ? { kind: "current", keys: j.keys as string[], raw } : { kind: "stale", raw };
}
const ISO_SECONDS = /^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$/;
const sha256File = (p: string) => createHash("sha256").update(readFileSync(p)).digest("hex");
const sha256Text = (s: string) => createHash("sha256").update(s).digest("hex");

/** Bulk put or bulk delete through the helper in chunks, retried with backoff; exits on a chunk that fails every retry. */
function kvChunks(kind: "put" | "delete", items: unknown[], env: string, tmp: string): number {
  let retries = 0;
  for (let i = 0; i < items.length; i += BULK_CHUNK) {
    const chunk = items.slice(i, i + BULK_CHUNK);
    const path = join(tmp, `${kind}-${i / BULK_CHUNK}.json`);
    writeFileSync(path, JSON.stringify(chunk));
    console.log(`bulk ${kind} ${chunk.length} (${i + chunk.length}/${items.length}) ...`);
    const cmd = ["node", KV_CLI, "kv", "bulk", kind, q(sh(path)), "--binding=ENRICH_KV", `--env=${env}`, "--remote",
      ...(kind === "delete" ? ["--force"] : [])].join(" ");
    const r = putWithBackoff((c) => { execSync(c, { stdio: ["ignore", "pipe", "pipe"], encoding: "utf8" }); },
      cmd, [5, 15, 45, 120], sleepSync);
    retries += r.retries;
    if (!r.ok) {
      console.error(`REFUSING: the ${kind} chunk at offset ${i} failed every retry. Nothing further is changed, and`);
      console.error("meta:routes is untouched, so this run reads as the failure it is.");
      console.error(`--- last wrangler output ---\n${r.lastErr}\n----------------------------`);
      process.exit(1);
    }
  }
  return retries;
}

/**
 * --replay (section 2d): an UNCHANGED source re-verifies before it re-seals. Writes and deletes NOTHING. It proves the
 * sealed meta is --file's (every shard hash, and sourceCommit) and that --prev is the snapshot that meta's last change
 * came from (prevSourceCommit), then re-emits that change's written and deleted sets for the verify. The value
 * construction stays here, in the writer, as the rule says.
 */
function replayMain(args: Args) {
  const env = args.env as string;
  if (!args.file || !args.prev) refuse(2, "REFUSING (UN): --replay needs --file <the sealed source> and --prev <the snapshot its last change came from>.");
  const nw = parseSource(args.file as string, tmpDir("routes-rp-new-"), "new: ");
  const pv = parseSource(args.prev as string, tmpDir("routes-rp-prev-"), "prev: ");
  const m = readMeta(env);
  if (m.kind === "absent") refuse(1, "REFUSING (UN): no meta:routes, so nothing is sealed to replay.");
  const { meta, raw } = m as { kind: "ok"; meta: Meta; raw: string };
  // D1 (2n): a replay writes nothing, so it cannot clear unsealed writes. With a CURRENT journal it refuses (exit 6),
  // and the run script takes the key-level write for the same source instead.
  const pend = readPending(env, raw);
  if (pend.kind === "current") {
    refuse(6, `REFUSING (JN1): ${META_PENDING_KEY} holds ${pend.keys.length} key(s) from unsealed runs; a replay writes nothing and cannot clear them. Run the key-level write for this source. Nothing emitted.`);
  }
  const names = new Set([...Object.keys(meta.shards), ...nw.shards.keys()]);
  let eq = 0;
  for (const n of names) if ((meta.shards[n] ?? "").split(":")[0] === nw.shards.get(n)?.hash) eq++;
  console.log(`replay proof: ${eq}/${names.size} shard hashes equal meta:routes`);
  const why: string[] = [];
  if (eq !== names.size) why.push("--file is not the source meta:routes was sealed from");
  if (meta.sourceCommit !== nw.commit) why.push("meta's sourceCommit is not --file's commit");
  if (meta.prevSourceCommit !== pv.commit) why.push("meta's prevSourceCommit is not --prev's commit");
  if (why.length > 0) refuse(1, `REFUSING (UN): ${why.join("; ")}. Nothing emitted.`);
  const valuesOf = (p: ParsedSource) => new Map([...p.shards.values()].flatMap((s) => s.pairs));
  const newVal = valuesOf(nw), prevVal = valuesOf(pv);
  const writes = [...newVal].filter(([k, v]) => prevVal.get(k) !== v);
  const dels = [...prevVal.keys()].filter((k) => !newVal.has(k));
  const outside = [...writes.map(([k]) => k), ...dels].filter((k) => !pfxOk(k)).length;
  if (outside > 0) refuse(1, `REFUSING (PFX1): ${outside} key(s) outside the route prefix in the replayed change.`);
  const run = {
    v: 1, env, mode: "replay", sourceCommit: nw.commit, sourceSha256: sha256File(args.file as string), prevSourceCommit: pv.commit,
    prevMetaSha256: sha256Text(raw), deletesRefused: false, readback: "not run", journalSha256: journalSha(pend),
  };
  if (args.outWritten) writeFileSync(args.outWritten, JSON.stringify({ ...run, written: Object.fromEntries(writes) }));
  if (args.outDeleted) writeFileSync(args.outDeleted, JSON.stringify({ ...run, deleted: dels }));
  console.log(`replay: the last change re-emitted for the verify -- written ${writes.length}, deleted ${dels.length}; nothing written or deleted now`);
}

/**
 * --restore <file> (2q): the undo of one backed-up run. It re-puts every key the backup holds a value for, deletes every
 * key it recorded as "not found" (the run added it), restores the journal's prior state, deletes the run's sentinel, and
 * re-puts meta:routes LAST. PFX1 covers every key. Attended only: on production it is a write, on the owner's word.
 */
function restoreMain(args: Args) {
  const env = args.env as string;
  if (args.dryRun || args.sealOnly || args.replay || args.backup !== undefined || args.file || args.prev || args.noSeal) {
    refuse(2, "REFUSING (RS0): --restore runs alone, with --key-level and --env only. Nothing done.");
  }
  let b: BackupPayload | null = null;
  try { b = JSON.parse(readFileSync(args.restore as string, "utf8")) as BackupPayload; } catch { /* refused below */ }
  if (!b || b.v !== 1 || b.env !== env || typeof b.meta !== "string" || typeof b.keys !== "object" || b.keys === null) {
    refuse(2, "REFUSING (RS1): the file is not a v1 backup for this environment. Nothing done.");
  }
  const bk = b as BackupPayload;
  const puts = Object.entries(bk.keys).filter(([, v]) => v !== null).map(([key, value]) => ({ key, value: value as string }));
  const dels = Object.entries(bk.keys).filter(([, v]) => v === null).map(([k]) => k);
  if (bk.sentinel) dels.push(bk.sentinel);
  if (bk.journal === null) dels.push(META_PENDING_KEY);
  else puts.push({ key: META_PENDING_KEY, value: bk.journal });
  const outside = [...puts.map((p) => p.key), ...dels, META_KEY].filter((k) => !pfxOk(k)).length;
  if (outside > 0) refuse(1, `REFUSING (PFX1): ${outside} key(s) in the backup outside the route prefix. Nothing restored.`);
  const tmp = tmpDir("routes-rs-");
  kvChunks("put", puts, env, tmp);
  kvChunks("delete", dels, env, tmp);
  kvChunks("put", [{ key: META_KEY, value: bk.meta }], env, tmp);   // meta last, as in every write here
  console.log(`restore: re-put ${puts.length} key(s), deleted ${dels.length} key(s) (added by the run, the sentinel, or a journal that was not there), meta:routes restored`);
}

/**
 * --seal-only: write meta:routes and nothing else, and only on a verify receipt for THIS source, THIS environment and
 * the meta the run was built against (GR9). The manifest comes from the source, so the next run can prove it.
 */
function sealOnlyMain(args: Args) {
  const env = args.env as string;
  if (!args.file) refuse(2, "REFUSING (GR9): --seal-only needs --file <the source the verified run wrote>.");
  if (!args.receipt) refuse(2, "REFUSING (GR9): --seal-only needs --receipt <the verify's receipt>. Nothing is sealed without one.");
  if (args.sourceDate !== undefined && !ISO_SECONDS.test(args.sourceDate)) {
    refuse(2, "REFUSING (SD1): --source-date must be an ISO UTC time such as 2026-11-12T10:00:00Z. Nothing sealed.");
  }
  let r: Record<string, unknown>;
  try { r = JSON.parse(readFileSync(args.receipt as string, "utf8")) as Record<string, unknown>; }
  catch { refuse(2, "REFUSING (GR9): the receipt does not parse. Nothing sealed."); }
  const sourceSha256 = sha256File(args.file as string);
  const nw = parseSource(args.file as string, tmpDir("routes-seal-"), "new: ");
  const m = readMeta(env);
  if (m.kind === "absent") refuse(1, "REFUSING (GR9): no meta:routes to seal over; the key-level mode needs the meta its run was proven against.");
  const metaSha = sha256Text((m as { raw: string }).raw);
  const why: string[] = [];
  if (r.v !== 1) why.push("receipt version");
  if (r.result !== "PASS") why.push("the verify did not PASS");
  if (r.env !== env) why.push("a receipt for another environment");
  if (r.sourceCommit !== nw.commit) why.push("a receipt for another source commit");
  if (r.sourceSha256 !== sourceSha256) why.push("a receipt for another source tarball");
  if (r.deletesRefused !== false) why.push("the run's deletes were refused, and a run that cannot delete must not seal");
  if (r.prevMetaSha256 !== metaSha) why.push("meta:routes changed since the run the receipt verified");
  if (why.length > 0) refuse(1, `REFUSING (GR9): the receipt does not match this seal: ${why.join("; ")}. Nothing sealed.`);
  // J7 (2n): the receipt must name the journal now stored. A newer unsealed run rewrote it, so an older receipt describes
  // writes that are no longer the last ones -- and meta has not changed, so GR9 cannot see it. Absent and null are both
  // "no journal".
  const nowJournal = journalSha(readPending(env, (m as { raw: string }).raw));
  const saw = typeof r.journalSha256 === "string" && r.journalSha256 !== "" ? r.journalSha256 : null;
  if (saw !== nowJournal) refuse(1, "REFUSING (J7): the journal changed since the verified run, so this receipt no longer describes the last writes. Nothing sealed.");
  const meta = {
    v: 1,
    ruleRev: RULE_REV,
    rows: nw.totalRows,
    written: r.written,
    deleted: r.deleted,
    pendingDeletes: 0, // a run whose deletes were refused never seals, so a sealed meta never carries any
    builtAt: new Date().toISOString(),
    source: "vradarserver/standing-data (CC0 1.0)",
    mode: "key-level",
    sourceCommit: nw.commit,
    sourceSha256,
    prevSourceCommit: r.prevSourceCommit,
    readback: r.readback,
    ...(typeof r.sentinel === "string" ? { sentinel: r.sentinel } : {}),
    ...(args.sourceDate !== undefined ? { sourceCommittedAt: args.sourceDate } : {}),
    shards: Object.fromEntries([...nw.shards.entries()].map(([n, s]) => [n, `${s.hash}:${s.pairs.length}`])),
  };
  if (!pfxOk(META_KEY)) refuse(1, "REFUSING (PFX1): the meta key is outside the route prefix.");
  const mt = tmpDir("routes-meta-");
  kvChunks("put", [{ key: META_KEY, value: JSON.stringify(meta) }], env, mt);
  console.log(`meta sealed: rows=${nw.totalRows} written=${String(r.written)} deleted=${String(r.deleted)} mode=key-level source ${nw.commit.slice(0, 12)} readback=${String(r.readback)}`);
  // D1 (2n): the seal clears the journal. A failed clear is a WARN, never a refusal: the journal is stale by
  // construction now (its `since` is the old meta's), so the next run ignores it.
  try {
    const p = join(mt, "journal-delete.json");
    writeFileSync(p, JSON.stringify([META_PENDING_KEY]));
    execSync(["node", KV_CLI, "kv", "bulk", "delete", q(sh(p)), "--binding=ENRICH_KV", `--env=${env}`, "--remote", "--force"].join(" "),
      { stdio: ["ignore", "pipe", "pipe"] });
    console.log(`journal: ${META_PENDING_KEY} cleared`);
  } catch { console.log(`WARN: could not clear ${META_PENDING_KEY}; it is stale now (its since is the old meta), so the next run ignores it`); }
}

/**
 * THE KEY-LEVEL MODE (docs/route-weekly-refresh.md 2.3-2.5; docs/route-weekly-code.md, W2). It writes only the keys
 * whose value changed, deletes only keys the mirror itself wrote that the source has dropped, and NEVER seals in-run:
 * meta:routes is written by a later --seal-only, after a full verify (seal last, by construction).
 *
 * The previous snapshot is not trusted, it is PROVEN: --prev must hash, shard for shard, to the current meta:routes,
 * and --prev-also (the first run's older snapshot) to a saved manifest. The delete list is computed from those proven
 * snapshots and never from a KV listing of rt:, which also holds the Worker's TTL'd runtime rows.
 *
 * Stage 1 (this commit) is the dry run: the proofs, the diff and G1-G6, printing counts only. The write, delete and
 * seal steps follow in their own commits, each red-first.
 */
async function keyLevelMain(args: Args) {
  if (!args.env) refuse(2, "REFUSING: the key-level mode needs --env <staging|production>");
  // B1 (the review): a seal has no dry run. --seal-only went straight to its meta write, ignoring --dry-run, so the
  // combination is REFUSED before any KV call rather than half-honoured.
  if (args.sealOnly && args.dryRun) refuse(2, "REFUSING (SOD): --seal-only cannot be a --dry-run: a seal writes meta:routes and nothing else. Nothing done.");
  // D5 (docs/route-weekly-code.md 2k): --force-shard is a full-mode repair. Every key-level path would ignore it, so it is
  // refused here, before the seal-only and replay dispatch. The key-level repair is the verify's.
  if (args.forceShards.length > 0) refuse(2, "REFUSING (FS1): --force-shard is a full-mode repair, and the key-level mode would ignore it. The key-level repair is the verify's. Nothing done.");
  // 2q, BK0: a backup is for a run that writes and deletes, and it lives outside every git tree. Both checked before any
  // KV call. --dry-run, --seal-only and --replay change no route key, so a --backup there would be silently ignored (N7).
  if (args.backup !== undefined) {
    if (args.dryRun || args.sealOnly || args.replay) refuse(2, "REFUSING (BK0): --backup is for a run that writes; --dry-run, --seal-only and --replay change no route key. Nothing done.");
    const why = backupPathRefusal(args.backup);
    if (why) refuse(2, `REFUSING (BK0): ${why}. A backup is private and is never written over. Nothing done.`);
  }
  if (args.restore !== undefined) return restoreMain(args);
  if (args.sealOnly) return sealOnlyMain(args);
  if (args.replay) return replayMain(args);
  if (!args.file || !args.prev) refuse(2, "REFUSING: the key-level mode needs --file <the new source> and --prev <the snapshot meta:routes was built from>");
  const scheduled = process.env.GITHUB_EVENT_NAME === "schedule";
  const overrides = args.maxDeletes !== undefined || args.allowShards.length > 0 || args.allowRemoved.length > 0;
  if (scheduled && overrides) refuse(5, "REFUSING: an override was passed to a scheduled run. A scheduled run can never override a guard (G5).");
  // KL1, seal last by construction: a key-level run that writes never seals, so it must say so.
  if (!args.dryRun && !args.noSeal) {
    refuse(2, "REFUSING (KL1): a key-level run that writes must be given --no-seal. meta:routes is written only by --seal-only, with a verify receipt.");
  }
  // 2q, BK3: the attended over-cap path cannot skip the backup. A dry run with --max-deletes stays allowed (it writes
  // nothing), and a scheduled run never passes --max-deletes (refused above).
  if (!args.dryRun && args.maxDeletes !== undefined && args.backup === undefined) {
    refuse(2, "REFUSING (BK3): --max-deletes lifts the delete cap, so a writing run must also pass --backup <file outside every git tree>. Nothing done.");
  }

  const nw = parseSource(args.file, tmpDir("routes-new-"), "new: ");
  const pv = parseSource(args.prev, tmpDir("routes-prev-"), "prev: ");

  // ---- proof 1: --prev IS the snapshot the current meta:routes was built from ---------------------------------
  const m = readMeta(args.env as string);
  if (m.kind === "absent") refuse(1, "REFUSING: no meta:routes, so --prev cannot be proven. The key-level mode needs a proven previous snapshot.");
  const manifest = (m as { kind: "ok"; meta: Meta }).meta.shards;
  const names = new Set([...Object.keys(manifest), ...pv.shards.keys()]);
  let eq = 0;
  for (const n of names) if ((manifest[n] ?? "").split(":")[0] === pv.shards.get(n)?.hash) eq++;
  console.log(`prev proof: ${eq}/${names.size} shard hashes equal meta:routes (manifest ${Object.keys(manifest).length} shards, prev ${pv.shards.size})`);
  if (eq !== names.size) refuse(1, "REFUSING: --prev is not the snapshot meta:routes was built from, so a diff against it would be wrong. Nothing written.");

  // ---- proof 2 (the first run only): --prev-also against its saved manifest, under ONE hash scheme for all -------
  let also: ParsedSource | null = null;
  if (args.prevAlso) {
    if (!args.prevManifest) refuse(2, "REFUSING: --prev-also needs --prev-manifest <the saved meta:routes it was built into>");
    also = parseSource(args.prevAlso, tmpDir("routes-also-"), "prev-also: ");
    const man = (JSON.parse(readFileSync(args.prevManifest as string, "utf8")) as Meta).shards;
    const all = new Set([...Object.keys(man), ...also.shards.keys()]);
    let eqRule = 0, eqPlain = 0;
    for (const n of all) {
      const h = (man[n] ?? "").split(":")[0];
      if (h === also.shards.get(n)?.hash) eqRule++;
      if (h === also.shards.get(n)?.plainHash) eqPlain++;
    }
    console.log(`prev-also proof: ${Math.max(eqRule, eqPlain)}/${all.size} shard hashes equal its manifest (${eqRule === all.size ? "rule-hashed" : eqPlain === all.size ? "pre-rule plain hash" : "NEITHER scheme whole"})`);
    if (eqRule !== all.size && eqPlain !== all.size) refuse(1, "REFUSING: --prev-also is not the snapshot its manifest records. Nothing written.");
  }

  // ---- the diff ---------------------------------------------------------------------------------------------------
  const valuesOf = (p: ParsedSource) => new Map([...p.shards.values()].flatMap((s) => s.pairs));
  const byShard = (p: ParsedSource) => new Map([...p.shards].map(([n, s]) => [n, new Set(s.pairs.map(([k]) => k))]));
  const newVal = valuesOf(nw), prevVal = valuesOf(pv);
  const newKeys = new Set(newVal.keys()), prevKeys = new Set(prevVal.keys());
  const alsoKeys = also ? new Set(valuesOf(also).keys()) : new Set<string>();
  // D1 (2n): the journal of unsealed runs. Its keys are the mirror's own (written by us since the last seal), so they
  // join `ours` for G6's provenance; each is rewritten to the new source's value, or deleted if the source lacks it.
  const metaRaw = (m as { kind: "ok"; raw: string }).raw;
  const pend = readPending(args.env as string, metaRaw);
  const pendingKeys = new Set(pend.kind === "current" ? pend.keys : []);
  const ours = new Set([...prevKeys, ...alsoKeys, ...pendingKeys]); // every key the mirror is proven to have written
  const newByShard = byShard(nw);
  const unionByShard = new Map<string, Set<string>>();
  for (const src of [also ? byShard(also) : new Map<string, Set<string>>(), byShard(pv)]) {
    for (const [n, ks] of src) unionByShard.set(n, new Set([...(unionByShard.get(n) ?? []), ...ks]));
  }
  let added = 0, changed = 0;
  for (const [k, v] of newVal) { if (!prevVal.has(k)) added++; else if (prevVal.get(k) !== v) changed++; }
  const deletes = [...ours].filter((k) => !newKeys.has(k));
  const writes = [...newVal].filter(([k, v]) => prevVal.get(k) !== v || pendingKeys.has(k));
  const removedShards = new Set([...unionByShard.keys()].filter((n) => !newByShard.has(n)));
  let fromRemoved = 0;
  for (const n of removedShards) for (const k of unionByShard.get(n) ?? []) if (!newKeys.has(k)) fromRemoved++;
  console.log(`sources: new ${nw.commit.slice(0, 12)}, prev ${pv.commit.slice(0, 12)}${also ? `, prev-also ${also.commit.slice(0, 12)}` : ""}`);
  const rewrite = [...pendingKeys].filter((k) => newKeys.has(k)).length;
  console.log(pend.kind === "current"
    ? `pending: ${pendingKeys.size} key(s) from unsealed runs (rewrite ${rewrite}, delete ${pendingKeys.size - rewrite})`
    : pend.kind === "stale" ? "pending: stale, ignored" : "pending: none");
  const onlyPending = writes.length - added - changed;
  console.log(`key-level writes: ${writes.length} (added ${added}, value changed ${changed}${onlyPending > 0 ? `, pending rewrite ${onlyPending}` : ""}); deletes: ${deletes.length} (${deletes.length - fromRemoved} from surviving shards, ${fromRemoved} from ${removedShards.size} removed shard(s))`);

  // ---- the guards ---------------------------------------------------------------------------------------------
  const g1 = G.g1Completeness(pv.totalRows, nw.totalRows);
  console.log(`  ${g1.ok ? "ok  " : "FAIL"} ${g1.reason}`);
  if (!g1.ok) refuse(1, "REFUSING EVERYTHING (G1): the new source is not whole. Nothing would be written or deleted.");
  const anchors = (JSON.parse(readFileSync("scripts/routes-anchors.json", "utf8")) as { anchors: string[] }).anchors;
  const verdicts = [
    G.g2Anchors(anchors, prevKeys, newKeys),
    G.g3SurvivingShare(unionByShard, newByShard, new Set(args.allowShards)),
    G.g4Removed(unionByShard, newByShard, new Set(args.allowRemoved)),
    G.g5Cap(deletes.length, { scheduled, maxDeletes: args.maxDeletes }),
    G.g6ShapeAndProvenance(deletes, ours),
  ];
  for (const v of verdicts) {
    console.log(`  ${v.ok ? "ok  " : "FAIL"} ${v.reason}`);
    if (!v.ok && v.names) console.log(`       shard(s): ${v.names.join(", ")}`);
  }
  const deletesAllowed = verdicts.every((v) => v.ok);
  // D2 (docs/route-weekly-code.md 2k): PFX1 is counted BEFORE the dry-run return, over exactly the keys stage 2 would
  // touch, so a dry run predicts the refusal a real run would meet. The live check before the first write stays: it
  // also covers the sentinel, which does not exist yet.
  const plannedKeys = [...writes.map(([k]) => k), ...(deletesAllowed ? deletes : [])];
  const pfxPlanned = plannedKeys.filter((k) => !pfxOk(k)).length;
  console.log(`plan: write ${writes.length}; ${deletesAllowed ? `delete ${deletes.length}` : `delete NONE (refused), so the run would NOT seal`}; seal: never in-run (--seal-only after a verify PASS); PFX1: ${pfxPlanned} outside`);
  if (pfxPlanned > 0) refuse(1, `REFUSING (PFX1): ${pfxPlanned} planned key(s) outside the route prefix. Nothing written or deleted.`);
  if (args.dryRun) {
    console.log("--dry-run: stopping before any write");
    return;
  }

  // ---- stage 2: write, delete, read back; NEVER seal (W2) --------------------------------------------------------
  const env = args.env as string;
  const tmp = tmpDir("routes-kl-");
  const dels = deletesAllowed ? deletes : [];
  const live = args.readback === "off" ? null : liveReader(env);
  const runId = randomBytes(4).toString("hex").toUpperCase().slice(0, 5);
  const sentinelCs = `ZZ${runId}S`, anchorCs = `ZZ${runId}A`;
  const sentinelVal = JSON.stringify({ o: runId.slice(0, 3), d: `${runId.slice(3, 5)}Z` });
  const toWrite: [string, string][] = live ? [[`rt:${sentinelCs}`, sentinelVal], ...writes] : writes;

  // PFX1: every key of every put and delete, BEFORE the first write. One key outside the prefix refuses the whole run.
  const outside = [...toWrite.map(([k]) => k), ...dels].filter((k) => !pfxOk(k)).length;
  if (outside > 0) refuse(1, `REFUSING (PFX1): ${outside} key(s) outside the route prefix. Nothing written or deleted.`);

  if (!live) console.log("readback: NOT RUN (the routes-refresh test identity is not set)");
  else {
    // The anchor before any write: a ZZ callsign must resolve to nothing (the full mode's rule 3, part 1).
    const pre = live.enrich(anchorCs);
    if (pre === null) refuse(1, `REFUSING: the live path is unreachable at ${live.base}. Nothing written.`);
    if (pre && (pre.o || pre.d)) refuse(1, `REFUSING: anchor ${anchorCs} resolves before any write; this probe cannot be trusted. Nothing written.`);
    console.log(`anchor: ${anchorCs} is absent through the live path (as required)`);
  }

  // 2q and its amendments 2 and 3: THE BACKUP, before the journal and the first write. Every key this run touches, read
  // with meta and the journal through the reader the verify uses. A null is an OBSERVATION -- the answer's own "not
  // found" -- and is checked against the plan's classes, because the undo deletes every key recorded as null.
  if (args.backup !== undefined) {
    const touched = [...new Set([...writes.map(([k]) => k), ...dels])].sort();
    const want = [...touched, META_KEY, META_PENDING_KEY];
    let got = new Map<string, string | null>();
    try { got = bulkGet(want, env, tmp, KV_CLI); }
    catch { refuse(1, "REFUSING (BK-R): the backup's read failed, so no pre-run value can be trusted. Nothing written."); }
    const short = want.filter((k) => !got.has(k)).length;
    if (short > 0) refuse(1, `REFUSING (BK-R): the backup's read answered for ${want.length - short} of ${want.length} keys; a missing answer is not "not found". Nothing written.`);
    const metaNow = got.get(META_KEY);
    if (typeof metaNow !== "string") refuse(1, "REFUSING (BK-R): meta:routes read as not found for the backup. Nothing written.");
    // BK-X and BK1: each touched key against its class in the plan. A key in a CURRENT journal (an unsealed run's), or
    // one only --prev-also holds (the first run's older snapshot), was written by the mirror since --prev, so it may be
    // present or absent: recorded as observed, never class-checked (amendment 3's stated exception).
    const writeKeys = new Set(writes.map(([k]) => k));
    let disagree = 0, emptyDeletes = 0;
    for (const k of touched) {
      if (pendingKeys.has(k)) continue;
      const v = got.get(k) ?? null;
      if (writeKeys.has(k)) {
        if (prevVal.has(k)) { if (v === null) disagree++; }          // CHANGED: must be found
        else if (!alsoKeys.has(k) && v !== null) disagree++;          // ADDED: must be "not found"
      } else if (v === null) disagree++;                              // DELETED: must be found ...
      else if (v === "") emptyDeletes++;                              // ... and hold a value
    }
    if (disagree > 0) refuse(1, `REFUSING (BK-X): ${disagree} touched key(s) disagree with the plan (a changed or deleted key not found, or an added key already present), so the store is not the proven snapshot. Nothing written.`);
    if (emptyDeletes > 0) refuse(1, `REFUSING (BK1): ${emptyDeletes} key(s) to delete hold an empty value, so the plan and the store disagree. Nothing written.`);
    const payload: BackupPayload = {
      v: 1, env, runId, sentinel: live ? `rt:${sentinelCs}` : null, meta: metaNow as string,
      journal: got.get(META_PENDING_KEY) ?? null, keys: Object.fromEntries(touched.map((k) => [k, got.get(k) ?? null])),
    };
    let digest = "";
    try { digest = writeBackup(args.backup, payload); }
    catch (e) { refuse(1, e instanceof Error ? e.message : "REFUSING (BK2): the backup could not be written. Nothing written to KV."); }
    console.log(`backup: ${touched.length} key(s) (${dels.length} to delete, ${writes.length} to write), meta and journal saved, sha256 ${digest}`);
  }

  // D1 (2n), WRITE-AHEAD: before the first data write, the journal names every key this run may touch, plus a current
  // journal's keys. If the run then dies or never seals, the next run still knows them. A stale journal is overwritten.
  const journalKeys = [...new Set([...pendingKeys, ...writes.map(([k]) => k), ...dels])].sort();
  let journalRaw: string | null = pend.kind === "absent" ? null : pend.raw;   // what is stored after this step
  if (journalKeys.length > 0) {
    // J7 (2n): the journal names its writer (source and runId), so a later run over the same meta and the same key set
    // still changes it -- and an older receipt can never match it again.
    journalRaw = JSON.stringify({ v: 1, since: sha256Text(metaRaw), source: nw.commit, runId, keys: journalKeys });
    kvChunks("put", [{ key: META_PENDING_KEY, value: journalRaw }], env, tmp);
    // N2 (the re-review, 2q): the receipt's journal hash is taken from the journal AS READ BACK, through readPending --
    // the reader the seal uses -- so both sides of J7 hash the same bytes even if the store's output adds one.
    const back = readPending(env, metaRaw);
    if (back.kind !== "current") refuse(1, `REFUSING (JN2): the journal just put reads back as ${back.kind}. The route keys are untouched; nothing else written.`);
    journalRaw = (back as { raw: string }).raw;
    console.log(`journal: ${journalKeys.length} key(s) recorded in ${META_PENDING_KEY} before the first write`);
  }

  const retries = kvChunks("put", toWrite.map(([key, value]) => ({ key, value })), env, tmp)
    + kvChunks("delete", dels, env, tmp);
  if (retries > 0) console.log(`(${retries} chunk retries were needed)`);
  console.log(`wrote ${writes.length} route keys${live ? " (+1 sentinel)" : ""}; deleted ${dels.length}`);

  if (live) {
    const want = JSON.parse(sentinelVal) as { o: string; d: string };
    const t0 = Date.now();
    let seen = false;
    for (let attempt = 1; attempt <= 8 && !seen; attempt++) {
      const got = live.enrich(sentinelCs);
      if (got && got.o === want.o && got.d === want.d) {
        seen = true;
        console.log(`sentinel: ${sentinelCs} on attempt ${attempt} after ${Math.round((Date.now() - t0) / 1000)}s (PROVENANCE PROVED)`);
      } else if (attempt < 8) sleepSync(15_000);
    }
    if (!seen) refuse(1, `REFUSING: sentinel ${sentinelCs} never came back through ${live.base}. The keys are written; meta is not, and no receipt can follow.`);
    let canaryOk = 0;
    for (const cs of CANARIES) { const j = live.enrich(cs); if (j && j.o && j.d) canaryOk++; }
    console.log(`canaries: ${canaryOk}/${CANARIES.length}`);
    if (canaryOk < 3) refuse(1, `REFUSING: only ${canaryOk}/${CANARIES.length} canaries resolved; the read path is broken.`);
    // Not kvChunks: that exits on failure, and a leftover sentinel is harmless (one key under rt:ZZ, swept next run).
    try {
      const p = join(tmp, "sentinel-delete.json");
      writeFileSync(p, JSON.stringify([`rt:${sentinelCs}`]));
      execSync(["node", KV_CLI, "kv", "bulk", "delete", q(sh(p)), "--binding=ENRICH_KV", `--env=${env}`, "--remote", "--force"].join(" "),
        { stdio: ["ignore", "pipe", "pipe"] });
      console.log(`cleanup: rt:${sentinelCs} deleted`);
    } catch { console.log(`WARN: could not delete rt:${sentinelCs}; harmless, one key under rt:ZZ`); }
  }

  // The KV-level sample: written keys read straight from the namespace, byte for byte (the verify reads them all).
  const SAMPLE = 12, stride = Math.max(1, Math.floor(writes.length / SAMPLE));
  let sampleOk = 0, sampleChecked = 0;
  for (let i = 0; i < writes.length && sampleChecked < SAMPLE; i += stride) {
    const [k, v] = writes[i] as [string, string];
    sampleChecked++;
    try {
      const got = execSync(["node", KV_CLI, "kv", "key", "get", q(k), "--binding=ENRICH_KV", `--env=${env}`, "--remote"].join(" "),
        { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }).trim();
      if (got === v) sampleOk++;
    } catch { /* counted as a miss below */ }
  }
  if (sampleOk !== sampleChecked) refuse(1, `REFUSING: ${sampleOk}/${sampleChecked} sampled written keys read back byte-identical.`);
  console.log(`sample: ${sampleOk}/${sampleChecked} written keys read back byte-identical`);

  // The verifier's inputs: local files, never printed (W3 reads them, and writes the receipt --seal-only needs).
  const run = {
    v: 1, env, sourceCommit: nw.commit, sourceSha256: sha256File(args.file as string), prevSourceCommit: pv.commit,
    prevMetaSha256: sha256Text((m as { raw: string }).raw), deletesRefused: !deletesAllowed,
    journalSha256: journalRaw === null ? null : sha256Text(journalRaw),   // J7 (2n)
    readback: live ? "on" : "not run", ...(live ? { sentinel: sentinelCs } : {}),
  };
  if (args.outWritten) writeFileSync(args.outWritten, JSON.stringify({ ...run, written: Object.fromEntries(writes) }));
  if (args.outDeleted) writeFileSync(args.outDeleted, JSON.stringify({ ...run, deleted: dels }));
  console.log(deletesAllowed
    ? "not sealed: meta:routes is untouched. Seal with --seal-only --receipt <the verify's receipt>."
    : "not sealed, and must not be: a guard refused the deletes, so no receipt may follow (a wrong route is worse than none).");
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  // FK1 (docs/route-weekly-code.md): ROUTES_KV_CLI swaps real KV for a JSON fake, for tests. On the production path it
  // would turn a run into a silent green -- "writes" to a file, verified against the same file. So it is refused, before
  // any KV call, wherever a REAL credential is in reach: the routes-refresh workflow, --env production, or a token
  // variable by name (revised by the reviewer, section 2e; the one rule lives in routes-fk1.ts, shared with the verify).
  // D4 (docs/route-weekly-code.md 2l): the FULL mode never writes production. It has no PFX1, and its meta carries no
  // sourceCommit, so one production write would break the key-level chain (the next weekly run refuses as
  // meta-not-key-level). Checked BEFORE FK1, so the two refusals stay distinguishable; a production dry run still reads.
  if (!args.keyLevel && !args.sealOnly && args.env === "production" && !args.dryRun) {
    refuse(2, "REFUSING (FM1): the full mode does not write production -- use --key-level (it has PFX1, G5, G6 and the seal chain). A production --dry-run is still allowed. Nothing done.");
  }
  const fk1 = process.env.ROUTES_KV_CLI ? fk1Refusal(process.env, args.env, existsSync) : null;
  if (fk1) refuse(2, `REFUSING (FK1): ROUTES_KV_CLI is a test-only stand-in for KV, and it is not allowed here: ${fk1}.`);
  sweepAtStart(TEMP_PREFIXES); // a killed run's leftovers, older than 6 h and only ours (routes-tmp.ts)
  if (args.keyLevel || args.sealOnly) return keyLevelMain(args);
  const keyLevelOnly = args.noSeal || args.prev || args.prevAlso || args.prevManifest || args.receipt || args.readback === "off"
    || args.maxDeletes !== undefined || args.allowShards.length > 0 || args.allowRemoved.length > 0
    || args.outWritten || args.outDeleted || args.sourceDate !== undefined || args.replay
    || args.backup !== undefined || args.restore !== undefined;
  if (keyLevelOnly) refuse(2, "REFUSING: --no-seal, --prev, --prev-also, --prev-manifest, --receipt, --readback, --out-written, --out-deleted, --source-date, --replay, --backup, --restore and the overrides belong to --key-level.");
  if (!args.dryRun && !args.env) {
    console.error("refusing to run: pass --env <staging|production> or --dry-run");
    process.exit(2);
  }

  const tmp = tmpDir("routes-");
  const tarPath = args.file ?? join(tmp, "sd.tar.gz");

  if (!args.file) {
    console.log(`downloading ${TARBALL}`);
    execSync(`curl -sL --max-time 300 ${q(TARBALL)} -o ${q(sh(tarPath))}`, { stdio: "inherit" });
  }
  const { root, shards, totalRows } = parseSource(tarPath, tmp);
  void root;

  if (totalRows < MIN_ROWS || totalRows > MAX_ROWS) {
    console.error(`REFUSING: ${totalRows} rows is outside the sane band ${MIN_ROWS}-${MAX_ROWS}.`);
    console.error("A truncated or restructured source is exactly what this band exists to catch.");
    process.exit(1);
  }

  // ---- rule 4: diff against the stored manifest ----------------------------
  //
  // "ABSENT" AND "CANNOT READ" MUST NOT COLLAPSE INTO ONE BRANCH.
  //
  // The first version of this caught every error and called it a first run. That
  // is right for a missing key and catastrophically wrong for an expired token,
  // a wrong working directory, or a 401 -- each of which produces the identical
  // empty object and would silently turn an incremental refresh of a few hundred
  // keys into a blind 619,103-key rewrite of production KV. Same shape as the
  // verify-release.sh square probe: the failure mode of a broken reader is
  // "everything is missing", which is exactly what a first run looks like.
  //
  // So the reason is captured and inspected, and anything that is NOT a clean
  // not-found aborts. stderr goes to its own pipe and is PRINTED -- a guard that
  // can tell you a result is untrustworthy should also tell you why, or you
  // spend the next hour bisecting a silent empty string.
  let prevManifest: Record<string, string> = {};
  if (args.env) {
    const m = readMeta(args.env);
    if (m.kind === "absent") console.log(`no ${META_KEY} in ${args.env} -- confirmed absent, so this is a FULL build`);
    else {
      prevManifest = m.meta.shards;
      console.log(`previous manifest: ${Object.keys(prevManifest).length} shards`);
    }
  }

  // Manifest values are "<hash>:<rowcount>". Compare on the HASH half only --
  // reading the whole string would make every shard look changed the first time
  // the format moved, and a diff that silently degrades to a full rewrite is
  // indistinguishable from a diff that is working.
  const prevHash = (name: string) => (prevManifest[name] ?? "").split(":")[0] ?? "";
  const prevCount = (name: string) => Number((prevManifest[name] ?? "").split(":")[1] ?? NaN);

  // --force-shard: REWRITE A SHARD WHOSE SOURCE HAS NOT CHANGED.
  //
  // The diff keys off the SOURCE hash, which is the right thing for a refresh and
  // the wrong thing for a repair -- and the difference is not theoretical. The
  // 2026-08-25 staging load wrote 619,103 keys, reported Success! on every chunk,
  // and silently dropped exactly one: rt:IGO7J, mid-chunk, in a chunk unrelated
  // to the one 524 that was retried. `written` counted it. A 12-key sample had a
  // 1-in-51,592 chance of catching it.
  //
  // Without this flag that hole is PERMANENT: the IGO shard's hash is unchanged,
  // so every subsequent run writes nothing and the key never comes back. A repair
  // path is not a convenience here, it is the difference between a mirror that
  // converges on correct and one that accumulates silent gaps forever.
  //
  // It lives in the writer rather than the verifier on purpose -- the writer is
  // what knows how to turn a corpus row into a value, and a second implementation
  // of that would be free to disagree with this one.
  //
  //   npm run verify:routes -- --env staging          # names the bad shards
  //   npm run ingest:routes -- --env staging --force-shard IGO-all.csv
  const forced = new Set<string>();
  if (args.forceShards.length > 0) {
    for (const name of shards.keys()) {
      if (args.forceShards.some((f) => name.includes(f))) forced.add(name);
    }
    console.log(`--force-shard matched ${forced.size} shard(s): ${[...forced].join(", ")}`);
    if (forced.size === 0) {
      console.error("REFUSING: --force-shard matched nothing. A repair that silently repairs");
      console.error("no shard is worse than no repair, because it reports success.");
      process.exit(1);
    }
  }

  const changed = [...shards.entries()]
    .filter(([name, s]) => forced.has(name) || prevHash(name) !== s.hash);
  const changedPairs = changed.flatMap(([, s]) => s.pairs);
  console.log(`diff: ${changed.length}/${shards.size} shards changed -> ${changedPairs.length} keys to write`);

  // THE DELETION GAP, STATED OUT LOUD RATHER THAN LEFT TO BE DISCOVERED.
  //
  // A changed shard is rewritten wholesale, so a callsign REMOVED upstream keeps
  // its old `rt:` key forever -- these are written without a TTL, and nothing
  // here lists or deletes. The effect is a retired flight number serving a route
  // that was true once. Not corrupting, but not self-healing either, and the
  // only way it ever becomes visible is if someone counts.
  //
  // So it gets counted every run. Fixing it needs per-shard key lists in the
  // manifest (or a prefix list per changed shard) and belongs in its own change;
  // shipping the measurement first means the follow-up has a number to justify it.
  let shrink = 0;
  for (const [name, s] of changed) {
    const before = prevCount(name);
    if (Number.isFinite(before) && before > s.pairs.length) shrink += before - s.pairs.length;
  }
  if (shrink > 0) {
    console.log(`NOTE: ${shrink} callsigns disappeared from changed shards and are NOT deleted`);
    console.log("      (known gap -- keys have no TTL; see the deletion note in the diff block)");
  }

  if (args.dryRun) {
    console.log("--dry-run: stopping before any write");
    return;
  }

  // ---- rule 3, part 1: the SENTINEL -- the only provenance-proof check -----
  //
  // WHY THE AIRLINE CANARIES BELOW CANNOT DO THIS JOB.
  //
  // resolveRoute() in src/enrich.ts reads `rt:` from KV and, on a miss, falls
  // through to the route-source chain -- adsb.lol routeset, then adsbdb, which
  // is still enabled (ROUTE_ADSBDB_ENABLED = "true") until the cutover. So
  // "BAW117 resolved through /v1/enrich" is true when our mirror answered, true
  // when adsbdb answered, and true when a leftover 24 h adsbdb cache entry
  // answered. It is a green light that stays green against a bulk put that
  // wrote nothing at all -- which is the precise failure it was added to catch.
  //
  // The fix is a value no upstream can invent. A per-run sentinel callsign is
  // written through the SAME bulk-put path as the data, then read back through
  // the SAME live path; adsbdb has never heard of it and adsb.lol returns
  // nothing, so a resolved sentinel can only have come from this run's write.
  //
  // And it is probed BEFORE the write as well as after. A check that only ever
  // observes the present state cannot tell a working write from a stale hit --
  // the same reason the square probe has an anchor control. Absent-then-present
  // is the pair of observations that means something; either one alone does not.
  // TWO CALLSIGNS, NOT ONE, AND THE REASON IS A RACE THAT ONLY EXISTS BECAUSE
  // THE ANCHOR DRIVES A REAL LOOKUP.
  //
  // The negative cache lives at `rt:<cs>` itself -- enrich.ts reads it at :214
  // and writes it at :224, and those are the only two KV touches on the route
  // path, so a bulk put simply overwrites a cached miss. That much is safe.
  //
  // What is NOT safe is WHEN the miss gets written. handleEnrich races the route
  // lookup against a 2,500 ms serve deadline and, on a timeout, returns the empty
  // body while ctx.waitUntil keeps resolveRoute running -- so the negative write
  // can land at an arbitrary moment AFTER the probe's response. With adsb.lol's
  // routeset dead and adsbdb having to be asked about a callsign that does not
  // exist, overshooting 2,500 ms is the likely case, not the corner case.
  //
  // Probing and writing the SAME key therefore has a real ordering hazard: the
  // waitUntil negative write can land after our bulk put and blank the sentinel,
  // and no amount of retrying outruns it because the poison arrives later than
  // the write. It fails safe -- meta is not written -- but it aborts a good
  // 619k-key load and sends someone hunting a bug that is not there.
  //
  // So the anchor and the sentinel are different callsigns. The anchor is probed
  // and never written (any negative entry it collects is inert and expires on the
  // 24 h route TTL). The sentinel is never probed before the write, so nothing
  // can poison it -- and as a bonus no edge has cached a miss for it either, so
  // the readback only has to cover write propagation, not negative-cache expiry.
  //
  // Both live under `rt:ZZ`, which is safe as a scratch namespace: zero of the
  // corpus's 619,103 callsigns begin with ZZ (checked, not assumed).
  const runId = randomBytes(4).toString("hex").toUpperCase().slice(0, 5);
  const anchorCs = `ZZ${runId}A`;                        // probed, never written
  const sentinelCs = `ZZ${runId}S`;                      // written, never probed first
  const sentinelVal = JSON.stringify({ o: runId.slice(0, 3), d: `${runId.slice(3, 5)}Z` });

  const { base, enrich } = liveReader(args.env as string);

  // SWEEP STALE SENTINELS FIRST. A run that dies between the bulk put and the
  // cleanup at the end leaves one junk key behind, and that is precisely the
  // never-deleted accumulation documented in the diff block above. Clearing them
  // at the START rather than trusting the END means a crash costs nothing, and it
  // exercises the delete path every run instead of only on the happy path.
  try {
    const listed = execSync(
      ["node", KV_CLI, "kv", "key", "list", "--prefix=rt:ZZ",
        "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
      { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] },
    );
    const stale = (JSON.parse(listed) as { name: string }[]).map((k) => k.name);
    if (stale.length > 0) {
      const p = join(tmp, "stale.json");
      writeFileSync(p, JSON.stringify(stale));
      execSync(
        ["node", KV_CLI, "kv", "bulk", "delete", q(sh(p)), "--binding=ENRICH_KV",
          `--env=${args.env}`, "--remote", "--force"].join(" "),
        { stdio: "inherit" },
      );
      console.log(`swept ${stale.length} stale sentinel key(s) from previous runs`);
    }
  } catch (e) {
    // Not fatal: a failed sweep leaves junk, it does not corrupt anything. But it
    // is said out loud rather than swallowed, because silence here is how the
    // accumulation would go unnoticed.
    console.log(`WARN: could not sweep rt:ZZ -- ${(e as Error).message.slice(0, 120)}`);
  }

  // THE ANCHOR, BEFORE THE WRITE. A ZZ-prefixed callsign must resolve to NOTHING
  // through the live path. If it resolves, the read path is inventing routes and
  // the post-write check would pass for a reason unrelated to our write -- which
  // is worse than no check at all. Refuse before touching a single key.
  //
  // This is the observation that makes the later one mean something: a probe that
  // has only ever seen the present state cannot tell a good write from a stale
  // hit. Absent-here, present-there is the pair that carries the information.
  const pre = enrich(anchorCs);
  if (pre === null) {
    console.error(`REFUSING: the live path is unreachable at ${base} -- nothing written.`);
    process.exit(1);
  }
  if (pre.o || pre.d) {
    console.error(`REFUSING: anchor ${anchorCs} resolves to ${pre.o}-${pre.d} before any write.`);
    console.error("A nonsense callsign must not resolve; this probe cannot be trusted.");
    process.exit(1);
  }
  console.log(`anchor: ${anchorCs} is absent through the live path (as required)`);

  // ---- write the data keys, tallying what ACTUALLY went (rule 2) -----------
  //
  // The sentinel rides in the FIRST chunk, through the same bulk-put path as the
  // data. Writing it by some other route would prove that other route works.
  const toWrite: [string, string][] = [[`rt:${sentinelCs}`, sentinelVal], ...changedPairs];
  let written = 0;
  let retries = 0;
  for (let i = 0; i < toWrite.length; i += BULK_CHUNK) {
    const chunk = toWrite.slice(i, i + BULK_CHUNK);
    const path = join(tmp, `bulk-${i / BULK_CHUNK}.json`);
    writeFileSync(path, JSON.stringify(chunk.map(([k, value]) => ({ key: k, value }))));
    console.log(`bulk put ${chunk.length} (${i + chunk.length}/${toWrite.length}) ...`);

    // RETRIED, because without this a single transient upstream hiccup destroys a
    // 619,103-key load. Measured 2026-08-25: the first staging attempt died on
    // chunk 3 of 62 with a Cloudflare 524 (a gateway timeout on their own KV API,
    // returned as an HTML error page). At 62 chunks even a low per-chunk failure
    // rate makes a complete run unlikely, so retry is not a nicety here -- it is
    // what makes the operation possible at all.
    //
    // Backoff is long because a 524 means the far side is already struggling;
    // hammering it is how a transient failure becomes a sustained one.
    //
    // The loop lives in chunk-retry.ts so it can be proven to retry; the wait
    // between attempts is native (it was `execSync("sleep N")`, inside the
    // catch, where a sleep that threw escaped the retry entirely).
    const BACKOFF_S = [5, 15, 45, 120];
    const put = putWithBackoff(
      (c) => { execSync(c, { stdio: ["ignore", "pipe", "pipe"], encoding: "utf8" }); },
      ["node", KV_CLI, "kv", "bulk", "put", q(sh(path)), "--binding=ENRICH_KV",
        `--env=${args.env}`, "--remote"].join(" "),
      BACKOFF_S,
      sleepSync,
    );
    const ok = put.ok;
    const lastErr = put.lastErr;
    retries += put.retries;
    if (!ok) {
      console.error(`REFUSING: chunk at offset ${i} failed every retry. Nothing further is`);
      console.error("written and the meta key is untouched, so this run reads as the failure");
      console.error("it is. If 524s persist, lower BULK_CHUNK -- 10,000 is the API maximum,");
      console.error("not a target.");
      console.error(`--- last wrangler output ---\n${lastErr}\n----------------------------`);
      process.exit(1);
    }
    // Incremented only after the put SUCCEEDS.
    written += chunk.length;
  }
  if (retries > 0) console.log(`(${retries} chunk retries were needed)`);
  const writtenData = written - 1; // the sentinel is not a route
  console.log(`wrote ${writtenData} route keys (+1 sentinel)`);

  // ---- rule 3, part 2: the sentinel, read back through the LIVE path -------
  //
  // Retried, because KV is eventually consistent. 8 attempts at 15 s is a little
  // over two minutes.
  //
  // MEASURED ON STAGING 2026-08-25, and the measurement is the argument for the
  // anchor/sentinel split rather than a footnote to it. Two keys, same bulk put,
  // one difference -- whether the key had been read before it was written:
  //
  //     probed first, then written    visible after 62 s (attempt 5 of 8)
  //     never probed, then written    visible after  0 s (attempt 1, first try)
  //
  // The whole 62 s is the negative cache: reading a key that does not exist
  // teaches an edge to remember the miss, and Cloudflare's documented ceiling for
  // that is 60 s. Write propagation itself was not observable at this resolution.
  //
  // So the single-key design was not merely racy, it spent a minute per run
  // waiting out a miss it had caused itself, with the window only twice the delay
  // it was measuring. This key is never probed before the write, so the window is
  // ~120 s against an observed 0 s. The attempt and elapsed seconds are still
  // printed on success, so a regression shows up as a number rather than a hang.
  const expected = JSON.parse(sentinelVal) as { o: string; d: string };
  const startedAt = Date.now();
  let sentinelSeen = false;
  for (let attempt = 1; attempt <= 8 && !sentinelSeen; attempt++) {
    const got = enrich(sentinelCs);
    if (got && got.o === expected.o && got.d === expected.d) {
      sentinelSeen = true;
      const secs = Math.round((Date.now() - startedAt) / 1000);
      console.log(`sentinel: ${sentinelCs} -> ${got.o}-${got.d} on attempt ${attempt} after ${secs}s (PROVENANCE PROVED)`);
    } else if (attempt < 8) {
      sleepSync(15_000); // native: `execSync("sleep 15")` needed Git's sleep.exe on PATH
    }
  }
  if (!sentinelSeen) {
    console.error(`REFUSING to write the meta key: sentinel ${sentinelCs} never came back through`);
    console.error(`${base} after 2 minutes. The bulk put reported success and the live read path`);
    console.error("cannot see it, so a fresh row count would be a claim about unreadable data.");
    console.error(`The data keys ARE written. Re-run to retry; the sweep will clear ${sentinelCs}.`);
    process.exit(1);
  }

  // ---- rule 2, verified: sample the DATA keys, not just the tally ----------
  //
  // `written` counts chunks the CLI accepted without erroring, which is a weaker
  // thing than keys that exist. This reads a spread-out sample straight from the
  // namespace and compares byte-for-byte with what we handed it.
  const SAMPLE = 12;
  const stride = Math.max(1, Math.floor(changedPairs.length / SAMPLE));
  let sampleOk = 0;
  let sampleChecked = 0;
  for (let i = 0; i < changedPairs.length && sampleChecked < SAMPLE; i += stride) {
    const [k, expected] = changedPairs[i] as [string, string];
    sampleChecked++;
    try {
      const got = execSync(
        ["node", KV_CLI, "kv", "key", "get", q(k), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
        { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] },
      ).trim();
      if (got === expected) sampleOk++;
      else console.log(`  sample MISMATCH ${k}: expected ${expected} got ${got.slice(0, 80)}`);
    } catch (e) {
      const err = e as { stdout?: string; stderr?: string };
      console.log(`  sample MISSING ${k}: ${`${err.stdout ?? ""} ${err.stderr ?? ""}`.trim().slice(0, 120)}`);
    }
  }
  if (sampleChecked > 0 && sampleOk !== sampleChecked) {
    console.error(`REFUSING to write the meta key: ${sampleOk}/${sampleChecked} sampled keys matched.`);
    console.error("The bulk put claimed success for data that is not in the namespace.");
    process.exit(1);
  }
  console.log(`sample: ${sampleOk}/${sampleChecked} written keys read back byte-identical`);

  // ---- rule 3, part 3: the airline canaries -- LIVENESS, not provenance ----
  //
  // Deliberately kept, and deliberately demoted. These prove the enrich path
  // answers real scheduled callsigns end to end; they do NOT prove the answer
  // came from our mirror, because the adsbdb fallback is still wired in until
  // the cutover. Read them as "the endpoint works", never as "the mirror works"
  // -- the sentinel above is the only check that separates those two.
  let canaryOk = 0;
  for (const cs of CANARIES) {
    const j = enrich(cs);
    if (j && j.o && j.d) { canaryOk++; console.log(`  canary ${cs} -> ${j.o}-${j.d}`); }
    else if (j) console.log(`  canary ${cs} -> NO ROUTE`);
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
    ruleRev: RULE_REV,
    rows: totalRows,
    written: writtenData,
    builtAt: new Date().toISOString(),
    source: "vradarserver/standing-data (CC0 1.0)",
    sentinel: sentinelCs,
    // Per-shard hash AND row count. The count is not used by the diff -- it is
    // here so the next run can see a shard SHRINK, which is the one thing this
    // design cannot currently act on (see the deletion note at the top).
    shards: Object.fromEntries([...shards.entries()].map(([n, s]) => [n, `${s.hash}:${s.pairs.length}`])),
  };
  const metaPath = join(tmp, "meta.json");
  writeFileSync(metaPath, JSON.stringify([{ key: META_KEY, value: JSON.stringify(meta) }]));
  execSync(
    ["node", KV_CLI, "kv", "bulk", "put", q(sh(metaPath)), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
    { stdio: "inherit" },
  );
  console.log(`meta written: rows=${totalRows} written=${writtenData} sentinel=OK canaries=${canaryOk}/${CANARIES.length}`);

  // ---- cleanup: the sentinel is scaffolding, not data ----------------------
  //
  // Deleted AFTER the meta key, deliberately. Meta is the commit point; once it
  // is written the run has succeeded, and cleanup that fails must not undo that.
  // A failure here leaves exactly one junk key, which the next run's sweep
  // collects -- so the accumulation this guards against is bounded at one per
  // failed cleanup rather than one per run.
  // `kv bulk delete` for a single key, not `kv key delete`: only the bulk form
  // takes --force. The single-key form has no such flag and prompts for
  // confirmation, which in a script with no stdin is a hang, not a refusal.
  try {
    const delPath = join(tmp, "sentinel-delete.json");
    writeFileSync(delPath, JSON.stringify([`rt:${sentinelCs}`]));
    execSync(
      ["node", KV_CLI, "kv", "bulk", "delete", q(sh(delPath)), "--binding=ENRICH_KV",
        `--env=${args.env}`, "--remote", "--force"].join(" "),
      { stdio: ["ignore", "pipe", "pipe"] },
    );
    console.log(`cleanup: rt:${sentinelCs} deleted`);
  } catch (e) {
    console.log(`WARN: could not delete rt:${sentinelCs} -- ${(e as Error).message.slice(0, 120)}`);
    console.log("      Harmless: it is one key under rt:ZZ and the next run sweeps that prefix.");
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
