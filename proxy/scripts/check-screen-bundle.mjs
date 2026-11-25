#!/usr/bin/env node
// P-B2 (screen-registration-1.md, accepted 2026-11-25): the screen service is a SEPARATE script, and nothing of it
// rides the boards' Worker. Read from what WOULD SHIP -- `wrangler deploy --dry-run` bundles -- never the source.
//
//   B2   the boards' production bundle (wrangler.toml -e production) holds SCREEN_MARKER 0 times.
//        Anchor: it holds "enrich_gap" (metrics.ts), else the read is BLIND.
//        Control: the screen's production bundle (screen.wrangler.toml -e production) holds the marker EXACTLY once,
//        else this check cannot see the screen code at all: BLIND.
//
//   node scripts/check-screen-bundle.mjs              exit 0 PASS, 1 FAIL, 2 BLIND
//   node scripts/check-screen-bundle.mjs --selftest   the real check must PASS; B2s (the boards' entry re-exporting
//        the screen worker, dry-run as the boards' production script) must FAIL with the marker found once. The
//        sabotage entry must be written, and it is deleted afterwards.
// Dry-runs only, through scripts/wrangler-run.mjs (docs/wrangler-run.md); no credentials, nothing deployed.
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const PROXY = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const WRANGLER_RUN = join(PROXY, "scripts", "wrangler-run.mjs");
const BOARDS_TOML = "wrangler.toml";
const SCREEN_TOML = "screen.wrangler.toml";
const SCREEN_ENTRY = "src/screen/worker.ts";
const ANCHOR = "enrich_gap";

// The marker is read from the screen entry itself (the other side of the contract), never retyped here.
function readMarker() {
  const p = join(PROXY, SCREEN_ENTRY);
  if (!existsSync(p)) return null;
  return /export const SCREEN_MARKER = "([^"]+)";/.exec(readFileSync(p, "utf8"))?.[1] ?? null;
}

function dryRun(config, env, script = null) {
  const out = mkdtempSync(join(tmpdir(), "screen-bundle-"));
  const args = [WRANGLER_RUN, "deploy", "--dry-run", "--outdir", out, "-c", config, "-e", env];
  if (script) args.push(script);
  const r = spawnSync(process.execPath, args, { cwd: PROXY, encoding: "utf8" });
  const js = existsSync(out) ? readdirSync(out).filter((f) => f.endsWith(".js")).map((f) => readFileSync(join(out, f), "utf8")).join("\n") : "";
  rmSync(out, { recursive: true, force: true });
  return { code: r.status, stderr: r.stderr ?? "", js };
}

const count = (hay, needle) => (needle ? hay.split(needle).length - 1 : 0);

/** One B2 read. `boardsScript` overrides the boards' entry (the sabotage only). */
function check(boardsScript = null) {
  const marker = readMarker();
  if (!marker) return { verdict: "BLIND", line: `B2 BLIND: SCREEN_MARKER not read from ${SCREEN_ENTRY}` };
  if (!existsSync(join(PROXY, SCREEN_TOML))) return { verdict: "BLIND", line: `B2 BLIND: ${SCREEN_TOML} absent, so the control cannot be read` };
  const screen = dryRun(SCREEN_TOML, "production");
  const inScreen = count(screen.js, marker);
  if (screen.code !== 0 || inScreen !== 1) {
    return { verdict: "BLIND", line: `B2 BLIND: control: the screen bundle holds the marker ${inScreen} time(s) (want exactly 1; dry-run exit ${screen.code})` };
  }
  const boards = dryRun(BOARDS_TOML, "production", boardsScript);
  if (boards.code !== 0 || count(boards.js, ANCHOR) === 0) {
    return { verdict: "BLIND", line: `B2 BLIND: the boards' bundle lacks its anchor "${ANCHOR}" (dry-run exit ${boards.code})` };
  }
  const inBoards = count(boards.js, marker);
  return inBoards === 0
    ? { verdict: "PASS", line: `B2 PASS: the boards' production bundle holds the screen marker 0 times (anchor present; control: screen bundle 1)` }
    : { verdict: "FAIL", line: `B2 FAIL: the boards' production bundle holds the screen marker ${inBoards} time(s)` };
}

const exitFor = (v) => (v === "PASS" ? 0 : v === "FAIL" ? 1 : 2);

if (process.argv.includes("--selftest")) {
  const real = check();
  console.log(`real: ${real.line}`);
  if (real.verdict !== "PASS") process.exit(1);
  // B2s: the boards' entry re-exporting the screen worker, written beside index.ts so its imports resolve.
  const sab = join(PROXY, "src", "__sab_screen_entry.ts");
  writeFileSync(sab, `export * from "./index";\nexport { default } from "./index";\nexport { SCREEN_MARKER, ScreenWebsite, ScreenApp } from "./screen/worker";\n`);
  let r;
  try {
    if (!existsSync(sab)) throw new Error("B2s: the sabotage entry was not written");
    r = check("src/__sab_screen_entry.ts");
  } finally {
    rmSync(sab, { force: true });
  }
  const ok = r.verdict === "FAIL" && /holds the screen marker 1 time/.test(r.line);
  console.log(`B2s: ${r.line} -> ${ok ? "AS PREDICTED" : "NOT AS PREDICTED"}`);
  console.log(ok ? "SELFTEST PASSED" : "SELFTEST FAILED");
  process.exit(ok ? 0 : 1);
}

const r = check();
console.log(r.line);
process.exit(exitFor(r.verdict));
