# Sourced by smoke-prod.sh and watch-upstream.sh (docs/blip-creds-token-file.md).
#
# While BLIP_KEY and BLIP_DEVICE are both set, this does nothing: the variables keep precedence, so every reader
# behaves exactly as before until Daniel moves the values. Otherwise, when BLIPSCOPE_TOKEN_FILE is set, the calling
# script is run ONCE more through with-blip-creds.mts, which reads the file through token-file.ts (the one reader,
# with its refusals) and passes the two values in the child's environment only -- never on argv, stdout, a file or a
# pipe. BLIPSCOPE_CREDS_REEXEC guards a second re-run: still unset then, the script's own "not set" error stands.
#
#   . "$SCRIPT_DIR/blip-creds.sh"; blip_creds_or_reexec "$SCRIPT_DIR/<this script>" "$@"
blip_creds_or_reexec() {
  if [ -n "${BLIP_KEY:-}" ] && [ -n "${BLIP_DEVICE:-}" ]; then return 0; fi
  if [ -n "${BLIPSCOPE_CREDS_REEXEC:-}" ] || [ -z "${BLIPSCOPE_TOKEN_FILE:-}" ]; then return 0; fi
  local here
  here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  exec node "$here/with-blip-creds.mts" "$@"
}
