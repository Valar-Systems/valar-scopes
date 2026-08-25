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
 * Multi-leg routes ("VHHH-UACC-EBLG") take the FIRST leg as origin and the LAST
 * as destination, matching the parser this replaces.
 */
import { execSync } from "node:child_process";
import { createHash, randomBytes } from "node:crypto";
import { mkdtempSync, readFileSync, writeFileSync, readdirSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { q, sh } from "./shquote";

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
  console.log(`airports: ${icaoToIata.size} ICAO->IATA mappings, ${airportSkipped} malformed`);
  // A handful of odd rows is the source's business. A flood means the columns
  // moved under us, and every route code downstream would be quietly wrong.
  if (airportSkipped > icaoToIata.size / 100) {
    console.error(`REFUSING: ${airportSkipped} malformed airport rows is over 1% -- the CSV columns`);
    console.error("have almost certainly shifted, which would corrupt every code silently.");
    process.exit(1);
  }

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
    const cmd = ["npx", "wrangler", "kv", "key", "get", q(META_KEY),
      "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" ");
    let raw: string | null = null;
    try {
      raw = execSync(cmd, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
    } catch (e) {
      const err = e as { status?: number; stdout?: string; stderr?: string };
      const blob = `${err.stdout ?? ""}\n${err.stderr ?? ""}`;
      // wrangler's not-found wording, matched loosely because it has changed
      // between releases. Everything else -- auth, network, config -- is fatal.
      const notFound = /not found|does not exist|key .* not/i.test(blob);
      if (!notFound) {
        console.error(`REFUSING: could not read ${META_KEY} from ${args.env}, and this is NOT a`);
        console.error("clean not-found. Treating it as a first run would blind-write every key.");
        console.error(`exit status: ${err.status ?? "?"}`);
        console.error(`--- wrangler output ---\n${blob.trim()}\n-----------------------`);
        process.exit(1);
      }
      console.log(`no ${META_KEY} in ${args.env} -- confirmed absent, so this is a FULL build`);
    }
    if (raw !== null) {
      // A successful exit still has to yield the shape we expect. wrangler has
      // been known to put a banner on stdout ahead of the value; a parse failure
      // here is a reason to stop, not a reason to assume a first run.
      try {
        const parsed = JSON.parse(raw) as { shards?: Record<string, string> };
        if (!parsed || typeof parsed !== "object" || !parsed.shards) {
          throw new Error("no `shards` field in the meta value");
        }
        prevManifest = parsed.shards;
      } catch (e) {
        console.error(`REFUSING: ${META_KEY} read back but did not parse as a manifest.`);
        console.error(`reason: ${(e as Error).message}`);
        console.error(`--- first 200 bytes ---\n${raw.slice(0, 200)}\n-----------------------`);
        process.exit(1);
      }
      console.log(`previous manifest: ${Object.keys(prevManifest).length} shards`);
    }
  }

  // Manifest values are "<hash>:<rowcount>". Compare on the HASH half only --
  // reading the whole string would make every shard look changed the first time
  // the format moved, and a diff that silently degrades to a full rewrite is
  // indistinguishable from a diff that is working.
  const prevHash = (name: string) => (prevManifest[name] ?? "").split(":")[0] ?? "";
  const prevCount = (name: string) => Number((prevManifest[name] ?? "").split(":")[1] ?? NaN);
  const changed = [...shards.entries()].filter(([name, s]) => prevHash(name) !== s.hash);
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

  const base = args.env === "production"
    ? "https://scopes.valarsystems.com"
    : "https://scopes-staging.valarsystems.com";
  const key = process.env["BLIP_KEY"] ?? "";
  const device = process.env["BLIP_DEVICE"] ?? "";
  if (!key || !device) {
    console.error("REFUSING: BLIP_KEY/BLIP_DEVICE are unset, so nothing can be read back through");
    console.error("the live path. An unverified build must not publish a fresh row count.");
    process.exit(1);
  }

  /** GET /v1/enrich for one callsign. Returns null when the request itself failed. */
  const enrich = (cs: string): { o?: string; d?: string } | null => {
    const url = `${base}/v1/enrich/000000?cs=${cs}`;
    try {
      const body = execSync(
        `curl -s --max-time 25 -H ${q(`X-Blip-Key: ${key}`)} -H ${q(`X-Blip-Device: ${device}`)} ${q(url)}`,
        { encoding: "utf8" },
      );
      return JSON.parse(body) as { o?: string; d?: string };
    } catch (e) {
      console.log(`  ! ${cs} request failed: ${(e as Error).message.slice(0, 120)}`);
      return null;
    }
  };

  // SWEEP STALE SENTINELS FIRST. A run that dies between the bulk put and the
  // cleanup at the end leaves one junk key behind, and that is precisely the
  // never-deleted accumulation documented in the diff block above. Clearing them
  // at the START rather than trusting the END means a crash costs nothing, and it
  // exercises the delete path every run instead of only on the happy path.
  try {
    const listed = execSync(
      ["npx", "wrangler", "kv", "key", "list", "--prefix=rt:ZZ",
        "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
      { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] },
    );
    const stale = (JSON.parse(listed) as { name: string }[]).map((k) => k.name);
    if (stale.length > 0) {
      const p = join(tmp, "stale.json");
      writeFileSync(p, JSON.stringify(stale));
      execSync(
        ["npx", "wrangler", "kv", "bulk", "delete", q(sh(p)), "--binding=ENRICH_KV",
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
  for (let i = 0; i < toWrite.length; i += BULK_CHUNK) {
    const chunk = toWrite.slice(i, i + BULK_CHUNK);
    const path = join(tmp, `bulk-${i / BULK_CHUNK}.json`);
    writeFileSync(path, JSON.stringify(chunk.map(([k, value]) => ({ key: k, value }))));
    console.log(`bulk put ${chunk.length} (${i + chunk.length}/${toWrite.length}) ...`);
    execSync(
      ["npx", "wrangler", "kv", "bulk", "put", q(sh(path)), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
      { stdio: "inherit" },
    );
    // Incremented only after the put RETURNS. execSync throws on a non-zero
    // exit, so a failed chunk aborts the run with the meta key untouched.
    written += chunk.length;
  }
  const writtenData = written - 1; // the sentinel is not a route
  console.log(`wrote ${writtenData} route keys (+1 sentinel)`);

  // ---- rule 3, part 2: the sentinel, read back through the LIVE path -------
  //
  // Retried, because KV is eventually consistent. The window is sized against
  // Cloudflare's documented "up to 60 s" for a write to become globally visible,
  // doubled -- 8 attempts at 15 s is a little over two minutes.
  //
  // THAT NUMBER IS A DOCUMENTED CEILING, NOT AN OBSERVED ONE, and it should not
  // be treated as measured until it has been. So the attempt and the elapsed
  // seconds are printed on success: the staging run is the first real data point,
  // and if it lands on attempt 1 every time the window can be argued down from
  // evidence rather than trimmed on a hunch.
  //
  // Note this key was never probed before the write, so no edge has cached a miss
  // for it. The window therefore only has to cover write propagation -- negative
  // cache expiry, which is the slower of the two, is not in play at all.
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
      execSync("sleep 15", { stdio: "ignore" });
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
        ["npx", "wrangler", "kv", "key", "get", q(k), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
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
    ["npx", "wrangler", "kv", "bulk", "put", q(sh(metaPath)), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
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
      ["npx", "wrangler", "kv", "bulk", "delete", q(sh(delPath)), "--binding=ENRICH_KV",
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
