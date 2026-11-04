// S-TISTARVE (docs/test-identity.md, "CI on b7f9f7a"): the sabotage of the checks' selftest's sampler yield.
// The yield in sampleWhile is removed; on Linux the sampler then only awaits resolved promises, the event loop starves,
// and the selftest must HANG. Graded by a bounded wait: the run is killed at a fixed deadline and that kill is the red.
// Nothing here waits unbounded. Its control: the unsabotaged selftest finishes, exit 0, inside the same deadline.
//
//   node scripts/sabotage-tistarve.mjs        (from proxy/; Linux only)
//
// Exit 0 as registered, 1 off (a sabotaged run that ENDS BY ITSELF is an instrument failure, never a red), 2 BLIND
// (not Linux, the anchor not found once, or the control did not pass), 3 a restore failed.
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const DEADLINE_MS = 300_000;
const SELFTEST = "scripts/test-identity.selftest.mts", TOKENFILE = "scripts/token-file.ts";
const YIELD = "await new Promise((r) => setTimeout(r, 20));"; // the statement only: the line ending is left, so CRLF or LF
const sha = (b) => createHash("sha256").update(b).digest("hex");

if (process.platform !== "linux") {
  console.log(`S-TISTARVE: BLIND -- needs Linux: the starvation is in the /proc sampler, and on ${process.platform} the sampler waits on a process`);
  process.exit(2);
}

/** Run the selftest in its own process group; kill the whole group at the deadline. */
function bounded() {
  return new Promise((done) => {
    const t0 = Date.now();
    const c = spawn("npx", ["tsx", SELFTEST], { detached: true, stdio: ["ignore", "pipe", "pipe"] });
    let out = "", killed = false;
    c.stdout.on("data", (x) => { out += x; }); c.stderr.on("data", (x) => { out += x; });
    const t = setTimeout(() => { killed = true; try { process.kill(-c.pid, "SIGKILL"); } catch { /* already gone */ } }, DEADLINE_MS);
    c.on("close", (code) => { clearTimeout(t); done({ code, killed, out, s: Math.round((Date.now() - t0) / 1000) }); });
  });
}
const scratch = () => readdirSync(tmpdir()).filter((d) => d.startsWith("tid-selftest-"));

const selftest = readFileSync(SELFTEST), tokenfile = readFileSync(TOKENFILE);
const before = { s: sha(selftest), t: sha(tokenfile) };
const src = selftest.toString("utf8");
if (src.split(YIELD).length !== 2) { console.log("S-TISTARVE: BLIND -- the yield's anchor is not in the selftest exactly once"); process.exit(2); }

// the control: unsabotaged, same deadline
const c = await bounded();
const controlOk = !c.killed && c.code === 0 && /SELFTEST PASSED/.test(c.out);
console.log(`control (unsabotaged): ${c.killed ? "KILLED at the deadline" : `exit ${c.code}`} after ${c.s} s; SELFTEST PASSED: ${/SELFTEST PASSED/.test(c.out)}`);
if (!controlOk) { console.log("S-TISTARVE: BLIND -- the control did not pass inside the deadline, so a kill would prove nothing"); process.exit(2); }

let code = 1;
const left = new Set(scratch());
try {
  writeFileSync(SELFTEST, src.replace(YIELD, "")); // SABOTAGE S-TISTARVE: the yield removed
  if (sha(readFileSync(SELFTEST)) === before.s) { console.log("S-TISTARVE: BLIND -- the sabotage did not change the file"); process.exit(2); }
  const r = await bounded();
  const reachedTI2 = /S-TIGUARD:/.test(r.out), printedTI2 = /^TI2:/m.test(r.out);
  console.log(`sabotaged (the yield removed): ${r.killed ? "KILLED at the deadline" : `ENDED BY ITSELF, exit ${r.code}`} after ${r.s} s; printed S-TIGUARD: ${reachedTI2}; printed TI2: ${printedTI2}`);
  if (!r.killed) console.log("S-TISTARVE: INSTRUMENT FAILURE -- a run that ends by itself is not a hang, and is never read as red");
  code = r.killed && reachedTI2 && !printedTI2 ? 0 : 1;
  console.log(code === 0 ? "S-TISTARVE: AS REGISTERED (red: the sampler hangs without its yield)" : "S-TISTARVE: OFF");
} finally {
  writeFileSync(SELFTEST, selftest);
  if (sha(readFileSync(TOKENFILE)) !== before.t) writeFileSync(TOKENFILE, tokenfile);   // a kill inside S-TIGUARD
  const ok = sha(readFileSync(SELFTEST)) === before.s && sha(readFileSync(TOKENFILE)) === before.t;
  for (const d of scratch()) if (!left.has(d)) rmSync(join(tmpdir(), d), { recursive: true, force: true });
  console.log(`restored byte-identical: ${ok}`);
  if (!ok) process.exit(3);
}
process.exit(code);
