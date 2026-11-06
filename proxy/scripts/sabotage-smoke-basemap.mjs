// The sabotages of docs/smoke-basemap-query.md, registered before they ran. Each is an in-place edit, asserted to
// apply exactly once; the WHOLE proxy suite then runs, and the exact set of failed tests (and the failed count) must
// equal the registered set. Files are restored from byte copies, sha256 checked. A crash, a timeout or an unparseable
// summary is an instrument failure, never a red.
//
// THE PORT GATE HOLDS OVER THE WHOLE RUN (A2.12). Each vitest-pool-workers run opens thousands of loopback connections,
// and each lingers in TIME_WAIT: one whole-suite run took the count from 22 to 13,357 (2026-11-06), against Windows'
// 16,384 ephemeral ports, and the runtime fails when they run out. Until A2.12 the gate read the count only at a run's
// start (under 8,000), which let a run start that the ports could not hold. Now:
//   - a run starts only when TIME_WAIT is under TW_START, polled; past TW_CAP_MS it is a RIG, never a reading;
//   - TIME_WAIT is sampled every TW_POLL_MS DURING the run and once at its end, and a run whose peak reaches TW_CEILING
//     is an instrument failure whatever its summary says: a port-starved run can drop a file's cases unmarked;
//   - the reader refuses when netstat fails, overflows its buffer, or prints no connection table, rather than
//     counting 0 (or a truncated count) and opening the gate;
//   - a RIG is returned, never exited on, inside a plant, so the planted file is always restored.
// THE RAW OUTPUT OF A RUN IT DOES NOT GRADE IS KEPT (A2.12): stdout and stderr, whole, in a file under
// os.tmpdir()/sabotage-smoke-basemap/, whose path, size and last lines are printed. A refusal can be diagnosed.
//
//   node scripts/sabotage-smoke-basemap.mjs                   (from proxy/)  exit 0 as registered, 1 off, 2 rig, 3 restore failed
//   node scripts/sabotage-smoke-basemap.mjs --selftest        the gate's and the raw output's checks, on planted runs
//                                                             (no vitest, no repo file touched): exit 0 all as registered, 1 off
//   node scripts/sabotage-smoke-basemap.mjs --selftest-plants each of the driver's own defects, planted in a COPY of
//                                                             this file, must redden exactly its registered checks
import { spawn, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const sha = (b) => createHash("sha256").update(b).digest("hex");
const SABOTAGES = [
  { name: "S-SQ", file: "scripts/smoke-prod.sh", from: "&eps=$BM_EPS", to: "", red: ["SQ1:", "SQ2:"] },
  { name: "S-SQP", file: "src/basemap/query.ts", from: 'const eps = p.get("eps") ?? "";', to: 'const eps = p.get("eps") ?? "100584"; // SABOTAGE S-SQP', red: ["SQ-C:"] },
];

// TW_START and the 20-minute cap are the ruling's (2026-11-05 ~00:15Z and 2026-11-06 ~00:56Z). TW_CEILING sits between
// every count read after a passing whole-suite run (12,768 to about 13,800) and every count read after a failing one
// (15,721 and 16,715): docs/smoke-basemap-query.md, A2.12.
const TW_START = 2000, TW_CEILING = 15000, TW_POLL_MS = 5000, TW_CAP_MS = 1_200_000, RUN_TIMEOUT_MS = 1_500_000;
const RAW_DIR = join(tmpdir(), "sabotage-smoke-basemap");
const SUMMARY = /Tests\s+(?:(\d+) failed \| )?(\d+) passed(?: \| (\d+) skipped)? \((\d+)\)/;
const FAIL_LINE = /^\s*FAIL\s+\S+\.test\.ts\s+>\s+(.*)$/;
const NETSTAT_TABLE = /^\s*Proto\s+Local Address\s+Foreign Address\s+State\s*$/m;
const strip = (s) => s.replace(/\x1b\[[0-9;]*m/g, "");
const sleep = (ms) => new Promise((res) => setTimeout(res, ms));

// The TIME_WAIT count in netstat's table. No table is a refusal, never a 0: a reader that fails open opens the gate.
function parseTimeWait(text) {
  if (!NETSTAT_TABLE.test(text)) throw new Error("netstat printed no connection table");
  return text.split("\n").filter((l) => l.includes("TIME_WAIT")).length;
}
// 16,000 rows of netstat are over a megabyte, spawnSync's default buffer: past it the output is truncated and the count
// silently low, so the buffer is wide and an overflow is a refusal.
function netstatTimeWait(maxBuffer = 64 * 1024 * 1024) {
  const r = spawnSync("netstat", ["-an"], { encoding: "utf8", maxBuffer });
  if (r.error || r.status !== 0) throw new Error(`netstat failed (${r.error?.code ?? `exit ${r.status}`})`);
  return parseTimeWait(r.stdout ?? "");
}

function killTree(child) {
  if (process.platform === "win32") spawnSync("taskkill", ["/pid", String(child.pid), "/T", "/F"]);
  else child.kill("SIGKILL");
}

// One command, gated at its start and sampled for TIME_WAIT until it closes. A RIG comes back as { rig }.
async function run(label, cmd, args, o = {}) {
  const readTw = o.readTw ?? netstatTimeWait, pollMs = o.pollMs ?? TW_POLL_MS, capMs = o.capMs ?? TW_CAP_MS;
  const t0 = Date.now();
  let start;
  try {
    for (;;) {
      start = readTw();
      if (start < TW_START) break;
      if (Date.now() - t0 > capMs) return { label, rig: `TIME_WAIT read ${start} after ${Math.round(capMs / 1000)} s (the gate: under ${TW_START})` };
      await sleep(pollMs);
    }
  } catch (e) {
    return { label, rig: `the TIME_WAIT reader refused: ${e.message}` };
  }
  const held = Math.round((Date.now() - t0) / 1000);
  let peak = start, readerError = null;
  const sample = () => { try { peak = Math.max(peak, readTw()); } catch (e) { readerError ??= e.message; } };
  const timer = setInterval(sample, pollMs);
  const child = spawn(cmd, args, { shell: !!o.shell });
  let out = "", err = "", timedOut = false;
  child.stdout.setEncoding("utf8").on("data", (d) => { out += d; });
  child.stderr.setEncoding("utf8").on("data", (d) => { err += d; });
  const killer = setTimeout(() => { timedOut = true; killTree(child); }, o.timeoutMs ?? RUN_TIMEOUT_MS);
  const code = await new Promise((res) => { child.on("close", (c, sig) => res(c ?? sig)); child.on("error", (e) => res(e.code)); });
  clearTimeout(killer);
  clearInterval(timer);
  sample();
  return { label, cmd: [cmd, ...args].join(" "), code, timedOut, out, err, start, peak, held, readerError };
}

// Writes a run's raw output whole, and prints where it is and how it ends. Returns the file's path.
function keepRaw(r, why, dir) {
  mkdirSync(dir, { recursive: true });
  const file = join(dir, `${r.label}-${new Date().toISOString().replace(/[:.]/g, "")}.log`);
  const body = [`run: ${r.label}`, `command: ${r.cmd}`, `exit: ${r.code}; timed out: ${r.timedOut}`,
    `TIME_WAIT: start ${r.start}, peak ${r.peak}; held ${r.held} s`, `not graded: ${why}`,
    "--- stdout ---", r.out, "--- stderr ---", r.err].join("\n");
  writeFileSync(file, body);
  console.log(`   raw output kept: ${file} (${Buffer.byteLength(r.out)} bytes stdout, ${Buffer.byteLength(r.err)} stderr); its last lines:`);
  for (const l of strip(`${r.out}\n${r.err}`).split("\n").filter((x) => x.trim()).slice(-20)) console.log(`   | ${l.trimEnd().slice(0, 240)}`);
  return file;
}

// A run, graded: its summary and failed names, or why it cannot be graded -- and then its raw output is kept.
async function gradedRun(label, cmd, args, o = {}) {
  const r = await run(label, cmd, args, o);
  if (r.rig) return r;
  const text = strip(`${r.out}\n${r.err}`);
  r.total = SUMMARY.exec(text);
  r.names = [...new Set(text.split("\n").map((l) => FAIL_LINE.exec(l)?.[1]?.trim()).filter(Boolean))].map((f) => f.split(" > ").pop());
  r.why = r.timedOut ? "timed out"
    : r.readerError ? `the TIME_WAIT reader failed during the run (${r.readerError})`
    : r.peak >= TW_CEILING ? `ports: TIME_WAIT peaked at ${r.peak} during the run (the ceiling: ${TW_CEILING})`
    : !r.total ? "summary not parsed" : null;
  if (r.why) r.kept = keepRaw(r, r.why, o.rawDir ?? RAW_DIR);
  return r;
}

// Plants one edit, runs fn, and restores the file from its byte copy whatever fn did. A RIG is RETURNED from inside a
// plant, never exited on: until A2.12 the gate's RIG called process.exit mid-plant, which skips `finally` and leaves the
// sabotage in the tree.
async function withPlant(file, from, to, fn) {
  const orig = readFileSync(file), before = sha(orig);
  const text = orig.toString("utf8");
  if (text.split(from).length !== 2) return { rig: `its anchor is not in ${file} exactly once` };
  try {
    writeFileSync(file, text.replace(from, to));
    if (sha(readFileSync(file)) === before) return { rig: "the plant did not change the file" };
    return await fn();
  } finally {
    writeFileSync(file, orig);
    if (sha(readFileSync(file)) !== before) { console.log(`RESTORE FAILED: ${file}`); process.exit(3); }
  }
}

const ports = (r) => `TIME_WAIT start ${r.start}, peak ${r.peak}; held ${r.held} s`;
const WHOLE_SUITE = ["npx vitest run", [], { shell: true }];

async function driver() {
  const base = await gradedRun("CONTROL", WHOLE_SUITE[0], WHOLE_SUITE[1], WHOLE_SUITE[2]);
  if (base.rig) { console.log(`RIG: CONTROL: ${base.rig}`); return 2; }
  if (base.why || base.total[1]) {
    console.log(`RIG: the unsabotaged suite must pass first (failed ${base.total?.[1] ?? "?"}; not graded: ${base.why ?? "no"}; ${ports(base)})`);
    if (!base.why) keepRaw(base, `${base.total[1]} failed`, RAW_DIR);
    return 2;
  }
  console.log(`CONTROL (unsabotaged): ${base.total[2]} passed, ${base.total[3] ?? 0} skipped, of ${base.total[4]}; 0 failed (${ports(base)})`);

  let off = 0;
  for (const s of SABOTAGES) {
    const r = await withPlant(s.file, s.from, s.to, () => gradedRun(s.name, WHOLE_SUITE[0], WHOLE_SUITE[1], WHOLE_SUITE[2]));
    if (r.rig) { console.log(`RIG: ${s.name}: ${r.rig} (the file is byte-identical to before)`); return 2; }
    if (r.why) { console.log(`${s.name}: INSTRUMENT FAILURE (${r.why}); ${ports(r)}; restored byte-identical`); off++; continue; }
    const failedCount = Number(r.total[1] ?? 0);
    const matched = s.red.every((p) => r.names.filter((n) => n.startsWith(p)).length === 1);
    const extra = r.names.filter((n) => !s.red.some((p) => n.startsWith(p)));
    const ok = matched && extra.length === 0 && failedCount === s.red.length;
    off += !ok;
    console.log(`${s.name}: ${ok ? "as registered" : "NOT AS REGISTERED"}; failed ${failedCount} (want ${s.red.length}): ` +
      `${JSON.stringify(r.names.map((n) => n.split(":")[0]))} (want ${JSON.stringify(s.red.map((p) => p.replace(":", "")))}); ` +
      `${ports(r)}; restored byte-identical`);
  }
  console.log(off ? `${off} NOT AS REGISTERED` : "ALL AS REGISTERED");
  return off ? 1 : 0;
}

// The selftest: every check runs the driver's own gradedRun / withPlant / readers on PLANTED commands and readings.
const SELFTEST_IDS = ["R1", "R1-C", "W1", "W1-C", "W2-R", "W2", "W2-C", "W3", "W4", "W4-C", "W5", "W5-C"];
const TABLE_SAMPLE = "\r\nActive Connections\r\n\r\n  Proto  Local Address          Foreign Address        State\r\n" +
  "  TCP    127.0.0.1:50001        127.0.0.1:8787         TIME_WAIT\r\n  TCP    127.0.0.1:50002        127.0.0.1:8787         TIME_WAIT\r\n" +
  "  TCP    127.0.0.1:50003        127.0.0.1:8787         ESTABLISHED\r\n  TCP    [::1]:50004            [::1]:8787             TIME_WAIT\r\n";

async function selftest() {
  const dir = mkdtempSync(join(tmpdir(), "smoke-selftest-")), rawDir = join(dir, "raw");
  const file = (name, body) => { const f = join(dir, name); writeFileSync(f, body); return f; };
  const MARK_OUT = `RAWMARK-OUT-${process.pid}`, MARK_ERR = `RAWMARK-ERR-${process.pid}`;
  const noSummary = file("no-summary.cjs", `process.stdout.write(${JSON.stringify(`${MARK_OUT}\n`)}); process.stderr.write(${JSON.stringify(`${MARK_ERR}\n`)}); process.exitCode = 1;`);
  const passes = file("passes.cjs", `setTimeout(() => console.log(" Tests  3 passed (3)"), 1500);`);
  const marks = (m) => file(`marks-${m}.cjs`, `require("node:fs").writeFileSync(${JSON.stringify(join(dir, m))}, "ran"); console.log(" Tests  3 passed (3)");`);
  const ran = (m) => existsSync(join(dir, m));
  const fixed = (n) => () => n;
  const planted = (values, then) => { let i = 0; return () => { const v = i < values.length ? values[i++] : then; if (v instanceof Error) throw v; return v; }; };
  const o = (readTw) => ({ readTw, pollMs: 100, capMs: 400, rawDir });
  const node = (label, script, readTw) => gradedRun(label, process.execPath, [script], o(readTw));
  const kept = (r) => !!r.kept && existsSync(r.kept);
  const keptFor = (label) => existsSync(rawDir) && readdirSync(rawDir).some((f) => f.startsWith(`${label}-`));
  const refuses = (f) => { try { f(); return false; } catch { return true; } };
  let off = 0;
  const check = (id, ok, detail) => { off += !ok; console.log(`${id}: ${ok ? "as registered" : "NOT AS REGISTERED"}; ${detail}`); };

  let r = await node("R1", noSummary, fixed(100));
  const body = kept(r) ? readFileSync(r.kept, "utf8") : "";
  check("R1", r.why === "summary not parsed" && body.includes(MARK_OUT) && body.includes(MARK_ERR),
    `a run with no summary: ${r.why ?? "graded"}; kept ${kept(r)}, holding its stdout ${body.includes(MARK_OUT)} and its stderr ${body.includes(MARK_ERR)}`);
  r = await node("R1-C", passes, fixed(100));
  check("R1-C", !r.why && r.total?.[2] === "3" && r.total?.[4] === "3" && !keptFor("R1-C"),
    `CONTROL, a run with a summary: ${r.why ?? `graded, ${r.total?.[2]} passed of ${r.total?.[4]}`}; kept ${keptFor("R1-C")}`);

  r = await node("W1", passes, planted([100, 100, 15000], 100));
  check("W1", !!r.why?.startsWith("ports:") && kept(r), `15,000 read only DURING the run: ${r.why ?? `graded, peak ${r.peak}`}; kept ${kept(r)}`);
  r = await node("W1-C", passes, planted([100, 100, 14999], 100));
  check("W1-C", !r.why && r.peak === 14999 && !keptFor("W1-C"), `CONTROL, 14,999 during the run: ${r.why ?? "graded"}, peak ${r.peak}`);

  const target = file("plant-target.txt", "ORIGINAL\n"), before = sha(readFileSync(target));
  r = await withPlant(target, "ORIGINAL", "PLANTED", () => node("W2-R", passes, fixed(2000)));
  const restored = sha(readFileSync(target)) === before;
  check("W2-R", !!r.rig && restored, `a RIG inside a plant: ${r.rig ? "RIG returned" : "no RIG"}; the file restored byte-identical ${restored}`);
  r = await node("W2", marks("ran-a"), fixed(2000));
  check("W2", !!r.rig && !ran("ran-a"), `2,000 at the start: ${r.rig ? `RIG (${r.rig})` : "started"}; the command ran ${ran("ran-a")}`);
  r = await node("W2-C", marks("ran-b"), fixed(1999));
  check("W2-C", !r.rig && ran("ran-b"), `CONTROL, 1,999 at the start: ${r.rig ? "RIG" : "started"}; the command ran ${ran("ran-b")}`);

  r = await node("W3", passes, planted([100, 100, new Error("planted reader failure")], 100));
  check("W3", !!r.why?.startsWith("the TIME_WAIT reader failed during the run") && kept(r), `the reader fails only DURING the run: ${r.why ?? "graded"}; kept ${kept(r)}`);

  const headless = "netstat: something went wrong\r\n  TCP    127.0.0.1:50001        127.0.0.1:8787         TIME_WAIT\r\n";
  const w4 = refuses(() => parseTimeWait(headless));
  check("W4", w4, `netstat text with no connection table: ${w4 ? "refused" : `counted ${parseTimeWait(headless)}`}`);
  let n = parseTimeWait(TABLE_SAMPLE);
  check("W4-C", n === 3, `CONTROL, a table of 3 TIME_WAIT rows and 1 ESTABLISHED: read ${n}`);
  const w5 = refuses(() => netstatTimeWait(1000));
  check("W5", w5, `netstat's output past a 1,000-byte buffer: ${w5 ? "refused" : `counted ${netstatTimeWait(1000)}`}`);
  try { n = netstatTimeWait(); } catch (e) { n = e.message; }
  check("W5-C", Number.isInteger(n), `CONTROL, the real reader on this machine: read ${n}`);

  rmSync(dir, { recursive: true, force: true });
  console.log(`SELFTEST: ${SELFTEST_IDS.length - off} of ${SELFTEST_IDS.length} as registered; its files removed`);
  return off ? 1 : 0;
}

// Each plant is applied to a COPY of this file's code above the cut line, which then runs --selftest; the checks it
// reddens (NOT AS REGISTERED, or never printed) and its exit status must equal the plant's registered ones exactly.
async function selftestPlants() {
  const src = readFileSync(fileURLToPath(import.meta.url), "utf8");
  const parts = src.split(CUT);
  if (parts.length !== 2) { console.log("RIG: the plants' cut line is not in this file exactly once"); return 2; }
  const [code, rest] = parts;
  const dir = mkdtempSync(join(tmpdir(), "smoke-plants-"));
  let off = 0;
  for (const p of [{ name: "C-PLANTS", red: [], exit: 0 }, ...PLANTS]) {
    let copy = code;
    if (p.from) {
      if (code.split(p.from).length !== 2) { console.log(`RIG: ${p.name}: its anchor is not in the code exactly once`); return 2; }
      copy = code.replace(p.from, () => p.to);
      if (copy === code) { console.log(`RIG: ${p.name} did not change the code`); return 2; }
    } else if (copy + CUT + rest !== src) { console.log("RIG: the unplanted copy is not the source"); return 2; }
    const path = join(dir, `${p.name}.mjs`);
    writeFileSync(path, copy + CUT + rest);
    const r = spawnSync(process.execPath, [path, "--selftest"], { encoding: "utf8", timeout: 180_000 });
    const passed = new Set([...(r.stdout ?? "").matchAll(/^(\S+): as registered;/gm)].map((m) => m[1]));
    const red = SELFTEST_IDS.filter((id) => !passed.has(id));
    const ok = red.join(",") === p.red.join(",") && r.status === p.exit;
    off += !ok;
    console.log(`${p.name}: ${ok ? "AS PREDICTED" : "OFF-PREDICTION"}, red {${red.join(", ")}} (want {${p.red.join(", ")}}); exit ${r.status} (want ${p.exit})`);
    if (!ok) for (const l of `${r.stdout ?? ""}\n${r.stderr ?? ""}`.split("\n").filter((x) => x.trim())) console.log(`   | ${l.trimEnd().slice(0, 240)}`);
  }
  rmSync(dir, { recursive: true, force: true });
  console.log(off ? `${off} OFF-PREDICTION` : "ALL PLANTS AS PREDICTED");
  return off ? 1 : 0;
}

const CUT = ["//", "PLANTS", "(A2.12)"].join(" ");
// PLANTS (A2.12): the driver's own defects, each anchored in the code ABOVE this line (never below it, so an anchor
// is not also found in this table), with the selftest checks it must redden, in SELFTEST_IDS order, and its exit.
const PLANTS = [
  { name: "P-RAW", from: "writeFileSync(file, body);", to: "void body; // PLANT P-RAW", red: ["R1", "W1", "W3"], exit: 1 },
  { name: "P-INRUN", from: "const timer = setInterval(sample, pollMs);", to: "const timer = null; // PLANT P-INRUN", red: ["W1", "W1-C", "W3"], exit: 1 },
  { name: "P-START", from: "if (start < TW_START) break;", to: "if (start <= TW_START) break; // PLANT P-START", red: ["W2-R", "W2"], exit: 1 },
  { name: "P-CEIL", from: "r.peak >= TW_CEILING ?", to: "r.peak > TW_CEILING ? /* PLANT P-CEIL */", red: ["W1"], exit: 1 },
  { name: "P-TABLE", from: "if (!NETSTAT_TABLE.test(text)) throw", to: "if (false) throw", red: ["W4"], exit: 1 },
  { name: "P-BUF", from: "if (r.error || r.status !== 0) throw", to: "if (false) throw", red: ["W5"], exit: 1 },
  { name: "P-RIGEXIT", from: "if (Date.now() - t0 > capMs) return {", to: "if (Date.now() - t0 > capMs) process.exit(2); if (false) return {",
    red: ["W2-R", "W2", "W2-C", "W3", "W4", "W4-C", "W5", "W5-C"], exit: 2 },
];

const mode = process.argv[2];
process.exit(await (mode === "--selftest" ? selftest() : mode === "--selftest-plants" ? selftestPlants() : driver()));
