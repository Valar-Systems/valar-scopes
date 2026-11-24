# enrich-fixed-verdict.sh: smoke-prod.sh section 2c, enrich on a FIXED hex. Sourced by smoke-prod.sh; graded by
# enrich-fixed-verdict.selftest.sh, which CI runs. Pure functions over saved responses: nothing here makes a request.
#
# WHY 2c EXISTS. Section 2 enriches a LIVE hex (the first one /v1/blips returns) and its row grades the HTTP status
# only. So a deploy's before-run and after-run enrich different aircraft whenever the sky changes, and a grader comparing
# them can only say "not comparable" -- which is how an enrichment change went unproven on 2026-11-23. 2c asks the SAME
# hex in both runs and grades the ANSWER.
#
#   - THE HEX. SMOKE_FIXED_HEX when the deploy runner hands one over (the hex the before-run's "2c hex:" line printed),
#     else this run's T1 hex. The row NAMES carry the hex only and the source is an information line, so a forgotten
#     handoff gives either the same hex (the same aircraft: a genuine compare) or a different one (different names: no
#     compare). A wrong aircraft is never compared silently.
#   - NOT A CONSTANT HEX, deliberately. Its stored entry expires within 30 days, and once that aircraft stops flying
#     every run reads the same EMPTY answer, which compares equal and passes forever: a check that cannot fail.
#   - THE CONTROL, /v1/enrich/~<hex>, must read NOT FOUND from the BODY: enrich.ts answers every "~" id with the empty
#     body before any KV read or upstream fetch. A grade that read only the status would pass it.
#
# Printed: statuses, v, r, t, tn, pk and p. Never op, never a body. A malformed SMOKE_FIXED_HEX is refused by its
# LENGTH, never echoed.

# A Windows node cannot open an MSYS path such as /tmp/x; Git Bash's cygpath converts it, and elsewhere it is a no-op.
_ef_path() {
  if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi
}

# _ef_read <body file>: "NOTJSON", or "JSON v=<v> r=<json> t=<json> tn=<json> pk=<json> p=<json>", a missing field
# reading null. op is never read. Fails closed: no node, or an unreadable file, reads NOTJSON.
_ef_read() {
  node -e '
    let j;
    try { j = JSON.parse(require("fs").readFileSync(process.argv[1], "utf8")); } catch { j = undefined; }
    if (j === null || typeof j !== "object" || Array.isArray(j)) { console.log("NOTJSON"); process.exit(0); }
    const shown = ["v", "r", "t", "tn", "pk", "p"];
    console.log(["JSON", ...shown.map((k) => k + "=" + JSON.stringify(j[k] ?? null))].join(" "));
  ' "$(_ef_path "$1")" 2>/dev/null || printf 'NOTJSON\n'
}

# enrich_fixed_hex <SMOKE_FIXED_HEX value> <this run's T1 hex>: "<hex> SMOKE_FIXED_HEX", "<hex> T1", "REFUSED <length>"
# (a malformed handoff: its length only, and no request), or "NONE <why>".
enrich_fixed_hex() {
  local fixed="$1" t1="$2"
  if [ -n "$fixed" ]; then
    printf '%s' "$fixed" | grep -qxE '[0-9a-f]{6}' || { printf 'REFUSED %s\n' "${#fixed}"; return; }
    printf '%s SMOKE_FIXED_HEX\n' "$fixed"; return
  fi
  case "$t1" in
    "") printf 'NONE no T1 hex in this run\n' ;;
    "~"*) printf "NONE this run's T1 is a non-ICAO ~ id\n" ;;
    *) if printf '%s' "$t1" | grep -qxE '[0-9a-f]{6}'; then printf '%s T1\n' "$t1"
       else printf "NONE this run's T1 hex is not 6 lowercase hex digits\n"; fi ;;
  esac
}

# enrich_fixed_row <http status> <body file>: one line, PASS or FAIL, for 2c-E0, 2c-E6 and 2c-E7.
enrich_fixed_row() {
  local status="$1" f
  [ "$status" = "200" ] || { printf 'FAIL -- HTTP %s\n' "${status:-none}"; return; }
  f="$(_ef_read "$2")"
  [ "$f" != "NOTJSON" ] || { printf 'FAIL -- HTTP 200, but the body is not JSON\n'; return; }
  case " $f " in
    *" v=1 "*) printf 'PASS -- HTTP 200, JSON, v 1\n' ;;
    *) printf 'FAIL -- HTTP 200, JSON, but v is not 1\n' ;;
  esac
}

# _ef_notfound_misses <_ef_read line>: the fields that are not as NOT FOUND, by NAME only (never a value).
_ef_notfound_misses() {
  local f=" $1 " m="" k
  case "$f" in *" v=1 "*) ;; *) m="$m v" ;; esac
  for k in r t tn; do case "$f" in *" $k=\"\" "*) ;; *) m="$m $k" ;; esac; done
  case "$f" in *" p=null "*|*' p="" '*) ;; *) m="$m p" ;; esac
  printf '%s' "${m# }"
}

# enrich_fixed_control <http status> <body file>: 2c-K. PASS only on NOT FOUND read from the body; anything else is
# "FAIL (control)", which the smoke counts as a failure: a control that can fail silently is no control.
enrich_fixed_control() {
  local status="$1" f bad
  [ "$status" = "200" ] || { printf 'FAIL (control) -- HTTP %s\n' "${status:-none}"; return; }
  f="$(_ef_read "$2")"
  [ "$f" != "NOTJSON" ] || { printf 'FAIL (control) -- HTTP 200, but the body is not JSON\n'; return; }
  bad="$(_ef_notfound_misses "$f")"
  [ -z "$bad" ] || { printf 'FAIL (control) -- not NOT FOUND (%s)\n' "$bad"; return; }
  printf "PASS -- NOT FOUND, the code's answer for a non-ICAO id\n"
}

# enrich_fixed_answer <hex> <E0 body> <E6 body> <E7 body>: the one line a grader compares between two runs. The
# identity comes from E0 (no FW headers); the pointers from all three. An unreadable body reads "?". op is never read.
enrich_fixed_answer() {
  node -e '
    const fs = require("fs");
    const rd = (f) => { try { const j = JSON.parse(fs.readFileSync(f, "utf8")); return j && typeof j === "object" && !Array.isArray(j) ? j : null; } catch { return null; } };
    const [hex, a, b, c] = process.argv.slice(1);
    const e0 = rd(a), e6 = rd(b), e7 = rd(c);
    const q = (j, k) => (j === null ? "?" : JSON.stringify(j[k] ?? null));
    console.log(`2c-ANSWER ${hex} r=${q(e0, "r")} t=${q(e0, "t")} tn=${q(e0, "tn")} pk=${q(e0, "pk")} p0=${q(e0, "p")} p6=${q(e6, "p")} p7=${q(e7, "p")}`);
  ' "$1" "$(_ef_path "$2")" "$(_ef_path "$3")" "$(_ef_path "$4")" 2>/dev/null || printf '2c-ANSWER %s ?\n' "$1"
}
