// The /credits sabotages (docs/credits-correctness.md, registered before they ran). Each is an in-place edit, asserted
// to apply exactly once, then photolicense.test.ts + basemap-attribution.test.ts are run and the set of RED tests, with
// their counts, must equal the registered set exactly. Each file is restored from a byte copy and its sha256 checked.
// A crash or an unparseable run is an instrument failure, never a red.
//
//   node scripts/sabotage-credits.mjs        (from proxy/)    exit 0 all as registered, 1 off, 2 rig, 3 restore failed
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const SRC = "src/photolicense.ts", HTML = "photos/credits.html";
const sha = (b) => createHash("sha256").update(b).digest("hex");
const TESTS = {
  CV1: "CV1: the real manifest", CV2: "CV2: one row per form", SA1: "SA1: 114 of 114", RD1: "RD1: credits",
  QC1: "Q-C1: equals", F16: "renders license links for CC-BY-SA", PD: "courtesy-credits PD", MRMS: "carries the NOAA MRMS",
  MA1: "MA1: every credit", MA2: "MA2: /credits carries",
};

const SABOTAGES = [
  {
    name: "S-CV-BLANKET", file: SRC,
    edits: [
      ['    if (!m) return { label: token, sa: cls === "CC-BY-SA" };',
       '    { const k = cls === "CC-BY-SA" ? "BY-SA" : "BY"; return { label: `CC ${k} 4.0`, url: `https://creativecommons.org/licenses/${k.toLowerCase()}/4.0/`, sa: k === "BY-SA" }; } // SABOTAGE S-CV-BLANKET'],
      ['    if (!m) return { label: token, sa: false };\n    return {\n      label: `Open Government Licence v${m[1]}.0`,',
       '    if (!m || m) return { label: "Open Government Licence v3.0", url: "https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/", sa: false }; // SABOTAGE\n    return {\n      label: `Open Government Licence v${m[1]}.0`,'],
    ],
    red: { CV1: "{ checked: 176, wrong: 104 }", CV2: "{ checked: 11, wrong: 8 }", SA1: "saOk: 48", QC1: null, F16: null },
  },
  {
    name: "S-SA", file: SRC,
    edits: [['    const shareAlike = d.sa ?', '    const shareAlike = false && d.sa ? /* SABOTAGE S-SA */']],
    red: { SA1: "saOk: 0", QC1: null },
  },
  {
    name: "S-RD", file: SRC,
    edits: [[
      '<li>Route data from <a href="https://github.com/vradarserver/standing-data" rel="noopener">vradarserver/standing-data</a>,\nreleased into the public domain under <a href="https://creativecommons.org/publicdomain/zero/1.0/" rel="noopener">CC0 1.0</a>.</li>',
      '<li>Route data from <a href="https://www.adsbdb.com" rel="noopener">adsbdb</a>.</li>',
    ]],
    red: { RD1: "{ standingData: 0, adsbdb: 1 }", QC1: null },
  },
  {
    name: "S-QC1", file: HTML,
    edits: [["<title>Blipscope credits</title>", "<title>Blipscope credits (edited)</title>"]],
    red: { QC1: null },
  },
];

function runTests() {
  const r = spawnSync("npx", ["vitest", "run", "test/photolicense.test.ts", "test/basemap-attribution.test.ts"], { shell: true, encoding: "utf8" });
  const out = ((r.stdout ?? "") + (r.stderr ?? "")).replace(/\x1b\[[0-9;]*m/g, "");
  const total = /Tests\s+(?:(\d+) failed \| )?(\d+) passed \((\d+)\)/.exec(out);
  return { out, total };
}

let off = 0;
const base = runTests();
const baseRed = Object.entries(TESTS).filter(([, t]) => base.out.split("\n").some((l) => l.includes("×") && l.includes(t))).map(([k]) => k);
if (!base.total || baseRed.length || base.total[1]) {
  console.log(`RIG: the unsabotaged run must be all green first (red: ${baseRed.join(", ") || "none"}; parsed: ${!!base.total})`); process.exit(2);
}
console.log(`CONTROL (unsabotaged): ${base.total[2]} passed of ${base.total[3]}, red none`);

for (const s of SABOTAGES) {
  const orig = readFileSync(s.file), before = sha(orig);
  let text = orig.toString("utf8");
  for (const [a, b] of s.edits) {
    if (text.split(a).length !== 2) { console.log(`RIG: ${s.name}: an edit's anchor is not in ${s.file} exactly once`); process.exit(2); }
    text = text.replace(a, b);
  }
  let r;
  try {
    writeFileSync(s.file, text);
    if (sha(readFileSync(s.file)) === before) { console.log(`RIG: ${s.name} did not change the file`); process.exit(2); }
    r = runTests();
  } finally {
    writeFileSync(s.file, orig);
    if (sha(readFileSync(s.file)) !== before) { console.log(`${s.name}: RESTORE FAILED`); process.exit(3); }
  }
  if (!r.total) { console.log(`${s.name}: INSTRUMENT FAILURE -- the run's summary could not be read`); off++; continue; }
  const lines = r.out.split("\n");
  const red = Object.entries(TESTS).filter(([, t]) => lines.some((l) => l.includes("×") && l.includes(t))).map(([k]) => k);
  const want = Object.keys(s.red);
  const sameSet = red.length === want.length && want.every((k) => red.includes(k));
  // The registered counts are compared as NUMBERS read out of the received object in vitest's message ("expected
  // { saOk: +0, ... } to deeply equal"): vitest writes zero as "+0", which a string match reads as absent.
  const counts = Object.entries(s.red).filter(([, c]) => c).map(([k, c]) => {
    const i = lines.findIndex((l) => l.includes("×") && l.includes(TESTS[k]));
    const msg = i >= 0 ? (lines[i + 1] ?? "") : "";
    const received = /expected (\{[^}]*\})/.exec(msg)?.[1] ?? "";
    const got = Object.fromEntries([...received.matchAll(/(\w+): \+?(\d+)/g)].map((m) => [m[1], Number(m[2])]));
    const wantPairs = [...c.matchAll(/(\w+): \+?(\d+)/g)].map((m) => [m[1], Number(m[2])]);
    return [k, c, wantPairs.length > 0 && wantPairs.every(([key, v]) => got[key] === v), msg.trim()];
  });
  for (const [k, , hit, msg] of counts) if (!hit) console.log(`   ${s.name} ${k}: the failure message reads: ${msg}`);
  const ok = sameSet && counts.every(([, , hit]) => hit);
  off += !ok;
  console.log(`${s.name}: ${ok ? "as registered" : "NOT AS REGISTERED"}; red ${JSON.stringify(red)} (want ${JSON.stringify(want)}); ` +
    `counts ${counts.map(([k, c, hit]) => `${k} ${c} ${hit ? "seen" : "NOT SEEN"}`).join("; ") || "none registered"}; restored byte-identical`);
}
console.log(off ? `${off} NOT AS REGISTERED` : "ALL AS REGISTERED");
process.exit(off ? 1 : 0);
