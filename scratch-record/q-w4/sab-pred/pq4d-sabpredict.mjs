// R12 (b)/(c): each build()-reaching sabotage's red set among the rows this PR re-froze or added (MB, MN, MW13, SEA-CI), read from the
// PINNED PROTOTYPE pq4d under the SAME plant -- never from the code under test. pq4d's own files are planted in place from a byte copy
// and restored, with every sha256 checked after each plant (pq4d is scratch, outside every repository).
//   node pq4d-sabpredict.mjs --control      the clean instrument vs the frozen JSONs (must be EQUAL before anything is predicted)
//   node pq4d-sabpredict.mjs [name ...]     the plants (default: all), a grade per plant; JSON to sab-pred/predicted.json
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";

const S = resolve(".");
const PQ = join(S, "pq4d"), RUN_DIR = join(S, "wt-main", "proxy");
const ENV = { ...process.env, Q4_LADDER: "b", Q4_D2: "1", Q4_TP: "1", Q4_PROV: "1", Q4_SEA: "1" };
for (const k of ["Q4_SEAPLANT", "Q4_BACKSTOP", "Q4_PLANT_MIB"]) delete ENV[k];
const sha = (b) => createHash("sha256").update(b).digest("hex");
const lf = (t) => t.split("\r\n").join("\n");

function instrument(env = ENV) {
  const r = spawnSync("node", ["--import", "tsx", "../../pq4d-sabrows.mts"], { cwd: RUN_DIR, env, encoding: "utf8", shell: false, timeout: 600_000 });
  try { return { ok: true, j: JSON.parse(r.stdout) }; } catch { return { ok: false, why: `exit ${r.status}: ${(r.stderr || r.stdout || "").slice(-400)}` }; }
}
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const at = (o, path) => path.split(".").reduce((x, k) => (x == null ? x : x[k]), o);

if (process.argv[2] === "--control") {
  const got = instrument();
  if (!got.ok) { console.log(`CONTROL: the instrument did not run: ${got.why}`); process.exit(2); }
  const old = JSON.parse(readFileSync(join(S, "cp-out", "oldrows-pq4d.json"), "utf8")), sea = JSON.parse(readFileSync(join(S, "cp-out", "seaci-pq4d.json"), "utf8"));
  const diffs = [];
  for (const k of ["A@812", "A@811", "A@667", "A@623", "A@624", "B@841", "B@838", "A8@1322", "A8@1321", "A@100", "MW13"]) if (!eq(got.j[k], old[k])) diffs.push(k);
  for (const n of ["kb1", "kb1c", "edge2", "kb2cross", "tsea0-without", "tsea0-with", "bowtie", "xisland", "xisland-tall", "provA-both", "provA-square", "provB", "d2", "d2-control"]) {
    const { chosenCap, floor162, ...frozen } = sea[n];
    if (!eq(got.j[`sea:${n}`], frozen)) diffs.push(`sea:${n}`);
  }
  const reg = { "xchk:xisland": { status: 200, repairs: [{ rings: 2, restored: 2, rounds: 2 }] }, "xchk:xisland-tall": { status: 500, code: "sea_repair_failed", stop: { rings: 2, restored: 2, rounds: 3 } }, "provB-ctl": { w2: 1, w8: 1 } };
  for (const [k, v] of Object.entries(reg)) if (!eq(got.j[k], v)) diffs.push(`${k} (got ${JSON.stringify(got.j[k])})`);
  console.log(`CONTROL: ${Object.keys(got.j).length} objects; ${diffs.length} differ from the frozen values${diffs.length ? `: ${diffs.join(", ")}` : ""}`);
  process.exit(diffs.length ? 1 : 0);
}

// ---- the rows, each graded RED when a field it asserts differs from the clean (= frozen = the test's expected) value, or its fixture threw
const MBF = ["status", "kOk", "totalBytes", "toleranceMm", "water", "roads", "dropped", "eps"];
const SEA5 = ["kb1", "kb1c", "kb2cross", "edge2", "tsea0-with"];
const ROWS = {
  MB1: [["A@812", MBF]], MB2: [["A@811", MBF]], MB3: [["A@667", [...MBF, "roadEnds"]]], MB4: [["A@623", [...MBF, "radiusMm", "steps"]]],
  MB5: [["A@624", [...MBF, "steps"]]], MB6: [["B@841", [...MBF, "roadPts"]]], MB7: [["B@838", [...MBF, "roadPts"]]],
  MB8a: [["A8@1322", MBF]], MB8b: [["A8@1321", MBF]],
  MB9: [["A@795", [...MBF, "roadEnds"]]], // PROPOSED (R12 finding): A at 795, rung 3 with only the shortest secondary dropped
  MN1: [["A@100", ["status", "rung", "nt", "steps", "sections"]]], MN2: [["A@812", ["status", "rung", "nt"]]], MN3: [["A@811", ["status", "rung"]]],
  MN4: [["A@624", ["status", "rung"]]], MN5: [["B@841", ["status", "rung"]]],
  MW13: [["MW13", ["status", "code", "reads"]]],
  "T-SEA0": [["sea:tsea0-without", ["status", "kOk", "seaRings", "seaLines", "rings"]]],
  "T-SEA0c": [["sea:tsea0-with", ["status", "seaRings", "seaLines"]]],
  KB2: SEA5.map((n) => [`sea:${n}`, ["status", "crossings"]]),
  BRIDGES: SEA5.map((n) => [`sea:${n}`, ["status", "sea.bridges", "seaRings"]]),
  EDGE2: [["sea:edge2", ["status", "edge"]]],
  KB7C: [["sea:bowtie", ["status", "sea.refused", "sea.collapsed", "sea.dropped", "sea.fates", "rings"]], ["sea:kb1", ["status", "sea.refused", "sea.collapsed", "sea.dropped"]]],
  XISLAND: [["sea:xisland", ["status", "sea.repairs", "sea.repairVertices", "sea.fates", "sea.refused5", "crossings"]], ["xchk:xisland", ["status", "repairs"]]],
  "XISLAND-TALL": [["sea:xisland-tall", ["status", "code"]], ["xchk:xisland-tall", ["status", "code", "stop"]]],
  REFUSED5: ["kb1", "kb1c", "edge2", "kb2cross", "tsea0-with", "bowtie"].map((n) => [`sea:${n}`, ["status", "sea.refused5", "sea.repairs"]]),
  "T-PROV-a": [["sea:provA-both", ["status", "rung", "steps", "rings", "crossings", "refused", "refusedBuilt", "collapsedWater"]], ["sea:provA-square", ["status", "rings", "refused", "refusedBuilt", "collapsedWater"]]],
  "T-PROV-b": [["sea:provB", ["status", "rung", "collapsedWater", "refused", "refusedBuilt"]], ["provB-ctl", ["w2", "w8"]]],
  "T-D2": [["sea:d2", ["status", "steps", "radiusMm"]]],
  "T-D2c": [["sea:d2-control", ["status", "steps"]]],
  "T-SEALOG": [["xchk:xisland", ["status", "repairs"]], ["xchk:xisland-tall", ["status", "code", "stop"]]],
};
// Two rows assert a relation, not a value: MN6's identity and T-PROV-a's byte equality. Each is evaluated on the planted output itself.
const PRED = {
  MN6: (j) => [["A", 812], ["A", 811], ["A", 624], ["B", 841]].every(([w, c]) => { const o = j[`${w}@${c}`]; return o && o.status === 200 && 32 + 16 * o.sectionCount + Object.values(o.sections).reduce((a, x) => a + x, 0) === o.totalBytes; })
    && j["A@100"]?.status === 422 && Object.values(j["A@100"].sections ?? {}).reduce((a, x) => a + x, 0) === 511,
  "T-PROV-a": (j) => j["sea:provA-both"]?.sha12 !== undefined && j["sea:provA-both"].sha12 === j["sea:provA-square"]?.sha12,
  "KB2/rings": (j) => SEA5.every((n) => (j[`sea:${n}`]?.rings ?? 0) > 0),
};
function grade(clean, j) {
  const red = new Set(), why = {};
  for (const [row, checks] of Object.entries(ROWS)) for (const [key, fields] of checks) {
    if (j[key]?.error) { red.add(row); why[row] = `${key} threw`; continue; }
    for (const f of fields) if (!eq(at(j[key], f), at(clean[key], f))) { red.add(row); why[row] ??= `${key}.${f}`; }
  }
  for (const [row, p] of Object.entries(PRED)) { const id = row.split("/")[0]; if (!p(j)) { red.add(id); why[id] ??= `${row} relation`; } }
  return { red: [...red].sort(), why };
}

// ---- the plants: the shipped driver's 28 build() plants (its own text), S-RUNG at pq4d's b-path occurrence, and (c)'s four
const SHIPPED = join(S, "wt-code", "proxy", "scripts", "sabotage-basemap.mjs");
const src = lf(readFileSync(SHIPPED, "utf8"));
const head = src.slice(src.indexOf("const LIB ="), src.indexOf("const SABOTAGES = {"));
const body = src.slice(src.indexOf("const SABOTAGES = {"), src.indexOf("\nconst sha ="));
const SAB = new Function(`${head}\n${body}\nreturn SABOTAGES;`)();
const BUILD_FILES = /src\/basemap\/(frame|layers|terrain|budget|build|provenance)\.ts$/;
const indent2 = (t) => t.replace(/^( +)/gm, "  $1");
export const C_PLANTS = JSON.parse(readFileSync(join(S, "sab-pred", "c-plants.json"), "utf8")); // (c)'s four, shipped text
const PLANTS = {};
for (const [name, s] of Object.entries(SAB)) {
  const files = s.files ?? [[s.file, s.edits]];
  if (files.every(([f]) => BUILD_FILES.test(f)) && (s.set ?? "ingest") === "all") PLANTS[name] = { files, expect: s.expect };
}
PLANTS["S-RUNG"].files = [["src/basemap/budget.ts", [["return done(2, { content: c2,", "return done(1, { content: c2, /* SABOTAGE S-RUNG */"]]]];
PLANTS["S-RUNG"].pq4dOverride = true;
for (const [name, p] of Object.entries(C_PLANTS)) PLANTS[name] = { files: p.files.map(([f, edits]) => [f, edits.map(([a, b]) => [indent2(a), indent2(b)])]), expect: null, reanchored: true };

const only = process.argv.slice(2);
const cleanRun = instrument();
if (!cleanRun.ok) { console.log(`clean run failed: ${cleanRun.why}`); process.exit(2); }
const clean = cleanRun.j;
if (process.argv[2] === "--knobs") {
  // The grader's check: each prototype knob plant must grade EXACTLY its registered SEA-CI set (docs/basemap-dense-code.md, SEA-CI)
  const SEACI_ROWS = new Set(["T-SEA0", "T-SEA0c", "KB2", "BRIDGES", "EDGE2", "KB7C", "XISLAND", "XISLAND-TALL", "REFUSED5", "T-PROV-a", "T-PROV-b", "T-D2", "T-D2c", "T-SEALOG"]);
  const KNOBS = [["cross", { Q4_SEAPLANT: "cross" }, ["KB2"]], ["nobridge", { Q4_SEAPLANT: "nobridge" }, ["BRIDGES", "T-SEA0c"]],
    ["order", { Q4_SEAPLANT: "order" }, ["EDGE2", "T-SEALOG", "XISLAND", "XISLAND-TALL"]], ["edgeunlock", { Q4_SEAPLANT: "edgeunlock" }, ["EDGE2"]],
    ["limit1", { Q4_SEAPLANT: "limit1" }, ["T-SEALOG", "XISLAND", "XISLAND-TALL"]], ["backstop off", { Q4_BACKSTOP: "0" }, ["KB7C", "T-PROV-a", "T-PROV-b"]],
    ["prov unset", { Q4_PROV: "" }, ["KB7C", "T-PROV-a", "T-PROV-b"]]];
  let same = 0;
  for (const [name, extra, want] of KNOBS) {
    const env = { ...ENV, SABROWS_KNOB: "1", ...extra };
    if (extra.Q4_PROV === "") delete env.Q4_PROV;
    const r = instrument(env);
    if (!r.ok) { console.log(`knob ${name}: did not run: ${r.why.slice(0, 200)}`); continue; }
    const g = grade(clean, r.j), sea = g.red.filter((x) => SEACI_ROWS.has(x)), other = g.red.filter((x) => !SEACI_ROWS.has(x));
    const ok = eq(sea, [...want].sort()); if (ok) same++;
    console.log(`knob ${name.padEnd(12)} SEA-CI red {${sea.join(", ")}} vs registered {${[...want].sort().join(", ")}}: ${ok ? "SAME" : "DIFFERENT"}; other graded rows red: {${other.join(", ")}}`);
  }
  console.log(`grader check: ${same} of ${KNOBS.length} knob plants reproduce their registered sets`);
  process.exit(same === KNOBS.length ? 0 : 1);
}
const results = {};
for (const [name, p] of Object.entries(PLANTS)) {
  if (only.length && !only.includes(name)) continue;
  const copies = p.files.map(([f, edits]) => { const path = join(PQ, f), orig = readFileSync(path); return { f, edits, path, orig, before: sha(orig) }; });
  let res;
  try {
    for (const c of copies) {
      let t = c.orig.toString("utf8");
      const nl = t.includes("\r\n") ? (x) => x.split("\n").join("\r\n") : (x) => x; // pq4d's files are CRLF: the edit in the file's own line ending
      for (const [a0, b0] of c.edits) { const a = nl(a0), b = nl(b0); const n = t.split(a).length - 1; if (n !== 1) throw new Error(`${name}: anchor found ${n} times in pq4d ${c.f}: ${a.slice(0, 80)}`); t = t.replace(a, () => b); }
      if (t === c.orig.toString("utf8")) throw new Error(`${name}: the plant changed nothing`);
      writeFileSync(c.path, t);
    }
    const r = instrument();
    res = r.ok ? { ...grade(clean, r.j) } : { loadFailed: r.why.slice(0, 200) };
  } catch (e) { res = { rig: String(e.message) }; }
  finally {
    for (const c of copies) writeFileSync(c.path, c.orig);
    for (const c of copies) if (sha(readFileSync(c.path)) !== c.before) { console.log(`${name}: RESTORE FAILED for pq4d ${c.f}`); process.exit(3); }
  }
  results[name] = { ...res, frozen: p.expect };
  console.log(`${name.padEnd(16)} ${res.rig ? `RIG ${res.rig}` : res.loadFailed ? `LOAD FAILED (${res.loadFailed.split("\n")[0].slice(0, 100)})` : `red among graded rows: {${res.red.join(", ")}}`}`);
}
writeFileSync(join(S, "sab-pred", only.length ? "predicted-partial.json" : "predicted.json"), JSON.stringify(results, null, 1));
