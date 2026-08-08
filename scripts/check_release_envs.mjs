#!/usr/bin/env node
// check_release_envs.mjs -- the env CI builds must BE the product.
//
// WHY THIS EXISTS. On 2026-08-08 the pilot came within a flash run of shipping wrong.
// The cloud feed (-DFEATURE_CLOUD_FEED + a production CLOUD_FEED_BASE) lived in a
// separate `blipscope-s3-128-prodburn` env. Boards were to be flashed from that, but
// the CI release matrix builds `blipscope-s3-128` -- so `firmware-s3-128.bin`, the
// asset every one of those boards downloads on its first OTA, had the feed compiled
// out. All 50 units would have quietly stopped using Blipscope Cloud one update in.
//
// It was invisible for three compounding reasons, and each is why a checker beats a
// convention here:
//   1. A non-cloud build does not fail. It just never contacts the proxy, which looks
//      like a quiet device, not a fault.
//   2. The OTA rehearsal env (`-otatest`) extended the same non-cloud base, so the
//      rehearsal would have passed while proving nothing about the shipping image.
//   3. Nothing tied the CI matrix to the flags. Two files had to agree and neither
//      mentioned the other.
//
// WHAT IT ASSERTS, for every radar SKU in .github/workflows/firmware.yml:
//   - FEATURE_CLOUD_FEED survives flag resolution (-D/-U, last one wins)
//   - CLOUD_FEED_BASE resolves to the PRODUCTION host, never staging
// Edition SKUs (Missileer/Orbitscope/...) are skipped: they are different products and
// carry their own feed config.
//
// RUN:  node scripts/check_release_envs.mjs [--selftest]
// --selftest proves the checker catches each violation before it is trusted to pass.
// That order is deliberate: a checker nobody has seen fail is not evidence.

import { readFileSync } from "node:fs";

const PROD_HOST = "scopes.valarsystems.com";
const EDITION_FLAGS = [
  "FEATURE_EAM", "FEATURE_SPACE", "FEATURE_SEISMIC", "FEATURE_BIRDING",
  "FEATURE_FISHING", "FEATURE_CLAUDESCOPE", "FEATURE_SPEED",
];

// ---- platformio.ini ---------------------------------------------------------
// Minimal INI reader: sections -> { key: value }, values may be multi-line.
export function parseIni(text) {
  const out = {};
  let sec = null;
  let key = null;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.replace(/\s+$/, "");
    const m = /^\[([^\]]+)\]/.exec(line.trim());
    if (m) { sec = m[1]; out[sec] ??= {}; key = null; continue; }
    if (!sec) continue;
    const kv = /^([A-Za-z0-9_.]+)\s*=(.*)$/.exec(line);
    if (kv) { key = kv[1]; out[sec][key] = kv[2].trim(); continue; }
    // continuation line (indented) belongs to the last key
    if (key && /^\s+/.test(raw)) out[sec][key] += "\n" + line.trim();
  }
  return out;
}

// Expand ${section.option} the way PlatformIO does -- ANY option, not just
// build_flags. The shipping envs are composed as `${s3128_hw.build_flags} ${cloud.prod}`,
// so a resolver that only understood `.build_flags` would see no cloud flags at all and
// report the fixed tree as broken.
function expand(ini, text, seen) {
  let out = text;
  for (let i = 0; i < 10 && /\$\{[^}]+\}/.test(out); i++) {
    out = out.replace(/\$\{([^}]+)\.([A-Za-z0-9_]+)\}/g, (whole, sec, opt) => {
      if (seen.has(`${sec}.${opt}`)) return "";
      const val = ini[sec]?.[opt];
      if (val === undefined) return whole; // leave unknown refs visible
      return expand(ini, val, new Set([...seen, `${sec}.${opt}`]));
    });
  }
  return out;
}

// Resolve build_flags through `extends` and ${...} interpolation.
export function resolveFlags(ini, section, seen = new Set()) {
  if (seen.has(section) || !ini[section]) return "";
  seen.add(section);
  let out = expand(ini, ini[section].build_flags ?? "", new Set());
  const ext = (ini[section].extends ?? "").trim();
  if (ext) out = resolveFlags(ini, ext, new Set(seen)) + "\n" + out;
  // strip inline `;` comments so prose cannot look like a flag
  return out.split("\n").map((l) => l.split(";")[0]).join("\n");
}

// Walk -D/-U in order. LAST ONE WINS, which is what the compiler does and what makes
// `-UCLOUD_FEED_BASE -DCLOUD_FEED_BASE="staging"` in a bench env mean what it looks
// like. A checker that just grepped for the production string would pass a staging env.
export function resolveMacro(flags, name) {
  let defined = false;
  let value = null;
  for (const tok of flags.split(/\s+/)) {
    if (tok === `-U${name}`) { defined = false; value = null; }
    else if (tok.startsWith(`-D${name}=`)) { defined = true; value = tok.slice(name.length + 3).replace(/\\?"/g, ""); }
    else if (tok === `-D${name}`) { defined = true; value = ""; }
  }
  return { defined, value };
}

// ---- firmware.yml matrix ----------------------------------------------------
export function matrixEnvs(yaml) {
  return [...yaml.matchAll(/^\s*-\s*\{\s*env:\s*([A-Za-z0-9_-]+)\s*,\s*slug:\s*([A-Za-z0-9_-]+)/gm)]
    .map((m) => ({ env: m[1], slug: m[2] }));
}

export function check(iniText, yamlText) {
  const ini = parseIni(iniText);
  const problems = [];
  const rows = matrixEnvs(yamlText);
  if (rows.length === 0) problems.push("no matrix rows found in firmware.yml -- the parser or the file changed shape");

  for (const { env, slug } of rows) {
    const sec = `env:${env}`;
    if (!ini[sec]) { problems.push(`${env}: in the CI matrix but not in platformio.ini`); continue; }
    const flags = resolveFlags(ini, sec);
    if (EDITION_FLAGS.some((f) => resolveMacro(flags, f).defined)) continue; // another product

    if (!resolveMacro(flags, "FEATURE_CLOUD_FEED").defined) {
      problems.push(
        `${env} (slug ${slug}): no FEATURE_CLOUD_FEED. CI publishes firmware-${slug}.bin from this env, ` +
        `so every device on this SKU would OTA into a build that never contacts the proxy.`,
      );
      continue;
    }
    const base = resolveMacro(flags, "CLOUD_FEED_BASE");
    if (!base.defined || !base.value) {
      problems.push(`${env} (slug ${slug}): FEATURE_CLOUD_FEED with no CLOUD_FEED_BASE -- the shipping image has no backend.`);
    } else if (!base.value.includes(PROD_HOST) || base.value.includes("staging")) {
      problems.push(`${env} (slug ${slug}): CLOUD_FEED_BASE is ${base.value}, not ${PROD_HOST}. CI would publish a bench image to the fleet.`);
    }
  }
  return problems;
}

// ---- selftest ---------------------------------------------------------------
const GOOD_YAML = `        include:
          - { env: blipscope-s3-128, slug: s3-128 }
`;
const GOOD_INI = `[common]
build_flags = -DBASE

[env:blipscope-s3-128]
extends = common
build_flags =
    \${common.build_flags}
    -DFEATURE_CLOUD_FEED
    -DCLOUD_FEED_BASE=\\"https://${PROD_HOST}\\"
`;

// Mirrors the real file's shape: a URL-free hw section plus a named backend.
const COMPOSED_INI = `[common]
build_flags = -DBASE

[cloud]
prod    = -DFEATURE_CLOUD_FEED -DCLOUD_FEED_BASE=\\"https://${PROD_HOST}\\"
staging = -DFEATURE_CLOUD_FEED -DCLOUD_FEED_BASE=\\"https://scopes-staging.valarsystems.com\\"

[s3128_hw]
build_flags =
    \${common.build_flags}
    -DBLIPSCOPE_VARIANT_S3_128

[env:blipscope-s3-128]
extends = common
build_flags =
    \${s3128_hw.build_flags}
    \${cloud.prod}
`;

function selftest() {
  const cases = [
    ["baseline passes", GOOD_INI, GOOD_YAML, 0],
    ["missing FEATURE_CLOUD_FEED is caught",
      GOOD_INI.replace("    -DFEATURE_CLOUD_FEED\n", ""), GOOD_YAML, 1],
    ["a staging CLOUD_FEED_BASE is caught",
      GOOD_INI.replace(`https://${PROD_HOST}`, "https://scopes-staging.valarsystems.com"), GOOD_YAML, 1],
    ["FEATURE_CLOUD_FEED with no base is caught",
      GOOD_INI.replace(/    -DCLOUD_FEED_BASE=.*\n/, ""), GOOD_YAML, 1],
    // The one a naive grep-for-the-production-string checker would wave through.
    ["a later -U undefining the base is caught",
      GOOD_INI + "    -UCLOUD_FEED_BASE\n", GOOD_YAML, 1],
    ["a matrix env missing from platformio.ini is caught",
      GOOD_INI, GOOD_YAML + "          - { env: ghost-s3-999, slug: s3-999 }\n", 1],
    // The shape platformio.ini actually uses: flags composed from ${section.option}
    // rather than written inline. A resolver that only expanded `.build_flags` saw no
    // cloud flags here and called the correct tree broken -- so both directions are
    // pinned, or the checker is only exercised against a form we no longer write.
    ["composition via ${cloud.prod} resolves and passes", COMPOSED_INI, GOOD_YAML, 0],
    ["composition via ${cloud.staging} is caught",
      COMPOSED_INI.replace("${cloud.prod}", "${cloud.staging}"), GOOD_YAML, 1],
    // Must NOT fire on other products, which legitimately have no cloud feed.
    ["an edition SKU is skipped, not flagged",
      GOOD_INI.replace("    -DFEATURE_CLOUD_FEED\n", "    -DFEATURE_EAM\n").replace(/    -DCLOUD_FEED_BASE=.*\n/, ""),
      GOOD_YAML.replace("blipscope-s3-128, slug: s3-128", "blipscope-s3-128, slug: eam-s3-128"), 0],
  ];
  let bad = 0;
  for (const [name, ini, yaml, want] of cases) {
    const got = check(ini, yaml).length;
    const ok = want === 0 ? got === 0 : got >= 1;
    console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : ` (expected ${want ? "a problem" : "none"}, got ${got})`}`);
    if (!ok) bad++;
  }
  if (bad) { console.error(`\nselftest FAILED: ${bad} case(s). The checker cannot be trusted.`); process.exit(1); }
  console.log("\nselftest passed: the checker catches each violation.");
}

if (process.argv.includes("--selftest")) {
  selftest();
} else {
  const problems = check(
    readFileSync("platformio.ini", "utf8"),
    readFileSync(".github/workflows/firmware.yml", "utf8"),
  );
  if (problems.length) {
    console.error("Release envs do not match what we ship:\n");
    for (const p of problems) console.error(`  - ${p}`);
    console.error("\nThe env named in the CI matrix IS the product. Put the flag there,");
    console.error("not in a sibling env that CI never builds.");
    process.exit(1);
  }
  console.log("Release envs OK: every radar SKU in the CI matrix builds a production cloud image.");
}
