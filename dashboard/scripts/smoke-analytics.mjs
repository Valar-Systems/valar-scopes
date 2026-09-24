// smoke-analytics.mjs -- run every SQL statement the dashboard can issue against
// the LIVE Analytics Engine SQL endpoint, and fail on any non-200.
//
//   npm run smoke:analytics                        # the code in src/analytics.ts
//   node --experimental-strip-types scripts/smoke-analytics.mjs --module <path.ts>
//                                                  # another copy (e.g. a prior commit's)
//
// WHY THIS EXISTS. vitest never executes the SQL: it stubs fetch and inspects
// strings. Two live failures reached the browser in one morning -- IF() branches
// of different types (Double vs Integer) and a function the engine does not have
// (uniq) -- and both were invisible to a green suite. Only the engine can say
// what the engine accepts.
//
// THE STATEMENTS ARE THE DASHBOARD'S OWN. Each one is produced by calling the
// real query function from src/analytics.ts with a real token, so this can never
// test a copy of the SQL that has drifted from what the pages send.
// Every tab x every window the UI offers: 6/24/72/168/720 h.
//
// TWO CONTROLS, both required before any result is believed:
//   - CONTROL+  a trivial valid query must return 200, or the run is UNTRUSTWORTHY
//               (a dead token makes EVERY statement fail, which is not a finding);
//   - CONTROL-  a deliberately invalid query must FAIL, or the smoke cannot see
//               failure and its zeros mean nothing.
//
// TOKEN (Account Analytics: Read), never printed -- only its source's NAME:
//   1. %USERPROFILE%/.config/blipscope/cf-analytics-token (the operator's file)
//   2. CF_API_TOKEN (CI: the repo secret, read-only)
//   3. CLOUDFLARE_API_TOKEN (a local fallback; CI never sets it)
//
// Exit 0: every statement 200 and both controls behaved. 1: a statement failed.
// 3: untrustworthy (no token, or a control misbehaved).
import { existsSync, readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1";
const WINDOWS = [6, 24, 72, 168, 720];

const argi = process.argv.indexOf("--module");
const modPath = resolve(argi >= 0 ? process.argv[argi + 1] : new URL("../src/analytics.ts", import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1"));

function token() {
  const f = join(homedir(), ".config", "blipscope", "cf-analytics-token");
  if (existsSync(f)) {
    const t = readFileSync(f, "utf8").trim();
    if (t) return { t, source: "cf-analytics-token file" };
  }
  if (process.env.CF_API_TOKEN) return { t: process.env.CF_API_TOKEN, source: "CF_API_TOKEN" };
  if (process.env.CLOUDFLARE_API_TOKEN) return { t: process.env.CLOUDFLARE_API_TOKEN, source: "CLOUDFLARE_API_TOKEN (local fallback)" };
  return { t: "", source: "" };
}

const { t, source } = token();
console.log(`token: ${t ? "present" : "absent"}${source ? ` (from ${source})` : ""}`);
if (!t) {
  console.error("UNTRUSTWORTHY: no analytics token -- nothing was measured");
  process.exit(3);
}

const q = await import(pathToFileURL(modPath).href);
const env = { CF_ACCOUNT_ID: ACCOUNT, CF_API_TOKEN: t, AE_DATASET: "blipscope_proxy", ENRICH_KV: {} };
console.log(`module: ${modPath}`);

const short = (e) => String(e instanceof Error ? e.message : e).replace(/\s+/g, " ").slice(0, 260);

// The controls go through the module's own runSql, so they share its transport.
let untrustworthy = false;
try {
  await q.runSql(env, "SELECT count() AS n FROM blipscope_proxy WHERE timestamp > NOW() - INTERVAL '1' HOUR");
  console.log("CONTROL+ valid query: 200");
} catch (e) {
  console.log(`CONTROL+ valid query: FAILED -- ${short(e)}`);
  untrustworthy = true;
}
try {
  await q.runSql(env, "SELECT no_such_function_smoke(1) FROM blipscope_proxy");
  console.log("CONTROL- invalid query: PASSED (the smoke cannot see failure)");
  untrustworthy = true;
} catch (e) {
  // It must be the ENGINE refusing the query, not the credentials: a 401/403
  // here would "fail as it must" while proving nothing about query checking.
  if (/\((401|403)\)/.test(short(e))) {
    console.log(`CONTROL- invalid query: failed on AUTH, not on the query -- ${short(e).slice(0, 80)}`);
    untrustworthy = true;
  } else {
    console.log(`CONTROL- invalid query: rejected by the engine, as it must be -- ${short(e).slice(0, 80)}`);
  }
}
if (untrustworthy) {
  console.error("UNTRUSTWORTHY: a control misbehaved; no statement result below would mean anything");
  process.exit(3);
}

// Every query the pages issue. The Fleet page issues rows + totals; Usage issues
// usage rows + the seen-device set.
const QUERIES = [
  ["Fleet", "fleetRows"],
  ["Fleet", "fleetTotals"],
  ["Firmware", "firmwareSpread"],
  ["OTA", "otaOutcomes"],
  ["Enrichment gaps", "enrichGaps"],
  ["Usage", "usageRows"],
  ["Usage", "seenDevices"],
];

const failed = [];
let ran = 0;
for (const [tab, fn] of QUERIES) {
  if (typeof q[fn] !== "function") {
    failed.push(`${tab}/${fn}: not exported by the module`);
    continue;
  }
  for (const h of WINDOWS) {
    ran++;
    try {
      await q[fn](env, h);
      console.log(`ok    ${tab.padEnd(15)} ${fn.padEnd(15)} ${String(h).padStart(3)}h`);
    } catch (e) {
      failed.push(`${tab}/${fn} ${h}h: ${short(e)}`);
      console.log(`FAIL  ${tab.padEnd(15)} ${fn.padEnd(15)} ${String(h).padStart(3)}h  ${short(e)}`);
    }
  }
}

const tabs = [...new Set(QUERIES.map(([tab]) => tab))];
console.log("");
for (const tab of tabs) {
  const bad = failed.filter((f) => f.startsWith(`${tab}/`));
  console.log(`tab ${tab}: ${bad.length ? `failed: ${bad[0].split(": ").slice(1).join(": ").slice(0, 160)}` : "loaded (every statement 200)"}`);
}
console.log(`\n${failed.length} of ${ran} statements failed`);
process.exit(failed.length ? 1 : 0);
