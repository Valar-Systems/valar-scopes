/**
 * verify-routes.ts -- prove every route key the mirror claims to have written is
 * actually in KV, and report it PER SHARD.
 *
 *   npm run verify:routes -- --env staging --file sd.tar.gz
 *
 * ============================================================================
 * WHY A SEPARATE TOOL, WHEN THE INGEST ALREADY CHECKS ITSELF
 *
 * The ingest's checks are real but small, and their weakness is specific:
 *
 *   written=619103   is the tally of chunks the CLI ACCEPTED, not keys that
 *                    exist. A silently dropped 10,000-key chunk still
 *                    increments it, because the increment happens on a
 *                    zero exit status.
 *   sample 12/12     is 0.002% of the set, on a fixed stride. Any given
 *                    dropped chunk has roughly a 1-in-60 chance of holding a
 *                    sampled key.
 *
 * Both pass against a load missing an entire chunk. So this enumerates the
 * WHOLE `rt:` namespace from the API and diffs it against the keys the corpus
 * says should exist -- authoritative on both sides, no sampling anywhere.
 *
 * PER SHARD, BECAUSE THAT IS WHERE A GAP WILL LIVE. A dropped chunk is 10,000
 * CONSECUTIVE keys, and pairs are built shard by shard in sorted order, so a
 * gap lands inside a handful of adjacent airlines. Reported globally that is
 * "98.4% present" -- a number comfortable enough to wave through. Reported per
 * shard it is a few airlines at 0% and everything else at 100%, which cannot be
 * waved through. Same asymmetry this project keeps meeting: a global percentage
 * buries a concentrated failure, and concentrated is how real failures arrive.
 */
import { execSync } from "node:child_process";
import { mkdtempSync, readFileSync, readdirSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { isGenuineSelfLoop, routeEndpoints } from "./routerule";
import { q, sh } from "./shquote";

const TARBALL = "https://codeload.github.com/vradarserver/standing-data/tar.gz/refs/heads/main";

interface Args {
  env?: string;
  file?: string;
}

function parseArgs(argv: string[]): Args {
  const a: Args = {};
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--env") a.env = argv[++i];
    else if (argv[i] === "--file") a.file = argv[++i];
  }
  return a;
}

/**
 * Quote-aware CSV split -- the same one ingest-routes.ts uses, and it must stay
 * the same one. If this file parsed the corpus differently from the writer, the
 * diff would report failures that are really disagreements between two parsers,
 * which is worse than no check at all.
 */
function splitCsvLine(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  let inQuotes = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i] as string;
    if (inQuotes) {
      if (c === '"') {
        if (line[i + 1] === '"') { cur += '"'; i++; }
        else inQuotes = false;
      } else cur += c;
    } else if (c === '"') inQuotes = true;
    else if (c === ",") { out.push(cur); cur = ""; }
    else cur += c;
  }
  out.push(cur);
  return out;
}

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

function csvFiles(dir: string, out: string[] = []): string[] {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) csvFiles(p, out);
    else if (name.toLowerCase().endsWith(".csv")) out.push(p);
  }
  return out;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (!args.env) {
    console.error("usage: --env <staging|production> [--file sd.tar.gz]");
    process.exit(2);
  }

  const tmp = mkdtempSync(join(tmpdir(), "verify-"));
  const tarPath = args.file ?? join(tmp, "sd.tar.gz");
  if (!args.file) {
    execSync(`curl -sL --max-time 300 ${q(TARBALL)} -o ${q(sh(tarPath))}`, { stdio: "inherit" });
  }
  execSync(`tar xzf ${q(sh(tarPath))} -C ${q(sh(tmp))} --force-local`, { stdio: "inherit" });
  const rootDirs = readdirSync(tmp).filter((d) => d.startsWith("standing-data-"));
  const root = join(tmp, rootDirs[0] as string);

  // ---- what SHOULD be there, per shard ------------------------------------
  const expectedByShard = new Map<string, Set<string>>();
  const expectedAll = new Set<string>();
  // Values too, not just keys -- presence is not correctness.
  // `circular` records whether the SOURCE row was genuinely all-same, which is
  // what separates reality from a self-loop the rule manufactured.
  const expectedValues = new Map<string, { o: string; d: string; circular: boolean }>();
  for (const fp of csvFiles(join(root, "routes")).sort()) {
    const shard = sh(fp.slice(root.length + 1));
    const keys = new Set<string>();
    for (const r of rows(readFileSync(fp, "utf8"))) {
      const cs = (r["Callsign"] ?? "").toUpperCase();
      const codes = r["AirportCodes"] ?? "";
      if (!cs || !codes || codes.toLowerCase() === "unknown") continue;
      if (codes.split("-").filter((p) => p.length > 0).length < 2) continue;
      const legs = codes.split("-").filter((p) => p.length > 0);
      const [o, d] = routeEndpoints(legs);
      keys.add(`rt:${cs}`);
      expectedAll.add(`rt:${cs}`);
      expectedValues.set(`rt:${cs}`, { o, d, circular: isGenuineSelfLoop(legs) });
    }
    if (keys.size > 0) expectedByShard.set(shard, keys);
  }
  console.log(`expected: ${expectedByShard.size} shards, ${expectedAll.size} distinct keys`);

  // ---- what IS there: one authoritative enumeration ------------------------
  //
  // --prefix=rt: bounds this to route keys; the namespace also holds ac:, pa:,
  // tn:, mil:, ovr: and meta:. wrangler paginates internally, so this is one
  // command and several hundred API pages.
  console.log("enumerating the whole rt: namespace (several minutes) ...");
  const listed = execSync(
    ["npx", "wrangler", "kv", "key", "list", "--prefix=rt:", "--binding=ENRICH_KV",
      `--env=${args.env}`, "--remote"].join(" "),
    { encoding: "utf8", stdio: ["ignore", "pipe", "inherit"], maxBuffer: 512 * 1024 * 1024 },
  );
  const actual = new Set((JSON.parse(listed) as { name: string }[]).map((k) => k.name));
  console.log(`actual:   ${actual.size} keys present under rt:`);

  // THE PROBE MUST PROVE IT CAN OBSERVE PRESENCE BEFORE ITS ABSENCES MEAN
  // ANYTHING. An empty or truncated listing is the single most likely shape of a
  // broken enumeration -- wrong directory, expired token, wrong binding -- and it
  // reports as "everything is missing", which reads as a catastrophe rather than
  // as a broken tool. Refuse to draw conclusions from a listing that returned
  // implausibly little.
  if (actual.size < expectedAll.size / 2) {
    console.error("");
    console.error(`REFUSING TO JUDGE: the listing returned ${actual.size} keys against`);
    console.error(`${expectedAll.size} expected. That is far more likely to be a broken`);
    console.error("enumeration than a half-empty namespace. Check the binding, the env and");
    console.error("the token before believing any coverage number from this run.");
    process.exit(2);
  }

  // ---- the diff, per shard -------------------------------------------------
  let missingTotal = 0;
  const badShards: { shard: string; have: number; want: number; sample: string[] }[] = [];
  for (const [shard, want] of expectedByShard) {
    const missing: string[] = [];
    for (const k of want) if (!actual.has(k)) missing.push(k);
    if (missing.length > 0) {
      missingTotal += missing.length;
      badShards.push({ shard, have: want.size - missing.length, want: want.size, sample: missing.slice(0, 3) });
    }
  }

  console.log("");
  console.log("================ COVERAGE ================");
  console.log(`shards complete: ${expectedByShard.size - badShards.length}/${expectedByShard.size}`);
  console.log(`keys present:    ${expectedAll.size - missingTotal}/${expectedAll.size}`);
  if (missingTotal === 0) {
    console.log("RESULT: PASS -- every expected key present, in every shard");
  } else {
    console.log(`RESULT: FAIL -- ${missingTotal} keys missing across ${badShards.length} shard(s)`);
    // Worst first: a dropped chunk shows up as adjacent shards near 0%.
    badShards.sort((a, b) => a.have / a.want - b.have / b.want);
    for (const b of badShards.slice(0, 25)) {
      console.log(`  ${b.shard}: ${b.have}/${b.want} (${Math.round((100 * b.have) / b.want)}%)  e.g. ${b.sample.join(", ")}`);
    }
    if (badShards.length > 25) console.log(`  ... and ${badShards.length - 25} more shard(s)`);
  }

  // ---- VALUE assertion: origin must not equal destination ------------------
  //
  // Presence is not correctness. A key can be present, byte-identical to what
  // the writer produced, and still say the aircraft flew from Dallas to Dallas.
  //
  // That is not hypothetical -- it shipped to staging. The inherited multi-leg
  // rule took the first and last leg of "KDFW-KBUR-KDFW" and rendered DFW-DFW,
  // on 9,249 routes. Every coverage check passed, because every one of them
  // asked "is the key there" and none asked "is the value meaningful". It was
  // caught by eyeballing three AAL flight numbers in a disagreement sample,
  // which is not a process.
  //
  // A self-loop is the one route value that is provably wrong without needing a
  // second source to compare against -- no scheduled service departs and arrives
  // at the same airport -- so it is exactly the kind of thing a verifier should
  // assert rather than a human should notice.
  // A self-loop that traces to an all-same SOURCE row is reality -- 38 of them
  // are training circuits, test and positioning flights (CWL91 at RAF Cranwell,
  // CLX789 at Schiphol). Those pass, and the card renders them as a local flight.
  //
  // A self-loop the RULE manufactured from a row containing DISTINCT airports is
  // a bug and stays a hard failure. That is the case that caught DLH8985
  // (EGTE-EGTE-EGTE under rev 2, where legs[1] was also EGTE), and narrowing the
  // assertion must not blunt it.
  console.log("checking values (a self-loop must trace to a circular source row) ...");
  const manufactured: string[] = [];
  let circular = 0;
  for (const [shard, want] of expectedByShard) {
    for (const k of want) {
      const v = expectedValues.get(k);
      if (!v || !v.o || v.o !== v.d) continue;
      if (v.circular) { circular++; continue; }
      manufactured.push(`${k} = ${v.o}-${v.d}  [${shard}]`);
    }
  }
  if (manufactured.length > 0) {
    console.log("");
    console.log(`RESULT: FAIL -- ${manufactured.length} self-loop(s) NOT present in the source`);
    manufactured.slice(0, 15).forEach((x) => console.log(`  ${x}`));
    if (manufactured.length > 15) console.log(`  ... and ${manufactured.length - 15} more`);
    console.log("These rows contain distinct airports, so the RULE produced the self-loop.");
    console.log("See routeEndpoints() in scripts/routerule.ts and bump RULE_REV when fixing.");
    process.exit(1);
  }
  console.log(`values: 0 manufactured self-loops (${circular} genuine circular routes, which are real)`);

  // Keys under rt: the corpus does not expect. NOT a failure: adsbdb-era entries
  // are still aging out on their 24 h TTL, and an anchor's negative entry lives
  // here too. Stated rather than silently ignored, because a large number here
  // would mean something else is writing routes.
  let extra = 0;
  for (const k of actual) if (!expectedAll.has(k)) extra++;
  console.log(`extra keys under rt: not in the corpus: ${extra} (adsbdb-era TTL entries, anchors)`);

  process.exit(missingTotal === 0 ? 0 : 1);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
