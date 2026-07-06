#!/usr/bin/env python3
"""claudescope-sidecar -- serves your live Claude usage limits to a Blipscope Claudescope device.

WHY THIS EXISTS
    The session / weekly usage percentages the Claudescope screen shows are the same numbers you
    see in Claude Code's status bar and at claude.ai/settings/usage. There is no *supported* public
    API for them, but the desktop app and the CLI populate that widget from an undocumented,
    OAuth-authenticated endpoint. This sidecar reads that endpoint with YOUR OWN Claude credentials
    (the ones Claude Code already stores and refreshes) and republishes the result as a tiny, stable
    JSON document on your LAN. The ESP32 then just polls that JSON.

    The device never holds your OAuth token and never talks to Anthropic directly -- the sidecar is
    the only thing that touches the credential. Keep it on a machine you trust (typically the same
    machine you run Claude Code on).

    This is unofficial and reads YOUR account's own usage. The endpoint is undocumented and can change
    or break at any time; treat this as a fun read-out, not infrastructure. See README.md.

WHAT IT DOES
    1. Loads the Claude Code OAuth access token (from ~/.claude/.credentials.json, or a token you
       pass via CLAUDE_CODE_OAUTH_TOKEN). Because Claude Code refreshes that file while it runs, the
       sidecar re-reads it every poll and rides that refresh -- no OAuth dance of its own.
    2. Polls GET https://api.anthropic.com/api/oauth/usage on an interval and normalizes the reply.
    3. Serves the normalized shape at http://<this-host>:<port>/usage.json for the device.

    Standard library only -- no `pip install`. Python 3.8+.

USAGE
    python3 sidecar.py
    # then set the device's "Sidecar URL" (web config) to http://<this-host>:<port>

CONFIG (environment variables, all optional)
    CLAUDESCOPE_PORT           TCP port to serve on           (default 8080)
    CLAUDESCOPE_BIND           bind address                   (default 0.0.0.0 = all interfaces)
    CLAUDESCOPE_POLL_SECONDS   seconds between upstream polls (default 60; the endpoint rate-limits
                                                               hard, so don't go much below 30)
    CLAUDESCOPE_PLAN           plan label to show on-device   (default "": use whatever upstream gives)
    CLAUDE_CREDENTIALS_PATH    path to Claude Code creds JSON (default ~/.claude/.credentials.json)
    CLAUDE_CODE_OAUTH_TOKEN    use this bearer token directly, skipping the credentials file
    CLAUDE_CODE_USER_AGENT     UA sent upstream               (default "claude-code/1.0.0"; the
                                                               endpoint punishes non-claude-code UAs
                                                               with aggressive 429s, so keep the prefix)
"""

import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ----------------------------------------------------------------------------------- config
PORT          = int(os.environ.get("CLAUDESCOPE_PORT", "8080"))
BIND          = os.environ.get("CLAUDESCOPE_BIND", "0.0.0.0")
POLL_SECONDS  = max(15, int(os.environ.get("CLAUDESCOPE_POLL_SECONDS", "60")))
PLAN_OVERRIDE = os.environ.get("CLAUDESCOPE_PLAN", "")
CREDS_PATH    = os.environ.get(
    "CLAUDE_CREDENTIALS_PATH",
    os.path.join(os.path.expanduser("~"), ".claude", ".credentials.json"),
)
USER_AGENT    = os.environ.get("CLAUDE_CODE_USER_AGENT", "claude-code/1.0.0")
USAGE_URL     = "https://api.anthropic.com/api/oauth/usage"

# The latest normalized snapshot, guarded by a lock (poller writes, HTTP handler reads).
_state_lock = threading.Lock()
_state = {
    "valid": False,
    "plan": PLAN_OVERRIDE,
    "session": {},
    "week_all": {},
    "week_models": [],
    "extra_usage": False,
    "updated_at": 0,
    "error": "starting up",
}


# ----------------------------------------------------------------------------- token loading
def load_token():
    """Return the current Claude OAuth access token, or None. Re-read every poll so we pick up the
    token Claude Code refreshes in place."""
    env_tok = os.environ.get("CLAUDE_CODE_OAUTH_TOKEN")
    if env_tok:
        return env_tok.strip()
    try:
        with open(CREDS_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return None
    # Claude Code shape: {"claudeAiOauth": {"accessToken": "...", "refreshToken": "...", ...}}
    oauth = data.get("claudeAiOauth") or data.get("claude_ai_oauth") or {}
    tok = oauth.get("accessToken") or oauth.get("access_token")
    return tok.strip() if tok else None


# --------------------------------------------------------------------------- normalization
def to_epoch(value):
    """Coerce a reset timestamp (ISO-8601 string, ms epoch, or s epoch) to Unix seconds; 0 if unknown."""
    if value is None:
        return 0
    if isinstance(value, (int, float)):
        v = float(value)
        return int(v / 1000) if v > 1e12 else int(v)  # ms -> s if it looks like millis
    if isinstance(value, str):
        s = value.strip()
        if not s:
            return 0
        if s.isdigit():
            return to_epoch(int(s))
        try:
            # tolerate a trailing 'Z'
            dt = datetime.fromisoformat(s.replace("Z", "+00:00"))
            if dt.tzinfo is None:
                dt = dt.replace(tzinfo=timezone.utc)
            return int(dt.timestamp())
        except ValueError:
            return 0
    return 0


def to_pct(obj):
    """Pull a 0..100 percent-used value out of a window object, tolerating field-name variation."""
    if not isinstance(obj, dict):
        return None
    for key in ("utilization", "used_pct", "percent_used", "percentUsed", "pct", "percent"):
        if key in obj and obj[key] is not None:
            try:
                v = float(obj[key])
            except (TypeError, ValueError):
                continue
            if 0.0 <= v <= 1.0:  # some builds report a 0..1 fraction
                v *= 100.0
            return max(0.0, min(100.0, v))
    return None


def window(obj, label=""):
    """Normalize one upstream window object to {"pct", "resets_at", ["label"]} or None."""
    pct = to_pct(obj)
    if pct is None:
        return None
    reset = 0
    if isinstance(obj, dict):
        reset = to_epoch(obj.get("resets_at") or obj.get("reset_at") or obj.get("resetsAt"))
    out = {"pct": round(pct, 1), "resets_at": reset}
    if label:
        out["label"] = label
    return out


# Pretty labels for the per-model weekly windows the endpoint exposes as seven_day_<model>.
_MODEL_LABELS = {"opus": "Opus", "sonnet": "Sonnet", "haiku": "Haiku", "fable": "Fable"}


def normalize(raw):
    """Map the undocumented upstream reply to the shape the Claudescope device expects."""
    snap = {
        "valid": False,
        "plan": PLAN_OVERRIDE or str(raw.get("plan") or raw.get("plan_name") or ""),
        "session": {},
        "week_all": {},
        "week_models": [],
        "extra_usage": bool(raw.get("extra_usage") or raw.get("usage_credits_enabled") or False),
        "updated_at": int(time.time()),
    }

    sess = window(raw.get("five_hour") or raw.get("session"))
    if sess:
        snap["session"] = sess

    week = window(raw.get("seven_day") or raw.get("week_all") or raw.get("weekly"))
    if week:
        snap["week_all"] = week

    # Per-model weekly windows: any "seven_day_<model>" key with a utilization value.
    for key, val in raw.items():
        if not key.startswith("seven_day_"):
            continue
        model = key[len("seven_day_"):]
        w = window(val, _MODEL_LABELS.get(model, model.capitalize()))
        if w:
            snap["week_models"].append(w)

    snap["valid"] = bool(snap["session"] or snap["week_all"] or snap["week_models"])
    return snap


# --------------------------------------------------------------------------------- polling
def fetch_usage(token):
    req = urllib.request.Request(
        USAGE_URL,
        headers={
            "Authorization": f"Bearer {token}",
            "anthropic-beta": "oauth-2025-04-20",
            "anthropic-version": "2023-06-01",
            "User-Agent": USER_AGENT,
            "Accept": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.load(resp)


def poll_loop():
    while True:
        err = None
        try:
            token = load_token()
            if not token:
                err = (
                    "no Claude token found -- run Claude Code once (so it writes "
                    f"{CREDS_PATH}), or set CLAUDE_CODE_OAUTH_TOKEN"
                )
            else:
                raw = fetch_usage(token)
                snap = normalize(raw)
                with _state_lock:
                    _state.clear()
                    _state.update(snap)
                    _state["error"] = None
                s = snap["session"].get("pct")
                w = snap["week_all"].get("pct")
                print(f"[claudescope] ok  session={s} week={w} models={len(snap['week_models'])}")
        except urllib.error.HTTPError as e:
            if e.code == 401:
                err = "401 Unauthorized -- token expired; run Claude Code to refresh it"
            elif e.code == 429:
                err = "429 rate-limited by the usage endpoint -- increase CLAUDESCOPE_POLL_SECONDS"
            else:
                err = f"HTTP {e.code} from the usage endpoint"
        except urllib.error.URLError as e:
            err = f"network error: {e.reason}"
        except Exception as e:  # noqa: BLE001 -- keep the poller alive whatever happens
            err = f"{type(e).__name__}: {e}"

        if err:
            print(f"[claudescope] {err}", file=sys.stderr)
            with _state_lock:
                _state["error"] = err  # keep last-good data; just record why we didn't refresh

        time.sleep(POLL_SECONDS)


# ----------------------------------------------------------------------------------- server
class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body):
        payload = json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):  # noqa: N802 -- BaseHTTPRequestHandler API
        path = self.path.split("?", 1)[0]
        if path in ("/usage.json", "/usage", "/"):
            with _state_lock:
                self._send(200, dict(_state))
        else:
            self._send(404, {"error": "not found"})

    def log_message(self, *_args):
        pass  # quiet; the poller already logs what matters


def main():
    src = "env token" if os.environ.get("CLAUDE_CODE_OAUTH_TOKEN") else CREDS_PATH
    print(f"[claudescope] sidecar starting")
    print(f"[claudescope]   token source : {src}")
    print(f"[claudescope]   poll every   : {POLL_SECONDS}s")
    print(f"[claudescope]   serving      : http://{BIND}:{PORT}/usage.json")
    print(f"[claudescope]   set the device's 'Sidecar URL' to http://<this-host>:{PORT}")

    threading.Thread(target=poll_loop, daemon=True).start()
    ThreadingHTTPServer((BIND, PORT), Handler).serve_forever()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[claudescope] bye")
