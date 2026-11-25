#!/usr/bin/env node
// The screen service's pre-deploy check (its phase 1 registration, R4; docs/screen-logs-runbook.md). deploy.sh --screen
// runs it after its own guards and before `wrangler deploy`, on the env's OWN dry-run bundle. It deploys nothing.
//
//   P-M2  no deploy without the counter: the bundle holds the counter's write site, `.SCREEN_METRICS.writeDataPoint(`,
//         EXACTLY ONCE (src/screen/metrics.ts is asserted to hold exactly one such call, so the two cannot drift). The
//         write site itself is the marker: a separate marker constant would survive the write's removal.
//   P-U   the full-logs day's end: production-fulllogs REQUIRES --until, an ISO UTC time with now < until <= now + 24 h;
//         every other env REFUSES --until.
//
//   node scripts/screen-deploy-check.mjs <staging|production|production-fulllogs> [--until <ISO UTC>]
//        exit 0 pass, 1 refuse (each reason printed), 2 usage or BLIND (the bundle could not be built or read)
//   node scripts/screen-deploy-check.mjs --selftest-until     P-U's 7 cases on a fixed clock; then Us (a COPY with the 24 h
//                                                             cap removed) must flip exactly the +25 h case
//   node scripts/screen-deploy-check.mjs --selftest-bundles   P-M2 on each env's REAL dry-run bundle (through
//                                                             wrangler-run.mjs; no credentials); then Ms (a copy of the
//                                                             production bundle with the write site stripped) must refuse
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const PROXY = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const WRANGLER_RUN = join(PROXY, "scripts", "wrangler-run.mjs");
const SCREEN_TOML = "screen.wrangler.toml";
const METRICS_SRC = join(PROXY, "src", "screen", "metrics.ts");
export const WRITE_SITE = ".SCREEN_METRICS.writeDataPoint(";
export const ENVS = ["staging", "production", "production-fulllogs"];
const DAY_MS = 24 * 3_600_000;
const ISO_UTC = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2}(\.\d{1,3})?)?Z$/;

/** P-U: { ok, why } for deploying `env` with `untilArg` (a string or null) at time `now` (ms). */
export function checkUntil(env, untilArg, now) {
  /* STUB (red): the rules are not written yet. */
  return { ok: true, why: "stub" };
}

/** P-M2: { ok, why } for one bundle's text. */
export function checkWriteSite(bundleJs) {
  const n = bundleJs.split(WRITE_SITE).length - 1;
  return n === 1 ? { ok: true, why: "the counter's write site is in the bundle once" } : { ok: false, why: `the bundle holds the counter's write site ${n} time(s), want exactly 1` };
}

/** The env's own dry-run bundle text, or null (BLIND). */
function dryRunBundle(env) {
  const out = mkdtempSync(join(tmpdir(), "screen-deploy-check-"));
  try {
    const r = spawnSync(process.execPath, [WRANGLER_RUN, "deploy", "--dry-run", "--outdir", out, "-c", SCREEN_TOML, "-e", env], { cwd: PROXY, encoding: "utf8" });
    if (r.status !== 0) return null;
    const js = readdirSync(out).filter((f) => f.endsWith(".js")).map((f) => readFileSync(join(out, f), "utf8")).join("\n");
    return js || null;
  } finally {
    rmSync(out, { recursive: true, force: true });
  }
}

/** metrics.ts must hold exactly one write site, so the bundle check and the source cannot drift apart. */
function sourceWriteSites() {
  return existsSync(METRICS_SRC) ? readFileSync(METRICS_SRC, "utf8").split(WRITE_SITE).length - 1 : -1;
}

export const UNTIL_CASES = (now) => [
  ["fulllogs until +1 h", "production-fulllogs", new Date(now + 3_600_000).toISOString(), true],
  ["fulllogs until +25 h", "production-fulllogs", new Date(now + 25 * 3_600_000).toISOString(), false],
  ["fulllogs until -1 h", "production-fulllogs", new Date(now - 3_600_000).toISOString(), false],
  ["fulllogs no until", "production-fulllogs", null, false],
  ["fulllogs malformed until", "production-fulllogs", "tomorrow", false],
  ["staging with until", "staging", new Date(now + 3_600_000).toISOString(), false],
  ["production with until", "production", new Date(now + 3_600_000).toISOString(), false],
];

async function selftestUntil() {
  const now = Date.parse("2026-12-02T12:00:00Z");
  const rows = UNTIL_CASES(now).map(([name, env, u, want]) => ({ name, want, got: checkUntil(env, u, now).ok }));
  rows.forEach((r) => console.log(`${r.name}: want ${r.want ? "pass" : "refuse"}, got ${r.got ? "pass" : "refuse"} -> ${r.got === r.want ? "AS PREDICTED" : "NOT AS PREDICTED"}`));
  const good = rows.filter((r) => r.got === r.want).length;
  console.log(`P-U: ${good} of ${rows.length} as predicted`);
  if (good !== rows.length) { console.log("SELFTEST FAILED"); return 1; }
  // Us: the 24 h cap removed, in a COPY: exactly the +25 h case must flip to pass.
  const src = readFileSync(fileURLToPath(import.meta.url), "utf8");
  const sab = src.replace("/*P-U*/ t > now + DAY_MS", "/*P-U*/ false");
  if (sab === src) { console.log("Us: the edit did not apply"); return 1; }
  const dir = mkdtempSync(join(tmpdir(), "screen-deploy-check-us-"));
  try {
    const p = join(dir, "check-us.mjs");
    writeFileSync(p, sab);
    const mod = await import(pathToFileURL(p).href);
    const flipped = UNTIL_CASES(now).filter(([, env, u, want]) => mod.checkUntil(env, u, now).ok !== want).map(([name]) => name);
    const ok = flipped.length === 1 && flipped[0] === "fulllogs until +25 h";
    console.log(`Us: ${flipped.length} case(s) flipped (${flipped.join(", ")}) -> ${ok ? "AS PREDICTED" : "NOT AS PREDICTED"}`);
    console.log(ok ? "SELFTEST PASSED" : "SELFTEST FAILED");
    return ok ? 0 : 1;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

function selftestBundles() {
  const sites = sourceWriteSites();
  if (sites !== 1) { console.log(`BLIND: src/screen/metrics.ts holds the write site ${sites} time(s), want exactly 1`); return 2; }
  let bad = 0;
  let prodJs = null;
  for (const env of ENVS) {
    const js = dryRunBundle(env);
    if (js === null) { console.log(`${env}: BLIND (the dry-run did not build a bundle)`); return 2; }
    if (env === "production") prodJs = js;
    const r = checkWriteSite(js);
    console.log(`${env}: ${r.ok ? "pass" : "REFUSE"} (${r.why}) -> ${r.ok ? "AS PREDICTED" : "NOT AS PREDICTED"}`);
    if (!r.ok) bad++;
  }
  // Ms: the write site stripped from a copy of production's bundle must refuse.
  const stripped = prodJs.split(WRITE_SITE).join(".SCREEN_METRICS_STRIPPED(");
  if (stripped === prodJs) { console.log("Ms: the edit did not apply"); return 1; }
  const ms = checkWriteSite(stripped);
  console.log(`Ms: ${ms.ok ? "pass" : "REFUSE"} (${ms.why}) -> ${ms.ok ? "NOT AS PREDICTED" : "AS PREDICTED"}`);
  if (ms.ok) bad++;
  console.log(bad ? "SELFTEST FAILED" : "SELFTEST PASSED");
  return bad ? 1 : 0;
}

async function main(argv) {
  if (argv[0] === "--selftest-until") return selftestUntil();
  if (argv[0] === "--selftest-bundles") return selftestBundles();
  const env = argv[0];
  const ui = argv.indexOf("--until");
  const untilArg = ui >= 0 ? (argv[ui + 1] ?? "") : null;
  if (!ENVS.includes(env)) { console.log("usage: screen-deploy-check.mjs <staging|production|production-fulllogs> [--until <ISO UTC>] | --selftest-until | --selftest-bundles"); return 2; }
  const u = checkUntil(env, untilArg, Date.now());
  if (!u.ok) { console.log(`REFUSE (P-U): ${u.why}`); return 1; }
  const sites = sourceWriteSites();
  if (sites !== 1) { console.log(`BLIND (P-M2): src/screen/metrics.ts holds the write site ${sites} time(s), want exactly 1`); return 2; }
  const js = dryRunBundle(env);
  if (js === null) { console.log(`BLIND (P-M2): the ${env} dry-run did not build a bundle`); return 2; }
  const m = checkWriteSite(js);
  if (!m.ok) { console.log(`REFUSE (P-M2): ${m.why}`); return 1; }
  console.log(`PASS: ${env}: ${u.why}; ${m.why}`);
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? "").href) process.exitCode = await main(process.argv.slice(2));
