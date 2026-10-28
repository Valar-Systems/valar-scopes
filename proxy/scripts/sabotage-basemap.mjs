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
const LIB = "scripts/basemap/ingest-lib.ts";
const FORMAT = "src/basemap/format.ts";
const FRAME = "src/basemap/frame.ts";
const LAYERS = "src/basemap/layers.ts";
const TERRAIN = "src/basemap/terrain.ts";
// Each sabotage runs against a test set: its files, and optionally a vitest -t filter (a family), with its own
// clean baseline. A filter narrows what is graded, so a sabotage graded under one says so in its output.
// "all" is a path filter, not a list, so a basemap test file added later is graded without anyone remembering to.
const SETS = {
  ingest: { files: ["test/basemap-ingest.test.ts"] },
  mv: { files: ["test/basemap-format.test.ts"], filter: "MV" },
  all: { files: ["test/basemap-"] },
};

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
  "S-INOTESORT": {
    file: LIB,
    edits: [['  for (let i = 1; i < notes.length; i++) if (byteCmp(notes[i - 1], notes[i]) >= 0) throw new Error("manifest notes must be sorted, in byte order");\n',
             '  notes.sort(byteCmp); // SABOTAGE S-INOTESORT: sorted for the caller instead of refused\n']],
    expect: ["MI4b"],
  },
  "S-IKEYS": {
    file: LIB,
    edits: [['  if (top !== "builder,files,predicates" && top !== "builder,files,notes,predicates") throw new Error(`manifest keys ${top}`);\n',
             '  void top; // SABOTAGE S-IKEYS: any top-level key accepted\n']],
    expect: ["MI4", "MI4b"],
  },
  "S-VCRC": {
    file: FORMAT, set: "mv",
    edits: [['    if (packageCrc(b) !== dv.getUint32(28, true)) throw new Bad("kCrc");\n',
             '    // SABOTAGE S-VCRC: the CRC check skipped\n']],
    expect: ["MV/PK-crc"],
  },
  "S-VSWAP": {
    file: FORMAT, set: "mv",
    edits: [['      if (e.off < expect) throw new Bad("kOverlap");\n      if (e.off > expect) throw new Bad("kGap");\n',
             '      if (e.off < expect) throw new Bad("kGap"); // SABOTAGE S-VSWAP\n      if (e.off > expect) throw new Bad("kOverlap");\n']],
    expect: ["MV/PK-gap", "MV/PK-overlap"],
  },
  "S-IBUS": {
    file: LIB,
    edits: [['    case "roads": return a["MTFCC"] === "S1100" || a["MTFCC"] === "S1200";\n',
             '    case "roads": return (a["MTFCC"] === "S1100" || a["MTFCC"] === "S1200") && !/(^|[^A-Za-z0-9])Bus($|[^A-Za-z0-9])/.test(a["FULLNAME"] ?? ""); // SABOTAGE S-IBUS\n']],
    expect: ["MI1"],
  },
  // A2.5's MR sabotages and A1.7's S-ECLAMP, graded against every basemap family.
  "S-RLBRECOMP": {
    file: FRAME, set: "all",
    edits: [["  const P: Pt = u0 === 0 ? a : [x0 + u0 * dx, y0 + u0 * dy];\n  const Q: Pt = u1 === 1 ? b : [x0 + u1 * dx, y0 + u1 * dy];\n",
             "  const P: Pt = [x0 + u0 * dx, y0 + u0 * dy]; // SABOTAGE S-RLBRECOMP\n  const Q: Pt = [x0 + u1 * dx, y0 + u1 * dy];\n"]],
    expect: ["MR1"],
  },
  "S-RXCUR": {
    file: FRAME, set: "all",
    edits: [["      if (inside(cur)) { if (!inside(prev)) out.push(cross(prev, cur)); out.push(cur); }\n      else if (inside(prev)) out.push(cross(prev, cur));\n",
             "      if (inside(cur)) { if (!inside(prev)) out.push(cross(cur, prev)); out.push(cur); } // SABOTAGE S-RXCUR\n      else if (inside(prev)) out.push(cross(cur, prev));\n"]],
    expect: ["MR2"],
  },
  "S-RORDER": {
    file: FRAME, set: "all",
    edits: [["  for (const [axis, sign] of [[0, -1], [0, 1], [1, -1], [1, 1]] as const) {\n",
             "  for (const [axis, sign] of [[1, -1], [1, 1], [0, -1], [0, 1]] as const) { // SABOTAGE S-RORDER: N, S, W, E\n"]],
    expect: ["MR2"],
  },
  "S-RNOCLOSE": {
    file: FRAME, set: "all",
    edits: [["  if (ring) while (out.length > 1 && out[out.length - 1]![0] === out[0]![0] && out[out.length - 1]![1] === out[0]![1]) out.pop();\n",
             "  void ring; // SABOTAGE S-RNOCLOSE\n"]],
    expect: ["MR3"],
  },
  "S-RGE": {
    file: FRAME, set: "all",
    edits: [["    if (at >= 0 && best > epsM) {", "    if (at >= 0 && best >= epsM) { // SABOTAGE S-RGE\n"]],
    expect: ["MR4"],
  },
  "S-RHYPOT": {
    file: FRAME, set: "all",
    edits: [["export const planarDist = (ex: number, ey: number) => Math.sqrt(ex * ex + ey * ey);\n",
             "export const planarDist = (ex: number, ey: number) => Math.hypot(ex, ey); // SABOTAGE S-RHYPOT\n"]],
    expect: ["MR7"],
  },
  "S-ECLAMP": {
    file: FRAME, set: "all",
    edits: [["    if (n < -QMAX || n > QMAX) throw new VertexRange(`vertex_range: ${n}`);\n",
             "    if (n < -QMAX || n > QMAX) return n < 0 ? -QMAX : QMAX; // SABOTAGE S-ECLAMP\n"]],
    expect: ["ME4"],
  },
  "S-RZERO": {
    file: FRAME, set: "all",
    edits: [["  return s < 0;\n", "  return s <= 0; // SABOTAGE S-RZERO\n"]],
    expect: ["MR5"],
  },
  // A1.7's ML sabotages and A2.5's S-R0M, graded against every basemap family. S-SQUARELEN was frozen as {ML6}
  // (A1.7), ran {ML6, MR6} at aee95a4 as A2.13 predicted, and is re-registered as {ML6, MR6} by ruling (A2.16).
  "S-R0M": {
    file: LAYERS, set: "all",
    edits: [["  const ranked = [...length].filter(([, m]) => m > 0).sort(", "  const ranked = [...length].sort( // SABOTAGE S-R0M\n"]],
    expect: ["MR6"],
  },
  "S-BUSSUB": {
    file: LAYERS, set: "all",
    edits: [["export const isBusRoute = (fullname: string | undefined) => fullname !== undefined && BUS.test(fullname);\n",
             "export const isBusRoute = (fullname: string | undefined) => fullname !== undefined && fullname.includes(\"Bus\"); // SABOTAGE S-BUSSUB\nvoid BUS;\n"]],
    expect: ["ML7"],
  },
  "S-SQUARELEN": {
    file: LAYERS, set: "all",
    edits: [["    length.set(name, (length.get(name) ?? 0) + inDiscLengthM(km, f.rKm));\n",
             "    void inDiscLengthM; // SABOTAGE S-SQUARELEN: the clipped length in the square\n" +
             "    length.set(name, (length.get(name) ?? 0) + clipLine(km, f.rKm).reduce((s, run) => s + run.slice(1).reduce((t, p, i) => t + planarDist(p[0] - run[i]![0], p[1] - run[i]![1]) * 1000, 0), 0));\n"]],
    expect: ["ML6", "MR6"],
  },
  "S-DEDUPALL": {
    file: LAYERS, set: "all",
    edits: [["    if (kept.some((k) => within3km(c.p[0] - k.p[0], c.p[1] - k.p[1]))) continue;\n",
             "    if (ranked.slice(0, ranked.indexOf(c)).some((k) => within3km(c.p[0] - k.p[0], c.p[1] - k.p[1]))) continue; // SABOTAGE S-DEDUPALL\n"]],
    expect: ["ML11"],
  },
  "S-DEDUPLT": {
    file: FRAME, set: "all",
    edits: [["export const within3km = (ex: number, ey: number) => planarDist(ex, ey) <= 3.0;\n",
             "export const within3km = (ex: number, ey: number) => planarDist(ex, ey) < 3.0; // SABOTAGE S-DEDUPLT\n"]],
    expect: ["ML11b"],
  },
  "S-PLACESORT": {
    file: LAYERS, set: "all",
    edits: [["  return places;\n", "  return places.sort((a, b) => planarDist(a.q[0], a.q[1]) - planarDist(b.q[0], b.q[1])); // SABOTAGE S-PLACESORT\n"]],
    expect: ["ML13"],
  },
  "S-RIVERTIE": {
    file: LAYERS, set: "all",
    edits: [["b[1] - a[1] || byteCmp(a[0], b[0])", "b[1] - a[1] /* SABOTAGE S-RIVERTIE: a stable sort, so source order */"]],
    expect: ["ML4"],
  },
  // A1.7's MT sabotages. S-TNOMARGIN waits for MB (A2.17), as S-RZERO waited for MR5.
  "S-TALIGN": {
    file: TERRAIN, set: "all",
    edits: [["      const px = x - 2 + b, py = y - 2 + a;\n", "      const px = x - 1 + b, py = y - 1 + a; // SABOTAGE S-TALIGN\n"]],
    expect: ["MT-impulse", "MT-corner"],
  },
  "S-TPAD": {
    file: TERRAIN, set: "all",
    edits: [["      t += s(cx, cy);\n", "      t += cx !== px || cy !== py ? 0 : s(cx, cy); // SABOTAGE S-TPAD: zero padding\n"]],
    expect: ["MT-corner", "MT2a", "MT2b", "MT2c", "MT2d", "MT2e"],
  },
  "S-TROUND": {
    file: TERRAIN, set: "all",
    edits: [["[Math.floor(z9X(lon)), Math.floor(z9Y(lat))]", "[Math.round(z9X(lon)), Math.round(z9Y(lat))] /* SABOTAGE S-TROUND */"]],
    expect: ["MT4"],
  },
  "S-TCOS": {
    file: TERRAIN, set: "all",
    edits: [["export const mppOf = (lat0: number) => 156543.03 * Math.cos(lat0 * PI / 180) / 2 ** 9;\n",
             "export const mppOf = (lat0: number) => 156543.03 / 2 ** 9 + 0 * lat0; // SABOTAGE S-TCOS\n"]],
    expect: ["MT2e"],
  },
  // The CONTROL on the missing-cases check below: a module that fails to load must grade BLIND, never "as predicted".
  "C-LOADFAIL": {
    file: LAYERS, set: "all",
    edits: [["export const isRiverName = ", "export const isRiverName = ((( // CONTROL C-LOADFAIL: a syntax error\n"]],
    expect: "BLIND",
  },
};

const sha = (b) => createHash("sha256").update(b).digest("hex");
const caseOf = (title) => (/^(M[A-Z](?:\d+[a-z]?|-[a-z]+)|MV\/PK-[A-Za-z0-9]+)/.exec(title) ?? [null, `UNNAMED(${title})`])[1];

// Every vitest run, and how many met a Workers runtime failure (ruled 2026-10-28, A2.12: "If the runtime failure
// recurs, report how often"). Printed at the end of every pass, zeros included. A run whose output carries the
// runtime's failure text is counted whether nothing ran or only some files did: both are instrument failures.
const tally = { runs: 0, runtimeAll: 0, runtimePartial: 0, maxTimeWait: 0, heldMs: 0 };
const RUNTIME_FAIL = /ERR_RUNTIME_FAILURE|The Workers runtime failed to start|Fallback service failed to fetch module/;

// THE PORT GATE (A2.14). Each vitest-pool-workers run opens hundreds of loopback connections for its module service,
// and each lingers in TIME_WAIT after it closes. Back-to-back runs filled Windows' 16,384 ephemeral ports (15,998
// counted right after a failing pass), and the runtime's connect was then refused. So a run starts only when the
// TIME_WAIT count is under TW_LIMIT -- polled, never slept for a fixed time -- and the count is printed per run, so a
// runtime failure at a LOW count would show that ports are not (or not only) the cause.
const TW_LIMIT = 8000;
function timeWaitCount() {
  if (process.platform !== "win32") return null;
  const r = spawnSync("powershell", ["-NoProfile", "-Command", "@(Get-NetTCPConnection -State TimeWait -ErrorAction SilentlyContinue).Count"], { encoding: "utf8" });
  const n = Number(r.stdout.trim());
  if (r.status !== 0 || !Number.isInteger(n)) throw new Error(`cannot read the TIME_WAIT count: ${r.stderr.slice(-300)}`);
  return n;
}
function holdForPorts() {
  const t0 = Date.now();
  let n = timeWaitCount();
  while (n !== null && n >= TW_LIMIT) {
    if (Date.now() - t0 > 600_000) throw new Error(`TIME_WAIT still ${n} after 10 minutes`);
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 5000);
    n = timeWaitCount();
  }
  tally.heldMs += Date.now() - t0;
  if (n !== null && n > tally.maxTimeWait) tally.maxTimeWait = n;
  return n;
}

function runTests(setName) {
  const set = SETS[setName];
  const tw = holdForPorts();
  const dir = mkdtempSync(join(tmpdir(), "bm-sab-"));
  const out = join(dir, "r.json");
  const args = ["vitest", "run", ...set.files, ...(set.filter ? ["-t", set.filter] : []), "--reporter=json", `--outputFile=${out}`];
  const r = spawnSync("npx", args, { cwd: PROXY, shell: true, encoding: "utf8" });
  tally.runs++;
  const runtime = RUNTIME_FAIL.test(`${r.stderr}\n${r.stdout}`);
  const at = tw === null ? "" : ` [TIME_WAIT ${tw} at start]`;
  let json;
  try { json = JSON.parse(readFileSync(out, "utf8")); } catch { json = null; }
  rmSync(dir, { recursive: true, force: true });
  const ran = json ? (json.testResults ?? []).length : 0;
  if (runtime) { if (ran) tally.runtimePartial++; else tally.runtimeAll++; }
  if (!json) return { blind: true, runtime, at, why: runtime ? "the Workers runtime failed" : (r.stderr || r.stdout || "").slice(-800) };
  if (runtime && !ran) return { blind: true, runtime, at, why: "the Workers runtime failed (0 test files ran)" };
  const failed = new Set(), seen = new Set();
  for (const f of json.testResults ?? []) for (const a of f.assertionResults ?? []) {
    if (a.status === "skipped" || a.status === "pending") continue; // outside the set's filter
    const id = caseOf(a.title); seen.add(id);
    if (a.status !== "passed") failed.add(id);
  }
  // A suite that failed to load has no assertion results: that is BLIND, never "nothing failed".
  if (seen.size === 0) return { blind: true, runtime, at, why: "no test results" };
  return { failed: [...failed].sort(), seen, files: ran, runtime, at };
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
const baselines = {};
/** Each set's clean baseline, once: with nothing planted, nothing in the set may fail. */
function baseline(setName) {
  if (baselines[setName]) return;
  const base = runTests(setName);
  if (base.blind || base.failed.length) { console.log(`BASELINE[${setName}] ${base.blind ? "BLIND: " + base.why : "FAILED: " + base.failed.join(",")}`); printTally(); process.exit(2); }
  const f = SETS[setName].filter;
  console.log(`baseline[${setName}${f ? `, -t ${f}` : ""}]: 0 failing cases (${base.seen.size} cases seen in ${base.files} files)${base.at}`);
  baselines[setName] = base.seen;
}
for (const [name, s] of Object.entries(SABOTAGES)) {
  if (only && only !== name) continue;
  const setName = s.set ?? "ingest";
  baseline(setName);
  const path = join(PROXY, s.file);
  const orig = readFileSync(path);
  const before = sha(orig);
  try {
    writeFileSync(path, plant(name, orig.toString("utf8"), s.edits));
    const r = runTests(setName);
    // Every case the baseline saw must be seen again: a file that failed to load drops its cases from `seen`
    // WITHOUT adding them to `failed`, and would otherwise grade as "nothing else failed".
    const missing = r.blind ? [] : [...baselines[setName]].filter((id) => !r.seen.has(id));
    const why = r.blind ? r.why : r.runtime ? `the Workers runtime failed partway (${missing.length} baseline cases not run)` : missing.length ? `${missing.length} baseline cases not run: ${missing.slice(0, 6).join(", ")}${missing.length > 6 ? ", ..." : ""}` : null;
    if (s.expect === "BLIND") {
      // A control's BLIND must come from its OWN plant: one the runtime also failed on cannot tell the two apart.
      if (r.runtime) { ok = false; console.log(`${name}: UNREADABLE, the runtime also failed (${why})${r.at}`); }
      else if (why) console.log(`${name}: AS PREDICTED, BLIND (${why})${r.at}`);
      else { ok = false; console.log(`${name}: OFF-PREDICTION, graded {${r.failed.join(", ")}} where BLIND was required${r.at}`); }
      continue;
    }
    const want = [...s.expect].sort();
    if (why) { ok = false; console.log(`${name}: BLIND (${why})${r.at}`); }
    else if (r.failed.join(",") === want.join(",")) console.log(`${name}: AS PREDICTED, failing {${r.failed.join(", ")}}${r.at}`);
    else { ok = false; console.log(`${name}: OFF-PREDICTION, failing {${r.failed.join(", ")}} against {${want.join(", ")}}${r.at}`); }
  } finally {
    writeFileSync(path, orig);
    if (sha(readFileSync(path)) !== before) { console.log(`${name}: RESTORE FAILED for ${s.file}`); process.exit(3); }
  }
}
printTally();
process.exit(ok ? 0 : 1);

function printTally() {
  console.log(`runs: ${tally.runs}; runtime failures: ${tally.runtimeAll} with nothing run, ${tally.runtimePartial} partway; ` +
    `highest TIME_WAIT at a run's start: ${tally.maxTimeWait} (gate ${TW_LIMIT}); held for ports: ${Math.round(tally.heldMs / 1000)} s`);
}
