// The leaderboard model-shape sabotages (docs/leaderboard-model-shape.md, registered before they ran). Each is an in-place
// edit, asserted to apply exactly once, then leaderboard.test.ts is run and the set of RED LM rows must equal the registered set
// exactly. The file is restored from a byte copy and its sha256 checked. A crash or an unparseable run is an instrument
// failure, never a red.
//
//   node scripts/sabotage-lbmodel.mjs        (from proxy/)    exit 0 all as registered, 1 off, 2 rig, 3 restore failed
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const SRC = "src/leaderboard.ts";
const sha = (b) => createHash("sha256").update(b).digest("hex");
const ROWS = ["LM1:", "LM2:", "LM3:", "LM4:"];

const SABOTAGES = [
  {
    name: "S-LM-slice",
    edits: [['  const model = modelOf(request.headers.get("X-Blip-Model"));',
             '  const model = (request.headers.get("X-Blip-Model") ?? "").trim().slice(0, 16); /* SABOTAGE S-LM-slice */']],
    red: ["LM1:", "LM3:"],
  },
  {
    name: "S-LM-lax",
    edits: [['  return m.length <= MODEL_MAX && MODEL_RE.test(m) ? m : "";', '  return m; /* SABOTAGE S-LM-lax */']],
    red: ["LM3:"],
  },
];

function runTests() {
  const r = spawnSync("npx", ["vitest", "run", "test/leaderboard.test.ts"], { shell: true, encoding: "utf8" });
  const out = ((r.stdout ?? "") + (r.stderr ?? "")).replace(/\x1b\[[0-9;]*m/g, "");
  const total = /Tests\s+(?:(\d+) failed \| )?(\d+) passed \((\d+)\)/.exec(out);
  const red = ROWS.filter((t) => out.split("\n").some((l) => l.includes("×") && l.includes(t)));
  return { out, total, red };
}

let off = 0;
const base = runTests();
if (!base.total || base.red.length || base.total[1]) {
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
  const ok = r.red.length === s.red.length && s.red.every((k) => r.red.includes(k));
  off += ok ? 0 : 1;
  console.log(`${s.name}: ${ok ? "as registered" : "NOT AS REGISTERED"}; red ${JSON.stringify(r.red)} (want ${JSON.stringify(s.red)}); restored byte-identical`);
}
console.log(off ? `${off} NOT AS REGISTERED` : "ALL AS REGISTERED");
process.exit(off ? 1 : 0);
