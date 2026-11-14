// routes-guards.selftest.mts -- GR1-GR7 and FZ1-FZ4 (docs/route-weekly-code.md, section 2), against SYNTHETIC sets
// only: no KV, no network, no real callsign beyond the public canaries. Each row is red-first: before
// routes-guards.ts exists, the import fails and every row reads red.
//
//   npx tsx scripts/routes-guards.selftest.mts        exit 0 every row as expected, 1 otherwise
import * as G from "./routes-guards";

let off = 0;
const row = (id: string, ok: boolean, detail: string) => {
  console.log(`${ok ? "PASS" : "FAIL"} ${id}: ${detail}`);
  if (!ok) off++;
};

// A synthetic corpus, sized to the registered cases: 40 shards of 25 keys, plus A80 (80), A76 (76), A45 (45) and
// A400 (400). Keys are synthetic AAA0000-style callsigns.
const mk = (shard: string, n: number, from = 0) => new Set(Array.from({ length: n }, (_, i) => `rt:${shard}${String(i + from).padStart(4, "0")}`));
const prevByShard = new Map<string, Set<string>>();
for (let s = 0; s < 40; s++) prevByShard.set(`S${String(s).padStart(2, "0")}`, mk(`S${String(s).padStart(2, "0")}`, 25));
for (const [name, n] of [["A80", 80], ["A76", 76], ["A45", 45], ["A400", 400]] as const) prevByShard.set(name, mk(name, n));
const keysOf = (m: Map<string, Set<string>>) => new Set([...m.values()].flatMap((s) => [...s]));
const clone = (m: Map<string, Set<string>>) => new Map([...m].map(([k, v]) => [k, new Set(v)]));

// ---- G1, completeness --------------------------------------------------------------------------------------------
{
  // Real-sized counts (24ce5424's 628,956), so the ratio rule is what decides -- not the absolute band, which a
  // toy-sized count would trip for the wrong reason. GR1 asserts WHICH rule refused.
  const prev = 628_956;
  const r = G.g1Completeness(prev, Math.floor(prev * 0.989)); // 98.9%
  row("GR1", !r.ok && r.refuseAll === true && r.reason.includes("% of the previous"),
    `98.9% of prev -> ${r.ok ? "passes" : "refuses"}${r.refuseAll ? " ALL" : ""} (${r.reason.includes("% of the previous") ? "by the ratio" : "NOT by the ratio"})`);
  const c = G.g1Completeness(prev, Math.ceil(prev * 0.991));
  row("GR1c", c.ok, `99.1% of prev -> ${c.ok ? "passes" : "refuses"}`);
  const t = G.g1Completeness(prev, 314_000); // a truncated source: half the corpus
  row("GR2", !t.ok && t.refuseAll === true && t.reason.includes("outside"),
    `a truncated source -> ${t.ok ? "passes" : "refuses"}${t.refuseAll ? " ALL, writes included" : ""} (${t.reason.includes("outside") ? "by the band" : "NOT by the band"})`);
}

// ---- G2, anchors ---------------------------------------------------------------------------------------------------
{
  const prevKeys = keysOf(prevByShard);
  const anchors = [...prevKeys].slice(0, 50);
  const newMissing = new Set(prevKeys); newMissing.delete(anchors[7] as string);
  const r = G.g2Anchors(anchors, prevKeys, newMissing);
  row("GR3", !r.ok, `1 anchor missing from the new source -> ${r.ok ? "passes" : "refuses deletes"}`);
  const c = G.g2Anchors(anchors, prevKeys, new Set(prevKeys));
  row("GR3c", c.ok, `all 50 present in both -> ${c.ok ? "passes" : "refuses"}`);
  const bad = G.g2Anchors([...anchors.slice(0, 49), "rt:NOTPREV1"], prevKeys, new Set(prevKeys));
  row("GR3p", !bad.ok, `an anchor absent from PREV (the list's own control) -> ${bad.ok ? "passes" : "refuses"}`);
}

// ---- G3, a surviving shard's share (registered: refuse past 25% AND past 20 keys) ------------------------------------
{
  const n1 = clone(prevByShard); n1.set("A80", mk("A80", 59, 21)); // loses 21 of 80: 26.25%, 21 keys
  const r = G.g3SurvivingShare(prevByShard, n1, new Set());
  row("GR4", !r.ok, `a surviving shard loses 26% and 21 keys -> ${r.ok ? "passes" : "refuses deletes"}`);
  const n2 = clone(prevByShard); n2.set("A76", mk("A76", 56, 20)); // loses 20 of 76: 26.3%, 20 keys
  const c1 = G.g3SurvivingShare(prevByShard, n2, new Set());
  row("GR4c1", c1.ok, `26% but 20 keys -> ${c1.ok ? "passes" : "refuses"}`);
  const n3 = clone(prevByShard); n3.set("A400", mk("A400", 300, 100)); // loses 100 of 400: exactly 25%
  const c2 = G.g3SurvivingShare(prevByShard, n3, new Set());
  row("GR4c2", c2.ok, `25% and 100 keys -> ${c2.ok ? "passes" : "refuses"}`);
  const o = G.g3SurvivingShare(prevByShard, n1, new Set(["A80"]));
  row("GR4o", o.ok, `26% and 21 keys with that shard named in --allow-shard -> ${o.ok ? "passes" : "refuses"}`);
}

// ---- G4, removed shards (registered: refuse past 3 shards OR past 200 keys) ----------------------------------------
{
  // D8 (section 2j): the allowance is a SET of shard names. Each row also asserts WHICH rule decided (the GR1 lesson).
  const BOUND = "(bounds 3 and 200)";
  const n4 = clone(prevByShard); for (const s of ["S01", "S02", "S03", "S04"]) n4.delete(s);
  const r = G.g4Removed(prevByShard, n4, new Set());
  row("GR5a", !r.ok && r.reason.includes(BOUND), `4 shards removed -> ${r.ok ? "passes" : "refuses deletes"}: ${r.reason}`);
  const n5 = clone(prevByShard); for (const s of ["A80", "A76", "A45"]) n5.delete(s); // 3 shards, 201 keys
  const r2 = G.g4Removed(prevByShard, n5, new Set());
  row("GR5b", !r2.ok && r2.reason.includes(BOUND), `3 shards removed holding 201 keys -> ${r2.ok ? "passes" : "refuses deletes"}: ${r2.reason}`);
  const n6 = clone(prevByShard); for (const s of ["S05", "S06", "S07"]) n6.delete(s); // 3 shards, 75 keys
  const c = G.g4Removed(prevByShard, n6, new Set());
  row("GR5c", c.ok, `3 shards removed holding 75 keys -> ${c.ok ? "passes" : "refuses"}: ${c.reason}`);
  // ---- G4n, G4n-ok, G4n-stale (D8): by name, never by count ----
  const g4n = G.g4Removed(prevByShard, n4, new Set(["S01", "S02", "S03"]));
  // N3 (2q): G4n's reason check, ONE comparator shared with its near-miss control G4nx ("1 removed" is a fragment of
  // "11 removed").
  // 2r item 9: the WHOLE clause, as one "; "-separated part of the reason, never a fragment of one
  const g4nReasonOk = (reason: string) => reason.split("; ").some((part) => part === "1 removed shard(s) not named");
  row("G4n", !g4n.ok && g4nReasonOk(g4n.reason), `4 removed, 3 named -> ${g4n.ok ? "passes" : "refuses"}: ${g4n.reason}`);
  const nearG4n = "G4: 12 shard(s) removed, holding 300 keys (bounds 3 and 200); 11 removed shard(s) not named";
  row("G4nx", !g4nReasonOk(nearG4n), `G4n's comparator given "11 removed shard(s) not named" -> ${g4nReasonOk(nearG4n) ? "ACCEPTS it" : "rejects it"}`);
  const g4ok = G.g4Removed(prevByShard, n4, new Set(["S01", "S02", "S03", "S04"]));
  row("G4n-ok", g4ok.ok, `4 removed, all 4 named -> ${g4ok.ok ? "passes" : "refuses"}: ${g4ok.reason}`);
  const g4st = G.g4Removed(prevByShard, n4, new Set(["S01", "S02", "S03", "S04", "S05"]));
  row("G4n-stale", !g4st.ok && g4st.reason.includes("named but not removed"), `4 removed, all named plus one not removed -> ${g4st.ok ? "passes" : "refuses"}: ${g4st.reason}`);
}

// ---- G5, the run cap -----------------------------------------------------------------------------------------------
{
  const r = G.g5Cap(251, { scheduled: true });
  row("GR6", !r.ok, `251 deletes, scheduled -> ${r.ok ? "passes" : "refuses"}`);
  const o = G.g5Cap(827, { scheduled: false, maxDeletes: 1000 });
  row("GR6o", o.ok, `827 deletes, --max-deletes 1000, attended -> ${o.ok ? "passes" : "refuses"}`);
  const s = G.g5Cap(827, { scheduled: true, maxDeletes: 1000 });
  row("GR6s", !s.ok, `827 deletes, --max-deletes 1000, scheduled -> ${s.ok ? "passes" : "refuses: no override on a schedule"}`);
  const c = G.g5Cap(250, { scheduled: true });
  row("GR6c", c.ok, `250 deletes, scheduled -> ${c.ok ? "passes" : "refuses"}`);
}

// ---- G6, key shape and provenance -------------------------------------------------------------------------------
{
  const prevKeys = keysOf(prevByShard);
  for (const [id, k] of [["GR7a", "rt:AAL1:2"], ["GR7b", "rt:ZZ12345S"], ["GR7c", "rt:NOTPREV1"]] as const) {
    const r = G.g6ShapeAndProvenance([k], prevKeys);
    row(id, !r.ok, `delete key ${k.startsWith("rt:ZZ") ? "rt:ZZ<sentinel>" : k} -> ${r.ok ? "passes" : "refuses"}`);
  }
  const c = G.g6ShapeAndProvenance([...prevKeys].slice(0, 5), prevKeys);
  row("GR7c0", c.ok, `5 keys from prev, in shape -> ${c.ok ? "passes" : "refuses"}`);
}

// ---- the freeze ----------------------------------------------------------------------------------------------------
{
  const now = "2026-11-20T06:00:00Z";
  const f = (v: string | undefined) => G.freeze(now, v).action;
  row("FZ1", f("2026-11-20T07:00:00Z") === "skip", `a future freeze -> ${f("2026-11-20T07:00:00Z")}`);
  row("FZ2", f("2026-11-20T05:00:00Z") === "run", `a past freeze -> ${f("2026-11-20T05:00:00Z")}`);
  row("FZ3", f(undefined) === "run" && f("") === "run", `unset (and empty) -> ${f(undefined)}, ${f("")}`);
  row("FZ4", f("next tuesday") === "fail" && f("2026-11-20") === "fail", `malformed -> ${f("next tuesday")}, ${f("2026-11-20")}`);
}

// ---- FK1, revised (section 2e): refuse the fake KV when a real credential is in reach ------------------------------
// Imported dynamically, so before routes-fk1.ts exists these rows read red on their own and W1's rows still run.
{
  const mod = await import("./routes-fk1").catch(() => null);
  const fk1 = mod?.fk1Refusal as undefined | ((env: Record<string, string | undefined>, target: string | undefined, exists: (p: string) => boolean) => string | null);
  const none = (p: string) => p === "/exists/tokens";
  const r = (env: Record<string, string | undefined>, target = "staging") => (fk1 ? fk1(env, target, none) : "MODULE MISSING");
  const workersLike = { GITHUB_ACTIONS: "true", GITHUB_WORKFLOW: "workers", GITHUB_WORKFLOW_REF: "o/r/.github/workflows/workers.yml@refs/pull/9/merge" };
  row("FK1a", fk1 !== undefined && r(workersLike) === null, `a workers-like environment -> ${r(workersLike)}`);
  row("FK1b", fk1 !== undefined && /routes-refresh/.test(r({ GITHUB_WORKFLOW: "routes-refresh" }) ?? ""), `GITHUB_WORKFLOW=routes-refresh -> ${r({ GITHUB_WORKFLOW: "routes-refresh" })}`);
  const ref = { GITHUB_WORKFLOW: "Route refresh", GITHUB_WORKFLOW_REF: "o/r/.github/workflows/routes-refresh.yml@refs/heads/main" };
  row("FK1b2", fk1 !== undefined && r(ref) !== null, `GITHUB_WORKFLOW_REF names routes-refresh.yml -> ${r(ref)}`);
  row("FK1c", fk1 !== undefined && r({}, "production") !== null, `--env production -> ${r({}, "production")}`);
  row("FK1d", fk1 !== undefined && /ROUTES_KV_TOKEN/.test(r({ ROUTES_KV_TOKEN: "x" }) ?? ""), `ROUTES_KV_TOKEN set -> ${r({ ROUTES_KV_TOKEN: "x" })}`);
  row("FK1e", fk1 !== undefined && /CLOUDFLARE_API_TOKEN/.test(r({ CLOUDFLARE_API_TOKEN: "x" }) ?? ""), `CLOUDFLARE_API_TOKEN set -> ${r({ CLOUDFLARE_API_TOKEN: "x" })}`);
  row("FK1f", fk1 !== undefined && r({ BLIPSCOPE_TOKEN_FILE: "/exists/tokens" }) !== null, `BLIPSCOPE_TOKEN_FILE naming a file that exists -> ${r({ BLIPSCOPE_TOKEN_FILE: "/exists/tokens" })}`);
  row("FK1g", fk1 !== undefined && r({ BLIPSCOPE_TOKEN_FILE: "/missing/tokens" }) === null, `(control) BLIPSCOPE_TOKEN_FILE naming a missing file -> ${r({ BLIPSCOPE_TOKEN_FILE: "/missing/tokens" })}`);
  const planted = { ROUTES_KV_TOKEN: "planted-value-kv-0001", CLOUDFLARE_API_TOKEN: "planted-value-cf-0002", BLIPSCOPE_TOKEN_FILE: "/exists/tokens" };
  const txt = r(planted) ?? "";
  row("FK1h", fk1 !== undefined && txt !== "" && !txt.includes("planted-value"), `a refusal carries no variable's value (${txt.includes("planted-value") ? "VALUE FOUND" : "none found"})`);
}

// ---- TK1 (section 2c): the KV token's own expiry, at 14 days, on a stubbed verify answer ---------------------------
{
  const mod = await import("./routes-token-check").catch(() => null);
  type V = { action: "ok" | "warn" | "fail"; line: string };
  const tv = mod?.tokenVerdict as undefined | ((now: Date, answer: unknown) => V);
  const now = new Date("2026-11-20T06:00:00Z");
  const ans = (expires_on?: string) => ({ success: true, errors: [], result: { id: "0000", status: "active", ...(expires_on ? { expires_on } : {}) } });
  const v = (a: unknown): V => (tv ? tv(now, a) : { action: "fail", line: "MODULE MISSING" });
  const a13 = v(ans("2026-12-03T06:00:00Z")), a15 = v(ans("2026-12-05T06:00:00Z")), none = v(ans()), bad = v({ success: false, errors: [{ code: 1000 }] });
  row("TK1a", tv !== undefined && a13.action === "warn" && a13.line.includes("ROUTES_KV_TOKEN expires in 13 days"), `13 days out -> ${a13.action}: ${a13.line}`);
  row("TK1b", tv !== undefined && a15.action === "ok", `(control) 15 days out -> ${a15.action}: ${a15.line}`);
  row("TK1c", tv !== undefined && none.action === "warn" && /no expiry/.test(none.line), `no expires_on -> ${none.action}: ${none.line}`);
  row("TK1d", tv !== undefined && bad.action === "fail", `the verify call fails -> ${bad.action}: ${bad.line}`);
  const inactive = v({ success: true, result: { id: "0000", status: "disabled", expires_on: "2027-02-18T00:00:00Z" } });
  row("TK1e", tv !== undefined && inactive.action === "fail", `a token that is not active -> ${inactive.action}: ${inactive.line}`);
}

// ---- SC4 (section 2): the failure notice -- fixed text per class, counts only, no id, no key, no position -------------
{
  const mod = await import("./routes-notify").catch(() => null);
  type N = { title: string; body: string };
  const render = mod?.renderNotice as undefined | ((cls: string, env: string, counts: Record<string, number>) => N);
  const classes = (mod?.CLASSES as readonly string[] | undefined) ?? [];
  const want = ["refused-g1", "refused-g2", "refused-g3", "refused-g4", "refused-g5", "refused-g6", "verify-fail",
    "seal-refused", "source-unreachable", "secrets-not-set", "frozen-malformed", "token-expiring",
    "meta-not-key-level", "run-failed"];
  row("SC4set", render !== undefined && JSON.stringify([...classes].sort()) === JSON.stringify([...want].sort()), `the closed set: ${classes.length} classes`);
  const counts = { written: 70, deleted: 25, mismatched: 1, days: 13 };
  let clean = 0, named = 0;
  for (const c of want) {
    // A class the module cannot render reads as an empty notice, so it is a RED row, never a crash (a crash is an
    // instrument failure that hides every row after it; it did exactly that when the set grew from 12 to 14).
    let n = { title: "", body: "" };
    try { if (render) n = render(c, "production", counts); } catch { /* empty: SC4c reads it red */ }
    const t = `${n.title}\n${n.body}`;
    if (!/[0-9a-f]{16}/i.test(t) && !/-?\d{1,3}\.\d{3,}/.test(t) && !/\brt:[A-Z0-9]/.test(t)) clean++;
    if (n.body.trim() !== "" && t.includes(c)) named++;
  }
  row("SC4", render !== undefined && clean === want.length, `no 16-hex run, no coordinate and no rt: key in any class: ${clean}/${want.length}`);
  row("SC4c", render !== undefined && named === want.length, `(control) every class is non-empty and names itself: ${named}/${want.length}`);
  let refused = "no throw";
  try { if (render) render("not-a-class", "production", counts); } catch { refused = "throws"; }
  row("SC4x", render !== undefined && refused === "throws", `a class outside the closed set -> ${refused}`);
}

// ---- SC2 (section 2): the routes-refresh environment, as booleans: main-only, and which secret NAMES are set -------
{
  const mod = await import("./routes-env-check").catch(() => null);
  type E = { start: boolean; lines: string[]; readback: boolean };
  const ev = mod?.envVerdict as undefined | ((environment: unknown, policies: unknown, present: Record<string, boolean>) => E);
  const env = { name: "routes-refresh", deployment_branch_policy: { protected_branches: false, custom_branch_policies: true } };
  const mainOnly = { branch_policies: [{ name: "main", type: "branch" }] };
  const all = { ROUTES_KV_TOKEN: true, ROUTES_NTFY_TOPIC: true, ROUTES_TEST_DEVICE: true, ROUTES_TEST_KEY: true, ROUTES_TEST_KEY_STAGING: true };
  const v = (e: unknown, p: unknown, s: Record<string, boolean>): E => (ev ? ev(e, p, s) : { start: true, lines: ["MODULE MISSING"], readback: true });
  const has = (x: E, s: string) => x.lines.some((l) => l.includes(s));
  const noEnv = v(null, null, all);
  row("SC2a", ev !== undefined && !noEnv.start && has(noEnv, "environment: not set"), `no environment -> start ${noEnv.start}: ${noEnv.lines.join(" | ")}`);
  const prot = v({ ...env, deployment_branch_policy: { protected_branches: true, custom_branch_policies: false } }, null, all);
  row("SC2b", ev !== undefined && !prot.start && has(prot, "main-only: false"), `a protected-branches policy -> start ${prot.start}`);
  const two = v(env, { branch_policies: [{ name: "main", type: "branch" }, { name: "dev", type: "branch" }] }, all);
  row("SC2c", ev !== undefined && !two.start && has(two, "main-only: false"), `names [main, dev] -> start ${two.start}`);
  const noKv = v(env, mainOnly, { ...all, ROUTES_KV_TOKEN: false });
  row("SC2d", ev !== undefined && !noKv.start && has(noKv, "kv token: not set"), `ROUTES_KV_TOKEN missing -> start ${noKv.start}`);
  const noTopic = v(env, mainOnly, { ...all, ROUTES_NTFY_TOPIC: false });
  row("SC2e", ev !== undefined && !noTopic.start && has(noTopic, "ntfy: not set"), `ROUTES_NTFY_TOPIC missing -> start ${noTopic.start}`);
  const ok = v(env, mainOnly, all);
  row("SC2ok", ev !== undefined && ok.start && ok.readback && has(ok, "main-only: true") && has(ok, "kv token: set") && has(ok, "ntfy: set") && has(ok, "read-back identity: set"), `(control) all five set -> start ${ok.start}: ${ok.lines.join(" | ")}`);
  const rb = v(env, mainOnly, { ROUTES_KV_TOKEN: true, ROUTES_NTFY_TOPIC: true });
  row("SC2rb", ev !== undefined && rb.start && !rb.readback && has(rb, "read-back identity: not set"), `(control) the identity absent -> start ${rb.start}, readback ${rb.readback} (RB1)`);
}

// ---- SW1-SW4 (section 2g): the stale-temp sweep, on a PRIVATE dir that this block removes again -----------------------
{
  const mod = await import("./routes-tmp").catch(() => null);
  type S = { removed: number; kept: number; foreign: number };
  const sweep = mod?.sweepStale as undefined | ((dir: string, prefixes: string[], nowMs: number) => S);
  const fs = await import("node:fs"), os = await import("node:os"), path = await import("node:path");
  const D = fs.mkdtempSync(path.join(os.tmpdir(), "swtest-"));
  try {
    const H = 3_600_000, now = Date.now();
    const mk = (name: string, ageH: number, content: "tree" | "foreign") => {
      const p = path.join(D, name);
      fs.mkdirSync(p);
      if (content === "tree") fs.mkdirSync(path.join(p, "standing-data-" + "1".repeat(40)));
      else fs.writeFileSync(path.join(p, "someone-elses.txt"), "x");
      const t = new Date(now - ageH * H);
      fs.utimesSync(p, t, t);
      return p;
    };
    const stale = mk("routes-new-AAAAAA", 7, "tree"), fresh = mk("routes-new-BBBBBB", 1, "tree");
    const foreign = mk("routes-new-CCCCCC", 7, "foreign"), other = mk("other-DDDDDD", 7, "tree");
    const r: S = sweep ? sweep(D, ["routes-new-"], now) : { removed: -1, kept: -1, foreign: -1 };
    row("SW1", sweep !== undefined && !fs.existsSync(stale), `a stale dir of its own -> ${fs.existsSync(stale) ? "KEPT" : "removed"}`);
    row("SW2", sweep !== undefined && fs.existsSync(fresh), `a fresh dir (1 h) -> ${fs.existsSync(fresh) ? "kept" : "REMOVED"}`);
    // ONE PROPERTY PER ROW (the reviewer, after S-SW's first run read SW4 red for its count half): each row asserts one
    // dir's fate, and SW5 alone asserts the sweep's counts.
    row("SW3", sweep !== undefined && fs.existsSync(foreign), `a stale dir with foreign content -> ${fs.existsSync(foreign) ? "kept" : "REMOVED"}`);
    row("SW4", sweep !== undefined && fs.existsSync(other), `another prefix -> ${fs.existsSync(other) ? "untouched" : "REMOVED"}`);
    row("SW5", sweep !== undefined && r.removed === 1 && r.kept === 1 && r.foreign === 1, `the sweep's counts -> removed ${r.removed}, kept ${r.kept}, foreign ${r.foreign} (want 1, 1, 1)`);
  } finally {
    fs.rmSync(D, { recursive: true, force: true });
  }
}

// ---- GN1 (B2, the review): G5 refuses a bound that is not a non-negative integer, when called directly ----------------
// GN2 (G4's NaN allowance) is RETIRED by D8 (section 2j): the allowance is a set of names, which a NaN cannot reach.
{
  const g5 = G.g5Cap(827, { scheduled: false, maxDeletes: Number.NaN });
  row("GN1", !g5.ok, `g5Cap(827, maxDeletes NaN) -> ${g5.ok ? "PASSES" : "refuses"}: ${g5.reason}`);
}

// ---- SG1, SG2 (M3, the review): SIGINT and SIGTERM remove the temp dirs and exit 130 / 143 ----------------------------
{
  const cp = await import("node:child_process"), fs = await import("node:fs"), os = await import("node:os"), path = await import("node:path");
  for (const [id, sig, code] of [["SG1", "SIGINT", 130], ["SG2", "SIGTERM", 143]] as const) {
    const D = fs.mkdtempSync(path.join(os.tmpdir(), "sgtest-"));
    try {
      const env = { ...process.env, TMPDIR: D, TEMP: D, TMP: D };
      const r = cp.spawnSync(process.execPath, ["node_modules/tsx/dist/cli.mjs", "scripts/routes-tmp-signal.fixture.mts", sig], { env, encoding: "utf8" });
      const made = (r.stdout ?? "").trim();
      const left = fs.readdirSync(D).filter((n) => n.startsWith("routes-")).length;
      row(id, made !== "" && left === 0 && r.status === code, `${sig}: exit ${r.status ?? r.signal} (want ${code}), ${left} temp dir(s) left (want 0)`);
    } finally {
      fs.rmSync(D, { recursive: true, force: true });
    }
  }
}

// ---- BL1, BL2 (section 2i; the reviewer's ruling, 2026-11-14 07:28Z, after W8's instrument failure #3) ---------------
// A fake-KV selftest started where FK1 would refuse the fake must refuse to START: exit 2, FK1's condition named (never
// a value), and no row printed. Otherwise the same state reads as rows red for a reason none of them names. The trigger
// is an EMPTY file in a private temp, never the real token file. Literal row("BL1"/"BL2") calls, so W8's declared set
// sees them.
{
  const cp = await import("node:child_process"), fs = await import("node:fs"), os = await import("node:os"), path = await import("node:path");
  const blind = (script: string) => {
    const D = fs.mkdtempSync(path.join(os.tmpdir(), "bltest-"));
    try {
      const tf = path.join(D, "tokens");
      fs.writeFileSync(tf, "");
      const env: Record<string, string | undefined> = { ...process.env, BLIPSCOPE_TOKEN_FILE: tf };
      delete env.ROUTES_KV_TOKEN;
      delete env.CLOUDFLARE_API_TOKEN;
      const r = cp.spawnSync("bash", [script], { env, encoding: "utf8", maxBuffer: 64 << 20 });
      const out = `${r.stdout ?? ""}${r.stderr ?? ""}`;
      const rows = (out.match(/^(?:PASS|FAIL) /gm) ?? []).length, red = (out.match(/^FAIL /gm) ?? []).length;
      const named = out.includes("SELFTEST BLIND") && out.includes("BLIPSCOPE_TOKEN_FILE"), leaked = out.includes(tf);
      return { ok: r.status === 2 && rows === 0 && named && !leaked,
        detail: `${script}: exit ${r.status}, ${rows} row(s) printed (${red} red), the condition ${named ? "named" : "NOT named"}, the path ${leaked ? "PRINTED" : "not printed"}` };
    } finally {
      fs.rmSync(D, { recursive: true, force: true });
    }
  };
  { const b = blind("scripts/routes-writer.selftest.sh"); row("BL1", b.ok, b.detail); }
  { const b = blind("scripts/routes-run.selftest.sh"); row("BL2", b.ok, b.detail); }
}

// ---- BK2, BK2s (2q): the backup's read-back, and the hook that tests it is unreachable from the CLI ------------------
{
  const mod = await import("./routes-backup").catch(() => null);
  const fs = await import("node:fs"), os = await import("node:os"), path = await import("node:path");
  const D = fs.mkdtempSync(path.join(os.tmpdir(), "routes-bk2-"));
  try {
    const f = path.join(D, "b.json");
    const payload = { v: 1 as const, env: "staging", runId: "AB12C", sentinel: null, meta: "{}", journal: null, keys: { "rt:QX00001": "{\"o\":\"AAA\",\"d\":\"BBB\"}" } };
    // the hook flips one byte of the file after it is written; the read-back must notice and refuse (BK2)
    const flip = (p: string) => { const b = fs.readFileSync(p); b[b.length - 2] ^= 1; fs.writeFileSync(p, b); };
    let refused = false, why = "";
    try { mod?.writeBackup(f, payload, { afterWrite: flip }); } catch (e) { refused = String(e).includes("(BK2)"); why = String(e).slice(0, 80); }
    row("BK2", !!mod && refused, `writeBackup with a byte flipped after the write -> ${refused ? "refused (BK2)" : `NOT refused${why ? `: ${why}` : ""}`}`);
  } finally {
    fs.rmSync(D, { recursive: true, force: true });
  }
  // BK2s (the reviewer's condition): ingest-routes.ts calls writeBackup( exactly once, with exactly two arguments, so
  // the hook -- a function parameter only -- is never reached from a flag or an environment variable.
  const src = fs.readFileSync("scripts/ingest-routes.ts", "utf8");
  const calls: string[] = [];
  for (let at = src.indexOf("writeBackup("); at >= 0; at = src.indexOf("writeBackup(", at + 1)) {
    let depth = 0, i = at + "writeBackup".length, commas = 0;
    for (; i < src.length; i++) {
      const ch = src[i];
      if (ch === "(" || ch === "{" || ch === "[") depth++;
      else if (ch === ")" || ch === "}" || ch === "]") { depth--; if (depth === 0) break; }
      else if (ch === "," && depth === 1) commas++;
    }
    calls.push(String(commas + 1));
  }
  row("BK2s", calls.length === 1 && calls[0] === "2", `writeBackup( calls in ingest-routes.ts: ${calls.length}, argument counts [${calls.join(", ")}] (want exactly one call with 2)`);
}

// ---- BK0g, BK0e, BK0n, BK0s, RO1 (2r items 5 and 2) ---------------------------------------------------------------
{
  const mod = await import("./routes-backup").catch(() => null);
  const fs = await import("node:fs"), os = await import("node:os"), path = await import("node:path");
  const D = fs.mkdtempSync(path.join(os.tmpdir(), "routes-bk0-"));
  try {
    const p = path.join(D, "b.json");
    // an injected git answer: the CLI passes none (BK0s), so only this selftest reaches the parameter
    const ans = (status: number | null, stderr: string, error?: Error) => () => ({ status, stdout: "", stderr, error });
    const refusal =(git: unknown) => (mod?.backupPathRefusal as unknown as (p: string, g?: unknown) => string | null)?.(p, git);
    const g = refusal(ans(128, "fatal: detected dubious ownership in repository at '/x'"));
    row("BK0g", typeof g === "string", `git answering "dubious ownership" (exit 128) -> ${typeof g === "string" ? "refused" : "ALLOWED"}`);
    const e = refusal(ans(null, "", new Error("spawn git ENOENT")));
    row("BK0e", typeof e === "string", `git that cannot be run -> ${typeof e === "string" ? "refused" : "ALLOWED"}`);
    const n = refusal(ans(128, "fatal: not a git repository (or any of the parent directories): .git"));
    row("BK0n", n === null, `git answering "not a git repository" (the control) -> ${n === null ? "allowed" : `refused: ${n}`}`);
  } finally {
    fs.rmSync(D, { recursive: true, force: true });
  }
  const src = fs.readFileSync("scripts/ingest-routes.ts", "utf8");
  const argCounts = (name: string) => {
    const out: number[] = [];
    for (let at = src.indexOf(`${name}(`); at >= 0; at = src.indexOf(`${name}(`, at + 1)) {
      let depth = 0, i = at + name.length, commas = 0;
      for (; i < src.length; i++) {
        const ch = src[i];
        if (ch === "(" || ch === "{" || ch === "[") depth++;
        else if (ch === ")" || ch === "}" || ch === "]") { depth--; if (depth === 0) break; }
        else if (ch === "," && depth === 1) commas++;
      }
      out.push(commas + 1);
    }
    return out;
  };
  const bc = argCounts("backupPathRefusal");
  row("BK0s", bc.length === 1 && bc[0] === 1, `backupPathRefusal( calls in ingest-routes.ts: ${bc.length}, argument counts [${bc.join(", ")}] (want exactly one call with 1)`);
  // RO1 (item 2): in restoreMain, the journal's own kvChunks call comes after the data delete and before meta
  const s0 = src.indexOf("function restoreMain(");
  const body = s0 >= 0 ? src.slice(s0, src.indexOf("\n}\n", s0)) : "";
  const iDel = body.indexOf('kvChunks("delete", dels');
  const iJ = body.search(/kvChunks\([^\n]*META_PENDING_KEY/);
  const iMeta = body.indexOf("{ key: META_KEY, value: bk.meta }");
  row("RO1", iDel >= 0 && iJ > iDel && iMeta > iJ, `restoreMain: the data delete at ${iDel}, the journal's own kvChunks at ${iJ}, meta at ${iMeta} (want delete < journal < meta)`);
}

console.log(off ? `SELFTEST: ${off} row(s) off` : "SELFTEST PASSED");
process.exit(off ? 1 : 0);
