# Sourced by smoke-prod.sh and watch-upstream.sh (docs/test-identity.md, items 3 and 7). Two things:
#
# 1. The credential. These checks run ONLY as the test identity. Unless this is already the child, the calling script
#    is run ONCE more through with-test-identity.mts, which is the single refusal point: it refuses (exit 2, before any
#    request) when BLIP_KEY or BLIP_DEVICE is set, when test-device equals records-device, or when a test field is
#    missing; otherwise it passes test-device and test-key (test-key-staging when BASE is staging) to the child in its
#    ENVIRONMENT only, as TEST_IDENTITY_DEVICE and TEST_IDENTITY_KEY. blip-key and blip-device are never read.
#
# 2. The key off every command line. blip_auth_curl defines `curl` as a shell function: an argument --blip-auth is
#    replaced by `-K -`, and the two header lines go to curl on STDIN, built with the printf builtin. So no process
#    ever holds the key or the id as an argument. Call sites keep passing "${AUTH[@]}", which is (--blip-auth).
#
#   . "$SCRIPT_DIR/test-identity.sh"; test_identity_or_reexec "$SCRIPT_DIR/<this script>" "$@"; blip_auth_curl
test_identity_or_reexec() {
  [ -n "${TEST_IDENTITY_REEXEC:-}" ] && return 0
  local here
  here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  exec node "$here/with-test-identity.mts" "$@"
}

blip_auth_curl() {
  if [ -z "${TEST_IDENTITY_KEY:-}" ] || [ -z "${TEST_IDENTITY_DEVICE:-}" ]; then
    echo "REFUSED: no test identity in this environment (run the script itself, which resolves it; docs/test-identity.md)." >&2
    exit 2
  fi
  AUTH=(--blip-auth)
  BLIP_AUTH_CFG="$(printf 'header = "X-Blip-Key: %s"\nheader = "X-Blip-Device: %s"\n' "$TEST_IDENTITY_KEY" "$TEST_IDENTITY_DEVICE")"
  curl() {
    local a=() auth=""
    for x in "$@"; do if [ "$x" = "--blip-auth" ]; then auth=1; else a+=("$x"); fi; done
    if [ -n "$auth" ]; then command curl -K - "${a[@]}" <<<"$BLIP_AUTH_CFG"; else command curl "${a[@]}"; fi
  }
}
