#!/usr/bin/env bash
# routes-writer.selftest.sh -- the key-level writer's rows, graded through the fake KV only (docs/route-weekly-code.md,
# section 2c): KL1, GR8, RB1, PFX1, GR9a-GR9f, SD1. Run from proxy/. No network, no real namespace: every KV call goes
# to scripts/routes-fakekv.mjs, and FK1 keeps that stand-in off production and CI's refresh job.
#
# Each row asserts the REASON a refusal names, not only its exit code, and a store byte-identity (sha256 of the store
# file) where the row says nothing may be written. Keys are compared inside node and never printed.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
# BL1 (section 2i; the reviewer, 2026-11-14 07:28Z, after W8's instrument failure #3): where FK1 refuses the fake KV, no
# row can be graded, so refuse to START -- exit 2, FK1's condition named (never a value), before any temp dir or row.
# FK1's own check, so the rule keeps one home. Run with env -u BLIPSCOPE_TOKEN_FILE and no token variable set.
if ! fk1=$(npx --no-install tsx scripts/routes-fk1.ts --check staging 2>&1); then
  echo "SELFTEST BLIND (exit 2): FK1 refuses the fake KV in this environment, so no row can be graded. $fk1"; exit 2
fi
T="$(mktemp -d)"; F="$T/fx"; trap 'rm -rf "$T"' EXIT
# TMP1: every child's temp dirs go to a private dir, so what a run LEAVES can be counted exactly (2026-11-14: the tools
# left 371 dirs, 4.7 GB, in the system temp). Windows node reads TEMP/TMP, POSIX reads TMPDIR.
mkdir -p "$T/tmp"; PT="$(cygpath -w "$T/tmp" 2>/dev/null || echo "$T/tmp")"; export TMPDIR="$T/tmp" TEMP="$PT" TMP="$PT"
npx tsx scripts/routes-fixture.mts "$F" > "$T/fixture.out" 2>&1 || { cat "$T/fixture.out"; echo "SELFTEST FAILED: the fixture did not build"; exit 1; }
# The base store is a mirror built from prev: its meta AND every prev key at its value (the fixture writes it). So "a
# deleted key is gone" and "a changed key holds its new value" can each fail, and the verify's whole-prefix listing
# sees a real-shaped namespace.
cp "$F/store-prev.json" "$F/store.json"

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
# M1 (the review): GR10 and GR11 compare a WHOLE line, never a fragment ("MISMATCH 1" is a fragment of "MISMATCH 10").
# CMP is the comparator they use; M1c grades CMP itself.
line_is() { grep -qxF -- "$2" "$1"; }
CMP=line_is
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

# ---- SOD (B1, the review): --seal-only --dry-run is refused before any KV call, with nothing written ----------------
fresh; receipt "$T/sod.json"; h0=$(sha "$T/s.json")
rc=$(run "$T/sod.out" "${SO[@]}" --receipt "$T/sod.json" --dry-run)
if [ "$rc" = 2 ] && has "$T/sod.out" "(SOD)" && [ "$(sha "$T/s.json")" = "$h0" ]; then ok SOD; else bad SOD "exit $rc, store changed: $([ "$(sha "$T/s.json")" = "$h0" ] && echo no || echo yes)"; fi

# ---- OV1-OV5 (B2, the review): an override that is not a non-negative integer is refused, never read as NaN ----------
ovr() { # ovr <flag> <value>: true when the writer refuses it, exit 2 with (OVR), and the store is byte-identical
  fresh; local h; h=$(sha "$T/s.json")
  local r; r=$(run "$T/ovr.out" "${KL[@]}" --file "$F/new.tar.gz" --dry-run "$1" "$2")
  [ "$r" = 2 ] && has "$T/ovr.out" "(OVR)" && [ "$(sha "$T/s.json")" = "$h" ]
}
if ovr --max-deletes "1,000"; then ok OV1; else bad OV1 "--max-deletes 1,000 was not refused"; fi
if ovr --max-deletes "5x"; then ok OV2; else bad OV2 "--max-deletes 5x was not refused"; fi
if ovr --max-deletes ""; then ok OV3; else bad OV3 "an empty --max-deletes was not refused"; fi
if ovr --max-deletes "-1"; then ok OV4; else bad OV4 "--max-deletes -1 was not refused"; fi
if ovr --allow-removed "5x"; then ok OV5; else bad OV5 "--allow-removed 5x was not refused"; fi
# OV6 (D8, section 2j): --allow-removed takes shard NAMES; the old count form is refused loudly, never read as a name
if ovr --allow-removed "5"; then ok OV6; else bad OV6 "--allow-removed 5 (the old count form) was not refused"; fi

# =================================== W3: the verify, the receipt, and the replay ===================================
vrun() { # vrun <outfile> <args...>: the verify through the fake; echoes the exit status
  local o="$1"; shift
  ROUTES_KV_CLI=scripts/routes-fakekv.mjs ROUTES_FAKEKV_STORE="$T/s.json" npx tsx scripts/verify-routes.ts "$@" > "$o" 2>&1
  echo $?
}
metasha() { node -e 'process.stdout.write(require("crypto").createHash("sha256").update(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).staging["meta:routes"]).digest("hex"))' "$1"; }
plantkv() { # plantkv <key-source: changed|deleted> <value>: set the first such key in the store to <value>
  node -e '
const fs=require("fs"), [S,E,which,val]=process.argv.slice(1);
const s=JSON.parse(fs.readFileSync(S,"utf8")), e=JSON.parse(fs.readFileSync(E,"utf8"));
const k=(which==="changed"?e.changedKeys:e.deletedKeys)[0]; s.staging[k]=val; fs.writeFileSync(S, JSON.stringify(s));
' "$T/s.json" "$F/expect.json" "$1" "$2"
}
fresh; run "$T/w3w.out" "${KL[@]}" --file "$F/new.tar.gz" --no-seal --readback off --out-written "$T/w.json" --out-deleted "$T/d.json" > /dev/null
cp "$T/s.json" "$T/written.json"; from_written() { cp "$T/written.json" "$T/s.json"; }
V=(--env staging --file "$F/new.tar.gz" --check-values "$T/w.json" --expect-absent "$T/d.json")

# ---- FKW: the writer refuses the fake when a real credential variable is set (FK1 revised, section 2e) -------------
fresh; h0=$(sha "$T/s.json")
rc=$(CLOUDFLARE_API_TOKEN=fkw-planted-not-a-token run "$T/fkw.out" "${KL[@]}" --file "$F/new.tar.gz" --dry-run)
if [ "$rc" = 2 ] && has "$T/fkw.out" "(FK1)" && ! has "$T/fkw.out" "fkw-planted" && [ "$(sha "$T/s.json")" = "$h0" ]; then ok FKW; else bad FKW "exit $rc"; fi

# ---- FKV: the verify refuses the fake on production -------------------------------------------------------------
from_written; rc=$(vrun "$T/fkv.out" --env production --file "$F/new.tar.gz")
if [ "$rc" = 2 ] && has "$T/fkv.out" "(FK1)"; then ok FKV; else bad FKV "exit $rc"; fi

# ---- RCP2, GR10m, GR10a: a clean run writes a receipt, and --seal-only seals with it ------------------------------
from_written; rm -f "$T/r.json"; rc=$(vrun "$T/rcp2.out" "${V[@]}" --receipt "$T/r.json")
if has "$T/rcp2.out" "CONTROL (GR10m): a known-correct key against a deliberately wrong value: 1 mismatch (must be exactly 1)"; then ok GR10m; else bad GR10m "no control line"; fi
if has "$T/rcp2.out" "CONTROL (GR10a): the made-up key reads null (must be null)"; then ok GR10a; else bad GR10a "no control line"; fi
sr=$( [ -f "$T/r.json" ] && run "$T/rcp2s.out" "${SO[@]}" --receipt "$T/r.json" || echo none )
sc=$(node -e 'try{process.stdout.write(JSON.parse(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).staging["meta:routes"]).sourceCommit??"")}catch{}' "$T/s.json")
if [ "$rc" = 0 ] && [ -f "$T/r.json" ] && [ "$sr" = 0 ] && [ "$sc" = "$(printf '2%.0s' $(seq 40))" ]; then ok RCP2; cp "$T/s.json" "$T/sealed.json"; else bad RCP2 "verify exit $rc, receipt $([ -f "$T/r.json" ] && echo yes || echo no), seal $sr"; fi

# ---- GR10: one written value planted wrong; RCP1: a stale receipt does not survive the failure --------------------
from_written; plantkv changed '{"o":"XXX","d":"YYY"}'; echo '{"stale":true}' > "$T/r.json"
rc=$(vrun "$T/gr10.out" "${V[@]}" --receipt "$T/r.json")
if [ "$rc" = 1 ] && $CMP "$T/gr10.out" "values: checked 70, match 69, MISMATCH 1, ABSENT 0" && [ ! -f "$T/r.json" ]; then ok GR10; else bad GR10 "exit $rc"; fi
if [ ! -f "$T/r.json" ]; then ok RCP1; else bad RCP1 "the stale receipt survived a failed verify"; fi

# ---- GR11: one deleted key planted back ---------------------------------------------------------------------------
from_written; plantkv deleted '{"o":"AAA","d":"BBB"}'; rm -f "$T/r.json"
rc=$(vrun "$T/gr11.out" "${V[@]}" --receipt "$T/r.json")
if [ "$rc" = 1 ] && $CMP "$T/gr11.out" "absence: checked 25, absent 24, PRESENT 1" && [ ! -f "$T/r.json" ]; then ok GR11; else bad GR11 "exit $rc"; fi

# ---- M1c: the comparator GR10 and GR11 use rejects a near miss (MISMATCH 10 where it wants MISMATCH 1) --------------
printf 'values: checked 70, match 60, MISMATCH 10, ABSENT 0\n' > "$T/m1c.out"
if ! $CMP "$T/m1c.out" "values: checked 70, match 69, MISMATCH 1, ABSENT 0" && ! $CMP "$T/m1c.out" "MISMATCH 1"; then ok M1c; else bad M1c "the comparator accepted MISMATCH 10 as MISMATCH 1"; fi

# ---- UN1-UN3: an unchanged source is replayed, verified, and only then re-sealed ----------------------------------
RP=(--key-level --replay --env staging --prev "$F/prev.tar.gz" --file "$F/new.tar.gz" --out-written "$T/w2.json" --out-deleted "$T/d2.json")
V2=(--env staging --file "$F/new.tar.gz" --check-values "$T/w2.json" --expect-absent "$T/d2.json")
un() { # un <row> <plant: none|changed|deleted>
  local row="$1" p="$2"
  if [ ! -f "$T/sealed.json" ]; then bad "$row" "no sealed store (RCP2 failed)"; return; fi
  cp "$T/sealed.json" "$T/s.json"; [ "$p" = none ] || plantkv "$p" '{"o":"XXX","d":"YYY"}'
  local h0 m0 b0; h0=$(sha "$T/s.json"); m0=$(metasha "$T/s.json")
  b0=$(node -e 'process.stdout.write(JSON.parse(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).staging["meta:routes"]).builtAt)' "$T/s.json")
  local rr; rr=$(run "$T/$row-r.out" "${RP[@]}")
  if [ "$rr" != 0 ] || [ "$(sha "$T/s.json")" != "$h0" ]; then bad "$row" "replay exit $rr, store changed: $([ "$(sha "$T/s.json")" = "$h0" ] && echo no || echo yes)"; return; fi
  rm -f "$T/r2.json"; local rv; rv=$(vrun "$T/$row-v.out" "${V2[@]}" --receipt "$T/r2.json")
  if [ "$p" = none ]; then
    local rs; rs=$(run "$T/$row-s.out" "${SO[@]}" --receipt "$T/r2.json")
    local b1; b1=$(node -e 'process.stdout.write(JSON.parse(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).staging["meta:routes"]).builtAt)' "$T/s.json")
    if [ "$rv" = 0 ] && [ "$rs" = 0 ] && [ "$b1" != "$b0" ]; then ok "$row"; else bad "$row" "verify $rv, seal $rs, builtAt moved: $([ "$b1" != "$b0" ] && echo yes || echo no)"; fi
  else
    if [ "$rv" = 1 ] && [ ! -f "$T/r2.json" ] && [ "$(metasha "$T/s.json")" = "$m0" ]; then ok "$row"; else bad "$row" "verify $rv, receipt $([ -f "$T/r2.json" ] && echo yes || echo no)"; fi
  fi
}
un UN1 none
un UN2 changed
un UN3 deleted

# ---- TMP1: the tools leave no temp dir behind; TMP1-ctl: the children really used the private temp -----------------
seen=$(node -e 'process.stdout.write(require("os").tmpdir())')
case "$(echo "$seen" | tr '\\' '/' | tr 'A-Z' 'a-z')" in *"$(basename "$T" | tr 'A-Z' 'a-z')/tmp"*) ok TMP1-ctl ;; *) bad TMP1-ctl "a child's temp is not the private one" ;; esac
left=$(ls -1 "$T/tmp" 2>/dev/null | grep -cE '^(routes|verify)-')
if [ "$left" = 0 ]; then ok TMP1; else bad TMP1 "$left temp dir(s) left behind"; fi

echo "rows: $pass passed, $fail failed"
if [ "$fail" -gt 0 ]; then echo "SELFTEST FAILED:$failed"; exit 1; fi
echo "SELFTEST PASSED ($pass rows)"
