// The sabotages of docs/smoke-basemap-query.md, registered before they ran. Each is an in-place edit, asserted to
// apply exactly once; the WHOLE proxy suite then runs, and the exact set of failed tests (and the failed count) must
// equal the registered set. Files are restored from byte copies, sha256 checked. Before each run it waits for
// TIME_WAIT to fall below 8,000 (workers runs read BLIND above it), giving up after 10 minutes as a RIG -- never a
// reading. A crash, a timeout or an unparseable summary is an instrument failure, never a red.
//
//   node scripts/sabotage-smoke-basemap.mjs      (from proxy/)    exit 0 as registered, 1 off, 2 rig, 3 restore failed
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const sha = (b) => createHash("sha256").update(b).digest("hex");
const SABOTAGES = [
  { name: "S-SQ", file: "scripts/smoke-prod.sh", from: "&eps=$BM_EPS", to: "", red: ["SQ1:", "SQ2:"] },
  { name: "S-SQP", file: "src/basemap/query.ts", from: 'const eps = p.get("eps") ?? "";', to: 'const eps = p.get("eps") ?? "100584"; // SABOTAGE S-SQP', red: ["SQ-C:"] },
];

function timeWait() {
  const r = spawnSync("netstat", ["-an"], { encoding: "utf8" });
  return (r.stdout ?? "").split("\n").filter((l) => l.includes("TIME_WAIT")).length;
}
function gate() {
  const end = Date.now() + 600_000;
  for (;;) {
    const n = timeWait();
    if (n < 8000) return n;
    if (Date.now() > end) { console.log(`RIG: TIME_WAIT stayed at ${n} for 10 minutes`); process.exit(2); }
    spawnSync(process.execPath, ["-e", "setTimeout(() => {}, 20000)"]);
  }
}
function suite() {
  const tw = gate();
  const r = spawnSync("npx", ["vitest", "run"], { shell: true, encoding: "utf8", timeout: 1_500_000 });
  const out = ((r.stdout ?? "") + (r.stderr ?? "")).replace(/\x1b\[[0-9;]*m/g, "");
  const total = /Tests\s+(?:(\d+) failed \| )?(\d+) passed(?: \| (\d+) skipped)? \((\d+)\)/.exec(out);
  const failed = [...new Set(out.split("\n").map((l) => /^\s*FAIL\s+\S+\.test\.ts\s+>\s+(.*)$/.exec(l)?.[1]?.trim()).filter(Boolean))];
  const names = failed.map((f) => f.split(" > ").pop());
  return { tw, total, names, timedOut: r.error?.code === "ETIMEDOUT" };
}

const base = suite();
if (base.timedOut || !base.total || base.total[1]) {
  console.log(`RIG: the unsabotaged suite must pass first (failed ${base.total?.[1] ?? "?"}, parsed ${!!base.total}, timed out ${base.timedOut})`);
  process.exit(2);
}
console.log(`CONTROL (unsabotaged): ${base.total[2]} passed, ${base.total[3] ?? 0} skipped, of ${base.total[4]}; 0 failed (TIME_WAIT ${base.tw})`);

let off = 0;
for (const s of SABOTAGES) {
  const orig = readFileSync(s.file), before = sha(orig);
  const text = orig.toString("utf8");
  if (text.split(s.from).length !== 2) { console.log(`RIG: ${s.name}: its anchor is not in ${s.file} exactly once`); process.exit(2); }
  let r;
  try {
    writeFileSync(s.file, text.replace(s.from, s.to));
    if (sha(readFileSync(s.file)) === before) { console.log(`RIG: ${s.name} did not change the file`); process.exit(2); }
    r = suite();
  } finally {
    writeFileSync(s.file, orig);
    if (sha(readFileSync(s.file)) !== before) { console.log(`${s.name}: RESTORE FAILED`); process.exit(3); }
  }
  if (r.timedOut || !r.total) { console.log(`${s.name}: INSTRUMENT FAILURE (timed out ${r.timedOut}, summary parsed ${!!r.total})`); off++; continue; }
  const failedCount = Number(r.total[1] ?? 0);
  const matched = s.red.every((p) => r.names.filter((n) => n.startsWith(p)).length === 1);
  const extra = r.names.filter((n) => !s.red.some((p) => n.startsWith(p)));
  const ok = matched && extra.length === 0 && failedCount === s.red.length;
  off += !ok;
  console.log(`${s.name}: ${ok ? "as registered" : "NOT AS REGISTERED"}; failed ${failedCount} (want ${s.red.length}): ` +
    `${JSON.stringify(r.names.map((n) => n.split(":")[0]))} (want ${JSON.stringify(s.red.map((p) => p.replace(":", "")))}); ` +
    `TIME_WAIT ${r.tw}; restored byte-identical`);
}
console.log(off ? `${off} NOT AS REGISTERED` : "ALL AS REGISTERED");
process.exit(off ? 1 : 0);
