# routes_age_verdict <body file> <mode>: one line, "PASS -- ...", "WARN -- ..." or "FAIL -- ...", for smoke-prod.sh's
# /healthz routes mirror age block (docs/route-mirror-age.md, section 3). Sourced by smoke-prod.sh; graded by
# routes-age-verdict.selftest.sh, which CI runs. <mode> is warn or fail: the same judgement, and only the word for a bad
# reading changes. The limit comes from the body's own `maxAgeDays`, so the rule is never restated here.
#
#   routes.stale false, an age                 PASS  built <ISO>, <n> d old (limit <L> d)
#   routes.stale true, an age                  WARN|FAIL  STALE: built <ISO>, <n> d old (limit <L> d)
#   routes with a null age                     WARN|FAIL  no age: <why>
#   a JSON body without a routes field         WARN|FAIL  /healthz has no routes field (a Worker before this change)
#   an empty or non-JSON body                  WARN|FAIL  /healthz body unreadable
#
# It prints only the routes fields it names, never the body.
routes_age_verdict() {
  local body="$1" mode="$2" bad obj built age stale lim why
  if [ "$mode" = "fail" ]; then bad="FAIL"; else bad="WARN"; fi
  if [ ! -s "$body" ] || [ "$(tr -d ' \t\r\n' < "$body" | head -c 1)" != "{" ]; then
    printf '%s -- /healthz body unreadable\n' "$bad"; return
  fi
  obj="$(grep -oE '"routes":\{[^}]*\}' "$body" | head -1)"
  if [ -z "$obj" ]; then
    printf '%s -- /healthz has no routes field (a Worker before this change)\n' "$bad"; return
  fi
  built="$(printf '%s' "$obj" | grep -oE '"builtAt":("[^"]*"|null)' | head -1 | cut -d: -f2- | tr -d '"')"
  age="$(printf '%s' "$obj" | grep -oE '"ageDays":(-?[0-9]+|null)' | head -1 | cut -d: -f2)"
  stale="$(printf '%s' "$obj" | grep -oE '"stale":(true|false)' | head -1 | cut -d: -f2)"
  lim="$(printf '%s' "$obj" | grep -oE '"maxAgeDays":[0-9]+' | head -1 | cut -d: -f2)"
  why="$(printf '%s' "$obj" | grep -oE '"why":"[a-z]+"' | head -1 | cut -d: -f2 | tr -d '"')"
  if [ -z "$stale" ] || [ -z "$lim" ]; then
    printf '%s -- routes field malformed\n' "$bad"; return
  fi
  if [ -z "$built" ] || [ "$built" = "null" ] || [ -z "$age" ] || [ "$age" = "null" ]; then
    printf '%s -- no age: %s\n' "$bad" "${why:-unknown}"; return
  fi
  if [ "$stale" = "true" ]; then
    printf '%s -- STALE: built %s, %s d old (limit %s d)\n' "$bad" "$built" "$age" "$lim"; return
  fi
  printf 'PASS -- built %s, %s d old (limit %s d)\n' "$built" "$age" "$lim"
}
