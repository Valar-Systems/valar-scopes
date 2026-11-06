// MC's sabotages (docs/basemap-route.md A1.7, A2.40), graded by scripts/basemap-grade-mc.ts on the sealed store.
//
//   node scripts/sabotage-basemap-mc.mjs --cache <ingest cache> [S-NAME]
//
// Same discipline as sabotage-basemap.mjs: a clean baseline first (every MC row as frozen); each anchor found exactly
// once and the file changed; the failing ROWS compared with the frozen set exactly; the file restored from a byte copy
// and its sha256 checked. S-BBOX plants coverage.ts and grades against the coverage.bin that planted code computes
// (basemap-coverage-bin.ts, whose own control is reproducing the sealed file unplanted); the store is never touched.
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const PROXY = join(dirname(fileURLToPath(import.meta.url)), "..");
const ci = process.argv.indexOf("--cache"), cache = process.argv[ci + 1];
if (ci < 0 || !cache) throw new Error("--cache <dir>");
const only = process.argv.slice(2).find((a) => a.startsWith("S-"));
const BBOX_RINGS = "  for (const rings of states.map((rs) => { let a = 1e9, b = 1e9, c = -1e9, d = -1e9; for (const { xy } of rs) for (let i = 0; i < xy.length; i += 2) { a = Math.min(a, xy[i]!); c = Math.max(c, xy[i]!); b = Math.min(b, xy[i + 1]!); d = Math.max(d, xy[i + 1]!); } return [{ xy: Float64Array.from([a, b, c, b, c, d, a, d]) }]; })) { // SABOTAGE S-BBOX\n";
const SABOTAGES = {
  "S-BBOX": { file: "scripts/basemap/coverage.ts", edits: [["  for (const rings of states) {\n", BBOX_RINGS]], coverage: true, expect: ["MC2"] },
  "S-NOCANADA": { file: "src/basemap/build.ts", edits: [["      creditBits |= b; refs.push(ref);\n", "      creditBits |= b & ~8; refs.push(ref); // SABOTAGE S-NOCANADA\n"]], expect: ["MC4"] },
  "S-NOOCEAN": { file: "src/basemap/build.ts", edits: [["      creditBits |= b; refs.push(ref);\n", "      creditBits |= b & ~4; refs.push(ref); // SABOTAGE S-NOOCEAN\n"]], expect: ["MC4"] },
};
const sha = (b) => createHash("sha256").update(b).digest("hex");
const run = (args) => spawnSync("npx", ["tsx", ...args], { cwd: PROXY, shell: true, encoding: "utf8" });

/** The MC rows the grading script reports OFF-PREDICTION, or null when it could not read the store (BLIND). */
function grade(extra = []) {
  const r = run(["scripts/basemap-grade-mc.ts", "--cache", cache, ...extra]);
  const lines = (r.stdout ?? "").split("\n");
  if (r.status === 2 || !lines.some((l) => l.startsWith("store "))) return null;
  const offLines = lines.filter((l) => l.includes("OFF-PREDICTION"));
  grade.cells = offLines.map((l) => l.replace(": OFF-PREDICTION --", " ("). trim() + ")");
  return [...new Set(offLines.map((l) => l.split(/[ :]/)[0]))].sort();
}

const base = grade();
if (!base || base.length) { console.log(`BASELINE ${base ? "FAILED: " + base.join(",") : "BLIND"}`); process.exit(2); }
console.log("baseline: every MC row as frozen");
let ok = true;
for (const [name, s] of Object.entries(SABOTAGES)) {
  if (only && only !== name) continue;
  const path = join(PROXY, s.file), orig = readFileSync(path), before = sha(orig);
  try {
    let src = orig.toString("utf8");
    for (const [a, b] of s.edits) { const n = src.split(a).length - 1; if (n !== 1) throw new Error(`${name}: anchor found ${n} times`); src = src.replace(a, b); }
    if (src === orig.toString("utf8")) throw new Error(`${name}: the plant changed nothing`);
    writeFileSync(path, src);
    let extra = [];
    if (s.coverage) {
      const out = join(tmpdir(), `bm-cov-${name}.bin`);
      const c = run(["scripts/basemap-coverage-bin.ts", "--cache", cache, "--out", out]);
      if (c.status !== 0 || !/recomputed/.test(c.stdout ?? "")) { ok = false; console.log(`${name}: BLIND (the planted coverage did not compute)`); continue; }
      extra = ["--coverage", out];
    }
    const got = grade(extra), want = [...s.expect].sort();
    if (!got) { ok = false; console.log(`${name}: BLIND`); }
    else if (got.join(",") === want.join(",")) console.log(`${name}: AS PREDICTED, failing {${got.join(", ")}} through ${grade.cells.join(", ")}`);
    else { ok = false; console.log(`${name}: OFF-PREDICTION, failing {${got.join(", ")}} against {${want.join(", ")}}`); }
  } finally {
    writeFileSync(path, orig);
    if (sha(readFileSync(path)) !== before) { console.log(`${name}: RESTORE FAILED`); process.exit(3); }
  }
}
process.exit(ok ? 0 : 1);
