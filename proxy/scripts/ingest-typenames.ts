/**
 * ingest-typenames.ts -- friendly type-name backfill for the long tail.
 *
 * THE PROBLEM THIS SOLVES. A card shows a raw ICAO designator ("TWEN", "C140")
 * when three things miss at once: the feed carries no `desc`, `tn:<CODE>` is not
 * in KV, and the code isn't in the baked TYPE_NAMES table. adsbdb can't rescue it
 * either -- it 404s most light GA. TYPE_NAMES is hand-curated and deliberately
 * small (it ships in the Worker bundle), so it will never cover the ~1,600 types
 * that actually fly. This fills the gap from KV instead, where size is free.
 *
 * SOURCE. The Mictronics aircraft-database export -- the SAME zip ingest-mildb.ts
 * already downloads, under the same ODC-By 1.0 licence we already attribute on the
 * device config page and /credits. No new dependency, no new licence obligation.
 * Its `types.json` is NOT usable for this (its "desc" is the ICAO category code,
 * e.g. L1P/L2J -- not a name), so names are aggregated from the per-airframe `d`
 * field across ~447k rows: for each type, the most common non-empty description.
 *
 *   npm run ingest:typenames -- --dry-run              # aggregate + sample, no KV
 *   npm run ingest:typenames -- --dry-run --show 40    # inspect more samples
 *   npm run ingest:typenames -- --env staging          # bulk-load tn:<CODE>
 *
 * CURATION ALWAYS WINS. KV takes PRECEDENCE over TYPE_NAMES at request time (see
 * buildMeta in enrich.ts), so a blind bulk load would actively make things worse:
 * the aggregate for C140 is "140" and for C82S is "T182T", against our curated
 * "Cessna 140" and "Cessna T182 Turbo Skylane". This script therefore SKIPS every
 * code already present in TYPE_NAMES. It only ever fills gaps.
 */
import { execSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { unzipSync } from "fflate";
import { TYPE_NAMES } from "../src/typenames";

const EXPORT_URL =
  "https://raw.githubusercontent.com/Mictronics/aircraft-database/master/indexedDB_old.zip";

// Cloudflare's KV bulk API takes at most 10,000 pairs per request.
const BULK_CHUNK = 10_000;

// A name must be seen on at least this many airframes to be trusted. A single
// typo'd row shouldn't become the fleet-wide label for a type; two independent
// registrations agreeing is a low bar that still filters the worst noise.
const MIN_SUPPORT = 2;

// Longest name we'll publish. The card is a ~240 px round display, and anything
// past this is truncated on screen anyway -- better to fall through to the bare
// designator than to render a clipped half-word.
const MAX_NAME_LEN = 40;

interface SrcEntry {
  r?: string;
  t?: string;
  f?: string;
  d?: string;
}

interface Args {
  env?: string;
  dryRun: boolean;
  file?: string;
  show: number;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false, show: 25 };
  for (let i = 0; i < argv.length; i++) {
    const v = argv[i];
    if (v === "--env") a.env = argv[++i];
    else if (v === "--dry-run") a.dryRun = true;
    else if (v === "--file") a.file = argv[++i];
    else if (v === "--show") a.show = Number(argv[++i]);
    else throw new Error(`unknown argument: ${v}`);
  }
  if (!a.dryRun && !a.env) throw new Error("an upload run needs --env <name> (or use --dry-run)");
  return a;
}

function q(s: string): string {
  return `"${s.replace(/"/g, '\\"')}"`;
}

// Same rule as normType() in enrich.ts: the source marks an unconfirmed type with
// a trailing " ?" ("P8 ?"). Key on the bare code or the lookup never matches.
function normType(v: string): string {
  return v.toUpperCase().match(/^[A-Z0-9]+/)?.[0] ?? "";
}

// Tidy a raw per-airframe description into something worth putting on a card.
// Deliberately conservative: squash whitespace, drop obvious junk, and reject
// rather than guess. A bare designator beats a wrong or mangled name.
//
// The load-bearing rule is the "pronounceable word" test at the bottom. Roughly
// half of these descriptions are bare factory model numbers with no manufacturer
// -- "112A", "690B", "500-B", "A4D-2N", "S2R-T660". On a 240 px round display
// those read as noise, and they are strictly WORSE than falling through to the
// ICAO code, which at least looks like a type designator. Requiring one run of
// three consecutive letters keeps "Antonov An-124-100", "Aquila A.210" and
// "AIR CAM" while dropping all of the above.
//
// It also drops legitimate short names like the ICON "A5" -- but the fallback
// there is the code "A5", which is the same string, so the loss is nil. That
// asymmetry is why this filter is tuned aggressively: a false reject costs
// nothing, a false accept puts gibberish on a customer's card.
function cleanName(raw: string, code: string): string | null {
  const s = raw.replace(/\s+/g, " ").trim();
  if (!s) return null;
  // Some rows carry a placeholder instead of a model.
  if (/^(unknown|n\/a|none|test|private|tbd)$/i.test(s)) return null;
  if (s.length > MAX_NAME_LEN) return null;
  // Says nothing the designator doesn't already say.
  if (s.replace(/[^A-Za-z0-9]/g, "").toUpperCase() === code) return null;
  // The pronounceable-word test.
  if (!/[A-Za-z]{3}/.test(s)) return null;
  return s;
}

async function loadExport(file?: string): Promise<Record<string, SrcEntry>> {
  let zip: Uint8Array;
  if (file) {
    zip = new Uint8Array(readFileSync(file));
  } else {
    console.log(`downloading ${EXPORT_URL} ...`);
    const res = await fetch(EXPORT_URL);
    if (!res.ok) throw new Error(`export download failed: HTTP ${res.status}`);
    zip = new Uint8Array(await res.arrayBuffer());
  }
  const files = unzipSync(zip);
  const raw = files["aircrafts.json"];
  if (!raw) throw new Error("aircrafts.json not found in the export zip");
  return JSON.parse(new TextDecoder().decode(raw)) as Record<string, SrcEntry>;
}

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));
  const db = await loadExport(args.file);

  // type -> description -> count
  const byType = new Map<string, Map<string, number>>();
  for (const e of Object.values(db)) {
    const t = normType(String(e.t ?? ""));
    if (!t || t.length < 2 || t.length > 4) continue;
    const name = cleanName(String(e.d ?? ""), t);
    if (!name) continue;
    let counts = byType.get(t);
    if (!counts) byType.set(t, (counts = new Map()));
    counts.set(name, (counts.get(name) ?? 0) + 1);
  }

  const rows: { key: string; value: string }[] = [];
  const samples: string[] = [];
  let skippedCurated = 0;
  let skippedSupport = 0;
  for (const [t, counts] of [...byType].sort((a, b) => a[0].localeCompare(b[0]))) {
    if (TYPE_NAMES[t] !== undefined) {
      skippedCurated++; // curation always wins -- see the header
      continue;
    }
    let best = "";
    let bestN = 0;
    for (const [name, n] of counts) {
      if (n > bestN || (n === bestN && name.length < best.length)) {
        best = name;
        bestN = n;
      }
    }
    if (bestN < MIN_SUPPORT) {
      skippedSupport++;
      continue;
    }
    rows.push({ key: `tn:${t}`, value: best });
    if (samples.length < args.show) samples.push(`  ${t.padEnd(5)} ${String(bestN).padStart(6)}x  ${best}`);
  }

  console.log(`\nexport entries        : ${Object.keys(db).length.toLocaleString()}`);
  console.log(`distinct types w/ name: ${byType.size.toLocaleString()}`);
  console.log(`skipped (curated)     : ${skippedCurated}`);
  console.log(`skipped (support < ${MIN_SUPPORT}) : ${skippedSupport}`);
  console.log(`TO LOAD               : ${rows.length}\n`);
  console.log(`sample (code, airframes supporting the name, name):`);
  console.log(samples.join("\n"));

  if (args.dryRun || !args.env) {
    console.log("\ndry run: no KV writes");
    return;
  }

  const tmp = mkdtempSync(join(tmpdir(), "blip-tn-"));
  for (let i = 0; i < rows.length; i += BULK_CHUNK) {
    const chunk = rows.slice(i, i + BULK_CHUNK);
    const path = join(tmp, `bulk-${i / BULK_CHUNK}.json`);
    writeFileSync(path, JSON.stringify(chunk));
    console.log(`bulk put ${chunk.length} rows (${i + chunk.length}/${rows.length}) ...`);
    execSync(
      ["npx", "wrangler", "kv", "bulk", "put", q(path), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(
        " ",
      ),
      { stdio: "inherit" },
    );
  }
  console.log(`done: ${rows.length} tn:<CODE> rows loaded to ${args.env}`);
}

main().catch((err) => {
  console.error(String(err instanceof Error ? err.stack : err));
  process.exit(1);
});
