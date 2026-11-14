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
  const n4 = clone(prevByShard); for (const s of ["S01", "S02", "S03", "S04"]) n4.delete(s);
  const r = G.g4Removed(prevByShard, n4, 0);
  row("GR5a", !r.ok, `4 shards removed -> ${r.ok ? "passes" : "refuses deletes"}`);
  const n5 = clone(prevByShard); for (const s of ["A80", "A76", "A45"]) n5.delete(s); // 3 shards, 201 keys
  const r2 = G.g4Removed(prevByShard, n5, 0);
  row("GR5b", !r2.ok, `3 shards removed holding 201 keys -> ${r2.ok ? "passes" : "refuses deletes"}`);
  const n6 = clone(prevByShard); for (const s of ["S05", "S06", "S07"]) n6.delete(s); // 3 shards, 75 keys
  const c = G.g4Removed(prevByShard, n6, 0);
  row("GR5c", c.ok, `3 shards removed holding 75 keys -> ${c.ok ? "passes" : "refuses"}`);
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

console.log(off ? `SELFTEST: ${off} row(s) off` : "SELFTEST PASSED");
process.exit(off ? 1 : 0);
