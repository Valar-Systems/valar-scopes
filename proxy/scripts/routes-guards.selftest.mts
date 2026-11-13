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

console.log(off ? `SELFTEST: ${off} row(s) off` : "SELFTEST PASSED");
process.exit(off ? 1 : 0);
