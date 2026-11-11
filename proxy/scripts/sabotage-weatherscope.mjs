// W5's sabotages (docs/weatherscope-stage4.md S4.11.6, S4.13, S4.14; red sets frozen in S4.15 before this first ran).
// Each plant is an in-place edit, asserted to apply exactly once; then test/weatherscope.test.ts runs, and the set of
// RED rows must equal the registered set exactly. S-WS-PY plants in trim_nws.py and is graded by its --check-golden
// exit instead. Every file is restored from a byte copy and its sha256 checked. A crash, or a run whose summary can't
// be read, is an instrument failure, never a red.
//
// Before every vitest run it waits for TIME_WAIT under 8,000 (memory: pool-workers runs exhaust Windows' ephemeral
// ports), polled every 5 s for at most 10 minutes. This is a single-file driver, not a whole-suite run.
//
//   node scripts/sabotage-weatherscope.mjs     (from proxy/)   exit 0 all as registered, 1 off, 2 rig, 3 restore failed
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const sha = (b) => createHash("sha256").update(b).digest("hex");
const ROWS = ["WS1:", "WS2:", "WS3:", "WS4:", "WS5:", "WS6:", "WS7:", "WS8:", "WS9:", "WS10:", "WS11:", "WS12:", "WS13:", "WS14:",
              "WS15:", "WS16:", "WS17:", "WS18:", "WS18b:", "WS19:", "WS20:", "WS22:", "WS23:"];
const H = "src/weatherscope/handler.ts", F = "src/weatherscope/fetch.ts", T = "src/weatherscope/trim.ts", I = "src/index.ts";
const PY = "../tools/weatherscope/nws/trim_nws.py";
const ALL_ROUTE_ROWS = ROWS.filter((r) => !["WS4:", "WS13:", "WS18b:"].includes(r));

const SABOTAGES = [
  { name: "S-WS-EMPTY", file: H,
    edits: [['  if (g.kind === "none") return unavailable(g.reason);\n  return { ...head(g.kind, g.fetchedUtc), ...data(g.data) };',
             '  if (g.kind === "none") return { ...head("ok", nowS()), items: [] }; /* SABOTAGE S-WS-EMPTY */\n  return { ...head(g.kind, g.fetchedUtc), ...data(g.data) };']],
    red: ["WS6:", "WS7:"] },
  { name: "S-WS-UA", file: F,
    edits: [['      headers: { "User-Agent": UA, Accept: "application/geo+json" },',
             '      headers: kind === "alerts" ? { Accept: "application/geo+json" } : { "User-Agent": UA, Accept: "application/geo+json" }, /* SABOTAGE S-WS-UA */']],
    red: ["WS11:"] },
  { name: "S-WS-KEY", file: H,
    edits: [["`forecast/${p.grid}`", "`forecast/${lat2},${lon2}` /* SABOTAGE S-WS-KEY */"]],
    red: ["WS9:"] },
  { name: "S-WS-ROUND", file: T,
    edits: [["tempF: t !== null ? r(t * 9 / 5 + 32) : 0", "tempF: t !== null ? Math.trunc(t * 9 / 5 + 32) /* SABOTAGE S-WS-ROUND */ : 0"]],
    red: ["WS13:", "WS20:"] },
  { name: "S-WS-PREFIX", file: I,
    edits: [["  if (pathname.startsWith(API_PREFIX)) return { edition: EDITION, suffix: pathname.slice(API_PREFIX.length), legacy: false };",
             "  { const m = /^\\/api\\/v1\\/[a-z]+\\//.exec(pathname); if (m) return { edition: EDITION, suffix: pathname.slice(m[0].length), legacy: false }; } /* SABOTAGE S-WS-PREFIX */"]],
    red: ALL_ROUTE_ROWS },
  { name: "S-WS-404", file: F,
    edits: [["        if (f.countsForBreaker) breakerRecord(id, true);", "        breakerRecord(id, f.countsForBreaker); /* SABOTAGE S-WS-404 */"]],
    red: ["WS8:"] },
  { name: "S-WS-LOG", file: F,
    edits: [["  logCall(kind, res.status, t0);", '  logCall(kind + " " + url, res.status, t0); /* SABOTAGE S-WS-LOG */']],
    red: ["WS19:"] },
  { name: "S-WS-CUT", file: T,
    edits: [["    if (typeof raw === \"string\" && METAR_PRESENT.test(raw) && out.length < MAX_PRESENT) out.push(raw);",
             "    if (typeof raw === \"string\" && out.length < MAX_PRESENT) out.push(METAR_PRESENT.test(raw) ? raw : raw.slice(0, 4)); /* SABOTAGE S-WS-CUT */"]],
    red: ["WS14:"] },
  { name: "S-WS-RING", file: T,
    edits: [["  return ring;\n}", "  return ring.slice(0, 8); /* SABOTAGE S-WS-RING */\n}"]],
    red: ["WS15:", "WS16:", "WS20:"] },
  { name: "S-WS-AGE", file: H,
    edits: [["const head = (status: string, fetchedUtc: number): Body => ({ v: 1, status, fetchedUtc });",
             "const head = (status: string, fetchedUtc: number): Body => ({ v: 1, status, ageS: nowS() - fetchedUtc }); /* SABOTAGE S-WS-AGE */"]],
    // S4.16's set (WS23 added: unavailable() builds its head with fetchedUtc). S4.15's set was left here unchanged once,
    // and the driver read the frozen result as NOT AS REGISTERED (ruled: the transcription went off, not the code).
    red: ["WS5:", "WS6:", "WS7:", "WS8:", "WS15:", "WS17:", "WS20:", "WS23:"] },
  { name: "S-WS-COMMIT", file: H,
    edits: [["  return { breakers: BREAKERS.map((id) => ({ id, state: breakerState(id) })) };",
             '  return { commit: "x", breakers: BREAKERS.map((id) => ({ id, state: breakerState(id) })) }; /* SABOTAGE S-WS-COMMIT */']],
    red: ["WS18b:"] },
  { name: "S-WS-NOSTATION", file: H,
    edits: [['    if (typeof id !== "string" || !/^[A-Z0-9]{3,5}$/.test(id)) throw new NwsRefused("no_station");',
             '    if (typeof id !== "string" || !/^[A-Z0-9]{3,5}$/.test(id)) return { data: "KZZZ", countsForBreaker: true }; /* SABOTAGE S-WS-NOSTATION */']],
    red: ["WS23:"] },
  { name: "S-WS-PY", file: PY, python: true,
    edits: [["    return int(math.floor(x + 0.5))", "    return int(round(x))  # SABOTAGE S-WS-PY"]],
    red: ["golden: 2 cases, 2 differ"] },
];

function timeWait() {
  const r = spawnSync("netstat", ["-an"], { encoding: "utf8", maxBuffer: 256 * 1024 * 1024 });
  if (r.error || r.status !== 0) return null;
  return r.stdout.split("\n").filter((l) => l.includes("TIME_WAIT")).length;
}

function gate() {
  const t0 = Date.now();
  for (;;) {
    const n = timeWait();
    if (n === null) { console.log("RIG: netstat failed; cannot read TIME_WAIT"); process.exit(2); }
    if (n < 8000) return n;
    if (Date.now() - t0 > 600_000) { console.log(`RIG: TIME_WAIT still ${n} after 10 minutes`); process.exit(2); }
    spawnSync(process.platform === "win32" ? "powershell" : "sleep", process.platform === "win32" ? ["-NoProfile", "-Command", "Start-Sleep 5"] : ["5"]);
  }
}

function runTests() {
  const tw = gate();
  const r = spawnSync("npx", ["vitest", "run", "test/weatherscope.test.ts"], { shell: true, encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  const out = ((r.stdout ?? "") + (r.stderr ?? "")).replace(/\x1b\[[0-9;]*m/g, "");
  const total = /Tests\s+(?:(\d+) failed \| )?(\d+) passed \((\d+)\)/.exec(out) ?? /Tests\s+(\d+) failed \((\d+)\)/.exec(out);
  const red = ROWS.filter((t) => out.split("\n").some((l) => l.includes("×") && l.includes(t)));
  return { out, total, red, tw };
}

function runPy() {
  const r = spawnSync("python", ["-I", PY, "--check-golden"], { encoding: "utf8" });
  const out = (r.stdout ?? "") + (r.stderr ?? "");
  return { status: r.status, last: out.trim().split("\n").pop() ?? "" };
}

if (process.argv.includes("--anchors")) {
  // Every plant's anchor, counted, with no test run: each must be found exactly once.
  let bad = 0;
  for (const s of SABOTAGES) {
    const text = readFileSync(s.file, "utf8");
    const counts = s.edits.map(([a]) => text.split(a).length - 1);
    const ok = counts.every((c) => c === 1);
    bad += ok ? 0 : 1;
    console.log(`  ${ok ? "ok  " : "BAD "}  ${s.name}: anchor count ${counts.join(",")} in ${s.file}`);
  }
  console.log(`anchors: ${SABOTAGES.length} plants, ${bad} not found exactly once`);
  process.exit(bad ? 2 : 0);
}

let off = 0;
const base = runTests();
const pyBase = runPy();
if (!base.total || base.red.length || base.total[1] || pyBase.status !== 0) {
  console.log(`RIG: the unsabotaged runs must be all green first (vitest red: ${base.red.join(" ") || "none"}; parsed: ${!!base.total}; golden exit ${pyBase.status})`);
  if (!base.total) console.log(base.out.split("\n").slice(-40).join("\n"));
  process.exit(2);
}
console.log(`CONTROL (unsabotaged): vitest ${base.total[2]} passed of ${base.total[3]}, red none (TIME_WAIT ${base.tw}); golden "${pyBase.last}" exit 0`);
for (const s of SABOTAGES) {
  const orig = readFileSync(s.file), before = sha(orig);
  let text = orig.toString("utf8");
  for (const [a, b] of s.edits) {
    if (text.split(a).length !== 2) { console.log(`RIG: ${s.name}: its anchor is not in ${s.file} exactly once`); process.exit(2); }
    text = text.replace(a, b);
  }
  let r;
  try {
    writeFileSync(s.file, text);
    if (sha(readFileSync(s.file)) === before) { console.log(`RIG: ${s.name} did not change the file`); process.exit(2); }
    r = s.python ? runPy() : runTests();
  } finally {
    writeFileSync(s.file, orig);
    if (sha(readFileSync(s.file)) !== before) { console.log(`${s.name}: RESTORE FAILED`); process.exit(3); }
  }
  if (s.python) {
    const ok = r.status === 1 && r.last === s.red[0];
    off += ok ? 0 : 1;
    console.log(`${s.name}: ${ok ? "as registered" : "NOT AS REGISTERED"}; exit ${r.status}, "${r.last}" (want exit 1, "${s.red[0]}"); restored byte-identical`);
    continue;
  }
  if (!r.total) { console.log(`${s.name}: INSTRUMENT FAILURE -- the run's summary could not be read`); off++; continue; }
  const ok = r.red.length === s.red.length && s.red.every((k) => r.red.includes(k));
  off += ok ? 0 : 1;
  console.log(`${s.name}: ${ok ? "as registered" : "NOT AS REGISTERED"}; red ${JSON.stringify(r.red)} (want ${JSON.stringify(s.red)}); TIME_WAIT ${r.tw}; restored byte-identical`);
}
console.log(off ? `${off} NOT AS REGISTERED` : "ALL AS REGISTERED");
process.exit(off ? 1 : 0);
