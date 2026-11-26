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
//   P-O1  The owner's "screen service logs: sample 10%" (2026-11-25), read from screen.wrangler.toml: production
//         observability enabled at exactly 0.1, invocation logs off in every env, and no env above 0.1 but the exempt.
//   P-O2  every env with observability stamps a LOGS_RATE define equal to its head_sampling_rate, so /health's
//         logs.rate cannot misreport the rate the env actually samples at.
//   P-O3  the full-logs day exists: [env.production-fulllogs] observability at exactly 1.0 (docs/screen-logs-runbook.md).
//   P-O4  the unsampled counter has somewhere to write: staging, production and production-fulllogs each bind
//         SCREEN_METRICS once, to its dataset (production-fulllogs shares production's).
//   P-W   the watch is on exactly when there is something to watch: if production carries a route or a custom domain,
//         .github/workflows/screen-logs-watch.yml must carry a schedule trigger; with no route, it must carry none.
//   (P-E3, the military table's equality with the firmware's, is RETIRED with the table: the owner's 2026-11-25 rule
//   hides by LADD/PIA flags alone, so no military table is consulted -- registration A10.)
//   P-K2  (phase 1, enrich) the screen runs the boards' handleEnrich, so it must run it with the boards' upstream vars:
//         every var the enrich path reads -- `env.NAME` in src/enrich.ts and every file it imports, walked from the
//         source, not listed here -- that proxy/wrangler.toml sets for staging or production, screen.wrangler.toml sets
//         to the SAME value in the same env, and production-fulllogs to production's.
//   P-K3  (phase 1, enrich) the boards' ENRICH_KV binding is WRITABLE; the screen may hold it only through the
//         read-only view (src/screen/rokv.ts). In src/screen/**, comments stripped, every line holding ENRICH_KV
//         must be one of four CONTENT patterns in details.ts (the type field, the unbound-throw, the readOnlyKv( wrap,
//         the enrichEnv key that replaces it), each exactly once. P-K1's tests see only the paths they exercise; this
//         sees a second path around the wrapper wherever it is written. No line number is used.
//   P-NS1 (logs-nosubject) every console call in the screen's bundle -- src/screen/worker.ts and every .ts it imports
//         at run time, walked as P-K2 walks -- is DECIDED in src/screen/logallow.ts: a JSON line whose evt it allows or
//         drops, or a text line whose first word it drops. A new console line anywhere on that path (the boards' enrich
//         path included) fails here until someone decides it, rather than passing or vanishing unremarked.
//   P-NS2 (logs-nosubject) no screen env configures observability TRACES: a trace records subrequest URLs, and the
//         enrich path's carry the hex and the callsign. The premise the log guard rests on, asserted where it lives.
//
//   npx tsx scripts/check-screen.ts              exit 0 PASS, 1 FAIL, 2 BLIND (an input the check cannot read)
//   npx tsx scripts/check-screen.ts --selftest   the real check must PASS; then each sabotage, on a TEMP COPY that
//        is asserted to differ from its source, must FAIL exactly as predicted (F1s, F3s, F2s, T2s, P3s, O1s, O2s, O4s,
//        Ws, K2s, K3r, K3s, NS1s, NS2s).
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const PROXY = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PROXY, "..");
const INDEX = join(PROXY, "src", "index.ts");
const OTA = join(REPO, "src", "OtaUpdater.cpp");
const GATE = join(PROXY, "src", "screen", "gate.ts");
const RELEASE = join(PROXY, "src", "screen", "release.ts");
const TOML = join(PROXY, "screen.wrangler.toml");
const WATCH = join(REPO, ".github", "workflows", "screen-logs-watch.yml");
const BOARDS_TOML = join(PROXY, "wrangler.toml");
const ENRICH = join(PROXY, "src", "enrich.ts");
const SCREEN_DIR = join(PROXY, "src", "screen");
const SCREEN_ENTRY = join(SCREEN_DIR, "worker.ts");
const LOGALLOW = join(SCREEN_DIR, "logallow.ts");

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
interface LogAllowMod { LOG_ALLOW: Readonly<Record<string, unknown>>; LOG_DROP: { json: readonly string[]; text: readonly string[] } }

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

/**
 * P-O1, read from screen.wrangler.toml as TEXT, section by section: (a) production observability enabled;
 * (b) production head_sampling_rate exactly 0.1; (c) invocation_logs = false in every env with an observability
 * section; (d) no env above 0.1 except staging, production (pinned by b) and production-fulllogs (the full-logs day,
 * docs/screen-logs-runbook.md).
 */
const RATE_EXEMPT = new Set(["staging", "production", "production-fulllogs"]);
interface ObsEnv { enabled?: string; rate?: string; invocationLogs?: string }
function observabilityEnvs(tomlSrc: string): Map<string, ObsEnv> {
  const envs = new Map<string, ObsEnv>();
  let cur: { env: string; part: "obs" | "logs" } | null = null;
  for (const raw of tomlSrc.split("\n")) {
    const line = raw.replace(/#.*$/, "").trim();
    const head = /^\[(?:env\.([a-z0-9-]+)\.)?observability(\.logs)?\]$/.exec(line);
    if (head) {
      cur = { env: head[1] ?? "top", part: head[2] ? "logs" : "obs" };
      if (!envs.has(cur.env)) envs.set(cur.env, {});
      continue;
    }
    if (line.startsWith("[")) { cur = null; continue; }
    const kv = /^([a-z_]+)\s*=\s*(.+)$/.exec(line);
    if (!cur || !kv) continue;
    const e = envs.get(cur.env)!;
    if (cur.part === "obs" && kv[1] === "enabled") e.enabled = kv[2];
    if (cur.part === "obs" && kv[1] === "head_sampling_rate") e.rate = kv[2];
    if (cur.part === "logs" && kv[1] === "invocation_logs") e.invocationLogs = kv[2];
  }
  return envs;
}
function checkObservability(tomlSrc: string): Result {
  const envs = observabilityEnvs(tomlSrc);
  const prod = envs.get("production");
  if (!prod) return { verdict: "BLIND", lines: ["P-O1: no [env.production.observability] section read"], failures: ["P-O1 blind"] };
  const failures: string[] = [];
  if (prod.enabled !== "true") failures.push("P-O1: production observability is not enabled");
  if (Number(prod.rate) !== 0.1) failures.push(`P-O1: production head_sampling_rate is ${Number(prod.rate)}, want 0.1`);
  for (const [env, e] of envs) {
    if (e.invocationLogs !== "false") failures.push(`P-O1: ${env} does not set invocation_logs = false`);
    if (!RATE_EXEMPT.has(env) && Number(e.rate ?? "0") > 0.1) failures.push(`P-O1: ${env} head_sampling_rate is ${Number(e.rate)}, above 0.1`);
  }
  const off = [...envs.values()].filter((e) => e.invocationLogs === "false").length;
  return {
    verdict: failures.length ? "FAIL" : "PASS",
    lines: [`P-O1: production sampling ${Number(prod.rate)}, enabled ${prod.enabled}; invocation logs off in ${off} of ${envs.size} env(s)`],
    failures,
  };
}

/** `[env.<name>.define]` KEY = "<value>": the value's text per env (the JS expression wrangler substitutes). */
function defineValues(tomlSrc: string, key: string): Map<string, string> {
  const out = new Map<string, string>();
  let env: string | null = null;
  for (const raw of tomlSrc.split("\n")) {
    const line = raw.replace(/#.*$/, "").trim();
    const head = /^\[env\.([a-z0-9-]+)\.define\]$/.exec(line);
    if (head) { env = head[1]; continue; }
    if (line.startsWith("[")) { env = null; continue; }
    const kv = env ? new RegExp(`^${key}\\s*=\\s*"(.*)"$`).exec(line) : null;
    if (env && kv) out.set(env, kv[1]);
  }
  return out;
}

/** P-O2 and P-O3: each env's stamped LOGS_RATE equals what it samples at, and the full-logs day exists at 1.0. */
function checkRates(tomlSrc: string): Result {
  const envs = observabilityEnvs(tomlSrc);
  const rates = defineValues(tomlSrc, "LOGS_RATE");
  const failures: string[] = [];
  let matched = 0;
  for (const [env, e] of envs) {
    if (env === "top") continue;
    const d = rates.get(env);
    if (d === undefined) { failures.push(`P-O2: ${env} has no LOGS_RATE define`); continue; }
    if (Number(d) !== Number(e.rate)) failures.push(`P-O2: ${env} LOGS_RATE is ${Number(d)}, head_sampling_rate is ${Number(e.rate)}`);
    else matched++;
  }
  const fl = envs.get("production-fulllogs");
  if (!fl) failures.push("P-O3: no [env.production-fulllogs] observability section");
  else if (Number(fl.rate) !== 1) failures.push(`P-O3: production-fulllogs head_sampling_rate is ${Number(fl.rate)}, want 1`);
  return {
    verdict: failures.length ? "FAIL" : "PASS",
    lines: [`P-O2: LOGS_RATE equals the sampled rate in ${matched} env(s); P-O3: production-fulllogs ${fl ? `at ${Number(fl.rate)}` : "absent"}`],
    failures,
  };
}

/**
 * P-O4: the unsampled counter has somewhere to write. metrics.ts writes only `if (env.SCREEN_METRICS)`, so an env without
 * the binding deploys the write site (P-M2 passes) and counts nothing. Each env in METRICS_DATASETS that exists binds
 * SCREEN_METRICS exactly once, to its dataset; production-fulllogs shares production's. An absent env is P-O3's to name.
 */
const METRICS_DATASETS: Record<string, string> = { staging: "blipscope_screen_staging", production: "blipscope_screen", "production-fulllogs": "blipscope_screen" };
function metricsBindings(tomlSrc: string): Map<string, Array<{ binding?: string; dataset?: string }>> {
  const out = new Map<string, Array<{ binding?: string; dataset?: string }>>();
  let cur: { binding?: string; dataset?: string } | null = null;
  for (const raw of tomlSrc.split("\n")) {
    const line = raw.replace(/#.*$/, "").trim();
    const head = /^\[\[env\.([a-z0-9-]+)\.analytics_engine_datasets\]\]$/.exec(line);
    if (head) {
      cur = {};
      out.set(head[1], [...(out.get(head[1]) ?? []), cur]);
      continue;
    }
    if (line.startsWith("[")) { cur = null; continue; }
    const kv = /^(binding|dataset)\s*=\s*"([^"]*)"$/.exec(line);
    if (cur && kv) cur[kv[1] as "binding" | "dataset"] = kv[2];
  }
  return out;
}
function checkMetrics(tomlSrc: string): Result {
  const envs = observabilityEnvs(tomlSrc);
  const bound = metricsBindings(tomlSrc);
  const failures: string[] = [];
  let present = 0, good = 0;
  for (const [env, want] of Object.entries(METRICS_DATASETS)) {
    if (!envs.has(env)) continue;
    present++;
    const b = (bound.get(env) ?? []).filter((x) => x.binding === "SCREEN_METRICS");
    if (b.length === 0) failures.push(`P-O4: ${env} has no SCREEN_METRICS binding`);
    else if (b.length > 1) failures.push(`P-O4: ${env} has ${b.length} SCREEN_METRICS bindings`);
    else if (b[0].dataset !== want) failures.push(`P-O4: ${env} SCREEN_METRICS writes to ${b[0].dataset ?? "(no dataset)"}, want ${want}`);
    else good++;
  }
  return { verdict: failures.length ? "FAIL" : "PASS", lines: [`P-O4: SCREEN_METRICS bound in ${good} of ${present} env(s)`], failures };
}

/** Does production carry a route or a custom domain? A [[env.production.routes]] table, or a routes/route key in [env.production]. */
function productionHasRoute(tomlSrc: string): boolean {
  let inProd = false;
  for (const raw of tomlSrc.split("\n")) {
    const line = raw.replace(/#.*$/, "").trim();
    if (/^\[\[env\.production\.routes?\]\]$/.test(line)) return true;
    if (line.startsWith("[")) { inProd = line === "[env.production]"; continue; }
    if (inProd && /^(routes|route)\s*=/.test(line)) return true;
  }
  return false;
}

/** P-W: the watch is scheduled exactly when production has a route to watch. */
function checkWatch(tomlSrc: string, workflowSrc: string | null): Result {
  if (workflowSrc === null) return { verdict: "FAIL", lines: [], failures: ["P-W: .github/workflows/screen-logs-watch.yml is absent"] };
  const route = productionHasRoute(tomlSrc);
  const schedule = workflowSrc.split("\n").some((l) => /^\s*schedule\s*:/.test(l.replace(/#.*$/, "")));
  const failures: string[] = [];
  if (route && !schedule) failures.push("P-W: production carries a route but screen-logs-watch.yml has no schedule");
  if (!route && schedule) failures.push("P-W: screen-logs-watch.yml has a schedule but production carries no route");
  return { verdict: failures.length ? "FAIL" : "PASS", lines: [`P-W: production route ${route ? "yes" : "no"}, watch schedule ${schedule ? "yes" : "no"}`], failures };
}


/**
 * P-K2. The enrich path's files: src/enrich.ts and every .ts it imports by a relative path at RUN time, transitively.
 * An `import type` / `export type` statement is skipped: it loads nothing, and following it would pull in files the
 * path never executes (types.ts alone reaches the basemap and weather modules).
 */
const IMPORT_FROM = /(?:^|\n)[ \t]*(?:import|export)[ \t]+(type[ \t]+)?[^;]*?from[ \t]+"(\.{1,2}\/[^"]+)"/g;
export function enrichPathFiles(entry: string): string[] {
  const seen = new Set<string>();
  const stack = [entry];
  while (stack.length) {
    const f = stack.pop()!;
    if (seen.has(f)) continue;
    seen.add(f);
    for (const m of readFileSync(f, "utf8").matchAll(IMPORT_FROM)) {
      if (m[1]) continue;
      const base = resolve(dirname(f), m[2]);
      const hit = [`${base}.ts`, join(base, "index.ts"), base].find((p) => p.endsWith(".ts") && existsSync(p));
      if (hit) stack.push(hit);
    }
  }
  return [...seen].sort();
}
/** `[env.<env>.vars]` NAME = value: the value's TEXT (quotes kept), comments dropped. */
function varsOf(tomlSrc: string, env: string): Map<string, string> {
  const out = new Map<string, string>();
  let inside = false;
  for (const raw of tomlSrc.split("\n")) {
    const line = raw.trim();
    if (line.startsWith("[")) { inside = line === `[env.${env}.vars]`; continue; }
    const kv = inside ? /^([A-Z][A-Z0-9_]*)\s*=\s*("(?:[^"\\]|\\.)*"|[^#\s]+)/.exec(line) : null;
    if (kv) out.set(kv[1], kv[2]);
  }
  return out;
}
const VAR_ENVS = ["staging", "production"] as const;
function checkVars(screenToml: string, boardsToml: string, files: string[]): Result {
  const read = new Set<string>();
  for (const f of files) for (const m of readFileSync(f, "utf8").matchAll(/env\.([A-Z][A-Z0-9_]*)/g)) read.add(m[1]);
  const vars = [...read].filter((n) => VAR_ENVS.some((e) => varsOf(boardsToml, e).has(n))).sort();
  if (vars.length === 0) return { verdict: "BLIND", lines: ["P-K2: no var of the enrich path read from proxy/wrangler.toml"], failures: ["P-K2 blind"] };
  const failures: string[] = [];
  for (const e of VAR_ENVS) {
    const b = varsOf(boardsToml, e), s = varsOf(screenToml, e);
    for (const n of vars) {
      if (b.get(n) === s.get(n)) continue;
      failures.push(`P-K2: ${e} ${n} ${!s.has(n) ? "is missing (the boards set it)" : !b.has(n) ? "is set (the boards do not set it)" : "differs from the boards'"}`);
    }
  }
  const prod = varsOf(screenToml, "production"), full = varsOf(screenToml, "production-fulllogs");
  for (const n of vars) if (prod.get(n) !== full.get(n)) failures.push(`P-K2: production-fulllogs ${n} differs from production's`);
  return {
    verdict: failures.length ? "FAIL" : "PASS",
    lines: [`P-K2: ${vars.length} vars read by the enrich path (${files.length} files from src/enrich.ts); staging and production ${failures.length ? "DIFFER" : "equal the boards'"}`],
    failures,
  };
}

/** P-K3. The four places ENRICH_KV may be named, by CONTENT, all in details.ts. */
const KV_ALLOWED: ReadonlyArray<{ name: string; re: RegExp }> = [
  { name: "the CoreEnv type field", re: /^\s*ENRICH_KV\?: KVNamespace;\s*$/ },
  { name: "the unbound-throw", re: /^\s*if \(!env\.ENRICH_KV\) throw\b/ },
  { name: "the readOnlyKv( wrap", re: /readOnlyKv\(env\.ENRICH_KV\)/ },
  { name: "the enrichEnv key", re: /\bENRICH_KV: this\.ro\.kv\b/ },
];
/** Comments blanked with the line count kept. A line comment starts at `//` after a separator or the line's start,
 *  so a URL's "://" is not one. */
function stripComments(src: string): string {
  return src
    .replace(/\/\*[\s\S]*?\*\//g, (m) => m.replace(/[^\n]/g, " "))
    .replace(/(^|[\s;{}(),])\/\/.*$/gm, "$1");
}
function checkEnrichKv(screenSrcs: ReadonlyMap<string, string>): Result {
  const hits: Array<{ file: string; line: number; text: string }> = [];
  for (const [file, src] of screenSrcs) {
    stripComments(src).split("\n").forEach((text, i) => { if (/\bENRICH_KV\b/.test(text)) hits.push({ file, line: i + 1, text }); });
  }
  const failures: string[] = [];
  const found = KV_ALLOWED.map(() => 0);
  for (const h of hits) {
    const k = h.file === "details.ts" ? KV_ALLOWED.findIndex((a) => a.re.test(h.text)) : -1;
    if (k < 0) failures.push(`P-K3: src/screen/${h.file}:${h.line} holds ENRICH_KV outside the read-only wrap`);
    else found[k]++;
  }
  KV_ALLOWED.forEach((a, k) => { if (found[k] !== 1) failures.push(`P-K3: ${a.name} found ${found[k]} time(s) in details.ts, want 1`); });
  const files = new Set(hits.map((h) => h.file));
  return {
    verdict: failures.length ? "FAIL" : "PASS",
    lines: [failures.length
      ? `P-K3: ENRICH_KV on ${hits.length} line(s) in ${[...files].join(", ") || "no file"}: FAIL`
      : `P-K3: ENRICH_KV on ${hits.length} line(s), all in details.ts, each an allowed pattern exactly once`],
    failures,
  };
}
/** Every .ts under src/screen, by its path relative to that folder ("/"-separated). */
function screenSources(): Map<string, string> {
  const out = new Map<string, string>();
  const walk = (dir: string, rel: string) => {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      if (e.isDirectory()) walk(join(dir, e.name), `${rel}${e.name}/`);
      else if (e.name.endsWith(".ts")) out.set(`${rel}${e.name}`, readFileSync(join(dir, e.name), "utf8"));
    }
  };
  walk(SCREEN_DIR, "");
  return out;
}

/** P-NS1. Every console call in a source, comments blanked: a text line by its first word, a JSON line by its evt.
 *  The argument is read to the statement's first `;` (no producer on the path has one inside its argument). */
const CONSOLE_CALL = /\bconsole\.(?:log|info|warn|error|debug)\s*\(/g;
export function consoleCalls(src: string): Array<{ line: number; kind: "json" | "text" | "unread"; name: string }> {
  const code = stripComments(src);
  const out: Array<{ line: number; kind: "json" | "text" | "unread"; name: string }> = [];
  for (const m of code.matchAll(CONSOLE_CALL)) {
    const start = (m.index ?? 0) + m[0].length;
    const semi = code.indexOf(";", start);
    const arg = code.slice(start, semi < 0 ? start + 600 : Math.min(semi, start + 600));
    const line = code.slice(0, m.index).split("\n").length;
    const text = /^\s*[`'"]([a-z_]+)\b/.exec(arg);
    const json = /\bevt:\s*"([a-z_]+)"/.exec(arg);
    out.push(text ? { line, kind: "text", name: text[1] } : json ? { line, kind: "json", name: json[1] } : { line, kind: "unread", name: "" });
  }
  return out;
}
function checkConsole(bundle: ReadonlyMap<string, string>, la: LogAllowMod): Result {
  const failures: string[] = [];
  let n = 0;
  let allowed = 0;
  let dropped = 0;
  for (const [rel, src] of bundle) {
    for (const c of consoleCalls(src)) {
      n++;
      /*P-NS1*/
      if (c.kind === "json" && Object.hasOwn(la.LOG_ALLOW, c.name)) allowed++;
      else if (c.kind === "json" && la.LOG_DROP.json.includes(c.name)) dropped++;
      else if (c.kind === "text" && la.LOG_DROP.text.includes(c.name)) dropped++;
      else if (c.kind === "unread") failures.push(`P-NS1: ${rel}:${c.line} a console call with no evt or first word this check can read`);
      else failures.push(`P-NS1: ${rel}:${c.line} logs ${c.kind} "${c.name}", which logallow.ts neither allows nor drops`);
    }
  }
  if (n === 0) return { verdict: "BLIND", lines: ["P-NS1: no console call found in the screen's bundle"], failures: ["P-NS1 blind"] };
  return {
    verdict: failures.length ? "FAIL" : "PASS",
    lines: [`P-NS1: ${n} console calls in ${bundle.size} files of the screen's bundle (${allowed} allowed, ${dropped} dropped)${failures.length ? "" : "; every one decided"}`],
    failures,
  };
}
/** P-NS2. A traces table, or a traces key, anywhere in screen.wrangler.toml (a comment line does not start so). */
function checkTraces(tomlSrc: string): Result {
  /*P-NS2*/
  const hits = [...tomlSrc.matchAll(/^[ \t]*\[([^\]\n]*\bobservability\.traces)\]|^[ \t]*(traces)[ \t]*=/gm)].map((m) => m[1] ?? m[2]);
  const failures = hits.map((h) => `P-NS2: screen.wrangler.toml configures traces (${h})`);
  return {
    verdict: failures.length ? "FAIL" : "PASS",
    lines: [failures.length
      ? `P-NS2: traces configured ${hits.length} time(s)`
      : "P-NS2: no env configures observability traces (their subrequest URLs would carry the hex)"],
    failures,
  };
}
/** The screen's bundle, as P-NS1 reads it: worker.ts and its run-time imports, keyed by their path from proxy/. */
function bundleSources(): Map<string, string> {
  return new Map(enrichPathFiles(SCREEN_ENTRY).map((f) => [relative(PROXY, f).split("\\").join("/"), readFileSync(f, "utf8")]));
}

interface Inputs {
  indexSrc: string; otaSrc: string; gatePath: string; releasePath: string; tomlSrc: string; workflowSrc: string | null;
  boardsTomlSrc: string; screenSrcs: ReadonlyMap<string, string>; bundleSrcs: ReadonlyMap<string, string>;
}
async function runAll(inp: Inputs): Promise<Result> {
  const mapMod = await import(pathToFileURL(join(PROXY, "src", "screen", "map.ts")).href) as { SCREEN_MAP: readonly Row[]; RELEASE_URL: string };
  const gate = await import(pathToFileURL(inp.gatePath).href) as GateMod;
  const rel = await import(pathToFileURL(inp.releasePath).href) as ReleaseMod;
  const la = await import(pathToFileURL(LOGALLOW).href) as LogAllowMod;
  const parts = [
    checkParity(inp.indexSrc, mapMod.SCREEN_MAP),
    checkUrl(inp.otaSrc, mapMod.RELEASE_URL),
    checkParser(rel),
    checkReleaseGate(gate),
    checkTierGate(gate),
    await checkMemo(rel),
    checkObservability(inp.tomlSrc),
    checkRates(inp.tomlSrc),
    checkMetrics(inp.tomlSrc),
    checkWatch(inp.tomlSrc, inp.workflowSrc),
    checkVars(inp.tomlSrc, inp.boardsTomlSrc, enrichPathFiles(ENRICH)),
    checkEnrichKv(inp.screenSrcs),
    checkConsole(inp.bundleSrcs, la),
    checkTraces(inp.tomlSrc),
  ];
  const verdict = parts.some((p) => p.verdict === "BLIND") ? "BLIND" : parts.some((p) => p.verdict === "FAIL") ? "FAIL" : "PASS";
  return { verdict, lines: parts.flatMap((p) => p.lines), failures: parts.flatMap((p) => p.failures) };
}

const real = (): Inputs => ({
  indexSrc: readFileSync(INDEX, "utf8"), otaSrc: readFileSync(OTA, "utf8"), gatePath: GATE, releasePath: RELEASE,
  tomlSrc: readFileSync(TOML, "utf8"), workflowSrc: existsSync(WATCH) ? readFileSync(WATCH, "utf8") : null,
  boardsTomlSrc: readFileSync(BOARDS_TOML, "utf8"), screenSrcs: screenSources(), bundleSrcs: bundleSources(),
});

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
    r.failures.forEach((f) => console.log(`  ${f}`)); // always: a sabotage's red is read by what it names, not its count
    if (!ok) bad++;
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
    // O1s: production's sampling put back to 1.0 in a copy of screen.wrangler.toml -- its LOGS_RATE define moved with it,
    // so P-O2 still holds and only P-O1 (the owner's 0.1) can see it.
    const toml = readFileSync(TOML, "utf8");
    const setProdDefine = (s: string, v: string) => s.replace(/(\[env\.production\.define\]\n(?:[^[\n].*\n)*?LOGS_RATE = ")[^"]*"/, `$1${v}"`);
    const tomlSab = setProdDefine(toml.replace(/(\[env\.production\.observability\]\n(?:[^[\n].*\n)*?head_sampling_rate = )[0-9.]+/, "$11.0"), "1.0");
    if (tomlSab === toml) throw new Error("sabotage O1s: the edit did not apply");
    expect("O1s", await runAll({ ...real(), tomlSrc: tomlSab }), ["P-O1: production head_sampling_rate is 1, want 0.1"]);
    // O2s: production's LOGS_RATE define alone moved to 1.0 -- /health would report 1.0 while the env samples 0.1.
    const tomlO2 = setProdDefine(toml, "1.0");
    if (tomlO2 === toml) throw new Error("sabotage O2s: the edit did not apply");
    expect("O2s", await runAll({ ...real(), tomlSrc: tomlO2 }), ["P-O2: production LOGS_RATE is 1, head_sampling_rate is 0.1"]);
    // O4s: production's counter binding removed -- the write site still deploys, and counts nothing.
    const prodBinding = "[[env.production.analytics_engine_datasets]]\nbinding = \"SCREEN_METRICS\"\ndataset = \"blipscope_screen\"\n";
    if (toml.split(prodBinding).length !== 2) throw new Error("sabotage O4s: production's binding table is not there exactly once");
    expect("O4s", await runAll({ ...real(), tomlSrc: toml.replace(prodBinding, "") }), ["P-O4: production has no SCREEN_METRICS binding"]);
    // Ws: a production custom domain with the watch still unscheduled -- the domain PR landing without the watch.
    const tomlW = toml.replace("[env.production]\n", "[env.production]\nroute = { pattern = \"screen.example.test\", custom_domain = true }\n");
    if (tomlW === toml) throw new Error("sabotage Ws: the edit did not apply");
    expect("Ws", await runAll({ ...real(), tomlSrc: tomlW }), ["P-W: production carries a route but screen-logs-watch.yml has no schedule"]);
    // K2s: production's relay base changed in a copy of screen.wrangler.toml -- production-fulllogs still mirrors the old.
    const tomlK2 = toml.replace(/(\[env\.production\.vars\]\n(?:[^[\n].*\n)*?UPSTREAM_ADSB_LOL_BASE = ")[^"]*"/, "$1https://relay.example.test\"");
    if (tomlK2 === toml) throw new Error("sabotage K2s: the edit did not apply");
    expect("K2s", await runAll({ ...real(), tomlSrc: tomlK2 }), [
      "P-K2: production UPSTREAM_ADSB_LOL_BASE differs from the boards'",
      "P-K2: production-fulllogs UPSTREAM_ADSB_LOL_BASE differs from production's",
    ]);
    // K3r (P-K3's red): the wrap moved OUT of details.ts into a rokv.ts helper -- the wrap's pattern is then found 0
    // times, and the helper's line names the raw binding outside details.ts.
    const srcs = screenSources();
    const withEdit = (file: string, edit: (s: string) => string): Map<string, string> => {
      const m = new Map(srcs);
      const before = m.get(file);
      if (before === undefined) throw new Error(`sabotage: src/screen/${file} is not read`);
      const after = edit(before);
      if (after === before) throw new Error(`sabotage on src/screen/${file}: the edit did not apply`);
      m.set(file, after);
      return m;
    };
    const k3r = withEdit("details.ts", (s) => s.replace("readOnlyKv(env.ENRICH_KV)", "readOnlyKv(kvOf(env))"));
    k3r.set("rokv.ts", `${k3r.get("rokv.ts")}\nexport const kvOf = (env: { ENRICH_KV?: KVNamespace }) => env.ENRICH_KV!;\n`);
    expect("K3r", await runAll({ ...real(), screenSrcs: k3r }), [
      "P-K3: src/screen/rokv.ts:", "P-K3: the readOnlyKv( wrap found 0 time(s) in details.ts, want 1",
    ]);
    // K3s (the orchestrator's): a raw read planted in worker.ts -- a second path around the read-only view.
    const k3s = withEdit("worker.ts", (s) => `${s}\nexport const leak = (env: any) => env.ENRICH_KV.get("k");\n`);
    expect("K3s", await runAll({ ...real(), screenSrcs: k3s }), ["P-K3: src/screen/worker.ts:"]);
    // NS1s: a new console line on the boards' enrich path, carrying a hex -- undecided, so it must fail.
    const bundle = bundleSources();
    const enrichSrc = bundle.get("src/enrich.ts");
    if (enrichSrc === undefined) throw new Error("sabotage NS1s: src/enrich.ts is not in the screen's bundle");
    const ns1 = new Map(bundle);
    ns1.set("src/enrich.ts", `${enrichSrc}\nexport const leakLog = (hex: string) => console.log(JSON.stringify({ evt: "new_thing", hex }));\n`);
    expect("NS1s", await runAll({ ...real(), bundleSrcs: ns1 }), ["P-NS1: src/enrich.ts:"]);
    // NS2s: production traces switched on in a copy of screen.wrangler.toml.
    expect("NS2s", await runAll({ ...real(), tomlSrc: `${toml}\n[env.production.observability.traces]\nenabled = true\n` }), [
      "P-NS2: screen.wrangler.toml configures traces",
    ]);
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
    ? `PASS (P-F1 ${routes} routes; P-F3 URL + ${PARSE_CASES.length} parser cases; P-F2 ${FW_ROWS.length * READS.length}; P-T2 ${CTXS.length * TIERS.length}; memo 3; P-O1 ${observabilityEnvs(readFileSync(TOML, "utf8")).size} envs; P-O2; P-O3; P-O4 ${Object.keys(METRICS_DATASETS).length} envs; P-W; P-K2; P-K3; P-NS1; P-NS2)`
    : r.verdict);
  return r.verdict === "PASS" ? 0 : r.verdict === "BLIND" ? 2 : 1;
}

process.exitCode = await main();
