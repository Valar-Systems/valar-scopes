/**
 * verify-airports.ts -- prove every `ap:` key the ingest claims to have written
 * is actually in KV, and report it PER SHARD.
 *
 *   npm run verify:airports -- --env staging
 *   npm run verify:airports -- --env production --file airports.csv   # offline source
 *
 * ============================================================================
 * WHY A SEPARATE TOOL, WHEN THE INGEST ALREADY CHECKS ITSELF
 *
 * The same weakness verify-routes.ts was written for. The ingest's `written`
 * tally counts chunks the CLI ACCEPTED, not keys that exist -- a silently
 * dropped 10,000-key bulk put still increments it, because the increment happens
 * on a zero exit status. And its canaries are four fields out of 39,000.
 *
 * Both pass against a load missing an entire chunk. So this enumerates the whole
 * `ap:` namespace from the API and diffs it against the codes the corpus says
 * should exist -- authoritative on both sides, no sampling anywhere.
 *
 * PER SHARD, BECAUSE THAT IS WHERE A GAP WILL LIVE. A dropped chunk is thousands
 * of CONSECUTIVE keys in sorted order, so it lands inside one or two
 * first-character shards. Reported globally that is "97% present", a number
 * comfortable enough to wave through; reported per shard it is one letter at 0%
 * and everything else at 100%, which cannot be.
 *
 * ============================================================================
 * AND IT REFUSES TO JUDGE A LISTING IT CANNOT TRUST
 *
 * "Everything is missing" is the single most likely shape of a BROKEN PROBE --
 * wrong binding, wrong env, expired token, wrong directory. Every one of those
 * reports as a fleet-wide emergency rather than as a tool that did not run. So
 * an implausibly small listing exits 2 and states that it cannot distinguish the
 * two, which is the true statement. See CLAUDE.md: a probe that reports absence
 * must first prove it can observe presence.
 */
import { execSync } from "node:child_process";
import { readFileSync } from "node:fs";

const CSV_URL = "https://davidmegginson.github.io/ourairports-data/airports.csv";

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
  if (!a.env) throw new Error("--env <name> is required");
  return a;
}

// Same minimal CSV parser as the ingest. Duplicated deliberately: this tool must
// be able to disagree with the ingest, and sharing its parser would guarantee
// the two make identical mistakes about the same file.
function parseCsvLine(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  let inQuotes = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (inQuotes) {
      if (c === '"' && line[i + 1] === '"') { cur += '"'; i++; }
      else if (c === '"') inQuotes = false;
      else cur += c;
    } else if (c === '"') inQuotes = true;
    else if (c === ",") { out.push(cur); cur = ""; }
    else cur += c;
  }
  out.push(cur);
  return out;
}

const KINDS = new Set(["large_airport", "medium_airport", "small_airport"]);
const CODE_RE = /^[A-Z0-9]{3,4}$/;

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));

  const csv = args.file
    ? readFileSync(args.file, "utf8")
    : await (async () => {
        console.log(`downloading ${CSV_URL} ...`);
        const res = await fetch(CSV_URL);
        if (!res.ok) throw new Error(`download failed: ${res.status}`);
        return res.text();
      })();

  const lines = csv.split("\n");
  const header = parseCsvLine(lines[0]!);
  const idx = (n: string) => {
    const i = header.indexOf(n);
    if (i < 0) throw new Error(`column ${n} missing`);
    return i;
  };
  const cType = idx("type"), cLat = idx("latitude_deg"), cLon = idx("longitude_deg");
  const cIata = idx("iata_code"), cIdent = idx("ident");

  // Rebuild the EXPECTED code set from the corpus, applying the same admission
  // rules as the ingest. Conflicts are recomputed rather than assumed: a code
  // the ingest dropped must not be reported here as missing.
  const claimed = new Map<string, { ident: string; src: "iata" | "icao" }>();
  const dropped = new Set<string>();
  for (let i = 1; i < lines.length; i++) {
    const line = lines[i]!;
    if (!line.trim()) continue;
    const f = parseCsvLine(line);
    if (!KINDS.has(f[cType] ?? "")) continue;
    const lat = parseFloat(f[cLat] ?? ""), lon = parseFloat(f[cLon] ?? "");
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) continue;
    if (Math.abs(lat) > 90 || Math.abs(lon) > 180) continue;
    if (lat === 0 && lon === 0) continue;

    const ident = (f[cIdent] ?? "").trim().toUpperCase();
    const iata = (f[cIata] ?? "").trim().toUpperCase();
    const forms: { code: string; src: "iata" | "icao" }[] = [];
    if (iata) forms.push({ code: iata, src: "iata" });
    if (ident && ident !== iata) forms.push({ code: ident, src: "icao" });

    for (const { code, src } of forms) {
      if (!CODE_RE.test(code)) continue;
      const prior = claimed.get(code);
      if (prior && prior.ident !== ident) {
        if (src === "iata" && prior.src === "icao") { claimed.set(code, { ident, src }); continue; }
        if (src === "icao" && prior.src === "iata") continue;
        claimed.delete(code);
        dropped.add(code);
        continue;
      }
      if (!dropped.has(code)) claimed.set(code, { ident, src });
    }
  }
  const expected = new Set([...claimed.keys()].map((c) => `ap:${c}`));
  console.log(`expected: ${expected.size} keys under ap: (${dropped.size} codes dropped to collisions)`);

  console.log("enumerating the whole ap: namespace ...");
  const listed = execSync(
    ["npx", "wrangler", "kv", "key", "list", "--prefix=ap:", "--binding=ENRICH_KV",
     `--env=${args.env}`, "--remote"].join(" "),
    { encoding: "utf8", stdio: ["ignore", "pipe", "inherit"], maxBuffer: 512 * 1024 * 1024 },
  );
  const actual = new Set((JSON.parse(listed) as { name: string }[]).map((k) => k.name));
  console.log(`actual:   ${actual.size} keys present under ap:`);

  // THE ANCHOR. Refuse to draw a conclusion from a listing that is far too small
  // to be real -- see the header. Exits 2 (untrustworthy), which is distinct
  // from exit 1 (a real coverage gap), so a caller can tell them apart.
  if (actual.size < expected.size / 2) {
    console.error("");
    console.error(`REFUSING TO JUDGE: the listing returned ${actual.size} keys against`);
    console.error(`${expected.size} expected. That is far more likely to be a broken`);
    console.error("enumeration than a half-empty namespace. Check the binding, the env and");
    console.error("the credentials before believing any coverage number from this run.");
    process.exit(2);
  }

  // ---- the diff, per shard (first character of the code) -------------------
  const byShard = new Map<string, Set<string>>();
  for (const k of expected) {
    const shard = k.slice(3, 4);
    let s = byShard.get(shard);
    if (!s) byShard.set(shard, (s = new Set()));
    s.add(k);
  }

  let missingTotal = 0;
  const bad: { shard: string; have: number; want: number; sample: string[] }[] = [];
  for (const [shard, want] of byShard) {
    const missing: string[] = [];
    for (const k of want) if (!actual.has(k)) missing.push(k);
    if (missing.length > 0) {
      missingTotal += missing.length;
      bad.push({ shard, have: want.size - missing.length, want: want.size, sample: missing.slice(0, 3) });
    }
  }

  console.log("");
  console.log("================ COVERAGE ================");
  console.log(`shards complete: ${byShard.size - bad.length}/${byShard.size}`);
  console.log(`keys present:    ${expected.size - missingTotal}/${expected.size}`);

  if (missingTotal === 0) {
    console.log("RESULT: PASS -- every expected key present, in every shard");
  } else {
    console.log(`RESULT: FAIL -- ${missingTotal} keys missing across ${bad.length} shard(s)`);
    bad.sort((a, b) => a.have / a.want - b.have / b.want);
    for (const b of bad) {
      console.log(
        `  ${b.shard}: ${b.have}/${b.want} (${Math.round((100 * b.have) / b.want)}%)  e.g. ${b.sample.join(", ")}`,
      );
    }
    console.log("");
    console.log("REPAIR: re-run the ingest with the failing shard letters, e.g.");
    console.log(`  npx tsx scripts/ingest-airports.ts --env ${args.env} --only ap --force-shard ${bad.map((b) => b.shard).join(",")}`);
    console.log("then run this again. --force-shard exists because the diff would");
    console.log("otherwise skip a shard whose HASH is current but whose keys are not.");
    process.exit(1);
  }

  // A code the corpus does not carry is not an error -- ap:ZZZZ is the ingest's
  // sentinel, and older builds may leave codes behind that a later corpus drops.
  let extra = 0;
  for (const k of actual) if (!expected.has(k)) extra++;
  console.log(`extra keys under ap: not in the corpus: ${extra} (sentinel, retired codes)`);
}

main().catch((e) => {
  console.error(String(e instanceof Error ? e.stack : e));
  process.exit(2);
});
