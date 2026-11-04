// S-TXNOEXCL (docs/test-identity.md, item 4): TX1's sabotage. The exclusion is removed (testDeviceIds returns no
// ids), TX1 is run, and it must FAIL with "Devices seen" up by exactly one (3 where 2 was expected) while its control
// still passes. The file is restored from a byte copy and its sha256 checked. Exit 0 as predicted, 1 off, 2 BLIND.
//
//   node scripts/sabotage-test-identity.mjs        (from dashboard/)
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const F = "src/analytics.ts";
const orig = readFileSync(F), before = createHash("sha256").update(orig).digest("hex");
const anchor = 'return (env.TEST_DEVICE_IDS ?? "").split(",").map((s) => s.trim()).filter(Boolean).map(deviceIdLiteral);';
const src = orig.toString("utf8");
if (src.split(anchor).length !== 2) { console.log("S-TXNOEXCL: BLIND -- anchor not found exactly once"); process.exit(2); }
let code = 1;
try {
  writeFileSync(F, src.replace(anchor, "return [] as string[]; // SABOTAGE S-TXNOEXCL"));
  const r = spawnSync("npx", ["vitest", "run", "test/test-identity.test.ts"], { shell: true, encoding: "utf8" });
  const out = ((r.stdout ?? "") + (r.stderr ?? "")).replace(/\x1b\[[0-9;]*m/g, "");
  const tx1Failed = /×[^\n]*TX1: every counter/.test(out) || /FAIL[^\n]*TX1: every counter/.test(out);
  const controlPassed = /✓[^\n]*TX1's control/.test(out) || !/FAIL[^\n]*TX1's control/.test(out);
  const upByOne = /expected 3 to be 2/.test(out);
  console.log(`S-TXNOEXCL: TX1 failed: ${tx1Failed}; with Devices seen 3 where 2 was expected (up by one): ${upByOne}; its control still passes: ${controlPassed}`);
  code = tx1Failed && upByOne && controlPassed ? 0 : 1;
  console.log(code === 0 ? "S-TXNOEXCL: AS PREDICTED" : "S-TXNOEXCL: OFF-PREDICTION");
} finally {
  writeFileSync(F, orig);
  if (createHash("sha256").update(readFileSync(F)).digest("hex") !== before) { console.log("S-TXNOEXCL: RESTORE FAILED"); process.exit(3); }
}
process.exit(code);
