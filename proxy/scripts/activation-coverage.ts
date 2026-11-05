/**
 * activation-coverage.ts -- the control that gates #548's deploy (docs/device-activation.md, "Seeding before the
 * deploy"): every device that has authenticated in production in the last 30 days must have an `act:` row, or the
 * deploy stops. Any device without one would be refused the next time it activated.
 *
 *   npx tsx scripts/activation-coverage.ts [--env production|staging] [--days 30]
 *
 * READ-ONLY. Its credentials are the kv-read and analytics-read lines of the token file (token-file.ts). Nothing is
 * written anywhere.
 *
 * WHO AUTHENTICATED, from two instruments, unioned. fw: alone misses a device that authenticates without sending
 * a firmware version:
 *   - Analytics Engine: blob5 is the VERIFIED device id on per-request and boot points (setDeviceAttribution runs
 *     only past the auth gate). Other point types put non-id values in blob5, so only 16-hex values count.
 *   - fw: rows (src/fleet.ts) whose lastSeen is inside the window.
 * Ids on the repo's fake-id allowlist are excluded: production refuses them at the gate.
 *
 * PRINTS NO ID: counts and one boolean. Exit 0 none missing, 1 some missing, 2 cannot see (an anchor failed:
 * a source returned nothing, or the KV read cannot see cfg:fleet). "Cannot see" is never a pass.
 */
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { FAKE_DEVICE_IDS } from "../src/fakeids.generated";
import { coverage } from "./activation-lib";
import { bulkGet, kvTargetFromWranglerToml, listKeys, readControls } from "./kv-rest";
import { readTokenFile, TokenFileError } from "./token-file";

const ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"; // same as wrangler.toml account_id
const DATASET: Record<string, string> = { production: "blipscope_proxy", staging: "blipscope_proxy_staging" };
const ID16 = /^[0-9a-f]{16}$/;
const AE_LIMIT = 10000;

function cannotSee(msg: string): never {
  console.error(`activation-coverage: CANNOT SEE -- ${msg}`);
  process.exit(2);
}

const argv = process.argv.slice(2);
const opt = (name: string, dflt: string) => {
  const i = argv.indexOf(name);
  return i >= 0 && argv[i + 1] ? (argv[i + 1] as string) : dflt;
};
const env = opt("--env", "production");
const days = Number(opt("--days", "30"));
if (!DATASET[env] || !Number.isInteger(days) || days < 1 || days > 90) {
  console.error("usage: activation-coverage.ts [--env production|staging] [--days 1-90]");
  process.exit(2);
}

async function aeDevices(token: string): Promise<Set<string>> {
  const sql = `SELECT DISTINCT blob5 AS dev FROM ${DATASET[env]} ` +
    `WHERE timestamp > NOW() - INTERVAL '${days}' DAY AND blob5 != '' LIMIT ${AE_LIMIT}`;
  const res = await fetch(`https://api.cloudflare.com/client/v4/accounts/${ACCOUNT}/analytics_engine/sql`,
    { method: "POST", headers: { Authorization: `Bearer ${token}` }, body: sql });
  if (!res.ok) cannotSee(`Analytics Engine answered HTTP ${res.status}`);
  const body = (await res.json()) as { data?: { dev?: string }[] };
  const rows = body.data ?? [];
  if (rows.length >= AE_LIMIT) cannotSee(`Analytics Engine returned the limit (${AE_LIMIT}); the set may be cut`);
  const out = new Set<string>();
  for (const r of rows) {
    const id = String(r.dev ?? "").toLowerCase();
    if (ID16.test(id) && !FAKE_DEVICE_IDS.has(id)) out.add(id);
  }
  return out;
}

async function main(): Promise<void> {
  let kvToken: string, aeToken: string;
  try {
    kvToken = readTokenFile("kv-read");
    aeToken = readTokenFile("analytics-read");
  } catch (e) {
    cannotSee(e instanceof TokenFileError ? e.message : "the token file could not be read");
  }
  let testDevice: string | null = null;
  try {
    testDevice = readTokenFile("test-device").trim().toLowerCase();
  } catch {
    testDevice = null; // reported as "unknown", never guessed
  }

  const t = kvTargetFromWranglerToml(join(dirname(fileURLToPath(import.meta.url)), "..", "wrangler.toml"), env, kvToken!);
  const anchor = await readControls(t, "cfg:fleet");
  if (!anchor.knownPresentReadsBack || !anchor.knownAbsentReadsNull) {
    cannotSee(`the KV anchor failed (cfg:fleet present: ${anchor.knownPresentReadsBack}, ` +
      `absent key null: ${anchor.knownAbsentReadsNull})${anchor.error ? `: ${anchor.error.slice(0, 120)}` : ""}`);
  }

  const ae = await aeDevices(aeToken!);
  const fwIds = [...(await listKeys(t, "fw:"))].map((k) => k.slice(3)).filter((id) => ID16.test(id));
  const fwRows = await bulkGet(t, fwIds.map((id) => `fw:${id}`));
  const since = Date.now() - days * 86400000;
  const fw = new Set<string>();
  for (const id of fwIds) {
    try {
      const rec = JSON.parse(fwRows[`fw:${id}`] ?? "null") as { lastSeen?: number } | null;
      if (rec && typeof rec.lastSeen === "number" && rec.lastSeen >= since && !FAKE_DEVICE_IDS.has(id)) fw.add(id);
    } catch {
      // an unreadable row is not evidence either way; it is not counted as seen
    }
  }
  if (ae.size === 0) cannotSee(`Analytics Engine returned no device ids for ${days} days`);
  if (fw.size === 0) cannotSee(`no fw: row has a lastSeen inside ${days} days`);

  const seen = new Set([...ae, ...fw]);
  const listed = new Set([...(await listKeys(t, "act:"))].map((k) => k.slice(4)));
  const ledger = new Set([...(await listKeys(t, "enr:dev:"))].map((k) => k.slice(8)));
  const c = coverage(seen, listed, ledger, testDevice);

  console.log(`activation coverage (${env}, last ${days} days)`);
  console.log(`  authenticated: Analytics Engine ${ae.size}, fw: ${fw.size}, union ${c.union}`);
  console.log(`  with an act: row ${c.listed}, MISSING ${c.missing}`);
  console.log(`    missing with an enrolment-ledger row (self-enrolled): ${c.missingWithLedgerRow}`);
  console.log(`    missing without one: ${c.missingWithoutLedgerRow}`);
  console.log(`    the test identity among the missing: ${c.testIdentityMissing === null ? "unknown (no test-device line)" : c.testIdentityMissing}`);
  console.log(c.missing === 0 ? "VERDICT: every authenticated device has a row" : "VERDICT: MISSING ROWS -- the deploy stops");
  process.exit(c.missing === 0 ? 0 : 1);
}

main().catch((e) => cannotSee(String(e instanceof Error ? e.message : e).replace(/[0-9a-f]{16}/gi, "<id>").slice(0, 300)));
