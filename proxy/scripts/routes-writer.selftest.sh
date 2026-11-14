#!/usr/bin/env bash
# routes-writer.selftest.sh -- the key-level writer's rows, graded through the fake KV only (docs/route-weekly-code.md,
# section 2c): KL1, GR8, RB1, PFX1, GR9a-GR9f, SD1. Run from proxy/. No network, no real namespace: every KV call goes
# to scripts/routes-fakekv.mjs, and FK1 keeps that stand-in off production and CI's refresh job.
#
# Each row asserts the REASON a refusal names, not only its exit code, and a store byte-identity (sha256 of the store
# file) where the row says nothing may be written. Keys are compared inside node and never printed.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
T="$(mktemp -d)"; F="$T/fx"; trap 'rm -rf "$T"' EXIT
npx tsx scripts/routes-fixture.mts "$F" > "$T/fixture.out" 2>&1 || { cat "$T/fixture.out"; echo "SELFTEST FAILED: the fixture did not build"; exit 1; }
# The base store holds prev's meta AND the keys this run must change or delete, at their OLD values, so "a deleted key
# is gone" and "a changed key holds its new value" can each fail (an empty store would pass both for nothing).
node -e '
const fs=require("fs"), F=process.argv[1], e=JSON.parse(fs.readFileSync(F+"/expect.json","utf8"));
const ns={ "meta:routes": fs.readFileSync(F+"/meta-prev.json","utf8") };
for (const k of e.changedKeys) ns[k]=JSON.stringify({o:"AAA",d:"BBB"});
for (const k of e.deletedKeys) ns[k]=JSON.stringify({o:"AAA",d:"BBB"});
fs.writeFileSync(F+"/store.json", JSON.stringify({ staging: ns }));
' "$F"

pass=0; fail=0; failed=""
ok()  { echo "PASS $1"; pass=$((pass+1)); }
bad() { echo "FAIL $1: $2"; fail=$((fail+1)); failed="$failed $1"; }
sha() { node -e 'process.stdout.write(require("crypto").createHash("sha256").update(require("fs").readFileSync(process.argv[1])).digest("hex"))' "$1"; }
fresh() { cp "$F/store.json" "$T/s.json"; }
run() { # run <outfile> <args...>: the writer through the fake, stdout+stderr to <outfile>; echoes the exit status
  local o="$1"; shift
  ROUTES_KV_CLI=scripts/routes-fakekv.mjs ROUTES_FAKEKV_STORE="$T/s.json" npx tsx scripts/ingest-routes.ts "$@" > "$o" 2>&1
  echo $?
}
has() { grep -qF -- "$2" "$1"; }
receipt() { # receipt <file> <field=value>...: a valid receipt for new.tar.gz on staging, with fields overridden
  local out="$1"; shift
  node -e '
const fs=require("fs"), c=require("crypto"), [F,S,out,...kv]=process.argv.slice(1);
const store=JSON.parse(fs.readFileSync(S,"utf8"));
const r={ v:1, result:"PASS", env:"staging", sourceCommit:"2".repeat(40),
  sourceSha256:c.createHash("sha256").update(fs.readFileSync(F+"/new.tar.gz")).digest("hex"),
  prevSourceCommit:"1".repeat(40),
  prevMetaSha256:c.createHash("sha256").update(store.staging["meta:routes"]).digest("hex"),
  written:70, deleted:25, deletesRefused:false, readback:"not run", checkedAt:new Date().toISOString() };
for (const p of kv) { const i=p.indexOf("="); const k=p.slice(0,i), v=p.slice(i+1); r[k]= v==="true"?true: v==="false"?false: v; }
fs.writeFileSync(out, JSON.stringify(r));
' "$F" "$T/s.json" "$out" "$@"
}
KL=(--key-level --env staging --prev "$F/prev.tar.gz")

# ---- KL1: a key-level write without --no-seal is refused before any KV call -------------------------------------
fresh; h0=$(sha "$T/s.json"); rc=$(run "$T/kl1.out" "${KL[@]}" --file "$F/new.tar.gz" --readback off)
if [ "$rc" = 2 ] && has "$T/kl1.out" "(KL1)" && [ "$(sha "$T/s.json")" = "$h0" ]; then ok KL1; else bad KL1 "exit $rc, store changed: $([ "$(sha "$T/s.json")" = "$h0" ] && echo no || echo yes)"; fi

# ---- GR8 (stage 2's half) + RB1: --no-seal writes and deletes, and never touches meta:routes -----------------------
fresh; m0=$(node -e 'process.stdout.write(require("crypto").createHash("sha256").update(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).staging["meta:routes"]).digest("hex"))' "$T/s.json")
rc=$(run "$T/gr8.out" "${KL[@]}" --file "$F/new.tar.gz" --no-seal --readback off --out-written "$T/w.json" --out-deleted "$T/d.json")
v=$(node -e '
const fs=require("fs"), c=require("crypto"), [S,E,W,D,m0]=process.argv.slice(1);
const s=JSON.parse(fs.readFileSync(S,"utf8")).staging, e=JSON.parse(fs.readFileSync(E,"utf8"));
const bad=[];
if (c.createHash("sha256").update(s["meta:routes"]).digest("hex")!==m0) bad.push("meta changed");
for (const k of e.changedKeys) if (s[k]!==JSON.stringify({o:"BBB",d:"CCC"})) bad.push("a changed key not written");
for (const k of e.addedKeys) if (s[k]!==JSON.stringify({o:"CCC",d:"AAA"})) bad.push("an added key not written");
for (const k of e.deletedKeys) if (k in s) bad.push("a deleted key still present");
let w={}, d={}; try { w=JSON.parse(fs.readFileSync(W,"utf8")); d=JSON.parse(fs.readFileSync(D,"utf8")); } catch { bad.push("no out files"); }
if (Object.keys(w.written??{}).length!==e.writes) bad.push("out-written count "+Object.keys(w.written??{}).length);
if ((d.deleted??[]).length!==e.deletes) bad.push("out-deleted count "+(d.deleted??[]).length);
if (w.deletesRefused!==false) bad.push("deletesRefused not false");
console.log(bad.length?[...new Set(bad)].join("; "):"ok");
' "$T/s.json" "$F/expect.json" "$T/w.json" "$T/d.json" "$m0")
if [ "$rc" = 0 ] && [ "$v" = ok ]; then ok GR8; else bad GR8 "exit $rc; $v"; fi
if has "$T/gr8.out" "readback: NOT RUN (the routes-refresh test identity is not set)"; then ok RB1; else bad RB1 "no NOT RUN line"; fi

# ---- PFX1: one key outside the route prefix refuses the whole run, with nothing written --------------------------
fresh; h0=$(sha "$T/s.json"); rc=$(run "$T/pfx.out" "${KL[@]}" --file "$F/pfx.tar.gz" --no-seal --readback off)
if [ "$rc" != 0 ] && has "$T/pfx.out" "(PFX1)" && [ "$(sha "$T/s.json")" = "$h0" ]; then ok PFX1; else bad PFX1 "exit $rc, store changed: $([ "$(sha "$T/s.json")" = "$h0" ] && echo no || echo yes)"; fi

# ---- GR9a-f: --seal-only refuses a missing or mismatched receipt; the control seals --------------------------------
SO=(--seal-only --env staging --file "$F/new.tar.gz")
gr9() { # gr9 <row> <receipt overrides...>|NONE
  local row="$1"; shift; fresh; h0=$(sha "$T/s.json")
  if [ "$1" = NONE ]; then rc=$(run "$T/$row.out" "${SO[@]}"); else receipt "$T/$row.json" "$@"; rc=$(run "$T/$row.out" "${SO[@]}" --receipt "$T/$row.json"); fi
  if [ "$rc" != 0 ] && has "$T/$row.out" "(GR9)" && [ "$(sha "$T/s.json")" = "$h0" ]; then ok "$row"; else bad "$row" "exit $rc, store changed: $([ "$(sha "$T/s.json")" = "$h0" ] && echo no || echo yes)"; fi
}
gr9 GR9a NONE
gr9 GR9b "sourceCommit=$(printf '1%.0s' $(seq 40))"
gr9 GR9c env=production
gr9 GR9e deletesRefused=true
gr9 GR9f prevMetaSha256=0000000000000000000000000000000000000000000000000000000000000000
gr9 GR9g result=FAIL

fresh; receipt "$T/gr9d.json"
rc=$(run "$T/gr9d.out" "${SO[@]}" --receipt "$T/gr9d.json" --source-date 2026-11-12T10:00:00Z)
v=$(node -e '
const fs=require("fs"), [S,E]=process.argv.slice(1);
const m=JSON.parse(JSON.parse(fs.readFileSync(S,"utf8")).staging["meta:routes"]), e=JSON.parse(fs.readFileSync(E,"utf8"));
const bad=[];
if (JSON.stringify(m.shards)!==JSON.stringify(Object.fromEntries(Object.entries(e.newManifest).sort()))) {
  const a=Object.keys(m.shards).sort(), b=Object.keys(e.newManifest).sort();
  if (a.length!==b.length || a.some((k,i)=>k!==b[i] || m.shards[k]!==e.newManifest[k])) bad.push("manifest differs");
}
if (m.sourceCommit!=="2".repeat(40)) bad.push("sourceCommit");
if (m.prevSourceCommit!=="1".repeat(40)) bad.push("prevSourceCommit");
if (m.mode!=="key-level") bad.push("mode");
if (m.readback!=="not run") bad.push("readback");
if (m.written!==70 || m.deleted!==25 || m.pendingDeletes!==0) bad.push("counts");
if (m.rows!==e.rows.new) bad.push("rows");
if (m.sourceCommittedAt!=="2026-11-12T10:00:00Z") bad.push("sourceCommittedAt");
if (!/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$/.test(m.builtAt??"")) bad.push("builtAt");
console.log(bad.length?bad.join("; "):"ok");
' "$T/s.json" "$F/expect.json")
if [ "$rc" = 0 ] && [ "$v" = ok ]; then ok GR9d; else bad GR9d "exit $rc; $v"; fi

# ---- SD1: a malformed --source-date is refused, with nothing written ---------------------------------------------
fresh; receipt "$T/sd1.json"; h0=$(sha "$T/s.json")
rc=$(run "$T/sd1.out" "${SO[@]}" --receipt "$T/sd1.json" --source-date yesterday)
if [ "$rc" = 2 ] && has "$T/sd1.out" "(SD1)" && [ "$(sha "$T/s.json")" = "$h0" ]; then ok SD1; else bad SD1 "exit $rc"; fi

echo "rows: $pass passed, $fail failed"
if [ "$fail" -gt 0 ]; then echo "SELFTEST FAILED:$failed"; exit 1; fi
echo "SELFTEST PASSED ($pass rows)"
