// Sabotages for the basemap route's tests (docs/basemap-route.md A1.7: "the sabotage rule").
//
//   node scripts/sabotage-basemap.mjs            # every sabotage
//   node scripts/sabotage-basemap.mjs S-IBUS     # one
//
// For each: assert every anchor occurs EXACTLY once and that the file changed (a plant that did not take
// proves nothing); run the basemap tests; grade the set of failing CASES (the id each test name begins with)
// against the frozen set EXACTLY; restore the file from a byte copy and check its sha256. A clean baseline
// run comes first: with nothing planted, nothing may fail. Exit 0 only when every sabotage lands exactly.
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const PROXY = join(dirname(fileURLToPath(import.meta.url)), "..");
const TESTS = ["test/basemap-ingest.test.ts"];
const LIB = "scripts/basemap/ingest-lib.ts";

/** name: { file, edits: [[anchor, replacement]], expect: [case ids] } -- the sets are A1.7's, frozen. */
const SABOTAGES = {
  "S-ISIZE": {
    file: LIB,
    edits: [["    if (got.length !== o.bytes.length || (await sha256Hex(got)) !== (await sha256Hex(o.bytes))) mismatched.push(o.key);\n",
             "    if (got.length !== o.bytes.length) mismatched.push(o.key); // SABOTAGE S-ISIZE: size only\n"]],
    expect: ["MI7"],
  },
  "S-ISEAL": {
    file: LIB,
    edits: [["  for (const o of objects) await bucket.put(o.key, o.bytes);\n  let v = await verifyStore(bucket, objects);\n",
             "  for (const o of objects) await bucket.put(o.key, o.bytes);\n  await bucket.put(currentKey, currentBody); // SABOTAGE S-ISEAL: sealed before verifying\n  let v = await verifyStore(bucket, objects);\n"]],
    expect: ["MI7"],
  },
  "S-IBUS": {
    file: LIB,
    edits: [['    case "roads": return a["MTFCC"] === "S1100" || a["MTFCC"] === "S1200";\n',
             '    case "roads": return (a["MTFCC"] === "S1100" || a["MTFCC"] === "S1200") && !/(^|[^A-Za-z0-9])Bus($|[^A-Za-z0-9])/.test(a["FULLNAME"] ?? ""); // SABOTAGE S-IBUS\n']],
    expect: ["MI1"],
  },
};

const sha = (b) => createHash("sha256").update(b).digest("hex");
const caseOf = (title) => (/^(M[A-Z](?:\d+[a-z]?|-[a-z]+)|MV\/PK-[a-z0-9]+)/.exec(title) ?? [null, `UNNAMED(${title})`])[1];

function runTests() {
  const dir = mkdtempSync(join(tmpdir(), "bm-sab-"));
  const out = join(dir, "r.json");
  const r = spawnSync("npx", ["vitest", "run", ...TESTS, "--reporter=json", `--outputFile=${out}`], { cwd: PROXY, shell: true, encoding: "utf8" });
  let json;
  try { json = JSON.parse(readFileSync(out, "utf8")); } catch { rmSync(dir, { recursive: true, force: true }); return { blind: true, why: (r.stderr || r.stdout || "").slice(-800) }; }
  rmSync(dir, { recursive: true, force: true });
  const failed = new Set(), seen = new Set();
  for (const f of json.testResults ?? []) for (const a of f.assertionResults ?? []) {
    const id = caseOf(a.title); seen.add(id);
    if (a.status !== "passed") failed.add(id);
  }
  // A suite that failed to load has no assertion results: that is BLIND, never "nothing failed".
  if (seen.size === 0) return { blind: true, why: "no test results" };
  return { failed: [...failed].sort(), seen };
}

function plant(name, src, edits) {
  let out = src;
  for (const [a, b] of edits) {
    const n = out.split(a).length - 1;
    if (n !== 1) throw new Error(`${name}: anchor found ${n} times, need exactly 1:\n${a}`);
    out = out.replace(a, b);
  }
  if (out === src) throw new Error(`${name}: the plant changed nothing`);
  return out;
}

const only = process.argv[2];
let ok = true;
const base = runTests();
if (base.blind || base.failed.length) { console.log(`BASELINE ${base.blind ? "BLIND: " + base.why : "FAILED: " + base.failed.join(",")}`); process.exit(2); }
console.log(`baseline: 0 failing cases (${base.seen.size} cases seen)`);
for (const [name, s] of Object.entries(SABOTAGES)) {
  if (only && only !== name) continue;
  const path = join(PROXY, s.file);
  const orig = readFileSync(path);
  const before = sha(orig);
  try {
    writeFileSync(path, plant(name, orig.toString("utf8"), s.edits));
    const r = runTests();
    const want = [...s.expect].sort();
    if (r.blind) { ok = false; console.log(`${name}: BLIND (${r.why})`); }
    else if (r.failed.join(",") === want.join(",")) console.log(`${name}: AS PREDICTED, failing {${r.failed.join(", ")}}`);
    else { ok = false; console.log(`${name}: OFF-PREDICTION, failing {${r.failed.join(", ")}} against {${want.join(", ")}}`); }
  } finally {
    writeFileSync(path, orig);
    if (sha(readFileSync(path)) !== before) { console.log(`${name}: RESTORE FAILED for ${s.file}`); process.exit(3); }
  }
}
process.exit(ok ? 0 : 1);
