#!/usr/bin/env node
// sabotage-routes-weekly.mjs -- W8 (docs/route-weekly-code.md, sections 2g and 7): every recorded sabotage, replayed.
//
// THE BASELINE FIRST (the reviewer's ruling, after this driver's instrument failure #1 on 2026-11-14): every grader runs
// on the CLEAN tree and must read its full count before any plant. A grader that reads no rows, or the wrong count, is
// BLIND and STOPS the driver (exit 2): a grader that cannot start must never read as "red []", which is exactly how six
// npx-graded plants passed for nothing in the first run (execFileSync("npx") is ENOENT on Windows without a shell).
// So the graders run as `node node_modules/<pkg>/<bin>`: no npx, no shell, the same on Windows and Linux.
//
// Then each plant asserts its anchor appears EXACTLY once and that the file changed, runs its grader, compares the red
// set with the registered one EXACTLY, and restores the file from a byte copy checked by sha256. A plant that does not
// apply stops the driver.
//
//   cd proxy && node scripts/sabotage-routes-weekly.mjs                   exit 0 every set as registered, 1 off, 2 BLIND
//   cd proxy && node scripts/sabotage-routes-weekly.mjs --sabotage-blind  the driver's own sabotage: one grader pointed
//                                                                         at a missing binary; it must STOP, exit 2
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const BLIND_SAB = process.argv.includes("--sabotage-blind");
const sha = (b) => createHash("sha256").update(b).digest("hex");
const run = (cmd, args, cwd = ".") => {
  try { return execFileSync(cmd, args, { cwd, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], maxBuffer: 64 << 20 }); }
  catch (e) { return `${e.stdout ?? ""}\n${e.stderr ?? ""}`; }
};
const NODE = process.execPath;
const TSX = BLIND_SAB ? "node_modules/tsx/dist/no-such-binary.mjs" : "node_modules/tsx/dist/cli.mjs";
const VITEST = "node_modules/vitest/vitest.mjs";
// On Windows `python3` can be the Store's stub, which fails every call (deploy.sh's CL10 finding): use `python` there.
const PY = process.platform === "win32" ? "python" : "python3";

// Each grader returns { ids, fails }: the row ids it read, and which were red.
const passFail = (out) => ({
  ids: new Set([...out.matchAll(/^(?:PASS|FAIL) ([A-Za-z0-9-]+)/gm)].map((m) => m[1])),
  fails: new Set([...out.matchAll(/^FAIL (\S+?):/gm)].map((m) => m[1])),
});
// Instrument failure #2 (2026-11-14): vitest prints "✓ <file> > <describe> > RH1: ...", and two rows read
// "RH8 (control):", so the id is taken after the LAST "> ", up to a word boundary.
const vitest = (out) => {
  const o = out.replace(/\x1b\[[0-9;]*m/g, "");
  return { ids: new Set([...o.matchAll(/^\s+(?:✓|×) .*?> (RH\w+)\b/gm)].map((m) => m[1])),
    fails: new Set([...o.matchAll(/^\s+× .*?> (RH\w+)\b/gm)].map((m) => m[1])) };
};
const reconcileParse = (out) => {
  const m = out.match(/RC5-RC6 \(the real source\): [^\n]*red (?:\[([^\]]*)\]|none)/);
  return { ids: new Set(m ? ["RC5-RC6"] : []), fails: new Set(m && m[1] ? m[1].split(",").map((s) => s.trim().replace(/'/g, "")).filter(Boolean) : []) };
};
// The ids each selftest DECLARES, read from its own source (the reviewer's addition): the baseline compares SETS, so a
// parser that reads the right number of the wrong rows stops too.
const src = (p) => readFileSync(p, "utf8");
const all = (re, s) => new Set([...s.matchAll(re)].map((m) => m[1]));
const union = (...ss) => new Set(ss.flatMap((s) => [...s]));
const GRADERS = {
  // literal row("ID", ...) calls, plus G6's loop, which names its rows in [["GR7a", "rt:..."], ...] tuples, and M3's
  // signal loop, which names them in [["SG1", "SIGINT", 130], ...] tuples
  guards: { declared: () => { const s = src("scripts/routes-guards.selftest.mts"); return union(all(/\brow\("([A-Za-z0-9-]+)"/g, s), all(/\["(GR7[a-z])", "rt:/g, s), all(/\["(SG\d)", "SIG/g, s)); },
    read: () => passFail(run(NODE, [TSX, "scripts/routes-guards.selftest.mts"])) },
  writer: { declared: () => { const s = src("scripts/routes-writer.selftest.sh"); return union(all(/\b(?:ok|bad) ([A-Z][A-Za-z0-9-]*)\b/g, s), all(/^gr9 (GR9[a-z])\b/gm, s), all(/^un (UN\d)\b/gm, s)); },
    read: () => passFail(run("bash", ["scripts/routes-writer.selftest.sh"])) },
  freeze: { declared: () => all(/\b(?:ok|bad) (FZC\d)\b/g, src("scripts/routes-freeze.selftest.sh")),
    read: () => passFail(run("bash", ["scripts/routes-freeze.selftest.sh"])) },
  runs: { declared: () => all(/\b(?:ok|bad) ([A-Z][A-Za-z0-9-]*)\b/g, src("scripts/routes-run.selftest.sh")),
    read: () => passFail(run("bash", ["scripts/routes-run.selftest.sh"])) },
  health: { declared: () => all(/\bit\("(RH\w+)\b/g, src("test/routes-health.test.ts")),
    read: () => vitest(run(NODE, [VITEST, "run", "test/routes-health.test.ts", "--reporter=verbose"])) },
  reconcile: { declared: () => new Set(["RC5-RC6"]),
    read: () => reconcileParse(run(PY, ["scripts/reconcile-fleet.selftest.py"], "..")) },
};
const same = (a, b) => a.size === b.size && [...a].every((x) => b.has(x));

// ---- the baseline: every grader, on the clean tree, reads EXACTLY its declared ids with nothing red -------------------
for (const [name, g] of Object.entries(GRADERS)) {
  const want = g.declared(), r = g.read();
  g.want = want;
  if (want.size === 0 || !same(r.ids, want) || r.fails.size > 0) {
    const missing = [...want].filter((x) => !r.ids.has(x)).length, extra = [...r.ids].filter((x) => !want.has(x)).length;
    console.log(`BASELINE ${name}: BLIND -- read ${r.ids.size} of ${want.size} declared ids (${missing} missing, ${extra} not declared), ${r.fails.size} red. STOPPING: no plant was applied.`);
    process.exit(2);
  }
  console.log(`BASELINE ${name}: ${r.ids.size} of ${want.size} declared ids, 0 red`);
}
if (BLIND_SAB) { console.log("--sabotage-blind: the baseline did NOT stop on a missing binary -- the driver is broken"); process.exit(1); }

const PLANTS = [
  { id: "S-GR9", file: "scripts/ingest-routes.ts", a: "  if (why.length > 0) refuse(1, `REFUSING (GR9)", b: "  if (false && why.length > 0) refuse(1, `REFUSING (GR9)", grade: "writer", want: ["GR9b", "GR9c", "GR9e", "GR9f", "GR9g"] },
  { id: "S-GR8", file: "scripts/ingest-routes.ts", a: '  console.log(`wrote ${writes.length} route keys${live ? " (+1 sentinel)" : ""}; deleted ${dels.length}`);', b: '  kvChunks("put", [{ key: META_KEY, value: "planted" }], env, tmp);\n  console.log(`wrote ${writes.length} route keys${live ? " (+1 sentinel)" : ""}; deleted ${dels.length}`);', grade: "writer", want: ["GR8", "RCP2", "UN1", "UN2", "UN3"] },
  { id: "S-FK1", file: "scripts/routes-fk1.ts", a: "  for (const v of CREDENTIAL_VARS) if (env[v]) return `a real credential variable is set (${v})`;\n  const tf = env.BLIPSCOPE_TOKEN_FILE;\n  if (tf && exists(tf)) return", b: "  const tf = env.BLIPSCOPE_TOKEN_FILE;\n  if (false && tf && exists(tf)) return", grade: "guards", want: ["FK1d", "FK1e", "FK1f", "FK1h"] },
  { id: "S-VAL", file: "scripts/verify-routes.ts", a: "const same = (expected: string, got: string | null | undefined) => got === expected;", b: "const same = (expected: string, got: string | null | undefined) => true || got === expected;", grade: "writer", want: ["GR10m", "GR10a", "RCP2", "GR10", "GR11", "UN1", "UN2", "UN3"] },
  { id: "S-RH9s", file: "src/routeshealth.ts", a: 'typeof sc === "string" && SOURCE_DATE.test(sc) && Number.isFinite(Date.parse(sc))', b: 'typeof sc === "string"', grade: "health", want: ["RH9s"] },
  { id: "S-TK1", file: "scripts/routes-token-check.ts", a: "if (days <= WARN_DAYS) return", b: "if (days < 0) return", grade: "guards", want: ["TK1a"] },
  { id: "S-FZ", file: "scripts/routes-guards.ts", a: "  return Date.parse(nowIso) < Date.parse(freezeUntil)", b: "  return false && Date.parse(nowIso) < Date.parse(freezeUntil)", grade: "guards", want: ["FZ1"] },
  { id: "S-FZC", file: "scripts/routes-freeze.ts", a: 'process.exit(v.action === "run" ? 0 : v.action === "skip" ? 4 : 1);', b: 'process.exit(v.action === "run" ? 0 : v.action === "skip" ? 0 : 1);', grade: "freeze", want: ["FZC1"] },
  { id: "S-SC4", file: "scripts/routes-notify.ts", a: 'No receipt, so no seal.",', b: 'No receipt, so no seal. First failing key: rt:QX00001.",', grade: "guards", want: ["SC4"] },
  { id: "S-SC2", file: "scripts/routes-env-check.ts", a: "    mainOnly = !!p && p.protected_branches === false", b: "    mainOnly = !!p || p.protected_branches === false", grade: "guards", want: ["SC2b", "SC2c"] },
  { id: "S-RR3", file: "scripts/routes-refresh-run.sh", a: "  verify || finish verify-fail\n", b: "  verify || true\n", grade: "runs", want: ["RR3"] },
  { id: "S-TMP", file: "scripts/routes-tmp.ts", a: "  for (const d of made) { try { rmSync(", b: "  for (const d of [] as string[]) { try { rmSync(", grade: "writer", want: ["TMP1"] },
  { id: "S-SW", file: "scripts/routes-tmp.ts", a: "    if (nowMs - st.mtimeMs < STALE_MS) { out.kept++; continue; }", b: "    if (false && nowMs - st.mtimeMs < STALE_MS) { out.kept++; continue; }", grade: "guards", want: ["SW2", "SW5"] },
  { id: "S-RC6", file: "../scripts/reconcile-fleet.py", a: 'key.strip() in ("test-device", "ci-test-device")', b: 'key.strip() in ("test-device",)', grade: "reconcile", want: ["RC6"] },
];

// --only <id>: one plant, still after the full baseline (the reviewer's order: S-SW alone, then the whole driver).
const onlyAt = process.argv.indexOf("--only"), ONLY = onlyAt >= 0 ? process.argv[onlyAt + 1] : null;
const RUN = ONLY ? PLANTS.filter((p) => p.id === ONLY) : PLANTS;
if (ONLY && RUN.length !== 1) { console.log(`--only ${ONLY}: no such plant`); process.exit(2); }
let off = 0;
for (const p of RUN) {
  const clean = readFileSync(p.file);
  const s = clean.toString("utf8");
  const n = s.split(p.a).length - 1;
  if (n !== 1) { console.log(`${p.id}: PLANT DID NOT APPLY (anchor found ${n} times) -- stopping`); process.exit(1); }
  writeFileSync(p.file, s.replace(p.a, p.b));
  if (sha(readFileSync(p.file)) === sha(clean)) { console.log(`${p.id}: PLANT DID NOT CHANGE THE FILE -- stopping`); process.exit(1); }
  let r;
  try { r = GRADERS[p.grade].read(); } finally { writeFileSync(p.file, clean); }
  if (sha(readFileSync(p.file)) !== sha(clean)) { console.log(`${p.id}: RESTORE FAILED -- stopping`); process.exit(1); }
  if (!same(r.ids, GRADERS[p.grade].want)) { console.log(`${p.id}: BLIND under the plant -- read ${r.ids.size} of ${GRADERS[p.grade].want.size} declared ids. STOPPING`); process.exit(2); }
  const got = [...r.fails].sort(), want = [...p.want].sort();
  const ok = JSON.stringify(got) === JSON.stringify(want);
  off += ok ? 0 : 1;
  console.log(`${p.id}: ${ok ? "as registered" : "OFF"} -- red ${JSON.stringify(got)} (want ${JSON.stringify(want)}); restored, sha equal`);
}
console.log(off ? `SABOTAGE: ${off} off` : `SABOTAGE PASSED (${RUN.length} plant(s), after a full baseline)`);
process.exit(off ? 1 : 0);
