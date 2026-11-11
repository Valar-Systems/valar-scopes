# healthz-commit.sh -- /healthz's build stamp, read from the TOP-LEVEL "commit" key only (docs/healthz-commit-read.md).
# Sourced by proxy/scripts/confirm-deploy.sh, proxy/scripts/smoke-prod.sh and tools/wx/r12_prod.sh; graded by
# healthz-commit.selftest.sh, which CI runs.
#
# WHY NOT A GREP. /healthz carries more than one key named "commit": basemap.commit is the bound builder's sha once
# it has answered in an isolate. A pattern like '"commit":"[^"]*"' matches every one of them, so confirm-deploy read
# two lines (WRONG_SHA on a good deploy) and smoke's stamp check passed a "dev", "-dirty" or missing stamp sitting
# beside a real builder sha. And deploy.sh stamps the builder and the proxy with the SAME sha, so a nested match can
# even equal the sha being confirmed. Taking the first match is not the fix either: with no top-level stamp it reads
# the builder's (the selftest's P-FIRST plant).

# healthz_commit: a /healthz body on stdin -> its top-level "commit" when that is a string; nothing otherwise (absent,
# null, not a string, not JSON). It needs node, which every caller already has (deploy.sh runs wrangler through npx).
# Without node this prints nothing and the shell says why on stderr; nothing reads as WRONG_SHA in confirm-deploy and
# as a FAIL in smoke -- loud, never a pass.
healthz_commit() {
  node -e 'let s = ""; process.stdin.setEncoding("utf8");
process.stdin.on("data", (d) => { s += d; }).on("end", () => {
  let j; try { j = JSON.parse(s); } catch { return; }
  if (j !== null && typeof j === "object" && !Array.isArray(j) && typeof j.commit === "string") process.stdout.write(j.commit);
});'
}

# stamp_verdict <commit>: smoke-prod.sh's build-stamp decision, moved here unchanged so the selftest can grade it.
#   PASS         a real stamp
#   FAIL-dirty   a stamp ending in -dirty (deployed with ALLOW_DIRTY=1)
#   FAIL-absent  empty, or "dev" (a hand-run `wrangler deploy` bypassed deploy.sh's guards)
stamp_verdict() {
  if [ -n "$1" ] && [ "$1" != "dev" ]; then
    case "$1" in
      *-dirty) echo FAIL-dirty ;;
      *)       echo PASS ;;
    esac
  else
    echo FAIL-absent
  fi
}
