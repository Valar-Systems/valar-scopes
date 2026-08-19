/**
 * ingest-planealert.ts -- the CURATED military airframe loader (plane-alert-db).
 *
 * Downloads sdr-enthusiasts/plane-alert-db's military list (ODbL 1.0 --
 * attribution carried on the device config page and /credits alongside the
 * adsb.lol ODbL line and Mictronics' ODC-By line), and loads it into KV as
 * `pa:<hex>` rows.
 *
 *   npm run ingest:planealert -- --dry-run
 *   npm run ingest:planealert -- --env production
 *
 * WHY THIS EXISTS ALONGSIDE ingest-mildb.ts, WHICH LOOKS LIKE THE SAME JOB.
 *
 * Mictronics is a civil-registry aggregation. US military airframes are not in
 * civil registries by design, so for the block that matters most to US customers
 * it is thin: measured 2026-08-19, Mictronics has 170 P8* entries and
 * plane-alert-db has 269, of which 217 sit in the AE block. It also carries the
 * OPERATOR ("United States Navy"), which Mictronics has no column for at all.
 *
 * So for US military this is the PRIMARY source, not an enrichment overlay --
 * the reverse of how it was originally specified, changed on the measurement.
 * Mictronics stays primary for everything else, which is most of the world.
 *
 * PRECEDENCE IS A PROPERTY OF THE READ PATH, NOT OF RUN ORDER. These rows go to
 * their own `pa:` prefix and src/enrich.ts consults `pa:` before `mil:`. Writing
 * both loaders to one key and relying on "run plane-alert second" would work
 * exactly until someone re-ran the other one, and would then silently revert --
 * with nothing to see in either script.
 */
import { execSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const CSV_URL =
  "https://raw.githubusercontent.com/sdr-enthusiasts/plane-alert-db/main/plane-alert-mil.csv";

const BULK_CHUNK = 10_000;

/**
 * Registration values that are NOT registrations.
 *
 * plane-alert-db uses the registration column for a placeholder when the
 * airframe behind a hex is not a single known tail -- most often because the hex
 * is a TACTICAL CODE shared across a fleet rather than assigned to one aircraft.
 * Measured 2026-08-19: 153 of 9,839 rows fleet-wide, and 39 of the 64 rows in
 * the AE68xx US Navy P-8 block (61%, ~38x the fleet rate).
 *
 * Storing one of these would print "????" or "Tactical" on a customer's card as
 * that aircraft's registration, which is worse than the blank it replaces: a
 * blank field reads as "not known", and a filled one reads as a fact.
 *
 * Case-insensitive because the data is not consistent -- both "Tactical" and
 * "TACTICAL" appear in the same block.
 */
const NOT_A_REGISTRATION = new Set(["????", "tactical", "unknown", "n/a", "-", "none"]);

function isRealRegistration(r: string): boolean {
  const t = r.trim();
  return t.length > 0 && !NOT_A_REGISTRATION.has(t.toLowerCase());
}

/**
 * One CSV row. The file is comma-separated with no quoting in the fields we
 * read, but operator/type names can contain commas in principle, so split with a
 * limit and keep the leading fixed columns only.
 */
interface Row {
  hex: string;
  reg: string;
  op: string;
  typeName: string;
  icaoType: string;
}

function parseLine(line: string): Row | null {
  const f = line.split(",");
  if (f.length < 5) return null;
  const hex = (f[0] ?? "").trim().toLowerCase();
  if (!/^[0-9a-f]{6}$/.test(hex)) return null;
  return {
    hex,
    reg: (f[1] ?? "").trim(),
    op: (f[2] ?? "").trim(),
    typeName: (f[3] ?? "").trim(),
    icaoType: (f[4] ?? "").trim().toUpperCase(),
  };
}

interface Args { env?: string; dryRun: boolean; file?: string }

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false };
  for (let i = 0; i < argv.length; i++) {
    const v = argv[i];
    if (v === "--env") a.env = argv[++i];
    else if (v === "--dry-run") a.dryRun = true;
    else if (v === "--file") a.file = argv[++i];
    else throw new Error(`unknown argument: ${v}`);
  }
  if (!a.dryRun && !a.env) throw new Error("an upload run needs --env <name> (or use --dry-run)");
  return a;
}

// Same shell-quoting workaround as ingest-mildb.ts / ingest-photos.ts.
function q(s: string): string { return `"${s.replace(/"/g, '\\"')}"`; }

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));

  let text: string;
  if (args.file) {
    text = readFileSync(args.file, "utf8");
  } else {
    console.log(`downloading ${CSV_URL} ...`);
    const res = await fetch(CSV_URL);
    if (!res.ok) throw new Error(`download failed: ${res.status}`);
    text = await res.text();
  }

  const lines = text.split(/\r?\n/).slice(1); // drop the header
  const rows: { key: string; value: string }[] = [];
  let placeholderRegs = 0;
  let noContent = 0;

  for (const line of lines) {
    const r = parseLine(line);
    if (!r) continue;

    const row: Record<string, string> = {};
    if (isRealRegistration(r.reg)) row.r = r.reg;
    else if (r.reg.trim()) placeholderRegs++;
    if (/^[A-Z0-9]+$/.test(r.icaoType)) row.t = r.icaoType;
    if (r.typeName) row.tn = r.typeName;
    if (r.op) row.op = r.op;

    // A row with nothing but a hex tells the card nothing the military operator
    // floor does not already say, so it is not worth a KV read at serve time.
    if (!row.r && !row.t && !row.op) { noContent++; continue; }
    rows.push({ key: `pa:${r.hex}`, value: JSON.stringify(row) });
  }

  console.log(
    `selected ${rows.length} rows from ${lines.length} CSV lines ` +
      `(${placeholderRegs} placeholder registrations dropped, ${noContent} rows with no usable content)`,
  );

  if (args.dryRun || !args.env) { console.log("dry run: no KV writes"); return; }

  const tmp = mkdtempSync(join(tmpdir(), "blip-pa-"));
  for (let i = 0; i < rows.length; i += BULK_CHUNK) {
    const chunk = rows.slice(i, i + BULK_CHUNK);
    const path = join(tmp, `bulk-${i / BULK_CHUNK}.json`);
    writeFileSync(path, JSON.stringify(chunk));
    console.log(`bulk put ${chunk.length} rows (${i + chunk.length}/${rows.length}) ...`);
    execSync(
      ["npx", "wrangler", "kv", "bulk", "put", q(path), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
      { stdio: "inherit" },
    );
  }
  console.log(`done: ${rows.length} pa:<hex> rows loaded to ${args.env}`);
}

main().catch((err) => {
  console.error(String(err instanceof Error ? err.stack : err));
  process.exit(1);
});
