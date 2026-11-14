#!/usr/bin/env node
// sabotage-routes-weekly.mjs -- W8 (docs/route-weekly-code.md): every sabotage recorded in section 7, replayed. Each
// plant asserts its anchor appears EXACTLY once and that the file changed, runs the selftest that grades it, compares
// the red set with the registered one EXACTLY (a superset or a subset is off), and restores the file from a byte copy,
// checked by sha256. A plant that does not apply stops the driver: an unsabotaged run is never read as a result.
//
//   cd proxy && node scripts/sabotage-routes-weekly.mjs          exit 0 every red set as registered, 1 otherwise
// Not a CI step (the selftests are). Run it before a merge of the weekly job, as the other sabotage drivers are.
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const sha = (b) => createHash("sha256").update(b).digest("hex");
const run = (cmd, args, cwd = ".") => {
  try { return execFileSync(cmd, args, { cwd, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], maxBuffer: 64 << 20 }); }
  catch (e) { return `${e.stdout ?? ""}\n${e.stderr ?? ""}`; }
};
const fails = (out) => new Set([...out.matchAll(/^FAIL (\S+?):/gm)].map((m) => m[1]));
const vitestFails = (out) => new Set([...out.replace(/\x1b\[[0-9;]*m/g, "").matchAll(/^\s+× (RH\w+):/gm)].map((m) => m[1]));
const reconcileFails = (out) => {
  const m = out.match(/RC5-RC6 \(the real source\): [^\n]*red \[([^\]]*)\]/);
  return new Set(m ? m[1].split(",").map((s) => s.trim().replace(/'/g, "")).filter(Boolean) : []);
};
const guards = () => fails(run("npx", ["--no-install", "tsx", "scripts/routes-guards.selftest.mts"]));
const writer = () => fails(run("bash", ["scripts/routes-writer.selftest.sh"]));
const freeze = () => fails(run("bash", ["scripts/routes-freeze.selftest.sh"]));
const runs = () => fails(run("bash", ["scripts/routes-run.selftest.sh"]));
const health = () => vitestFails(run("npx", ["--no-install", "vitest", "run", "test/routes-health.test.ts", "--reporter=verbose"]));
// On Windows `python3` can be the Store's stub, which fails every call (deploy.sh's CL10 finding): use `python` there.
const PY = process.platform === "win32" ? "python" : "python3";
const reconcile = () => reconcileFails(run(PY, ["scripts/reconcile-fleet.selftest.py"], ".."));

const PLANTS = [
  { id: "S-GR9", file: "scripts/ingest-routes.ts", a: "  if (why.length > 0) refuse(1, `REFUSING (GR9)", b: "  if (false && why.length > 0) refuse(1, `REFUSING (GR9)", grade: writer, want: ["GR9b", "GR9c", "GR9e", "GR9f", "GR9g"] },
  { id: "S-GR8", file: "scripts/ingest-routes.ts", a: '  console.log(`wrote ${writes.length} route keys${live ? " (+1 sentinel)" : ""}; deleted ${dels.length}`);', b: '  kvChunks("put", [{ key: META_KEY, value: "planted" }], env, tmp);\n  console.log(`wrote ${writes.length} route keys${live ? " (+1 sentinel)" : ""}; deleted ${dels.length}`);', grade: writer, want: ["GR8"] },
  { id: "S-FK1", file: "scripts/routes-fk1.ts", a: "  for (const v of CREDENTIAL_VARS) if (env[v]) return `a real credential variable is set (${v})`;\n  const tf = env.BLIPSCOPE_TOKEN_FILE;\n  if (tf && exists(tf)) return", b: "  const tf = env.BLIPSCOPE_TOKEN_FILE;\n  if (false && tf && exists(tf)) return", grade: guards, want: ["FK1d", "FK1e", "FK1f", "FK1h"] },
  { id: "S-VAL", file: "scripts/verify-routes.ts", a: "const same = (expected: string, got: string | null | undefined) => got === expected;", b: "const same = (expected: string, got: string | null | undefined) => true || got === expected;", grade: writer, want: ["GR10m", "GR10a", "RCP2", "GR10", "GR11", "UN1", "UN2", "UN3"] },
  { id: "S-RH9s", file: "src/routeshealth.ts", a: 'typeof sc === "string" && SOURCE_DATE.test(sc) && Number.isFinite(Date.parse(sc))', b: 'typeof sc === "string"', grade: health, want: ["RH9s"] },
  { id: "S-TK1", file: "scripts/routes-token-check.ts", a: "if (days <= WARN_DAYS) return", b: "if (days < 0) return", grade: guards, want: ["TK1a"] },
  { id: "S-FZ", file: "scripts/routes-guards.ts", a: "  return Date.parse(nowIso) < Date.parse(freezeUntil)", b: "  return false && Date.parse(nowIso) < Date.parse(freezeUntil)", grade: guards, want: ["FZ1"] },
  { id: "S-FZC", file: "scripts/routes-freeze.ts", a: 'process.exit(v.action === "run" ? 0 : v.action === "skip" ? 4 : 1);', b: 'process.exit(v.action === "run" ? 0 : v.action === "skip" ? 0 : 1);', grade: freeze, want: ["FZC1"] },
  { id: "S-SC4", file: "scripts/routes-notify.ts", a: 'No receipt, so no seal.",', b: 'No receipt, so no seal. First failing key: rt:QX00001.",', grade: guards, want: ["SC4"] },
  { id: "S-SC2", file: "scripts/routes-env-check.ts", a: "    mainOnly = !!p && p.protected_branches === false", b: "    mainOnly = !!p || p.protected_branches === false", grade: guards, want: ["SC2b", "SC2c"] },
  { id: "S-RR3", file: "scripts/routes-refresh-run.sh", a: "  verify || finish verify-fail\n", b: "  verify || true\n", grade: runs, want: ["RR3"] },
  { id: "S-TMP-W", file: "scripts/ingest-routes.ts", a: "  for (const d of TMP_DIRS) { try { rmSync(", b: "  for (const d of [] as string[]) { try { rmSync(", grade: writer, want: ["TMP1"] },
  { id: "S-TMP-V", file: "scripts/verify-routes.ts", a: "  for (const d of TMP_DIRS) { try { rmSync(", b: "  for (const d of [] as string[]) { try { rmSync(", grade: writer, want: ["TMP1"] },
  { id: "S-RC6", file: "../scripts/reconcile-fleet.py", a: 'key.strip() in ("test-device", "ci-test-device")', b: 'key.strip() in ("test-device",)', grade: reconcile, want: ["RC6"] },
];

let off = 0;
for (const p of PLANTS) {
  const clean = readFileSync(p.file);
  const s = clean.toString("utf8");
  const n = s.split(p.a).length - 1;
  if (n !== 1) { console.log(`${p.id}: PLANT DID NOT APPLY (anchor found ${n} times) -- stopping`); process.exit(1); }
  writeFileSync(p.file, s.replace(p.a, p.b));
  if (sha(readFileSync(p.file)) === sha(clean)) { console.log(`${p.id}: PLANT DID NOT CHANGE THE FILE -- stopping`); process.exit(1); }
  let got;
  try { got = [...p.grade()].sort(); } finally { writeFileSync(p.file, clean); }
  if (sha(readFileSync(p.file)) !== sha(clean)) { console.log(`${p.id}: RESTORE FAILED -- stopping`); process.exit(1); }
  const want = [...p.want].sort();
  const ok = JSON.stringify(got) === JSON.stringify(want);
  off += ok ? 0 : 1;
  console.log(`${p.id}: ${ok ? "as registered" : "OFF"} -- red ${JSON.stringify(got)} (want ${JSON.stringify(want)}); restored, sha equal`);
}
console.log(off ? `SABOTAGE: ${off} off` : `SABOTAGE PASSED (${PLANTS.length} plants)`);
process.exit(off ? 1 : 0);
