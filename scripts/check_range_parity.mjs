#!/usr/bin/env node
/**
 * check_range_parity.mjs -- the firmware and the proxy must agree on the ICAO
 * address ranges they BOTH carry.
 *
 * Two tables live twice, each for a good reason:
 *
 *   MIL_RANGES        the device tags a contact "MIL" offline with no network
 *                     at all; the Worker fills `op` with a truthful generic when
 *                     the registry has nothing. CLAUDE.md has asserted these are
 *                     "kept IDENTICAL" since they were written, and until this
 *                     script nothing checked it.
 *   NON_ICAO_RANGES   the device uses it to NOT make a request; the Worker still
 *                     has to answer the fleet already in the field, which will
 *                     keep asking.
 *
 * WHY A CHECK AND NOT A CONVENTION. Two copies of one fact is exactly the
 * arrangement that shipped a dead enrolment URL behind sixteen passing tests:
 * the firmware opened /enroll, the Worker routed /blipscope/enroll, each side
 * was internally consistent, and no test ever crossed between them. So this
 * parses BOTH real sources -- not a transcription of either -- and compares
 * them. Change one side and this fails until the other follows.
 *
 * AND THE DRIFT IS SELF-CAMOUFLAGING, which is what makes it worth a CI job.
 * A range wrongly present in the firmware's non-ICAO table silently blanks a
 * REAL aircraft -- producing the exact symptom of the enrichment bug that table
 * was added to fix. A missing military range just quietly drops a MIL tag.
 * Neither announces itself; both look like the ordinary gaps we are used to
 * seeing. (This is not hypothetical: the first draft of NON_ICAO_RANGES carried
 * four extra registry-empty regions and blanked f40001, and the failure read as
 * a data problem rather than a code one.)
 *
 *   node scripts/check_range_parity.mjs             # check the real pairs
 *   node scripts/check_range_parity.mjs --selftest  # prove the checker catches drift
 */
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const CPP = join(root, "src", "SpecialAircraft.cpp");
const MIL_TS = join(root, "proxy", "src", "military.ts");
const ALLOC_TS = join(root, "proxy", "src", "icaoalloc.ts");

const hex = (n) => "0x" + n.toString(16).toUpperCase().padStart(6, "0");

// constexpr Range NAME[] = { { 0x230000, 0x2FFFFF }, ... };
function parseCpp(src, name) {
  const block = src.match(new RegExp(`${name}\\[\\]\\s*=\\s*\\{([\\s\\S]*?)\\n\\};`));
  if (!block) throw new Error(`${name} table not found in ${CPP}`);
  return [...block[1].matchAll(/\{\s*0x([0-9A-Fa-f]{6})\s*,\s*0x([0-9A-Fa-f]{6})\s*\}/g)].map((m) => [
    parseInt(m[1], 16),
    parseInt(m[2], 16),
  ]);
}

// const NAME: T[] = [ { lo: 0x230000, hi: 0x2fffff, ... }, ... ];
function parseTs(src, name, file) {
  const block = src.match(new RegExp(`${name}:\\s*\\w+\\[\\]\\s*=\\s*\\[([\\s\\S]*?)\\n\\];`));
  if (!block) throw new Error(`${name} table not found in ${file}`);
  return [...block[1].matchAll(/lo:\s*0x([0-9A-Fa-f]{6})\s*,\s*hi:\s*0x([0-9A-Fa-f]{6})/g)].map((m) => [
    parseInt(m[1], 16),
    parseInt(m[2], 16),
  ]);
}

// Compares only lo/hi. The proxy's military rows also carry an `op` label the
// firmware has no use for; that asymmetry is intended and is not drift.
function comparePair(label, cpp, ts) {
  const problems = [];
  // An empty parse on either side makes every comparison below trivially pass,
  // so refuse it outright rather than report agreement.
  if (cpp.length === 0) problems.push(`${label}: firmware table parsed as EMPTY -- the regex no longer matches`);
  if (ts.length === 0) problems.push(`${label}: proxy table parsed as EMPTY -- the regex no longer matches`);
  if (problems.length) return problems;

  const fmt = (rs) => rs.map(([lo, hi]) => `${hex(lo)}-${hex(hi)}`).sort();
  const a = fmt(cpp);
  const b = fmt(ts);
  for (const r of a) if (!b.includes(r)) problems.push(`${label}: ${r} is in the FIRMWARE table but not the proxy's`);
  for (const r of b) if (!a.includes(r)) problems.push(`${label}: ${r} is in the PROXY table but not the firmware's`);
  for (const [lo, hi] of [...cpp, ...ts]) {
    if (lo > hi) problems.push(`${label}: inverted range ${hex(lo)}-${hex(hi)}`);
  }
  return problems;
}

function checkAll(cppSrc, milSrc, allocSrc) {
  const pairs = [
    { label: "MIL_RANGES", cpp: parseCpp(cppSrc, "MIL_RANGES"), ts: parseTs(milSrc, "MIL_RANGES", MIL_TS) },
    {
      label: "NON_ICAO_RANGES",
      cpp: parseCpp(cppSrc, "NON_ICAO_RANGES"),
      ts: parseTs(allocSrc, "UNALLOCATED", ALLOC_TS),
    },
  ];
  return { pairs, problems: pairs.flatMap((p) => comparePair(p.label, p.cpp, p.ts)) };
}

// --- selftest ----------------------------------------------------------------
if (process.argv.includes("--selftest")) {
  const goodCpp = readFileSync(CPP, "utf8");
  const goodMil = readFileSync(MIL_TS, "utf8");
  const goodAlloc = readFileSync(ALLOC_TS, "utf8");

  const CONTROLS = [
    {
      name: "non-ICAO: proxy gains a range the firmware lacks (devices waste TLS on dead lookups)",
      alloc: goodAlloc.replace(
        "{ lo: 0x230000, hi: 0x2fffff },",
        "{ lo: 0x230000, hi: 0x2fffff },\n  { lo: 0xb00000, hi: 0xbfffff },",
      ),
      expect: /NON_ICAO_RANGES.*PROXY table but not the firmware/,
    },
    {
      name: "non-ICAO: firmware gains a range the proxy lacks (REAL AIRCRAFT SILENTLY BLANK)",
      cpp: goodCpp.replace("{ 0x230000, 0x2FFFFF },", "{ 0x230000, 0x2FFFFF },\n    { 0xB00000, 0xBFFFFF },"),
      expect: /NON_ICAO_RANGES.*FIRMWARE table but not the proxy/,
    },
    {
      name: "military: a range dropped from the firmware (MIL tag silently lost)",
      cpp: goodCpp.replace("{ 0x0A4000, 0x0A4FFF }, // Algeria", ""),
      expect: /MIL_RANGES.*PROXY table but not the firmware/,
    },
    {
      name: "military: a bound edited on the proxy side only",
      mil: goodMil.replace("{ lo: 0x0a4000, hi: 0x0a4fff,", "{ lo: 0x0a4000, hi: 0x0a4ffe,"),
      expect: /MIL_RANGES.*FIRMWARE table but not the proxy/,
    },
    {
      name: "a table emptied entirely",
      cpp: goodCpp.replace(/NON_ICAO_RANGES\[\]\s*=\s*\{[\s\S]*?\n\};/, "NON_ICAO_RANGES[] = {\n};"),
      expect: /parsed as EMPTY/,
    },
  ];

  let failures = 0;
  for (const c of CONTROLS) {
    let problems = [];
    try {
      ({ problems } = checkAll(c.cpp ?? goodCpp, c.mil ?? goodMil, c.alloc ?? goodAlloc));
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
  // The unmodified sources must be clean, or "catches everything" is worthless.
  const { problems } = checkAll(goodCpp, goodMil, goodAlloc);
  if (problems.length) {
    failures++;
    console.log(`  MISSED: the real, unmodified tables should be clean but reported:`);
    for (const p of problems) console.log(`    - ${p}`);
  } else {
    console.log("  caught: the real tables are clean (no false positive)");
  }

  if (failures) {
    console.error(`\nFAIL: selftest found ${failures} problem(s) -- the checker is not checking.`);
    process.exit(1);
  }
  console.log(`\nOK: selftest passed -- ${CONTROLS.length} drift modes detected, no false positive.`);
  process.exit(0);
}

// --- normal run --------------------------------------------------------------
const { pairs, problems } = checkAll(
  readFileSync(CPP, "utf8"),
  readFileSync(MIL_TS, "utf8"),
  readFileSync(ALLOC_TS, "utf8"),
);

if (problems.length) {
  console.error(`FAIL: firmware and proxy range tables disagree\n`);
  for (const p of problems) console.error("  - " + p);
  console.error("");
  for (const p of pairs) console.error(`  ${p.label}: firmware ${p.cpp.length} range(s), proxy ${p.ts.length}`);
  process.exit(1);
}

for (const p of pairs) console.log(`OK: ${p.label} -- firmware and proxy agree on ${p.cpp.length} range(s).`);
