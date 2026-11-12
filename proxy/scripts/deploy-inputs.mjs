#!/usr/bin/env node
// deploy.sh's behind-check, PER TARGET (docs/deploy-behind-per-target.md: Q-DB1..Q-DB3 ruled 2026-11-06; the code's registration and
// the dry-run's guard, 2026-11-12). deploy.sh used to refuse on ANY commit behind origin/main under proxy/, for both Workers, whether or
// not the target bundles it -- and a guard that fires on unrelated work teaches people to bypass it. Here the bundler says what each
// Worker ships: `wrangler deploy --dry-run --outdir <tmp> --metafile <tmp>/meta.json` builds exactly what a deploy would, uploads
// nothing, and esbuild's metafile lists every input file. A target's SET is:
//   - the metafile's inputs (repo files), ANCHORED: the config's `main` must be among them, or the answer is refused;
//   - its wrangler config, proxy/tsconfig.json and the package files (the lockfile pins wrangler, and with it the bundler);
//   - its DEPLOY MACHINERY, kept by hand and checked by DB17: the scripts deploy.sh runs for that target.
// A commit behind that touches the set BLOCKS (ALLOW_BEHIND=1 unchanged); one touching only other files under proxy/ is NOTED.
// If the bundler cannot answer, the set falls back to today's rule plus the machinery (all of proxy/, scripts/session-claim.py): it
// never blocks less than today (Q-DB2).
//
// THE DRY-RUN'S GUARD (the orchestrator, 18:01:52Z): its argv is built in ONE place (dryRunArgv) and guardedSpawn ASSERTS, immediately
// before spawning, that it holds --dry-run and --outdir (and no --dry-run=<value>). If not, it refuses and spawns nothing. --outdir is
// a fresh dir under the OS temp dir, checked to be outside every git tree, and deleted afterwards. The spawned command line is printed
// on every run. wrangler runs as `node proxy/node_modules/wrangler/bin/wrangler.js` (no shell), in proxy/, with any Cloudflare token
// removed from its environment (a dry-run needs none).
//
//   node proxy/scripts/deploy-inputs.mjs set   <proxy|builder> <env>   the set, one repo-relative path per line; exit 0, or 2 refused
//   node proxy/scripts/deploy-inputs.mjs check <proxy|builder> <env>   deploy.sh's decision over HEAD..origin/main: exit 0 nothing
//                                                                      blocks, 1 blocking commits (printed), 2 usage or internal
//   node proxy/scripts/deploy-inputs.mjs --selftest [--inputs <json>] [--json]   DB1..DB18 (registered); the two real sets are computed
//                                                                      by the guarded dry-run unless --inputs gives their raw inputs
//   node proxy/scripts/deploy-inputs.mjs --selftest-plants             each plant, in a COPY of this file, must redden exactly its set
//   node proxy/scripts/deploy-inputs.mjs --selftest-guard              DB18 alone, spawning nothing: run before any real dry-run
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(process.env.DEPLOY_INPUTS_REPO ?? join(HERE, "..", ".."));
const PROXY = join(REPO, "proxy");
const CONFIG = { proxy: "wrangler.toml", builder: "basemap.wrangler.toml" };
const PACKAGE_FILES = ["proxy/tsconfig.json", "proxy/package.json", "proxy/package-lock.json"];
const MACHINERY_BUILDER = ["proxy/scripts/deploy.sh", "proxy/scripts/deploy-inputs.mjs", "scripts/session-claim.py"];
const MACHINERY = { builder: MACHINERY_BUILDER, proxy: [...MACHINERY_BUILDER, "proxy/scripts/confirm-deploy.sh", "proxy/scripts/healthz-commit.sh"] };
const TARGETS = ["proxy", "builder"];
const slash = (p) => p.replace(/\\/g, "/");
const q = (s) => (/[\s"'$]/.test(s) ? JSON.stringify(s) : s);

// ---- the dry-run: its argv in ONE place, and the guard immediately before the spawn ---------------------------------------------
function dryRunArgv(target, env, outdir) {
  const wrangler = join(PROXY, "node_modules", "wrangler", "bin", "wrangler.js");
  return [process.execPath, wrangler, "deploy", ...(target === "builder" ? ["--config", CONFIG.builder] : []), "--env", env,
    "--dry-run", "--outdir", outdir, "--metafile", join(outdir, "meta.json")];
}
/** Spawns argv only if it holds --dry-run and --outdir; otherwise refuses and spawns NOTHING. spawnFn is spawnSync except in DB18. */
function guardedSpawn(argv, opts, spawnFn = spawnSync) {
  if (!argv.includes("--dry-run") || !argv.includes("--outdir") || argv.some((a) => a.startsWith("--dry-run="))) {
    return { refused: "the dry-run's argv lacks --dry-run or --outdir: refused, nothing spawned" };
  }
  console.error(`deploy-inputs: spawning: ${argv.map(q).join(" ")}`);
  return { r: spawnFn(argv[0], argv.slice(1), opts) };
}
const childEnv = () => {
  const e = { ...process.env, WRANGLER_SEND_METRICS: "false" };
  for (const k of ["CLOUDFLARE_API_TOKEN", "CLOUDFLARE_API_KEY", "CLOUDFLARE_EMAIL"]) delete e[k];
  return e;
};
/** The real dry-run, through the guard, into a fresh OS temp dir outside every git tree, deleted afterwards. */
function realDryRun(argv, outdir) {
  const g = guardedSpawn(argv, { cwd: PROXY, env: childEnv(), encoding: "utf8", maxBuffer: 1 << 26 });
  if (g.refused) return { refused: g.refused };
  if (g.r.error) return { refused: `wrangler could not be spawned: ${g.r.error.message}` };
  if (g.r.status !== 0) return { refused: `the dry-run exited ${g.r.status}${(g.r.stderr || "").trim() ? `: ${(g.r.stderr || "").trim().split("\n").slice(-3).join(" | ")}` : ""}` };
  return { ok: true };
}
const mainOf = (configText) => (configText.match(/^\s*main\s*=\s*"([^"]+)"/m) ?? [])[1] ?? null;

/** The target's set, or { refused } when the bundler cannot answer. runDryRun(argv, outdir) is realDryRun except in the selftest. */
function computeSet(target, env, runDryRun = realDryRun) {
  const t0 = Date.now();
  const outdir = mkdtempSync(join(tmpdir(), "deploy-inputs-"));
  try {
    const inTree = spawnSync("git", ["-C", outdir, "rev-parse", "--show-toplevel"], { encoding: "utf8" });
    if (inTree.status === 0 || slash(outdir).startsWith(slash(REPO) + "/")) return { refused: `the outdir ${outdir} is inside a git tree`, ms: Date.now() - t0 };
    const argv = dryRunArgv(target, env, outdir);
    const d = runDryRun(argv, outdir);
    const ms = Date.now() - t0;
    if (d.refused) return { refused: d.refused, ms, argv };
    const metaPath = join(outdir, "meta.json");
    if (!existsSync(metaPath)) return { refused: "the dry-run wrote no metafile", ms, argv };
    let meta;
    try { meta = JSON.parse(readFileSync(metaPath, "utf8")); } catch { return { refused: "the metafile does not parse", ms, argv }; }
    if (!meta || typeof meta.inputs !== "object" || meta.inputs === null) return { refused: "the metafile has no inputs", ms, argv };
    const raw = Object.keys(meta.inputs).map(slash);
    const bundle = [...new Set(raw.filter((k) => !k.includes("node_modules/")).map((k) => slash(join("proxy", k))))].sort();
    const main = mainOf(readFileSync(join(PROXY, CONFIG[target]), "utf8"));
    if (!main || !bundle.includes(slash(join("proxy", main)))) return { refused: `the config's main (${main}) is not among the dry-run's inputs (the anchor)`, ms, argv };
    const set = [...bundle, `proxy/${CONFIG[target]}`, ...PACKAGE_FILES];
    return { set: [...new Set([...set, ...MACHINERY[target]])].sort(), bundle, raw, ms, argv };
  } finally {
    rmSync(outdir, { recursive: true, force: true });
  }
}

// ---- the decision ---------------------------------------------------------------------------------------------------------------
/** Is file f in what this target ships? (fallback: today's rule, all of proxy/, plus the machinery) */
function inSetFn(target, set, fallback) {
  return fallback ? (f) => f.startsWith("proxy/") || MACHINERY[target].includes(f) : (f) => set.includes(f);
}
/** commits: [{ sha, subject, files }]. Blocking: any file in the set. Noted: files under proxy/ but none in the set. */
function decide(target, s, commits) {
  const fallback = Boolean(s.refused);
  const inSet = inSetFn(target, s.set ?? [], fallback);
  const blocking = [], notes = [], others = [];
  for (const c of commits) {
    if (c.files.some(inSet)) blocking.push(c);
    else if (c.files.some((f) => f.startsWith("proxy/"))) notes.push(c);
    else others.push(c);
  }
  return { fallback, blocking, notes, others };
}
function commitsBehind(base, upstream, paths = []) {
  const r = spawnSync("git", ["-C", REPO, "log", "--format=%x00%H%x09%s", "--name-only", `${base}..${upstream}`, ...(paths.length ? ["--", ...paths] : [])], { encoding: "utf8", maxBuffer: 1 << 26 });
  if (r.status !== 0) throw new Error(`git log ${base}..${upstream} exited ${r.status}`);
  return r.stdout.split("\0").filter((b) => b.trim()).map((b) => {
    const [head, ...rest] = b.split("\n"); const [sha, subject] = head.split("\t");
    return { sha, subject: subject ?? "", files: rest.map((l) => l.trim()).filter(Boolean) };
  });
}

// ---- DB17: every script deploy.sh calls or sources is in its target's machinery list ---------------------------------------------
function stripComment(line) {
  let sq = false, dq = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (c === "'" && !dq) sq = !sq; else if (c === '"' && !sq) dq = !dq;
    else if (c === "#" && !sq && !dq && (i === 0 || /\s/.test(line[i - 1]))) return line.slice(0, i);
  }
  return line;
}
function scriptCalls(deployShText) {
  const lines = deployShText.replace(/\r/g, "").split("\n");
  const bIf = lines.findIndex((l) => /^\s*if \[ -n "\$BUILDER" \]; then\s*$/.test(l));
  const bExit = bIf < 0 ? -1 : lines.findIndex((l, i) => i > bIf && /^\s*exit 0\s*$/.test(l));
  const found = (from, to) => {
    const out = new Set();
    for (const l of lines.slice(from, to)) {
      const code = stripComment(l);
      for (const m of code.matchAll(/([A-Za-z0-9_./-]*\/)?([A-Za-z0-9_-]+\.(?:sh|py|mjs|cjs|js|ts))(?![A-Za-z0-9_])/g)) {
        const prefix = m[1] ?? "", base = m[2];
        if (prefix.includes("proxy/scripts/")) { out.add(`proxy/scripts/${base}`); continue; }
        const inProxy = existsSync(join(PROXY, "scripts", base)), inRoot = existsSync(join(REPO, "scripts", base));
        out.add(inRoot && !inProxy ? `scripts/${base}` : inProxy ? `proxy/scripts/${base}` : `${prefix}${base}`);
      }
    }
    return [...out].sort();
  };
  return { builderExitLine: bExit < 0 ? null : bExit + 1, builder: bExit < 0 ? null : found(0, bExit + 1), proxy: found(0, lines.length) };
}

// ---- the selftest: DB1..DB18 ----------------------------------------------------------------------------------------------------
const DB1_HEAD = "048ed7b", DB1_BASE = "788dab3c";
const PLANT = (files) => [{ sha: "0".repeat(40), subject: "planted", files }];
function selftest(rawInputs) {
  const rows = {}, detail = {};
  const ok = (row, good, why) => { rows[row] = Boolean(good); detail[row] = why; };
  const fromRaw = (target) => (argv, outdir) => { writeFileSync(join(outdir, "meta.json"), JSON.stringify({ inputs: Object.fromEntries(rawInputs[target].map((k) => [k, {}])) })); return { ok: true }; };
  const sets = {}, raw = {};
  for (const t of TARGETS) {
    const s = computeSet(t, "staging", rawInputs ? fromRaw(t) : realDryRun);
    if (s.refused) return { fatal: `the real ${t} set was refused: ${s.refused}` };
    sets[t] = s; raw[t] = s.raw;
    if (!rawInputs) console.error(`deploy-inputs: ${t} dry-run ${s.ms} ms; ${s.bundle.length} bundled files; set ${s.set.length}`);
  }
  const blocks = (t, files) => decide(t, sets[t], PLANT(files)).blocking.length > 0;
  const both = (files, wantP, wantB) => { const p = blocks("proxy", files), b = blocks("builder", files); return { good: p === wantP && b === wantB, why: `proxy ${p ? "blocks" : "does not"}, builder ${b ? "blocks" : "does not"}` }; };
  // DB1, DB1-C, DB2: the 2026-11-05 17:12Z refusal, replayed from history (HEAD 048ed7b, base 788dab3c, #543)
  const replay = commitsBehind(DB1_HEAD, DB1_BASE);
  const dB = decide("builder", sets.builder, replay), dP = decide("proxy", sets.proxy, replay);
  const n1 = dB.notes.find((c) => c.sha.startsWith(DB1_BASE));
  ok("DB1", dB.blocking.length === 0 && dB.notes.length === 1 && n1 && ["proxy/scripts/check-wx-plant.mjs", "proxy/wx-ingest-dev.wrangler.toml"].every((f) => n1.files.includes(f)),
    `builder: ${dB.blocking.length} blocking, ${dB.notes.length} noted (${dB.notes.map((c) => `${c.sha.slice(0, 8)} ${c.files.join(", ")}`).join("; ")})`);
  const today = commitsBehind(DB1_HEAD, DB1_BASE, ["proxy/"]).length;
  ok("DB1-C", today === 1, `today's rule: ${today} commit(s) behind in proxy/`);
  ok("DB2", dP.blocking.length === 0, `proxy: ${dP.blocking.length} blocking, ${dP.notes.length} noted`);
  const rowsBoth = [["DB3", ["proxy/src/basemap/worker.ts"], false, true], ["DB4", ["proxy/src/basemap/handler.ts"], true, false], ["DB5", ["proxy/src/basemap/build.ts"], true, true],
    ["DB6", ["proxy/package-lock.json"], true, true], ["DB7", ["proxy/wrangler.toml"], true, false], ["DB8", ["proxy/basemap.wrangler.toml"], false, true],
    ["DB9", ["proxy/scripts/deploy.sh"], true, true], ["DB10", ["proxy/src/wx/ingest-worker.ts"], false, false], ["DB13", ["proxy/scripts/deploy-inputs.mjs"], true, true],
    ["DB14", ["scripts/session-claim.py"], true, true], ["DB15", ["proxy/scripts/confirm-deploy.sh"], true, false], ["DB16", ["proxy/scripts/healthz-commit.sh"], true, false]];
  for (const [row, files, wp, wb] of rowsBoth) { const r = both(files, wp, wb); ok(row, r.good, r.why); }
  // DB11, DB12: the bundler cannot answer (planted by injecting the dry-run's result); one planted commit touching src/index.ts
  const idx = PLANT(["proxy/src/index.ts"]);
  const s11 = computeSet("proxy", "staging", () => ({ refused: "planted: the dry-run exited 1" }));
  const d11 = decide("proxy", s11, idx);
  ok("DB11", Boolean(s11.refused) && d11.fallback && d11.blocking.length === 1, `refused: ${s11.refused ?? "no"}; fallback ${d11.fallback}; blocking ${d11.blocking.length}`);
  const s12 = computeSet("proxy", "staging", (argv, outdir) => { writeFileSync(join(outdir, "meta.json"), JSON.stringify({ inputs: { "src/basemap/build.ts": {} } })); return { ok: true }; });
  const d12 = decide("proxy", s12, idx);
  ok("DB12", Boolean(s12.refused) && d12.fallback && d12.blocking.length === 1, `refused: ${s12.refused ?? "no"}; fallback ${d12.fallback}; blocking ${d12.blocking.length}`);
  // DB17: the machinery list is complete for each target's path through deploy.sh
  const dsh = readFileSync(process.env.DEPLOY_INPUTS_DEPLOYSH ?? join(PROXY, "scripts", "deploy.sh"), "utf8");
  const calls = scriptCalls(dsh);
  const missB = calls.builder === null ? ["(the builder's exit 0 was not found)"] : calls.builder.filter((p) => !MACHINERY.builder.includes(p));
  const missP = calls.proxy.filter((p) => !MACHINERY.proxy.includes(p));
  ok("DB17", missB.length === 0 && missP.length === 0, `builder path (to l.${calls.builderExitLine}): ${JSON.stringify(calls.builder)}; proxy: ${JSON.stringify(calls.proxy)}; missing ${JSON.stringify([...missB, ...missP])}`);
  const g = db18(); ok("DB18", g.good, g.why);
  return { rows, detail, raw, sets: Object.fromEntries(TARGETS.map((t) => [t, { set: sets[t].set.length, bundle: sets[t].bundle.length, ms: sets[t].ms }])) };
}
/** DB18, the guard. A counting fake spawner: an argv without --dry-run, and one without --outdir, must be refused with 0 spawns; the
 *  real argv must pass the guard (the fake records the call). Nothing real is spawned; --selftest-guard runs this alone, first. */
function db18() {
  let n = 0; const fake = () => { n++; return { status: 0 }; };
  const real = dryRunArgv("proxy", "staging", join(tmpdir(), "deploy-inputs-db18"));
  const a = guardedSpawn(real.filter((x) => x !== "--dry-run"), {}, fake), c0 = n;
  const oi = real.indexOf("--outdir"), b = guardedSpawn([...real.slice(0, oi), ...real.slice(oi + 2)], {}, fake), c1 = n;
  const c = guardedSpawn(real, {}, fake), c2 = n;
  return { good: Boolean(a.refused) && c0 === 0 && Boolean(b.refused) && c1 === 0 && !c.refused && c2 === 1,
    why: `no --dry-run: ${a.refused ? `REFUSED (${a.refused})` : "SPAWNED"}, spawns ${c0}; no --outdir: ${b.refused ? "REFUSED" : "SPAWNED"}, spawns ${c1}; the real argv: ${c.refused ? "refused" : "passes the guard"}, spawns ${c2 - c1}` };
}

// ---- the plants: each in a COPY of this file; an edit that does not apply exactly once refuses ---------------------------------
const PLANTS = [
  ["P-WIDE", "  return fallback ? (f) => f.startsWith(\"proxy/\") || MACHINERY[target].includes(f) : (f) => set.includes(f);\n",
    "  return (f) => f.startsWith(\"proxy/\"); // PLANT P-WIDE: today's rule for every set\n", ["DB1", "DB2", "DB3", "DB4", "DB7", "DB8", "DB10", "DB14", "DB15", "DB16"]],
  ["P-EMPTY", "    const set = [...bundle, `proxy/${CONFIG[target]}`, ...PACKAGE_FILES];\n", "    const set = []; // PLANT P-EMPTY\n", ["DB3", "DB4", "DB5", "DB6", "DB7", "DB8"]],
  ["P-NOMACH", "    return { set: [...new Set([...set, ...MACHINERY[target]])].sort(), bundle, raw, ms, argv };\n",
    "    return { set: [...new Set([...set])].sort(), bundle, raw, ms, argv }; // PLANT P-NOMACH\n", ["DB9", "DB13", "DB14", "DB15", "DB16"]],
  ["P-NOANCHOR", "    if (!main || !bundle.includes(slash(join(\"proxy\", main)))) return", "    if (false) return", ["DB12"]],
  ["P-NOFALLBACK", "  const fallback = Boolean(s.refused);\n", "  const fallback = Boolean(s.refused); if (fallback) return { fallback, blocking: [], notes: [], others: [] }; // PLANT P-NOFALLBACK\n", ["DB11", "DB12"]],
  ["P-NOPKG", "    const set = [...bundle, `proxy/${CONFIG[target]}`, ...PACKAGE_FILES];\n", "    const set = [...bundle, `proxy/${CONFIG[target]}`]; // PLANT P-NOPKG\n", ["DB6"]],
  ["P-NOGUARD", "  if (!argv.includes(\"--dry-run\") || !argv.includes(\"--outdir\") || argv.some((a) => a.startsWith(\"--dry-run=\"))) {\n",
    "  if (false) { // PLANT P-NOGUARD\n", ["DB18"]],
  ["P-CALLEE", null, null, ["DB17"]], // a copy of deploy.sh calling one more script, not in any list
];
function selftestPlants() {
  const self = readFileSync(fileURLToPath(import.meta.url), "utf8");
  const work = mkdtempSync(join(tmpdir(), "deploy-inputs-plants-"));
  let off = 0;
  try {
    const base = selftest(null);
    if (base.fatal) { console.log(`REFUSED: ${base.fatal}`); return 2; }
    const rawPath = join(work, "raw-inputs.json"); writeFileSync(rawPath, JSON.stringify(base.raw));
    const runCopy = (name, text, extraEnv = {}) => {
      const copy = join(work, `deploy-inputs.${name}.mjs`); writeFileSync(copy, text);
      const r = spawnSync(process.execPath, [copy, "--selftest", "--json", "--inputs", rawPath], { encoding: "utf8", env: { ...process.env, DEPLOY_INPUTS_REPO: REPO, ...extraEnv } });
      const line = (r.stdout || "").split("\n").find((l) => l.startsWith("{"));
      if (!line) return { error: `exit ${r.status}; ${(r.stderr || "").trim().split("\n").slice(-2).join(" | ")}` };
      return JSON.parse(line);
    };
    const red = (res) => Object.entries(res.rows).filter(([, v]) => !v).map(([k]) => k).sort();
    const ctl = runCopy("CONTROL", self);
    const ctlRed = ctl.error ? null : red(ctl);
    console.log(`  CONTROL (an unmodified copy): ${ctl.error ? `ERROR ${ctl.error}` : `red ${JSON.stringify(ctlRed)}`}`);
    if (ctl.error || ctlRed.length) { console.log("REFUSED: the unmodified copy is not all green, so no plant can be read"); return 2; }
    for (const [name, from, to, want] of PLANTS) {
      let res;
      if (name === "P-CALLEE") {
        const dsh = readFileSync(join(PROXY, "scripts", "deploy.sh"), "utf8").replace(/\r/g, "");
        const anchor = 'node "$ROOT/proxy/scripts/deploy-inputs.mjs" check "$DI_TARGET" "$ENVIRONMENT"; DI_RC=$?\n';
        if (dsh.split(anchor).length !== 2) { console.log(`  ${name}: REFUSED (deploy.sh's anchor found ${dsh.split(anchor).length - 1} times)`); off++; continue; }
        const planted = join(work, "deploy.P-CALLEE.sh"); writeFileSync(planted, dsh.replace(anchor, anchor + 'bash "$ROOT/proxy/scripts/planted-extra.sh"\n'));
        res = runCopy(name, self, { DEPLOY_INPUTS_DEPLOYSH: planted });
      } else {
        const n = self.split(from).length - 1;
        if (n !== 1) { console.log(`  ${name}: REFUSED (its anchor found ${n} times)`); off++; continue; }
        const text = self.replace(from, to);
        if (text === self) { console.log(`  ${name}: REFUSED (the copy is unchanged)`); off++; continue; }
        res = runCopy(name, text);
      }
      if (res.error) { console.log(`  ${name}: ERROR ${res.error}`); off++; continue; }
      const got = red(res), exact = JSON.stringify(got) === JSON.stringify([...want].sort());
      if (!exact) off++;
      console.log(`  ${name}: red ${JSON.stringify(got)} vs registered ${JSON.stringify([...want].sort())}: ${exact ? "AS REGISTERED" : "OFF"}`);
    }
  } finally { rmSync(work, { recursive: true, force: true }); }
  console.log(off ? `PLANTS: ${off} off` : "PLANTS: every plant reddens exactly its registered set");
  return off ? 1 : 0;
}

// ---- the commands ---------------------------------------------------------------------------------------------------------------
const [cmd, ...rest] = process.argv.slice(2);
if (cmd === "--selftest") {
  const ii = rest.indexOf("--inputs"), json = rest.includes("--json");
  const res = selftest(ii >= 0 ? JSON.parse(readFileSync(rest[ii + 1], "utf8")) : null);
  if (res.fatal) { console.log(json ? JSON.stringify({ fatal: res.fatal }) : `REFUSED: ${res.fatal}`); process.exit(2); }
  if (json) console.log(JSON.stringify({ rows: res.rows }));
  else {
    for (const [row, good] of Object.entries(res.rows)) console.log(`  ${row}: ${good ? "AS PREDICTED" : "OFF"} (${res.detail[row]})`);
    console.log(`sets: ${JSON.stringify(res.sets)}`);
  }
  process.exit(Object.values(res.rows).every(Boolean) ? 0 : 1);
}
if (cmd === "--selftest-plants") process.exit(selftestPlants());
if (cmd === "--selftest-guard") { const g = db18(); console.log(`  DB18: ${g.good ? "AS PREDICTED" : "OFF"} (${g.why})`); process.exit(g.good ? 0 : 1); }
if ((cmd === "set" || cmd === "check") && TARGETS.includes(rest[0]) && /^(staging|production)$/.test(rest[1] ?? "")) {
  const [target, env] = rest;
  const s = computeSet(target, env);
  console.log(`deploy-inputs: ${target} dry-run ${s.ms ?? "?"} ms`);
  if (cmd === "set") {
    if (s.refused) { console.log(`deploy-inputs: ${target}: REFUSED: ${s.refused}`); process.exit(2); }
    for (const p of s.set) console.log(p);
    process.exit(0);
  }
  if (s.refused) console.log(`deploy-inputs: ${target}: the bundler could not answer (${s.refused}); falling back to today's rule: all of proxy/ plus the machinery`);
  else console.log(`deploy-inputs: ${target} set: ${s.set.length} paths (${s.bundle.length} bundled)\n` + s.set.map((p) => `  ${p}`).join("\n"));
  let d;
  try { d = decide(target, s, commitsBehind("HEAD", "origin/main")); } catch (e) { console.log(`deploy-inputs: ${e.message}`); process.exit(2); }
  for (const c of d.notes) console.log(`note: ${c.sha.slice(0, 12)} ${c.subject}: outside ${target}'s bundle and machinery, not blocking (${c.files.filter((f) => f.startsWith("proxy/")).join(", ")})`);
  if (d.others.length) console.log(`note: ${d.others.length} commit(s) behind origin/main touch nothing under proxy/ or in the machinery (not blocking)`);
  if (d.blocking.length) {
    console.log(`deploy-inputs: ${d.blocking.length} commit(s) behind origin/main touch what the ${target} ships${d.fallback ? " (today's rule)" : ""}:`);
    for (const c of d.blocking) console.log(`  ${c.sha.slice(0, 12)} ${c.subject}`);
    process.exit(1);
  }
  process.exit(0);
}
console.log("usage: deploy-inputs.mjs set|check <proxy|builder> <staging|production> | --selftest [--inputs <json>] [--json] | --selftest-plants");
process.exit(2);
