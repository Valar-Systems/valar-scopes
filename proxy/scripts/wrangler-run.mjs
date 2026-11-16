#!/usr/bin/env node
// wrangler-run.mjs -- every in-tree wrangler call goes through here (the owner, 2026-11-13 06:01Z; docs/wrangler-run.md).
//
// WHY. wrangler 4.x appends every message it logs to a debug log -- a KV key list, a secret's name, a device id inside
// a key -- under %USERPROFILE%\.wrangler\logs (~/.wrangler/logs), and has no switch to turn that off.
// WRANGLER_LOG_PATH only moves it. So this moves it into a temp dir made for THIS call and removes the dir after,
// whatever wrangler's exit. scripts/check_wrangler_calls.py refuses any call in scripts/, tools/, proxy/scripts/ or
// .github/ that does not come through here.
//
//   node proxy/scripts/wrangler-run.mjs <wrangler args...>      exactly like `npx wrangler <args...>`, from any cwd
//   WRANGLER_RUN_SHOW=1 node proxy/scripts/wrangler-run.mjs ...  also print the removal line on success (stderr)
//   node proxy/scripts/wrangler-run.mjs --selftest              against a stand-in wrangler; no network, no account
//
// It changes nothing else: the caller's cwd, environment, stdin, stdout and stderr pass straight through, and the
// exit status is wrangler's. It prints NOTHING of its own on success (callers parse wrangler's stdout and some filter
// its stderr), unless WRANGLER_RUN_SHOW=1. If the temp dir cannot be removed it says so on stderr, with the path,
// and still exits with wrangler's status: a deploy that uploaded must not read as failed because of a log file.
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync, writeFileSync, readFileSync, readdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const PROXY = join(dirname(fileURLToPath(import.meta.url)), "..");
const BIN = process.env.WRANGLER_RUN_BIN || join(PROXY, "node_modules", "wrangler", "bin", "wrangler.js");

// run(args, opts) -> { status, dir, removed }. opts.stdio defaults to "inherit".
export function run(args, opts = {}) {
  const dir = mkdtempSync(join(tmpdir(), "wrangler-log-"));
  let status = 1;
  try {
    const env = { ...process.env, ...(opts.env || {}), WRANGLER_LOG_PATH: join(dir, "wrangler.log") };
    delete env.WRANGLER_RUN_BIN;
    const r = spawnSync(process.execPath, [opts.bin || BIN, ...args], { stdio: opts.stdio || "inherit", env, cwd: opts.cwd });
    status = r.status ?? (r.signal ? 128 : 1);
  } finally {
    rmSync(dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
  }
  return { status, dir, removed: !existsSync(dir) };
}

function selftest() {
  let bad = 0;
  const ok = (name, cond) => { console.log(`${cond ? "PASS" : "FAIL"} ${name}`); if (!cond) bad++; };
  const t = mkdtempSync(join(tmpdir(), "wrangler-run-selftest-"));
  try {
    // A stand-in wrangler: writes a key-name-shaped line to WRANGLER_LOG_PATH (as the real one does), records where
    // it wrote, echoes its args and stdin, and exits with the code it was asked for.
    const fake = join(t, "fake-wrangler.mjs");
    writeFileSync(fake, [
      'import { appendFileSync, mkdirSync, writeFileSync, readFileSync } from "node:fs";',
      'import { dirname } from "node:path";',
      'const p = process.env.WRANGLER_LOG_PATH; mkdirSync(dirname(p), { recursive: true });',
      'appendFileSync(p, "--- debug\\nkey:SENTINEL-KEY-NAME\\n");',
      'writeFileSync(process.argv[2], p);',
      'let input = ""; try { input = readFileSync(0, "utf8"); } catch {}',
      'process.stdout.write("args=" + process.argv.slice(3).join(",") + " stdin=" + input.trim() + "\\n");',
      'process.stderr.write("a wrangler warning\\n");',
      'process.exit(Number(process.argv[3] === "exit" ? process.argv[4] : 0));',
    ].join("\n"));
    const where = join(t, "where.txt");
    const out = spawnSync(process.execPath, [fileURLToPath(import.meta.url), where, "kv", "key", "list"],
      { env: { ...process.env, WRANGLER_RUN_BIN: fake, WRANGLER_RUN_SHOW: "" }, input: "from-stdin", encoding: "utf8" });
    const logPath = readFileSync(where, "utf8");
    ok("R1 wrangler's log path is inside a fresh temp dir named wrangler-log-*", /wrangler-log-[^\\/]+[\\/]wrangler\.log$/.test(logPath));
    ok("R2 that temp dir is GONE after the call (the removal)", !existsSync(dirname(logPath)));
    ok("R2-control: the stand-in did write there (else 'gone' proves nothing)", out.stdout.includes("args=kv,key,list"));
    ok("R3 stdout and stdin pass straight through", out.stdout.trim() === "args=kv,key,list stdin=from-stdin");
    ok("R4 stderr passes through, with nothing of the helper's own on success", out.stderr === "a wrangler warning\n");
    ok("R5 the exit status is wrangler's (0)", out.status === 0);
    const bad7 = spawnSync(process.execPath, [fileURLToPath(import.meta.url), where, "exit", "7"],
      { env: { ...process.env, WRANGLER_RUN_BIN: fake }, encoding: "utf8" });
    ok("R5 the exit status is wrangler's (7), and the dir is still removed", bad7.status === 7 && !existsSync(dirname(readFileSync(where, "utf8"))));
    const shown = spawnSync(process.execPath, [fileURLToPath(import.meta.url), where],
      { env: { ...process.env, WRANGLER_RUN_BIN: fake, WRANGLER_RUN_SHOW: "1" }, encoding: "utf8" });
    ok("R6 WRANGLER_RUN_SHOW=1 prints the removal line, and no log content", /wrangler-run: temp log removed/.test(shown.stderr) && !/SENTINEL/.test(shown.stdout + shown.stderr));
    const left = readdirSync(tmpdir()).filter((n) => n.startsWith("wrangler-log-") && existsSync(join(tmpdir(), n, "wrangler.log")) &&
      readFileSync(join(tmpdir(), n, "wrangler.log"), "utf8").includes("SENTINEL-KEY-NAME"));
    ok("R7 no temp dir holding the planted key name is left behind", left.length === 0);
  } finally {
    rmSync(t, { recursive: true, force: true });
  }
  console.log(bad ? `SELFTEST FAILED: ${bad}` : "SELFTEST PASSED");
  return bad ? 1 : 0;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args[0] === "--selftest") process.exit(selftest());
  const r = run(args);
  if (!r.removed) process.stderr.write(`wrangler-run: temp log NOT removed: ${r.dir} -- delete it by hand\n`);
  else if (process.env.WRANGLER_RUN_SHOW === "1") process.stderr.write("wrangler-run: temp log removed\n");
  process.exit(r.status);
}
