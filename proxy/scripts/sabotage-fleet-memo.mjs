// C1-red (docs/proxy-bill.md, step 4a): the fw: memo's controls must go red when the memo is planted out. One in-place
// edit to src/fleet.ts, asserted to apply exactly once; then test/fleet-memo.test.ts is run, and the set of RED rows must
// equal the registered set exactly, with C1 failing on exactly 22 reads (every request reading, none answered). The file
// is restored from a byte copy and its sha256 checked. A crash or an unparseable run is an instrument failure, never a red.
//
//   node scripts/sabotage-fleet-memo.mjs     (from proxy/)    exit 0 as registered, 1 off, 2 rig, 3 restore failed
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const SRC = "src/fleet.ts";
const sha = (b) => createHash("sha256").update(b).digest("hex");
const ROWS = ["C1:", "C2:", "C3:", "no decision", "a failed read", "P:", "P-red:"];

const SABOTAGES = [
  {
    name: "S-FM-nomemo",
    edits: [["    if (m && m.fw === fw && clock() - m.lastSeen < REFRESH_MS) {",
             "    if (false && m) { /* SABOTAGE S-FM-nomemo */"]],
    red: ["C1:", "C2:", "a failed read"],
    message: /expected 22 to be 3/,   // C1 reads on every one of its 22 requests
  },
];

function runTests() {
  const r = spawnSync("npx", ["vitest", "run", "test/fleet-memo.test.ts"], { shell: true, encoding: "utf8" });
  const out = ((r.stdout ?? "") + (r.stderr ?? "")).replace(/\x1b\[[0-9;]*m/g, "");
  const total = /Tests\s+(?:(\d+) failed \| )?(\d+) passed \((\d+)\)/.exec(out) ?? /Tests\s+(\d+) failed \((\d+)\)/.exec(out);
  const red = ROWS.filter((t) => out.split("\n").some((l) => l.includes("×") && l.includes(t)));
  return { out, total, red };
}

let off = 0;
const base = runTests();
if (!base.total || base.red.length || (base.total[1] && base.total[1] !== "0")) {
  console.log(`RIG: the unsabotaged run must be all green first (red: ${base.red.join(" ") || "none"}; parsed: ${!!base.total})`);
  process.exit(2);
}
console.log(`CONTROL (unsabotaged): ${base.total[2]} passed of ${base.total[3]}, red none`);
for (const s of SABOTAGES) {
  const orig = readFileSync(SRC), before = sha(orig);
  let text = orig.toString("utf8");
  for (const [a, b] of s.edits) {
    if (text.split(a).length !== 2) { console.log(`RIG: ${s.name}: its anchor is not in ${SRC} exactly once`); process.exit(2); }
    text = text.replace(a, b);
  }
  let r;
  try {
    writeFileSync(SRC, text);
    if (sha(readFileSync(SRC)) === before) { console.log(`RIG: ${s.name} did not change the file`); process.exit(2); }
    r = runTests();
  } finally {
    writeFileSync(SRC, orig);
    if (sha(readFileSync(SRC)) !== before) { console.log(`${s.name}: RESTORE FAILED`); process.exit(3); }
  }
  if (!r.total) { console.log(`${s.name}: INSTRUMENT FAILURE -- the run's summary could not be read`); off++; continue; }
  const setOk = r.red.length === s.red.length && s.red.every((k) => r.red.includes(k));
  const msgOk = s.message.test(r.out);
  const ok = setOk && msgOk;
  off += ok ? 0 : 1;
  console.log(`${s.name}: ${ok ? "as registered" : "NOT AS REGISTERED"}; red ${JSON.stringify(r.red)} (want ${JSON.stringify(s.red)}); `
    + `C1 on 22 reads: ${msgOk ? "yes" : "NO"}; restored byte-identical`);
}
console.log(off ? `${off} NOT AS REGISTERED` : "ALL AS REGISTERED");
process.exit(off ? 1 : 0);
