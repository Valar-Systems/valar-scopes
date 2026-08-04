#!/usr/bin/env node
/**
 * check_icao_country.mjs -- guard the invariants IcaoCountry::Lookup depends on.
 *
 * WHY A SCRIPT AND NOT A CODE REVIEW. src/IcaoCountry.cpp is a 188-entry table
 * searched with a binary search, so "sorted ascending and non-overlapping" is not
 * a tidiness preference -- it is a correctness precondition. Get it wrong and
 * nothing crashes, nothing warns, and the compiler is perfectly happy: a handful
 * of addresses simply resolve to the wrong flag, or to none at all. That lands in
 * a customer's lifelist as a permanent wrong entry, and it is exactly the class
 * of mistake a human skims straight past when adding one block to a long list.
 * (The --selftest controls below demonstrate both failure modes concretely: an
 * out-of-order insert silently makes Germany unresolvable; an overlap silently
 * turns Canadian addresses into "United States".)
 *
 * The firmware has no host-side test harness (there is no native env, and adding
 * one to unit-test a constant table would be a heavier lift than the table). This
 * parses the real .cpp -- not a copy of the data -- so it cannot drift from what
 * actually compiles.
 *
 *   node scripts/check_icao_country.mjs             # check the real table
 *   node scripts/check_icao_country.mjs --selftest  # prove the checker catches breakage
 *   node scripts/check_icao_country.mjs <path>      # check some other copy
 */
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const REAL = join(root, "src", "IcaoCountry.cpp");

const RE = /\{\s*0x([0-9A-Fa-f]{6}),\s*0x([0-9A-Fa-f]{6}),\s*"((?:[^"\\]|\\.)*)"\s*\}/g;
const hex = (n) => n.toString(16).toUpperCase().padStart(6, "0");

// Logbook truncates country names at MAX_CN_LEN. A longer name would be stored
// truncated, so the same country could later be counted twice under two prefixes
// if the name were ever edited.
const MAX_CN_LEN = 32;

// Anchors: one per major traffic source, plus block boundaries and the gaps.
// These are the addresses a regression would most plausibly break.
const EXPECT = [
  [0xa00000, "United States"],   // lower bound of the US block
  [0xafffff, "United States"],   // upper bound
  [0xadf7c8, "United States"],   // US military lives INSIDE the civil country block
  [0x406b2a, "United Kingdom"],
  [0x3c6444, "Germany"],
  [0x4ca123, "Ireland"],
  [0x484123, "Netherlands"],
  [0x152345, "Russia"],
  [0x780abc, "China"],
  [0x71c123, "South Korea"],
  [0x7c1234, "Australia"],
  [0xc01234, "Canada"],
  [0x0d0456, "Mexico"],
  [0xe48000, "Brazil"],
  [0x8a0123, "Indonesia"],
  [0x008123, "South Africa"],
  [0x2a0000, ""],                // unallocated gap: must NOT snap to a neighbour
  [0xffffff, ""],                // past the last block
  [0x000000, ""],                // before the first block
];

// Mirror of the C++ binary search, so the expectations test the real algorithm
// against the real data rather than a linear scan of a copy.
function lookup(rows, a) {
  let lo = 0, hi = rows.length;
  while (lo < hi) {
    const mid = lo + ((hi - lo) >> 1);
    if (a < rows[mid][0]) hi = mid;
    else if (a > rows[mid][1]) lo = mid + 1;
    else return rows[mid][2];
  }
  return "";
}

function check(src) {
  const rows = [...src.matchAll(RE)].map((m) => [parseInt(m[1], 16), parseInt(m[2], 16), m[3]]);
  const problems = [];

  if (rows.length < 150) problems.push(`only ${rows.length} blocks parsed -- did the table format change?`);

  let prev = null;
  for (const [lo, hi, name] of rows) {
    if (hi < lo) problems.push(`inverted range for ${name}: ${hex(lo)} > ${hex(hi)}`);
    if (prev && lo <= prev[1]) {
      problems.push(`unsorted or overlapping: ${name} starts ${hex(lo)}, but ${prev[2]} ends ${hex(prev[1])}`);
    }
    if (name.length > MAX_CN_LEN) problems.push(`name over ${MAX_CN_LEN} chars: "${name}"`);
    if (name.trim() !== name || name === "") problems.push(`bad name: "${name}"`);
    prev = [lo, hi, name];
  }

  // Two blocks may share a name only if intended; today none do, and an
  // accidental duplicate usually means a copy-paste with a stale name.
  const names = rows.map((r) => r[2]);
  const dupes = [...new Set(names.filter((n) => names.filter((x) => x === n).length > 1))];
  if (dupes.length) problems.push(`duplicate country names: ${dupes.join(", ")}`);

  for (const [addr, want] of EXPECT) {
    const got = lookup(rows, addr);
    if (got !== want) problems.push(`lookup(${hex(addr)}) = "${got}", expected "${want}"`);
  }

  return { rows, problems };
}

// --- selftest: the checker must actually catch each failure mode -------------
// Same reasoning as check-config-form.py --selftest: a checker that has quietly
// stopped checking passes everything, which is worse than having no checker at
// all because it also buys false confidence.
if (process.argv[2] === "--selftest") {
  const good = readFileSync(REAL, "utf8");
  const CONTROLS = [
    {
      name: "out-of-order insert",
      src: good.replace(
        '{ 0x3C0000, 0x3FFFFF, "Germany" },',
        '{ 0x3C0000, 0x3FFFFF, "Germany" },\n    { 0x010000, 0x0103FF, "Bogusland" },',
      ),
      expect: /unsorted or overlapping/,
    },
    {
      name: "overlapping block",
      src: good.replace('{ 0xA00000, 0xAFFFFF, "United States" },', '{ 0xA00000, 0xC10000, "United States" },'),
      expect: /unsorted or overlapping/,
    },
    {
      name: "inverted range",
      src: good.replace('{ 0x840000, 0x87FFFF, "Japan" },', '{ 0x840000, 0x83FFFF, "Japan" },'),
      expect: /inverted range/,
    },
    {
      name: "over-length name",
      src: good.replace('"Canada"', '"A Country Name That Is Far Too Long To Store"'),
      expect: /name over 32 chars/,
    },
    {
      name: "wrong lookup result",
      src: good.replace('{ 0xC00000, 0xC3FFFF, "Canada" },', '{ 0xC00000, 0xC3FFFF, "Canadia" },'),
      expect: /lookup\(C01234\)/,
    },
  ];

  let failures = 0;
  for (const c of CONTROLS) {
    if (c.src === good) {
      console.error(`  SELFTEST BROKEN: control "${c.name}" did not modify the source`);
      failures++;
      continue;
    }
    const { problems } = check(c.src);
    const caught = problems.some((p) => c.expect.test(p));
    console.log(`  ${caught ? "caught" : "MISSED"}: ${c.name}`);
    if (!caught) {
      failures++;
      console.error(`    expected a problem matching ${c.expect}, got: ${JSON.stringify(problems)}`);
    }
  }
  // The unmodified table must still pass, or the controls prove nothing.
  const { problems } = check(good);
  if (problems.length) {
    console.error(`  SELFTEST BROKEN: the real table does not pass: ${problems.join("; ")}`);
    failures++;
  } else {
    console.log("  caught: nothing (the real table passes clean)");
  }

  if (failures) {
    console.error(`\nFAIL: selftest found ${failures} problem(s) -- the checker is not checking.`);
    process.exit(1);
  }
  console.log(`\nOK: selftest passed -- ${CONTROLS.length} failure modes all detected.`);
  process.exit(0);
}

// --- normal run --------------------------------------------------------------
const target = process.argv[2] ?? REAL;
const { rows, problems } = check(readFileSync(target, "utf8"));

if (problems.length) {
  console.error(`FAIL: ${problems.length} problem(s) in ${target}\n`);
  for (const p of problems) console.error("  - " + p);
  process.exit(1);
}

console.log(`OK: ${rows.length} ICAO country blocks, sorted and non-overlapping; ${EXPECT.length} lookups correct.`);
