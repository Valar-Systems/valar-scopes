// W4's sabotages (docs/weatherscope-stage4.md S4.12.5; their exact red sets frozen in S4.26 before this first ran). Each is
// an in-place edit asserted to apply exactly once; then the five W4 test files run, and the set of RED test rows must equal
// the frozen set exactly. The file is restored from a byte copy and its sha256 checked. A crash or an unparseable run is an
// instrument failure, never a red. The unsabotaged run must be all green first.
//
//   node scripts/sabotage-wx-shsr.mjs        (from proxy/)    exit 0 all as frozen, 1 off, 2 rig, 3 restore failed
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const sha = (b) => createHash("sha256").update(b).digest("hex");
const FILES = ["test/wx-shsr.test.ts", "test/wx-endpoint.test.ts", "test/wx-ingest-split.test.ts",
               "test/wx-dev-products.test.ts", "test/wx-ingest-dev.test.ts"];

const SABOTAGES = [
  {
    name: "S-X-CODE", file: "src/wx/products.ts",
    old: "    codeOf: codeOfComposite,\n  },\n];",
    new: "    codeOf: (v: number) => (v === -99 ? 15 : codeOfComposite(v)), /* SABOTAGE S-X-CODE */\n  },\n];",
    red: ["X1", "X3"],
  },
  {
    name: "S-X-HEALTH", file: "src/wx/handler.ts",
    old: "  const bucket = env.WX;\n  if (!bucket) return \"not_configured\";\n  const recs = await Promise.all(products.map(",
    new: "  const bucket = env.WX;\n  if (!bucket) return \"not_configured\";\n  products = products.filter((p) => routed.has(p.id)); /* SABOTAGE S-X-HEALTH */\n  const recs = await Promise.all(products.map(",
    red: ["H1", "H2", "H3", "X5"],
  },
  {
    name: "S-X-CREF", file: "src/wx/ingest-worker.ts",
    old: 'export const CREF_WORKER: readonly Product[] = PRODUCTS.filter((p) => p.id === "cref");',
    new: 'export const CREF_WORKER: readonly Product[] = PRODUCTS.filter((p) => p.id === "cref" || p.id === "shsr"); /* SABOTAGE S-X-CREF */',
    red: ["FT1", "FX", "X7d"],
  },
  {
    name: "S-X-SWITCH", file: "src/wx/ingest-mrms-worker.ts",
    old: '  if (env.WX_INGEST !== "on") {',
    new: '  if (false) { /* SABOTAGE S-X-SWITCH */',
    red: ["FX", "X7c"],
  },
  {
    name: "S-X-LITERAL", file: "src/wx/ingest.ts",
    old: "export const RETAIN_S = 3600;\n",
    new: "export const RETAIN_S = 3600;\nexport const SABOTAGE_STEM = \"MRMS_SeamlessHSR_00.00\"; /* SABOTAGE S-X-LITERAL */\n",
    red: ["X8c"],
  },
];

/** The red rows: each failed test's leading token (X1, FT1, H3, ...), or its whole title when it has none. */
function runTests() {
  const r = spawnSync("npx", ["vitest", "run", "--reporter=verbose", ...FILES], { shell: true, encoding: "utf8" });
  const out = ((r.stdout ?? "") + (r.stderr ?? "")).replace(/\x1b\[[0-9;]*m/g, "");
  const total = /Tests\s+(?:(\d+) failed \| )?(\d+) passed \((\d+)\)/.exec(out);
  const red = new Set();
  for (const l of out.split("\n")) {
    const m = /^\s*×\s+(.*?)(?:\s+\d+ms)?$/.exec(l);
    if (!m) continue;
    const title = m[1].split(" > ").pop().trim();
    const tok = /^((?:X|FT|FX|H|DP|PS)\w*)\s/.exec(title);
    red.add(tok ? tok[1] : title);
  }
  return { out, total, red: [...red].sort(), crashed: r.status !== 0 && !total };
}

const base = runTests();
if (!base.total || base.red.length || base.total[1]) {
  console.log(`RIG: the unsabotaged run must be all green first (red: ${base.red.join(" ") || "none"}; parsed: ${!!base.total})`);
  console.log(base.out.slice(-1500));
  process.exit(2);
}
console.log(`base: all green, ${base.total[2]} tests`);
let off = 0;
for (const s of SABOTAGES) {
  const before = readFileSync(s.file);
  const text = before.toString("utf8");
  const n = text.split(s.old).length - 1;
  if (n !== 1) { console.log(`${s.name}: the anchor appears ${n} times, not once -- RIG`); process.exit(2); }
  writeFileSync(s.file, text.replace(s.old, s.new));
  if (readFileSync(s.file).equals(before)) { console.log(`${s.name}: the plant did not apply -- RIG`); process.exit(2); }
  let got;
  try { got = runTests(); } finally {
    writeFileSync(s.file, before);
  }
  if (sha(readFileSync(s.file)) !== sha(before)) { console.log(`${s.name}: RESTORE FAILED`); process.exit(3); }
  if (got.crashed || !got.total) { console.log(`${s.name}: the run crashed or could not be parsed -- RIG`); console.log(got.out.slice(-1500)); process.exit(2); }
  const want = [...s.red].sort();
  const ok = JSON.stringify(got.red) === JSON.stringify(want);
  if (!ok) off++;
  console.log(`${s.name}: red ${JSON.stringify(got.red)}, frozen ${JSON.stringify(want)}; restored by sha -> ${ok ? "AS FROZEN" : "OFF"}`);
}
console.log(off ? `OFF: ${off} sabotage(s) off their frozen set` : "ALL AS FROZEN");
process.exit(off ? 1 : 0);
