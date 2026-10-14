#!/usr/bin/env node
// check_photo_mode.mjs -- PHOTO_MODE is in exactly one env, and that env never ships.
//
// WHAT PHOTO_MODE IS. env:missileer-photo builds the Missileer firmware for product
// photos: no auto-rotation, no auto-dim, no jump on a fresh EAM, no OTA, a frame dump at
// /diag/fb. Every one of those is wrong on a customer's desk. A photo build reaching the
// fleet would be a device that never updates again and never dims at night, and nothing
// about it would look broken -- the same silent shape as every wrong-env incident in
// CLAUDE.md. So the env is fenced by a check that runs, not by a comment.
//
// THIS IS THE CONFIG HALF. The image half is scripts/check-no-bench-hooks.sh: a
// PHOTO_MODE build prints the "[photo-mode]" marker, every shipping image is scanned for
// it, and the photo image is REQUIRED to carry it (so the marker's absence elsewhere
// means something). This half fails a PR before anything is compiled:
//
//   1. env:missileer-photo is EXACTLY env:missileer-s3-128 + -DPHOTO_MODE: its only
//      keys are `extends` and `build_flags`, it extends that env, its flags are that
//      env's flags plus -DPHOTO_MODE and nothing else, and its resolved flag set equals
//      the parent's plus PHOTO_MODE. The photographed firmware is the firmware that ships.
//   2. PHOTO_MODE appears in NO other section of platformio.ini, raw or resolved through
//      `extends` and ${...}; no other env extends or interpolates the photo env.
//   3. No bench/fixture/demo macro is defined in the photo env. The macro list is DERIVED
//      from the #if lines in src/ and include/, not remembered, and the derivation must
//      find its anchors (ALERT_BENCH, SOAK_TEST) or the check is BLIND (exit 2).
//      The photo env's source filter drops every harness TU (gametest/hwtest/animtest).
//   4. skus.yml lists the photo env build-only with NO slug: CI compiles it on every run
//      and it publishes no asset, so no OTA channel can ever serve it.
//   5. No workflow names the photo env or PHOTO_MODE: no release/publish step can pick it
//      up by name. (The build leg asks this script for its role instead -- `--role`.)
//
// RUN:  node scripts/check_photo_mode.mjs [--selftest | --role <env>]
//   (no args)    check the tree; exit 0 clean, 1 a leak, 2 blind
//   --selftest   prove every assertion fires on a planted leak before trusting a pass
//   --role ENV   print photo | parent | none -- which image scan a CI build leg runs

import { readFileSync, readdirSync, statSync } from "node:fs";
import { join } from "node:path";
import { parseIni, resolveFlags, resolveMacro, matrixEnvs } from "./check_release_envs.mjs";

export const PHOTO_ENV = "missileer-photo";
export const PHOTO_PARENT = "missileer-s3-128";
const MACRO = "PHOTO_MODE";
// NOT /\bPHOTO_MODE\b/: in "-DPHOTO_MODE" the D and the P are both word characters, so
// \b finds no boundary and the flag spelled the way a build file spells it is missed.
// (The first draft did exactly that; the selftest's workflow case caught it.)
const MENTIONS_MACRO = new RegExp(`(?<![A-Za-z0-9_])(?:-[DU])?${MACRO}(?![A-Za-z0-9_])`);

// Harness translation units that define their own setup()/loop() and drive fake
// input; the photo env must drop every one.
const HARNESS_TUS = ["gametest_main.cpp", "hwtest_main.cpp", "animtest_main.cpp"];

// Macro names that gate bench / fixture / demo code. Matched against the identifiers
// on #if/#ifdef/#ifndef/#elif lines.
const BENCH_NAME_RE = /^[A-Z][A-Z0-9_]*(BENCH|SOAK|FIXTURE|DEMO|FAKE|MOCK|PROBE|TRACE|DIAG|INJECT|FAULT)[A-Z0-9_]*$|^GAMETEST_[A-Z0-9_]+$|^SOAK_TEST$/;
// The derivation must find these, or it is not reading the tree it thinks it is.
const BENCH_ANCHORS = ["ALERT_BENCH", "SOAK_TEST"];

class Blind extends Error {}

const stripComments = (v) => v.split("\n").map((l) => l.split(";")[0]).join("\n");
const tokens = (v) => stripComments(v).split(/\s+/).filter(Boolean);
const definedMacros = (flags) =>
  new Set(tokens(flags).filter((t) => t.startsWith("-D")).map((t) => t.slice(2).split("=")[0]));

// The `extends` chain of an env section, nearest first (["env:a", "env:b", "common"]).
function chain(ini, sec) {
  const out = [];
  let cur = sec;
  while (cur && ini[cur] && !out.includes(cur)) {
    out.push(cur);
    cur = (ini[cur].extends ?? "").trim();
  }
  return out;
}

// Every macro name gating bench code, from the sources' preprocessor lines.
export function benchMacrosFromSources(files) {
  const found = new Set();
  for (const text of files) {
    for (const line of text.split(/\r?\n/)) {
      if (!/^\s*#\s*(if|ifdef|ifndef|elif)\b/.test(line)) continue;
      for (const id of line.match(/[A-Z][A-Z0-9_]{2,}/g) ?? [])
        if (BENCH_NAME_RE.test(id)) found.add(id);
    }
  }
  return found;
}

export function check({ iniText, yamlText, workflows, benchMacros }) {
  const ini = parseIni(iniText);
  const problems = [];
  const photoSec = `env:${PHOTO_ENV}`;
  const parentSec = `env:${PHOTO_PARENT}`;

  for (const a of BENCH_ANCHORS)
    if (!benchMacros.has(a))
      throw new Blind(`the bench-macro derivation did not find ${a} in src/ or include/ -- ` +
                      "it is not reading the tree, so an absence below would mean nothing");

  // ---- 1. the photo env's shape -------------------------------------------------
  if (!ini[photoSec]) {
    problems.push(`[${photoSec}] is missing from platformio.ini`);
  } else if (!ini[parentSec]) {
    problems.push(`[${parentSec}], the env the photo build must equal, is missing`);
  } else {
    const keys = Object.keys(ini[photoSec]).sort();
    if (keys.join(",") !== "build_flags,extends")
      problems.push(`${PHOTO_ENV} sets [${keys.join(", ")}]; it may set only extends and build_flags. ` +
                    "Anything else (a build_src_filter, a partition table, a lib) makes the photographed firmware differ from the shipping one.");
    if ((ini[photoSec].extends ?? "").trim() !== parentSec)
      problems.push(`${PHOTO_ENV} extends '${(ini[photoSec].extends ?? "").trim()}', not ${parentSec}`);
    const own = tokens(ini[photoSec].build_flags ?? "");
    const want = [`\${${parentSec}.build_flags}`, `-D${MACRO}`];
    if (own.join(" ") !== want.join(" "))
      problems.push(`${PHOTO_ENV} build_flags are [${own.join(" ")}]; they must be exactly [${want.join(" ")}] -- ` +
                    "the shipping env's flags plus PHOTO_MODE, nothing else");

    const photoSet = definedMacros(resolveFlags(ini, photoSec));
    const parentSet = definedMacros(resolveFlags(ini, parentSec));
    const extra = [...photoSet].filter((m) => m !== MACRO && !parentSet.has(m));
    const missing = [...parentSet].filter((m) => !photoSet.has(m));
    if (!photoSet.has(MACRO)) problems.push(`${PHOTO_ENV} does not define ${MACRO} after resolution`);
    if (extra.length) problems.push(`${PHOTO_ENV} defines ${extra.join(", ")} that ${PHOTO_PARENT} does not`);
    if (missing.length) problems.push(`${PHOTO_ENV} drops ${missing.join(", ")} that ${PHOTO_PARENT} defines`);

    // ---- 3. no bench / fixture / demo macro, no harness TU ------------------------
    const bench = [...photoSet].filter((m) => benchMacros.has(m));
    if (bench.length)
      problems.push(`${PHOTO_ENV} defines bench/fixture macro(s) ${bench.join(", ")} -- a product photo must show real data only`);
    let filter = "";
    for (const s of chain(ini, photoSec)) if (ini[s].build_src_filter !== undefined) { filter = ini[s].build_src_filter; break; }
    for (let i = 0; i < 10 && /\$\{[^}]+\}/.test(filter); i++)
      filter = filter.replace(/\$\{([^}]+)\.([A-Za-z0-9_]+)\}/g, (w, s, o) => ini[s]?.[o] ?? w);
    for (const tu of HARNESS_TUS)
      if (!filter.includes(`-<${tu}>`))
        problems.push(`${PHOTO_ENV}'s build_src_filter does not drop ${tu} (a bench harness)`);
  }

  // ---- 2. PHOTO_MODE nowhere else ---------------------------------------------------
  for (const [sec, opts] of Object.entries(ini)) {
    if (sec === photoSec) continue;
    for (const [k, v] of Object.entries(opts)) {
      const text = stripComments(v);
      if (MENTIONS_MACRO.test(text))
        problems.push(`[${sec}] ${k} mentions ${MACRO}: it belongs to ${PHOTO_ENV} alone`);
      if (text.includes(photoSec))
        problems.push(`[${sec}] ${k} references ${photoSec}: nothing may inherit from the photo build`);
    }
    if (sec.startsWith("env:")) {
      if (resolveMacro(resolveFlags(ini, sec), MACRO).defined)
        problems.push(`${sec.slice(4)} resolves with ${MACRO} defined`);
      if (chain(ini, sec).includes(photoSec))
        problems.push(`${sec.slice(4)} extends ${PHOTO_ENV}`);
    }
  }

  // ---- 4. skus.yml: built every run, never published ---------------------------------
  const rows = matrixEnvs(yamlText);
  if (rows.length === 0) throw new Blind("no rows parsed from skus.yml");
  const photoRows = rows.filter((r) => r.env === PHOTO_ENV);
  if (photoRows.length !== 1)
    problems.push(`skus.yml must list ${PHOTO_ENV} exactly once (build-only) so CI compiles it; found ${photoRows.length}`);
  for (const r of photoRows) {
    if (r.status !== "build-only") problems.push(`skus.yml gives ${PHOTO_ENV} status '${r.status}'; it is build-only, always`);
    if (r.slug) problems.push(`skus.yml gives ${PHOTO_ENV} slug '${r.slug}': that would publish firmware-${r.slug}.bin to an OTA channel`);
  }

  // ---- 5. no workflow names it -------------------------------------------------------
  for (const [name, text] of Object.entries(workflows)) {
    if (text.includes(PHOTO_ENV)) problems.push(`${name} names ${PHOTO_ENV}; publish steps must never be able to select it by name`);
    if (MENTIONS_MACRO.test(text)) problems.push(`${name} mentions ${MACRO}; no workflow may set it`);
  }
  return problems;
}

// ---- the tree --------------------------------------------------------------------
function walk(dir, out = []) {
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) walk(p, out);
    else if (/\.(c|cc|cpp|h|hpp)$/.test(e)) out.push(readFileSync(p, "utf8"));
  }
  return out;
}

function realInputs() {
  const wfDir = ".github/workflows";
  const workflows = {};
  for (const f of readdirSync(wfDir)) if (/\.ya?ml$/.test(f)) workflows[`${wfDir}/${f}`] = readFileSync(join(wfDir, f), "utf8");
  return {
    iniText: readFileSync("platformio.ini", "utf8"),
    yamlText: readFileSync("skus.yml", "utf8"),
    workflows,
    benchMacros: benchMacrosFromSources([...walk("src"), ...walk("include")]),
  };
}

// ---- selftest --------------------------------------------------------------------
const GOOD_INI = `[common]
build_flags = -DBASE
build_src_filter = +<*> -<gametest_main.cpp> -<hwtest_main.cpp> -<animtest_main.cpp>

[env:blipscope-s3-128]
extends = common
build_flags = \${common.build_flags} -DFEATURE_CLOUD_FEED

[env:${PHOTO_PARENT}]
extends = common
build_flags =
    \${common.build_flags}
    -DFEATURE_EAM
    ; a comment naming nothing
build_src_filter = +<*> -<gametest_main.cpp> -<hwtest_main.cpp> -<animtest_main.cpp> +<eam/>

[env:${PHOTO_ENV}]
extends = env:${PHOTO_PARENT}
build_flags =
    \${env:${PHOTO_PARENT}.build_flags}
    -D${MACRO}
`;
const GOOD_YAML = `skus:
  - { env: blipscope-s3-128, status: shipping, slug: s3-128 }
  - { env: ${PHOTO_PARENT}, status: build-only }
  - { env: ${PHOTO_ENV}, status: build-only }
`;
const GOOD_WF = { "wf.yml": "jobs:\n  build:\n    steps:\n      - run: pio run -e ${{ matrix.env }}\n" };
const BENCH = new Set(["ALERT_BENCH", "SOAK_TEST", "TOUCH_WEDGE_BENCH"]);

function selftest() {
  const base = { iniText: GOOD_INI, yamlText: GOOD_YAML, workflows: GOOD_WF, benchMacros: BENCH };
  const ini = (f) => ({ ...base, iniText: f(GOOD_INI) });
  const cases = [
    ["baseline passes", base, 0],
    ["PHOTO_MODE added to a shipping env is caught",
      ini((t) => t.replace("-DFEATURE_CLOUD_FEED", `-DFEATURE_CLOUD_FEED -D${MACRO}`)), 1],
    ["PHOTO_MODE added to the shipping Missileer env is caught",
      ini((t) => t.replace("    -DFEATURE_EAM\n", `    -DFEATURE_EAM\n    -D${MACRO}\n`)), 1],
    ["PHOTO_MODE added to [common] is caught",
      ini((t) => t.replace("build_flags = -DBASE", `build_flags = -DBASE -D${MACRO}`)), 1],
    // Raw text, not resolution: a section no env reads YET is one `${...}` from a leak,
    // and it is spelled -DPHOTO_MODE, which a \b regex cannot see.
    ["-DPHOTO_MODE in a section no env reads is caught",
      ini((t) => t + `\n[spare]\nbuild_flags = -D${MACRO}\n`), 1],
    ["a shipping env extending the photo env is caught",
      ini((t) => t.replace("[env:blipscope-s3-128]\nextends = common", `[env:blipscope-s3-128]\nextends = env:${PHOTO_ENV}`)), 1],
    ["a shipping env interpolating the photo env's flags is caught",
      ini((t) => t.replace("${common.build_flags} -DFEATURE_CLOUD_FEED", `\${env:${PHOTO_ENV}.build_flags}`)), 1],
    ["an extra flag on the photo env is caught",
      ini((t) => t.replace(`    -D${MACRO}\n`, `    -D${MACRO}\n    -DSOMETHING_ELSE\n`)), 1],
    ["a bench macro on the photo env is caught",
      ini((t) => t.replace(`    -D${MACRO}\n`, `    -D${MACRO}\n    -DALERT_BENCH\n`)), 1],
    ["the photo env dropping PHOTO_MODE is caught",
      ini((t) => t.replace(`    -D${MACRO}\n`, "")), 1],
    ["the photo env restating flags instead of inheriting is caught",
      ini((t) => t.replace(`\${env:${PHOTO_PARENT}.build_flags}`, "-DBASE -DFEATURE_EAM")), 1],
    ["the photo env extending something else is caught",
      ini((t) => t.replace(`extends = env:${PHOTO_PARENT}\nbuild_flags =\n    \${env`, `extends = env:blipscope-s3-128\nbuild_flags =\n    \${env`)), 1],
    ["the photo env growing its own build_src_filter is caught",
      ini((t) => t + "build_src_filter = +<*>\n"), 1],
    ["a harness TU reaching the photo env through its parent's filter is caught",
      ini((t) => t.replace("-<gametest_main.cpp> -<hwtest_main.cpp> -<animtest_main.cpp> +<eam/>", "-<hwtest_main.cpp> -<animtest_main.cpp> +<eam/>")), 1],
    ["the photo env missing is caught",
      ini((t) => t.slice(0, t.indexOf(`[env:${PHOTO_ENV}]`))), 1],
    ["a slug on the photo row is caught",
      { ...base, yamlText: GOOD_YAML.replace(`${PHOTO_ENV}, status: build-only }`, `${PHOTO_ENV}, status: build-only, slug: eam-s3-128 }`) }, 1],
    ["the photo row promoted to shipping is caught",
      { ...base, yamlText: GOOD_YAML.replace(`${PHOTO_ENV}, status: build-only }`, `${PHOTO_ENV}, status: shipping, slug: eam-photo }`) }, 1],
    ["the photo row missing (CI would never build it) is caught",
      { ...base, yamlText: GOOD_YAML.replace(`  - { env: ${PHOTO_ENV}, status: build-only }\n`, "") }, 1],
    ["a workflow naming the photo env is caught",
      { ...base, workflows: { "wf.yml": `      - run: gh release upload v1 .pio/build/${PHOTO_ENV}/firmware.bin\n` } }, 1],
    ["a workflow setting PHOTO_MODE is caught",
      { ...base, workflows: { "wf.yml": `      - run: PLATFORMIO_BUILD_FLAGS=-D${MACRO} pio run\n` } }, 1],
  ];
  let bad = 0;
  for (const [name, input, want] of cases) {
    const got = check(input).length;
    const ok = want === 0 ? got === 0 : got >= 1;
    console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : ` (expected ${want ? "a problem" : "none"}, got ${got})`}`);
    if (!ok) bad++;
  }
  // BLIND, never clean: a derivation that found no bench macros must refuse to certify.
  let blind = false;
  try { check({ ...base, benchMacros: new Set() }); } catch (e) { blind = e instanceof Blind; }
  console.log(`  ${blind ? "PASS" : "FAIL"}  an empty bench-macro derivation is BLIND, not a pass`);
  if (!blind) bad++;
  // ...and the derivation itself reads #if lines, not prose.
  const derived = benchMacrosFromSources([
    "#ifdef ALERT_BENCH\n#endif\n#if defined(SOAK_TEST) || defined(FETCH_TRACE)\n// ALERT_BENCH_IN_PROSE\nint SHOWN_BENCH;\n",
  ]);
  const want = ["ALERT_BENCH", "FETCH_TRACE", "SOAK_TEST"];
  const dOk = [...derived].sort().join(",") === want.join(",");
  console.log(`  ${dOk ? "PASS" : "FAIL"}  bench macros are derived from #if lines only (${[...derived].sort().join(",")})`);
  if (!dOk) bad++;

  if (bad) { console.error(`\nselftest FAILED: ${bad} case(s). The checker cannot be trusted.`); process.exit(1); }
  console.log("\nselftest passed: the checker catches each leak.");
}

// ---- main ------------------------------------------------------------------------
const args = process.argv.slice(2);
if (args[0] === "--selftest") {
  selftest();
} else if (args[0] === "--role") {
  const env = args[1] ?? "";
  console.log(env === PHOTO_ENV ? "photo" : env === PHOTO_PARENT ? "parent" : "none");
} else {
  let problems;
  try {
    problems = check(realInputs());
  } catch (e) {
    if (e instanceof Blind) { console.error(`BLIND: ${e.message}`); process.exit(2); }
    throw e;
  }
  if (problems.length) {
    console.error("PHOTO_MODE could reach a shipping image:\n");
    for (const p of problems) console.error(`  - ${p}`);
    process.exit(1);
  }
  console.log(`ok: ${MACRO} is in ${PHOTO_ENV} alone, which is ${PHOTO_PARENT} + ${MACRO} exactly, ` +
              "build-only with no slug, named by no workflow, and carries no bench macro.");
}
