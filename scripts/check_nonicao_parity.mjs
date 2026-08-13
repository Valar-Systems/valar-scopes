#!/usr/bin/env node
/**
 * check_nonicao_parity.mjs -- the firmware and the proxy must agree on which
 * ICAO addresses are non-ICAO.
 *
 * WHY THIS EXISTS. The table lives twice on purpose: the device needs it to
 * decide NOT to make a request, and the Worker needs it to answer the fleet
 * already in the field, which will keep asking. Two copies of one fact is
 * exactly the arrangement this repo has been bitten by before -- the enrolment
 * URL the firmware opened and the route the Worker served were each internally
 * consistent and disagreed with each other, behind sixteen passing tests.
 *
 * And a drift here is SILENT in the worst direction. If the proxy's table grows
 * a range the firmware's lacks, the device keeps spending TLS handshakes on
 * addresses that can never resolve -- invisible, just slower. If the firmware's
 * grows one the proxy's lacks, real aircraft go blank on that device and the
 * symptom is indistinguishable from the enrichment bug this whole change was
 * built to fix.
 *
 * So this parses BOTH real sources -- not a transcription of either -- and
 * compares them. Change one side and this fails until the other follows.
 *
 *   node scripts/check_nonicao_parity.mjs             # check the real pair
 *   node scripts/check_nonicao_parity.mjs --selftest  # prove the checker catches drift
 */
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const CPP = join(root, "src", "SpecialAircraft.cpp");
const TS = join(root, "proxy", "src", "icaoalloc.ts");

const hex = (n) => "0x" + n.toString(16).toUpperCase().padStart(6, "0");

// constexpr Range NON_ICAO_RANGES[] = { { 0x230000, 0x2FFFFF }, ... };
function parseCpp(src) {
  const block = src.match(/NON_ICAO_RANGES\[\]\s*=\s*\{([\s\S]*?)\n\};/);
  if (!block) throw new Error(`NON_ICAO_RANGES table not found in ${CPP}`);
  return [...block[1].matchAll(/\{\s*0x([0-9A-Fa-f]{6})\s*,\s*0x([0-9A-Fa-f]{6})\s*\}/g)].map((m) => [
    parseInt(m[1], 16),
    parseInt(m[2], 16),
  ]);
}

// const UNALLOCATED: Range[] = [ { lo: 0x230000, hi: 0x2fffff }, ... ];
function parseTs(src) {
  const block = src.match(/UNALLOCATED:\s*Range\[\]\s*=\s*\[([\s\S]*?)\n\];/);
  if (!block) throw new Error(`UNALLOCATED table not found in ${TS}`);
  return [...block[1].matchAll(/lo:\s*0x([0-9A-Fa-f]{6})\s*,\s*hi:\s*0x([0-9A-Fa-f]{6})/g)].map((m) => [
    parseInt(m[1], 16),
    parseInt(m[2], 16),
  ]);
}

function compare(cppSrc, tsSrc) {
  const problems = [];
  const cpp = parseCpp(cppSrc);
  const ts = parseTs(tsSrc);

  // An empty parse on either side would make every comparison below trivially
  // pass, so refuse it outright rather than report agreement.
  if (cpp.length === 0) problems.push("firmware table parsed as EMPTY -- the regex no longer matches the source");
  if (ts.length === 0) problems.push("proxy table parsed as EMPTY -- the regex no longer matches the source");
  if (problems.length) return { cpp, ts, problems };

  const fmt = (rs) => rs.map(([lo, hi]) => `${hex(lo)}-${hex(hi)}`).sort();
  const a = fmt(cpp);
  const b = fmt(ts);
  for (const r of a) if (!b.includes(r)) problems.push(`${r} is in the FIRMWARE table but not the proxy's`);
  for (const r of b) if (!a.includes(r)) problems.push(`${r} is in the PROXY table but not the firmware's`);

  for (const [lo, hi] of [...cpp, ...ts]) {
    if (lo > hi) problems.push(`inverted range ${hex(lo)}-${hex(hi)}`);
  }
  return { cpp, ts, problems };
}

// --- selftest ----------------------------------------------------------------
if (process.argv.includes("--selftest")) {
  const goodCpp = readFileSync(CPP, "utf8");
  const goodTs = readFileSync(TS, "utf8");

  const CONTROLS = [
    {
      name: "proxy gains a range the firmware lacks (devices waste TLS on dead lookups)",
      cpp: goodCpp,
      ts: goodTs.replace("{ lo: 0x230000, hi: 0x2fffff },", "{ lo: 0x230000, hi: 0x2fffff },\n  { lo: 0xb00000, hi: 0xbfffff },"),
      expect: /PROXY table but not the firmware/,
    },
    {
      name: "firmware gains a range the proxy lacks (real aircraft silently blank)",
      cpp: goodCpp.replace("{ 0x230000, 0x2FFFFF },", "{ 0x230000, 0x2FFFFF },\n    { 0xB00000, 0xBFFFFF },"),
      ts: goodTs,
      expect: /FIRMWARE table but not the proxy/,
    },
    {
      name: "one side's bound edited",
      cpp: goodCpp.replace("{ 0x230000, 0x2FFFFF },", "{ 0x230000, 0x2EFFFF },"),
      ts: goodTs,
      expect: /FIRMWARE table but not the proxy/,
    },
    {
      name: "firmware table emptied",
      cpp: goodCpp.replace(/NON_ICAO_RANGES\[\]\s*=\s*\{[\s\S]*?\n\};/, "NON_ICAO_RANGES[] = {\n};"),
      ts: goodTs,
      expect: /parsed as EMPTY/,
    },
  ];

  let failures = 0;
  for (const c of CONTROLS) {
    let problems = [];
    try {
      ({ problems } = compare(c.cpp, c.ts));
    } catch (e) {
      problems = [String(e.message)];
    }
    const caught = problems.some((p) => c.expect.test(p));
    console.log(`  ${caught ? "caught" : "MISSED"}: ${c.name}`);
    if (!caught) {
      failures++;
      console.log(`    (problems were: ${JSON.stringify(problems)})`);
    }
  }
  // And the unmodified pair must be clean, or "catches everything" is worthless.
  const { problems } = compare(goodCpp, goodTs);
  if (problems.length) {
    failures++;
    console.log(`  MISSED: the real, unmodified pair should be clean but reported: ${JSON.stringify(problems)}`);
  } else {
    console.log("  caught: the real pair is clean (no false positive)");
  }

  if (failures) {
    console.error(`\nFAIL: selftest found ${failures} problem(s) -- the checker is not checking.`);
    process.exit(1);
  }
  console.log(`\nOK: selftest passed -- ${CONTROLS.length} drift modes detected, no false positive.`);
  process.exit(0);
}

// --- normal run --------------------------------------------------------------
const { cpp, ts, problems } = compare(readFileSync(CPP, "utf8"), readFileSync(TS, "utf8"));

if (problems.length) {
  console.error(`FAIL: firmware and proxy non-ICAO tables disagree\n`);
  for (const p of problems) console.error("  - " + p);
  console.error(`\n  firmware (${CPP}): ${cpp.length} range(s)`);
  console.error(`  proxy    (${TS}): ${ts.length} range(s)`);
  process.exit(1);
}

console.log(`OK: firmware and proxy agree on ${cpp.length} non-ICAO range(s).`);
