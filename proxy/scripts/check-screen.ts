// The screen service's phase-0 checks that run in plain Node (registration: screen-registration-1.md, accepted
// 2026-11-25; predictions frozen first). No workerd: the logic these grade is pure, so it is graded where this
// machine's runtime is reliable. The class-shape checks (P-T1, P-F4, /health) are test/screen-entry.test.ts.
//
//   P-F1  PARITY. The boards' route table is read FROM src/index.ts (the other side of the contract): every
//         `api.suffix === "x"`, every `api.suffix.match(/^x\//)` and every `api.edition === "x"`. It must equal the
//         routes of src/screen/map.ts, one row each. Prints: routes N, mapped-released, planned, excluded, not-ruled.
//   P-F3  "Released" comes from the release. The service's RELEASE_URL must equal the firmware's own
//         OTA_RELEASE_BASE + "/version.txt" (read from src/OtaUpdater.cpp). parseVersion takes an integer and nothing
//         else, and releasedVersion's 1 h memo fails CLOSED (null) once it expires onto an error.
//   P-F2  releaseAllows: a released row always passes; waitsFor n needs a read version >= n; unscheduled never.
//   P-T2  tierAllows: fail-closed; "cloud" allows free and cloud, "plus" and "business" allow all; anything else none.
//
//   npx tsx scripts/check-screen.ts              exit 0 PASS, 1 FAIL, 2 BLIND (an input the check cannot read)
//   npx tsx scripts/check-screen.ts --selftest   the real check must PASS; then each sabotage, on a TEMP COPY that
//        is asserted to differ from its source, must FAIL exactly as predicted (F1s, F3s, F2s, T2s, P3s).
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const PROXY = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PROXY, "..");
const INDEX = join(PROXY, "src", "index.ts");
const OTA = join(REPO, "src", "OtaUpdater.cpp");
const GATE = join(PROXY, "src", "screen", "gate.ts");
const RELEASE = join(PROXY, "src", "screen", "release.ts");

type Tier = "free" | "cloud" | "plus";
type Firmware = { released: number } | { waitsFor: number | "unscheduled" };
type Row =
  | { kind: "method"; method: string; route: string; tier: Tier; firmware: Firmware }
  | { kind: "excluded"; route: string; reason: string; ruling: string }
  | { kind: "not-ruled"; route: string; note: string };
interface GateMod { tierAllows(ctxTier: unknown, rowTier: Tier): boolean; releaseAllows(fw: Firmware, released: number | null): boolean }
interface ReleaseMod {
  parseVersion(text: string): number | null;
  releasedVersion(fetcher: (url: string) => Promise<Response>, now: number): Promise<number | null>;
  __resetReleaseMemoForTests(): void;
}
interface Result { verdict: "PASS" | "FAIL" | "BLIND"; lines: string[]; failures: string[] }

/** The boards' routes, from index.ts's text. Keys: "blips", "enrich/*", "edition:weatherscope". */
export function routesFromIndex(src: string): string[] {
  const out = new Set<string>();
  for (const m of src.matchAll(/api\.suffix === "([^"]+)"/g)) out.add(m[1]);
  for (const m of src.matchAll(/api\.suffix\.match\(\/\^([a-z][a-z0-9-]*)\\\//g)) out.add(`${m[1]}/*`);
  for (const m of src.matchAll(/api\.edition === "([^"]+)"/g)) out.add(`edition:${m[1]}`);
  return [...out].sort();
}

function checkParity(indexSrc: string, map: readonly Row[]): Result {
  const routes = routesFromIndex(indexSrc);
  if (routes.length === 0) return { verdict: "BLIND", lines: ["P-F1: no route read from index.ts"], failures: ["P-F1 blind"] };
  const failures: string[] = [];
  const mapped = new Map<string, number>();
  for (const r of map) mapped.set(r.route, (mapped.get(r.route) ?? 0) + 1);
  for (const rt of routes) if (!mapped.has(rt)) failures.push(`P-F1: route "${rt}" is in index.ts and has no map row`);
  for (const [rt, n] of mapped) {
    if (!routes.includes(rt)) failures.push(`P-F1: map row "${rt}" names a route index.ts does not have`);
    if (n !== 1) failures.push(`P-F1: route "${rt}" has ${n} map rows`);
  }
  const methods = map.filter((r): r is Extract<Row, { kind: "method" }> => r.kind === "method");
  const released = methods.filter((r) => "released" in r.firmware).length;
  const planned = methods.length - released;
  const excluded = map.filter((r) => r.kind === "excluded").length;
  const notRuled = map.filter((r) => r.kind === "not-ruled").length;
  return {
    verdict: failures.length ? "FAIL" : "PASS",
    lines: [`P-F1: routes ${routes.length} (read from src/index.ts): mapped-released ${released}, planned ${planned}, excluded ${excluded}, not-ruled ${notRuled}`],
    failures,
  };
}

function checkUrl(otaSrc: string, releaseUrl: string): Result {
  const base = /#define OTA_RELEASE_BASE "([^"]+)"/.exec(otaSrc)?.[1];
  const suffix = /VERSION_URL = OTA_RELEASE_BASE "([^"]+)"/.exec(otaSrc)?.[1];
  if (!base || !suffix) return { verdict: "BLIND", lines: ["P-F3: OTA_RELEASE_BASE or VERSION_URL not read from OtaUpdater.cpp"], failures: ["P-F3 blind"] };
  const want = base + suffix;
  return want === releaseUrl
    ? { verdict: "PASS", lines: ["P-F3: release URL equals the firmware's (OTA_RELEASE_BASE + VERSION_URL's suffix)"], failures: [] }
    : { verdict: "FAIL", lines: [], failures: [`P-F3: release URL differs from the firmware's (${want.length} vs ${releaseUrl.length} chars)`] };
}

const PARSE_CASES: Array<[string, number | null]> = [
  ["19", 19], ["20\n", 20], [" 21 ", 21], ["20x", null], ["", null], ["v20", null], ["1e3", null], ["-1", null], ["99999", null],
];
function checkParser(rel: ReleaseMod): Result {
  const failures = PARSE_CASES.filter(([t, want]) => rel.parseVersion(t) !== want).map(([t, want]) => `P-F3 parse ${JSON.stringify(t)}: want ${want}, got ${rel.parseVersion(t)}`);
  return { verdict: failures.length ? "FAIL" : "PASS", lines: [`P-F3: parser ${PARSE_CASES.length - failures.length} of ${PARSE_CASES.length} cases`], failures };
}

async function checkMemo(rel: ReleaseMod): Promise<Result> {
  rel.__resetReleaseMemoForTests();
  let answer: Response = new Response("20\n", { status: 200 });
  const fetcher = async () => answer.clone();
  const t0 = 1_800_000_000_000;
  const a = await rel.releasedVersion(fetcher, t0);
  answer = new Response("Not Found", { status: 404 });
  const b = await rel.releasedVersion(fetcher, t0 + 30 * 60_000);
  const c = await rel.releasedVersion(fetcher, t0 + 61 * 60_000);
  const got = [a, b, c];
  const want = [20, 20, null];
  const failures = got.some((v, i) => v !== want[i]) ? [`P-F3 memo: want ${JSON.stringify(want)}, got ${JSON.stringify(got)}`] : [];
  return { verdict: failures.length ? "FAIL" : "PASS", lines: [`P-F3: memo reads ${JSON.stringify(got)}`], failures };
}

const FW_ROWS: Array<[string, Firmware]> = [["released 15", { released: 15 }], ["waitsFor 20", { waitsFor: 20 }], ["unscheduled", { waitsFor: "unscheduled" }]];
const READS: Array<number | null> = [null, 19, 20, 21, Number.NaN];
function releaseWant(fw: Firmware, v: number | null): boolean {
  if ("released" in fw) return true;
  if (fw.waitsFor === "unscheduled") return false;
  return v === 20 || v === 21;
}
function checkReleaseGate(g: GateMod): Result {
  const failures: string[] = [];
  for (const [name, fw] of FW_ROWS) for (const v of READS) {
    const got = g.releaseAllows(fw, v);
    if (got !== releaseWant(fw, v)) failures.push(`P-F2 ${name} at ${v}: want ${releaseWant(fw, v)}, got ${got}`);
  }
  return { verdict: failures.length ? "FAIL" : "PASS", lines: [`P-F2: ${FW_ROWS.length * READS.length - failures.length} of ${FW_ROWS.length * READS.length} cases`], failures };
}

const CTXS: unknown[] = ["cloud", "plus", "business", undefined, "Plus"];
const TIERS: Tier[] = ["free", "cloud", "plus"];
function tierWant(c: unknown, t: Tier): boolean {
  if (c === "cloud") return t !== "plus";
  return c === "plus" || c === "business";
}
function checkTierGate(g: GateMod): Result {
  const failures: string[] = [];
  for (const c of CTXS) for (const t of TIERS) {
    const got = g.tierAllows(c, t);
    if (got !== tierWant(c, t)) failures.push(`P-T2 ctx ${String(c)} row ${t}: want ${tierWant(c, t)}, got ${got}`);
  }
  return { verdict: failures.length ? "FAIL" : "PASS", lines: [`P-T2: ${CTXS.length * TIERS.length - failures.length} of ${CTXS.length * TIERS.length} cases`], failures };
}

interface Inputs { indexSrc: string; otaSrc: string; gatePath: string; releasePath: string }
async function runAll(inp: Inputs): Promise<Result> {
  const mapMod = await import(pathToFileURL(join(PROXY, "src", "screen", "map.ts")).href) as { SCREEN_MAP: readonly Row[]; RELEASE_URL: string };
  const gate = await import(pathToFileURL(inp.gatePath).href) as GateMod;
  const rel = await import(pathToFileURL(inp.releasePath).href) as ReleaseMod;
  const parts = [
    checkParity(inp.indexSrc, mapMod.SCREEN_MAP),
    checkUrl(inp.otaSrc, mapMod.RELEASE_URL),
    checkParser(rel),
    checkReleaseGate(gate),
    checkTierGate(gate),
    await checkMemo(rel),
  ];
  const verdict = parts.some((p) => p.verdict === "BLIND") ? "BLIND" : parts.some((p) => p.verdict === "FAIL") ? "FAIL" : "PASS";
  return { verdict, lines: parts.flatMap((p) => p.lines), failures: parts.flatMap((p) => p.failures) };
}

const real = (): Inputs => ({ indexSrc: readFileSync(INDEX, "utf8"), otaSrc: readFileSync(OTA, "utf8"), gatePath: GATE, releasePath: RELEASE });

/** A sabotaged copy of one source file, written to a temp dir; refuses if the edit did not change the text. */
function sabotagedCopy(dir: string, src: string, name: string, edit: (s: string) => string): string {
  const before = readFileSync(src, "utf8");
  const after = edit(before);
  if (after === before) throw new Error(`sabotage ${name}: the edit did not apply`);
  const p = join(dir, name);
  writeFileSync(p, after);
  return p;
}

async function selftest(): Promise<number> {
  const base = await runAll(real());
  console.log(`real: ${base.verdict}`);
  if (base.verdict !== "PASS") { base.failures.forEach((f) => console.log(`  ${f}`)); return 1; }
  const dir = mkdtempSync(join(tmpdir(), "check-screen-"));
  let bad = 0;
  const expect = (name: string, r: Result, want: string[]) => {
    const ok = r.verdict === "FAIL" && r.failures.length === want.length && want.every((w) => r.failures.some((f) => f.startsWith(w)));
    console.log(`${name}: ${r.verdict}, ${r.failures.length} failure(s) -> ${ok ? "AS PREDICTED" : "NOT AS PREDICTED"}`);
    if (!ok) { bad++; r.failures.forEach((f) => console.log(`  ${f}`)); }
  };
  try {
    // F1s: an invented route in a copy of index.ts.
    const idx = readFileSync(INDEX, "utf8");
    const idxSab = idx.replace(`if (api.suffix === "blips")`, `if (api.suffix === "zzz") return new Response("x");\n  if (api.suffix === "blips")`);
    if (idxSab === idx) throw new Error("sabotage F1s: the edit did not apply");
    expect("F1s", await runAll({ ...real(), indexSrc: idxSab }), [`P-F1: route "zzz"`]);
    // F3s: the firmware's base changed in a copy of OtaUpdater.cpp.
    const ota = readFileSync(OTA, "utf8");
    const otaSab = ota.replace("valar-scopes/releases", "valar-scopez/releases");
    if (otaSab === ota) throw new Error("sabotage F3s: the edit did not apply");
    expect("F3s", await runAll({ ...real(), otaSrc: otaSab }), ["P-F3: release URL differs"]);
    // F2s: the waitsFor test dropped in a copy of gate.ts.
    const g2 = sabotagedCopy(dir, GATE, "gate-f2s.ts", (s) => s.replace("/*P-F2*/", "/*P-F2*/ return true;"));
    expect("F2s", await runAll({ ...real(), gatePath: g2 }), [
      "P-F2 waitsFor 20 at null", "P-F2 waitsFor 20 at 19", "P-F2 waitsFor 20 at NaN",
      "P-F2 unscheduled at null", "P-F2 unscheduled at 19", "P-F2 unscheduled at 20", "P-F2 unscheduled at 21", "P-F2 unscheduled at NaN",
    ]);
    // T2s: an unknown context defaulted to "plus" in a copy of gate.ts.
    const g3 = sabotagedCopy(dir, GATE, "gate-t2s.ts", (s) => s.replace("/*P-T2*/", "/*P-T2*/ if (ctxTier !== \"cloud\") ctxTier = \"plus\";"));
    expect("T2s", await runAll({ ...real(), gatePath: g3 }), [
      "P-T2 ctx undefined row free", "P-T2 ctx undefined row cloud", "P-T2 ctx undefined row plus",
      "P-T2 ctx Plus row free", "P-T2 ctx Plus row cloud", "P-T2 ctx Plus row plus",
    ]);
    // P3s: a parser that accepts a trailing letter, in a copy of release.ts.
    const r3 = sabotagedCopy(dir, RELEASE, "release-p3s.ts", (s) => s.replace("/*P-F3*/", "/*P-F3*/ text = text.replace(/[a-z]$/, \"\");"));
    expect("P3s", await runAll({ ...real(), releasePath: r3 }), [`P-F3 parse "20x"`]);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
  console.log(bad ? `SELFTEST FAILED (${bad} not as predicted)` : "SELFTEST PASSED");
  return bad ? 1 : 0;
}

async function main(): Promise<number> {
  if (process.argv.includes("--selftest")) return selftest();
  const r = await runAll(real());
  r.lines.forEach((l) => console.log(l));
  r.failures.forEach((f) => console.log(`FAIL ${f}`));
  const routes = routesFromIndex(readFileSync(INDEX, "utf8")).length;
  console.log(r.verdict === "PASS"
    ? `PASS (P-F1 ${routes} routes; P-F3 URL + ${PARSE_CASES.length} parser cases; P-F2 ${FW_ROWS.length * READS.length}; P-T2 ${CTXS.length * TIERS.length}; memo 3)`
    : r.verdict);
  return r.verdict === "PASS" ? 0 : r.verdict === "BLIND" ? 2 : 1;
}

process.exitCode = await main();
