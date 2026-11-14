#!/usr/bin/env bash
# routes-run.selftest.sh -- the run script, end to end, through the fake KV only (docs/route-weekly-code.md, section 2f,
# RR1-RR5). The same synthetic fixture as routes-writer.selftest.sh. No network: the source comes from ROUTES_SOURCE_DIR
# and main's sha from ROUTES_RUN_GH, and FK1 refuses both hooks wherever a real credential is in reach.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
T="$(mktemp -d)"; F="$T/fx"; trap 'rm -rf "$T"' EXIT
npx tsx scripts/routes-fixture.mts "$F" > "$T/fixture.out" 2>&1 || { cat "$T/fixture.out"; echo "SELFTEST FAILED: the fixture did not build"; exit 1; }
P1=$(printf '1%.0s' $(seq 40)); P2=$(printf '2%.0s' $(seq 40))
mkdir -p "$T/src"; cp "$F/prev.tar.gz" "$T/src/$P1.tar.gz"; cp "$F/new.tar.gz" "$T/src/$P2.tar.gz"
printf '{"sha":"%s","date":"2026-11-12T10:00:00Z"}' "$P2" > "$T/gh.json"
# A store whose meta is a key-level seal of prev (sourceCommit = prev), as the attended first run would leave it.
node -e '
const fs=require("fs"), [F,out,p1]=process.argv.slice(1);
const s=JSON.parse(fs.readFileSync(F+"/store-prev.json","utf8")), m=JSON.parse(s.staging["meta:routes"]);
m.sourceCommit=p1; m.mode="key-level"; s.staging["meta:routes"]=JSON.stringify(m); fs.writeFileSync(out, JSON.stringify(s));
' "$F" "$T/store-kl.json" "$P1"

pass=0; fail=0; failed=""
ok()  { echo "PASS $1"; pass=$((pass+1)); }
bad() { echo "FAIL $1: $2"; fail=$((fail+1)); failed="$failed $1"; }
sha() { node -e 'process.stdout.write(require("crypto").createHash("sha256").update(require("fs").readFileSync(process.argv[1])).digest("hex"))' "$1"; }
meta() { node -e 'const m=JSON.parse(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).staging["meta:routes"]); process.stdout.write(String(m[process.argv[2]]??"-"))' "$T/s.json" "$1"; }
run() { # run <outfile> [VAR=value ...]: the run script on staging through the fake; echoes the exit status
  local o="$1"; shift
  env -u ROUTES_FREEZE_UNTIL ROUTES_KV_CLI=scripts/routes-fakekv.mjs ROUTES_FAKEKV_STORE="$T/s.json" ROUTES_SOURCE_DIR="$T/src" \
    ROUTES_RUN_GH="$T/gh.json" "$@" bash scripts/routes-refresh-run.sh staging > "$o" 2>&1
  echo $?
}
has() { grep -qF -- "$2" "$1"; }

# ---- RR1: a changed source is written, verified and sealed -----------------------------------------------------------
cp "$T/store-kl.json" "$T/s.json"
rc=$(run "$T/rr1.out")
if [ "$rc" = 0 ] && has "$T/rr1.out" "routes-refresh: sealed staging at source 222222222222" && [ "$(meta sourceCommit)" = "$P2" ] \
  && [ "$(meta prevSourceCommit)" = "$P1" ] && [ "$(meta sourceCommittedAt)" = "2026-11-12T10:00:00Z" ]; then ok RR1; cp "$T/s.json" "$T/sealed.json"
else bad RR1 "exit $rc; sourceCommit $(meta sourceCommit | cut -c1-12)"; fi

# ---- RR2: the same source again replays, verifies and re-seals (builtAt moves) --------------------------------------
if [ -f "$T/sealed.json" ]; then
  cp "$T/sealed.json" "$T/s.json"; b0=$(meta builtAt)
  rc=$(run "$T/rr2.out")
  if [ "$rc" = 0 ] && has "$T/rr2.out" "replaying the last change" && [ "$(meta builtAt)" != "$b0" ] && [ "$(meta sourceCommit)" = "$P2" ]; then ok RR2
  else bad RR2 "exit $rc"; fi
else bad RR2 "no sealed store (RR1 failed)"; fi

# ---- RR3: a value that stays wrong through the one repair is verify-fail, with meta untouched -----------------------
cp "$T/store-kl.json" "$T/s.json"; m0=$(node -e 'process.stdout.write(require("crypto").createHash("sha256").update(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).staging["meta:routes"]).digest("hex"))' "$T/s.json")
PLANT="node -e 'const fs=require(\"fs\"),S=process.argv[1],E=process.argv[2];const s=JSON.parse(fs.readFileSync(S,\"utf8\")),e=JSON.parse(fs.readFileSync(E,\"utf8\"));s.staging[e.changedKeys[0]]=JSON.stringify({o:\"XXX\",d:\"YYY\"});fs.writeFileSync(S,JSON.stringify(s));' '$T/s.json' '$F/expect.json'"
rc=$(run "$T/rr3.out" ROUTES_RUN_PLANT="$PLANT")
m1=$(node -e 'process.stdout.write(require("crypto").createHash("sha256").update(JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).staging["meta:routes"]).digest("hex"))' "$T/s.json")
if [ "$rc" = 1 ] && has "$T/rr3.out" "one repair" && has "$T/rr3.out" "routes-refresh: FAILED, class verify-fail" \
  && has "$T/rr3.out" "Route refresh, staging: verify-fail" && [ "$m1" = "$m0" ]; then ok RR3
else bad RR3 "exit $rc, meta unchanged: $([ "$m1" = "$m0" ] && echo yes || echo no)"; fi

# ---- RR4: a meta with no sourceCommit is a first run, refused before any write ---------------------------------------
cp "$F/store-prev.json" "$T/s.json"; h0=$(sha "$T/s.json")
rc=$(run "$T/rr4.out")
if [ "$rc" = 1 ] && has "$T/rr4.out" "class meta-not-key-level" && [ "$(sha "$T/s.json")" = "$h0" ]; then ok RR4; else bad RR4 "exit $rc"; fi

# ---- RR5: a future freeze ends green with the stated line, and reads no KV at all -----------------------------------
cp "$T/store-kl.json" "$T/s.json"
rc=$(run "$T/rr5.out" ROUTES_FREEZE_UNTIL=2099-01-01T00:00:00Z ROUTES_KV_CLI=scripts/no-such-kv-cli.mjs)
if [ "$rc" = 0 ] && has "$T/rr5.out" "freeze: SKIP -- frozen until 2099-01-01T00:00:00Z" && has "$T/rr5.out" "routes-refresh: frozen, nothing done"; then ok RR5
else bad RR5 "exit $rc"; fi

echo "rows: $pass passed, $fail failed"
if [ "$fail" -gt 0 ]; then echo "SELFTEST FAILED:$failed"; exit 1; fi
echo "SELFTEST PASSED ($pass rows)"
