// A1's retention rule, read back from the bucket (ruled 2026-11-06 ~02:40Z, Daniel's decision: stored packages are
// kept 30 days, then deleted automatically, by an R2 lifecycle rule counted from upload). The check passes only when
// an ENABLED rule expires objects exactly 30 days after upload and covers the packages' prefix (bm/v1/pkg/), and no
// other enabled expiry rule covers it. Other rules are listed and left alone: R2 gives every bucket a default
// multipart-abort rule.
//
//   node scripts/check-pkg-lifecycle.mjs <bucket>    (from proxy/)  exit 0 OK, 1 MISSING or CONFLICT, 2 BLIND
//   node scripts/check-pkg-lifecycle.mjs --selftest  planted listings only (no wrangler): exit 0 all as registered, 1 off
//
// BLIND, never MISSING, unless wrangler both exited 0 AND printed a result: its "Listing lifecycle rules for bucket"
// line is printed BEFORE the fetch, so that line alone proves nothing, and a probe that cannot read the bucket would
// otherwise report every rule absent. The listing format is wrangler 4.35.0's (tableFromLifecycleRulesResponse).
import { spawnSync } from "node:child_process";

const PKG_PREFIX = "bm/v1/pkg/", DAYS = 30;

export function judge(stdout, status, bucket) {
  const lines = stdout.replace(/\r/g, "").split("\n");
  const rules = [];
  for (const l of lines) {
    const m = /^(name|enabled|prefix|action):\s+(.*)$/.exec(l.trim());
    if (!m) continue;
    if (m[1] === "name") rules.push({});
    if (rules.length) rules[rules.length - 1][m[1]] = m[2].trim();
  }
  const none = lines.some((l) => l.trim() === `There are no lifecycle rules for bucket '${bucket}'.`);
  const listed = lines.some((l) => l.trim() === `Listing lifecycle rules for bucket '${bucket}'...`);
  if (status !== 0 || !listed || (!none && rules.length === 0)) {
    return { verdict: "BLIND", why: status !== 0 ? `wrangler exited ${status}` : !listed ? `no listing for '${bucket}'` : "no rules and no 'no rules' line", rules };
  }
  const covers = (r) => r.enabled === "Yes" && (r.prefix === "(all prefixes)" || PKG_PREFIX.startsWith(r.prefix));
  const expiry = (r) => /^Expire objects (after (\d+) days|on \d{4}-\d{2}-\d{2})/.exec(r.action ?? "");
  const ours = rules.filter((r) => covers(r) && expiry(r)?.[2] === String(DAYS));
  const others = rules.filter((r) => covers(r) && expiry(r) && !ours.includes(r));
  const verdict = ours.length === 0 ? "MISSING" : others.length ? "CONFLICT" : "OK";
  return { verdict, why: verdict === "OK" ? `'${ours[0].name}' expires objects after ${DAYS} days, ${ours[0].prefix}` : verdict === "MISSING" ? `no enabled ${DAYS}-day expiry covers ${PKG_PREFIX}` : `another expiry also covers ${PKG_PREFIX}: ${others.map((r) => `'${r.name}' (${r.action})`).join(", ")}`, rules };
}

function read(bucket) {
  const r = spawnSync(`npx wrangler r2 bucket lifecycle list ${bucket}`, { shell: true, encoding: "utf8" });
  return { stdout: r.stdout ?? "", stderr: r.stderr ?? "", status: r.status };
}

const HEAD = (b) => ` ⛅️ wrangler 4.35.0\n──────────────────────────────────────────────\nListing lifecycle rules for bucket '${b}'...\n`;
const DEFAULT = "name:     Default Multipart Abort Rule\nenabled:  Yes\nprefix:   (all prefixes)\naction:   Abort incomplete multipart uploads after 7 days";
const rule = (name, enabled, prefix, action) => `name:     ${name}\nenabled:  ${enabled}\nprefix:   ${prefix}\naction:   ${action}`;
const OURS = rule("bm-pkg-30d", "Yes", "(all prefixes)", "Expire objects after 30 days");

function selftest() {
  const b = "blipscope-basemap-pkg-staging";
  const CASES = [
    ["C-OK", HEAD(b) + [DEFAULT, OURS].join("\n\n"), 0, "OK"],
    ["L-ABSENT", HEAD(b) + DEFAULT, 0, "MISSING"],
    ["L-31", HEAD(b) + [DEFAULT, rule("bm-pkg-30d", "Yes", "(all prefixes)", "Expire objects after 31 days")].join("\n\n"), 0, "MISSING"],
    ["L-OFF", HEAD(b) + [DEFAULT, rule("bm-pkg-30d", "No", "(all prefixes)", "Expire objects after 30 days")].join("\n\n"), 0, "MISSING"],
    ["L-PREFIX", HEAD(b) + [DEFAULT, rule("bm-pkg-30d", "Yes", "bm/v2/", "Expire objects after 30 days")].join("\n\n"), 0, "MISSING"],
    ["C-PREFIX", HEAD(b) + [DEFAULT, rule("bm-pkg-30d", "Yes", "bm/v1/", "Expire objects after 30 days")].join("\n\n"), 0, "OK"],
    ["L-SECOND", HEAD(b) + [DEFAULT, OURS, rule("sweep", "Yes", "(all prefixes)", "Expire objects after 7 days")].join("\n\n"), 0, "CONFLICT"],
    ["L-NONE", HEAD(b) + `There are no lifecycle rules for bucket '${b}'.`, 0, "MISSING"],
    ["B-EXIT", HEAD(b) + [DEFAULT, OURS].join("\n\n"), 1, "BLIND"],
    ["B-AUTH", HEAD(b) + "\n✘ [ERROR] A request to the Cloudflare API failed. Authentication error [code: 10000]", 1, "BLIND"],
    ["B-EMPTY", HEAD(b), 0, "BLIND"],
    ["B-BUCKET", HEAD("blipscope-basemap-pkg") + [DEFAULT, OURS].join("\n\n"), 0, "BLIND"],
  ];
  let off = 0;
  for (const [id, text, status, want] of CASES) {
    const got = judge(text, status, b);
    off += got.verdict !== want;
    console.log(`${id}: ${got.verdict === want ? "as registered" : "NOT AS REGISTERED"}; ${got.verdict} (want ${want}): ${got.why}`);
  }
  console.log(`SELFTEST: ${CASES.length - off} of ${CASES.length} as registered`);
  return off ? 1 : 0;
}

const arg = process.argv[2];
if (!arg) { console.log("usage: node scripts/check-pkg-lifecycle.mjs <bucket> | --selftest"); process.exit(2); }
if (arg === "--selftest") process.exit(selftest());
if (!/^[a-z0-9][a-z0-9-]{1,61}[a-z0-9]$/.test(arg)) { console.log(`BLIND: '${arg}' is not an R2 bucket name`); process.exit(2); }
const r = read(arg);
const j = judge(r.stdout, r.status, arg);
for (const x of j.rules) console.log(`  rule '${x.name}': enabled ${x.enabled}; ${x.prefix}; ${x.action}`);
if (j.verdict === "BLIND" && r.stderr.trim()) console.log(`  wrangler's stderr: ${r.stderr.trim().split("\n").slice(-3).join(" | ").slice(0, 400)}`);
console.log(`${j.verdict}: ${arg}: ${j.why}`);
process.exit(j.verdict === "OK" ? 0 : j.verdict === "BLIND" ? 2 : 1);
