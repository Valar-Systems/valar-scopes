#!/usr/bin/env node
// Plan S (docs/wx-plan-s.md): the plant never reaches production, and the staging copy binds only its own bucket.
// Reads what WOULD SHIP -- `wrangler deploy --dry-run` bundles and binding lists -- never the source.
//
//   PB1  production's bundle (wx-ingest.wrangler.toml) holds PLANT_MARKER 0 times.
//        Anchor: it holds "wx_ingest_error" (else the read is BLIND). Control: the dev bundle (-e plant) holds the
//        marker exactly once (else this check cannot see the plant: BLIND).
//        The opuk Worker's bundle (wx-ingest-opuk.wrangler.toml) holds it 0 times too, with the same anchor.
//   FB2  the opuk Worker's 2 deploy targets (top level, inert) bind exactly one R2 bucket, WX -> blipscope-wx, with
//        WX_INGEST "on" and "off" (docs/wx-opuk-split.md).
//   PB2  each of the dev config's 11 deploy targets (top level + 10 envs) binds exactly one R2 bucket, WX ->
//        blipscope-wx-dev, with MODE as the toml's table says. Control: production's dry-run reads WX -> blipscope-wx
//        (else the parse is BLIND).
//
//   node scripts/check-wx-plant.mjs              exit 0 PASS, 1 FAIL, 2 BLIND
//   node scripts/check-wx-plant.mjs --selftest   the real check must PASS; PB1s (production's entry re-exporting
//        from the dev entry) must FAIL with the marker found once; PB2s (the cref env bound to blipscope-wx) must FAIL
//        on cref alone; FB2s (the opuk toml's inert bound to blipscope-wx-dev) must FAIL on inert alone. Each
//        sabotage must change its file, and its file is deleted afterwards.
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const PROXY = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const PROD_TOML = "wx-ingest.wrangler.toml";
const DEV_TOML = "wx-ingest-dev.wrangler.toml";
const OPUK_TOML = "wx-ingest-opuk.wrangler.toml";
const OPUK_TARGETS = [[null, "on"], ["inert", "off"]];
const DEV_ENTRY = "src/wx/ingest-dev-worker.ts";
const ANCHOR = "wx_ingest_error";
const DEV_TARGETS = [[null, "off"], ["off", "off"], ["idle", "off"], ["plantcal", "plant"], ["plant", "plant"], ["cref", "cref"], ["opuk", "opuk"],
                     ["opwe", "opwe"], ["opsn", "opsn"], ["opux", "opux"], ["opmt", "opmt"]];

// The marker is read from the dev entry itself (the other side of the contract), never retyped here.
const markerMatch = /export const PLANT_MARKER = "([^"]+)";/.exec(readFileSync(join(PROXY, DEV_ENTRY), "utf8"));
const MARKER = markerMatch ? markerMatch[1] : null;

const cache = new Map();
function dryRun(config, env = null, script = null) {
  const key = `${config}|${env}|${script}`;
  if (cache.has(key)) return cache.get(key);
  const out = mkdtempSync(join(tmpdir(), "wx-plant-"));
  const args = ["wrangler", "deploy", "--dry-run", "--outdir", out, "-c", config];
  if (env) args.push("-e", env);
  if (script) args.push(script);
  const r = spawnSync("npx", args, { cwd: PROXY, encoding: "utf8", shell: process.platform === "win32" });
  const js = existsSync(out) ? readdirSync(out).filter((f) => f.endsWith(".js")).map((f) => readFileSync(join(out, f), "utf8")).join("\n") : "";
  rmSync(out, { recursive: true, force: true });
  const res = { code: r.status, stdout: r.stdout ?? "", stderr: r.stderr ?? "", js };
  cache.set(key, res);
  return res;
}
const count = (hay, needle) => (needle ? hay.split(needle).length - 1 : 0);
/** The dry-run's binding table: `env.NAME (resource)   Type`. */
function bindings(stdout) {
  return stdout.split(/\r?\n/).map((l) => /^env\.(\w+) \((.*)\)\s+(\S.*?)\s*$/.exec(l))
    .filter(Boolean).map((m) => ({ name: m[1], resource: m[2], type: m[3] }));
}

function pb1(prodScript = null) {
  if (!MARKER) return { verdict: "BLIND", why: `no PLANT_MARKER in ${DEV_ENTRY}` };
  const prod = dryRun(PROD_TOML, null, prodScript);
  if (prod.code !== 0) return { verdict: "BLIND", why: `production dry-run exit ${prod.code}`, stderr: prod.stderr.slice(-400) };
  const anchor = count(prod.js, ANCHOR);
  if (anchor < 1) return { verdict: "BLIND", why: `production bundle has no "${ANCHOR}": not the real bundle` };
  const dev = dryRun(DEV_TOML, "plant");
  if (dev.code !== 0) return { verdict: "BLIND", why: `dev dry-run exit ${dev.code}`, stderr: dev.stderr.slice(-400) };
  const devCount = count(dev.js, MARKER);
  if (devCount !== 1) return { verdict: "BLIND", why: `dev bundle holds the marker ${devCount} times, not 1: this check cannot see the plant` };
  const prodCount = count(prod.js, MARKER);
  const opuk = dryRun(OPUK_TOML);
  if (opuk.code !== 0) return { verdict: "BLIND", why: `opuk dry-run exit ${opuk.code}`, stderr: opuk.stderr.slice(-400) };
  const opukAnchor = count(opuk.js, ANCHOR);
  if (opukAnchor < 1) return { verdict: "BLIND", why: `the opuk bundle has no "${ANCHOR}": not the real bundle` };
  const opukCount = count(opuk.js, MARKER);
  return { verdict: prodCount === 0 && opukCount === 0 ? "PASS" : "FAIL", prodCount, opukCount, anchor, opukAnchor, devCount };
}

function fb2(opukToml = OPUK_TOML) {
  const rows = OPUK_TARGETS.map(([env, on]) => {
    const r = dryRun(opukToml, env);
    const b = bindings(r.stdout);
    const r2 = b.filter((x) => x.type === "R2 Bucket");
    const v = b.find((x) => x.name === "WX_INGEST");
    const ok = r.code === 0 && r2.length === 1 && r2[0].name === "WX" && r2[0].resource === "blipscope-wx"
      && v?.resource === JSON.stringify(on);
    return { target: env ?? "(top level)", ok, r2: r2.map((x) => `${x.name}->${x.resource}`), wxIngest: v?.resource ?? null, exit: r.code };
  });
  return { verdict: rows.every((x) => x.ok) ? "PASS" : "FAIL", rows };
}

function pb2(devToml = DEV_TOML) {
  const prod = dryRun(PROD_TOML);
  const pr2 = bindings(prod.stdout).filter((b) => b.type === "R2 Bucket");
  if (prod.code !== 0 || pr2.length !== 1 || pr2[0].name !== "WX" || pr2[0].resource !== "blipscope-wx") {
    return { verdict: "BLIND", why: "production's dry-run does not read WX -> blipscope-wx: the binding parse is not trustworthy" };
  }
  const rows = DEV_TARGETS.map(([env, mode]) => {
    const r = dryRun(devToml, env);
    const b = bindings(r.stdout);
    const r2 = b.filter((x) => x.type === "R2 Bucket");
    const m = b.find((x) => x.name === "MODE");
    const ok = r.code === 0 && r2.length === 1 && r2[0].name === "WX" && r2[0].resource === "blipscope-wx-dev"
      && m?.resource === JSON.stringify(mode);
    return { target: env ?? "(top level)", ok, r2: r2.map((x) => `${x.name}->${x.resource}`), mode: m?.resource ?? null, exit: r.code };
  });
  return { verdict: rows.every((x) => x.ok) ? "PASS" : "FAIL", rows };
}

const worst = (...vs) => (vs.includes("BLIND") ? "BLIND" : vs.includes("FAIL") ? "FAIL" : "PASS");
const CODE = { PASS: 0, FAIL: 1, BLIND: 2 };

if (!process.argv.includes("--selftest")) {
  const a = pb1(), b = pb2(), f = fb2();
  console.log("PB1 the plant never reaches production:", JSON.stringify(a));
  console.log("PB2 the dev copy binds only blipscope-wx-dev:", JSON.stringify(b));
  console.log("FB2 the opuk Worker binds only blipscope-wx:", JSON.stringify(f));
  const v = worst(a.verdict, b.verdict, f.verdict);
  console.log(`RESULT: ${v}`);
  process.exit(CODE[v]);
}

// --selftest
let ok = true;
const fail = (msg) => { ok = false; console.log(`SELFTEST FAIL: ${msg}`); };
const realA = pb1(), realB = pb2();
console.log("real PB1:", JSON.stringify(realA));
console.log("real PB2:", realB.verdict, JSON.stringify(realB.rows ?? realB.why));
if (realA.verdict !== "PASS") fail("the real PB1 does not PASS");
if (realB.verdict !== "PASS") fail("the real PB2 does not PASS");
const realF = fb2();
console.log("real FB2:", realF.verdict, JSON.stringify(realF.rows));
if (realF.verdict !== "PASS") fail("the real FB2 does not PASS");

// PB1s: production's entry, plus a re-export from the dev entry -- bundled with production's own config.
const ENTRY = join(PROXY, "src/wx/ingest-worker.ts");
const SAB_ENTRY_REL = "src/wx/ingest-worker.sabotage.ts";
const SAB_ENTRY = join(PROXY, SAB_ENTRY_REL);
const orig = readFileSync(ENTRY, "utf8");
const planted = `${orig}\nexport { plant as sabotagePlant } from "./ingest-dev-worker";\n`;
try {
  writeFileSync(SAB_ENTRY, planted);
  if (readFileSync(SAB_ENTRY, "utf8") === orig) fail("PB1s did not apply (the copy equals production's entry)");
  const s = pb1(SAB_ENTRY_REL);
  console.log("sabotaged PB1 (PB1s):", JSON.stringify(s));
  if (!(s.verdict === "FAIL" && s.prodCount === 1)) fail("PB1s did not read FAIL with the marker found once");
} finally {
  rmSync(SAB_ENTRY, { force: true });
}
if (existsSync(SAB_ENTRY)) fail(`${SAB_ENTRY_REL} was not deleted`);

// PB2s: the dev toml with the cref env bound to the production bucket.
const SAB_TOML = "wx-ingest-dev.sabotage.wrangler.toml";
const devText = readFileSync(join(PROXY, DEV_TOML), "utf8");
const re = /(\[\[env\.cref\.r2_buckets\]\]\r?\nbinding = "WX"\r?\nbucket_name = )"blipscope-wx-dev"/;
const sabText = devText.replace(re, '$1"blipscope-wx"');
try {
  if (sabText === devText) fail("PB2s did not apply (no cref binding block matched)");
  writeFileSync(join(PROXY, SAB_TOML), sabText);
  const s = pb2(SAB_TOML);
  const failed = (s.rows ?? []).filter((x) => !x.ok).map((x) => x.target);
  console.log("sabotaged PB2 (PB2s):", s.verdict, JSON.stringify(failed));
  if (!(s.verdict === "FAIL" && JSON.stringify(failed) === JSON.stringify(["cref"]))) fail("PB2s did not FAIL on cref alone");
} finally {
  rmSync(join(PROXY, SAB_TOML), { force: true });
}
if (existsSync(join(PROXY, SAB_TOML))) fail(`${SAB_TOML} was not deleted`);

// FB2s: the opuk toml with its inert target bound to the dev bucket.
const SAB_OPUK = "wx-ingest-opuk.sabotage.wrangler.toml";
const opukText = readFileSync(join(PROXY, OPUK_TOML), "utf8");
const reI = /(\[\[env\.inert\.r2_buckets\]\]\r?\nbinding = "WX"\r?\nbucket_name = )"blipscope-wx"/;
const sabOpuk = opukText.replace(reI, '$1"blipscope-wx-dev"');
try {
  if (sabOpuk === opukText) fail("FB2s did not apply (no inert binding block matched)");
  writeFileSync(join(PROXY, SAB_OPUK), sabOpuk);
  const s = fb2(SAB_OPUK);
  const failed = (s.rows ?? []).filter((x) => !x.ok).map((x) => x.target);
  console.log("sabotaged FB2 (FB2s):", s.verdict, JSON.stringify(failed));
  if (!(s.verdict === "FAIL" && JSON.stringify(failed) === JSON.stringify(["inert"]))) fail("FB2s did not FAIL on inert alone");
} finally {
  rmSync(join(PROXY, SAB_OPUK), { force: true });
}
if (existsSync(join(PROXY, SAB_OPUK))) fail(`${SAB_OPUK} was not deleted`);

console.log(ok ? "SELFTEST PASSED" : "SELFTEST FAILED");
process.exit(ok ? 0 : 1);
