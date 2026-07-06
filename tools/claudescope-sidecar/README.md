# claudescope-sidecar

Serves your live **Claude usage limits** (current session + weekly caps) to a Blipscope
**Claudescope** device as a small JSON document on your LAN.

The device shows the same numbers you see in Claude Code's status bar and at
`claude.ai/settings/usage`. There is no *supported* public API for them — but the desktop app and
the CLI populate that widget from an **undocumented, OAuth-authenticated** endpoint. This sidecar
reads that endpoint with **your own** Claude credentials (the ones Claude Code already stores and
refreshes) and republishes the result. The ESP32 only ever sees pre-chewed JSON — **your OAuth token
never leaves this machine**.

> ⚠️ **Unofficial and best-effort.** The endpoint is undocumented and reads *your* account's own
> usage. Anthropic can change or remove it at any time; treat this as a fun desk read-out, not
> infrastructure. It is not affiliated with or endorsed by Anthropic.

## Requirements

- Python 3.8+ (standard library only — no `pip install`).
- Claude Code installed and signed in on this machine (so a token exists and stays refreshed), **or**
  a token from `claude setup-token`.
- The machine stays on and reachable from the device's network while you want live numbers.

## Run it

```sh
python3 sidecar.py
```

You'll see something like:

```
[claudescope] sidecar starting
[claudescope]   token source : /home/you/.claude/.credentials.json
[claudescope]   poll every   : 60s
[claudescope]   serving      : http://0.0.0.0:8080/usage.json
[claudescope]   set the device's 'Sidecar URL' to http://<this-host>:8080
```

Then open the device's config page (`http://<device-name>.local`) and set **Sidecar URL** to
`http://<this-host>:8080` (a bare host/port — the device appends `/usage.json`). Use the machine's
LAN IP (e.g. `http://192.168.1.50:8080`) if `.local` names don't resolve on your network.

Check it yourself in a browser or with curl:

```sh
curl http://localhost:8080/usage.json
```

```json
{
  "valid": true,
  "plan": "Max (20x)",
  "session":  { "pct": 34.0, "resets_at": 1751824000 },
  "week_all": { "pct": 16.0, "resets_at": 1752000000 },
  "week_models": [ { "label": "Fable", "pct": 2.0, "resets_at": 1752000000 } ],
  "extra_usage": false,
  "updated_at": 1751820000,
  "error": null
}
```

## How the token works

Claude Code stores its OAuth token at `~/.claude/.credentials.json` and **refreshes it in place**
(access tokens last ~60 minutes). The sidecar re-reads that file on every poll, so it rides Claude
Code's refresh and never implements an OAuth flow of its own. Keep Claude Code signed in on this
machine and the token stays fresh.

If you'd rather not depend on the credentials file, generate a long-lived token with
`claude setup-token` and pass it via the `CLAUDE_CODE_OAUTH_TOKEN` env var (below).

> **macOS Keychain:** if your Claude Code build stores credentials in the login Keychain instead of
> the file, export them once with
> `security find-generic-password -s "Claude Code-credentials" -w > ~/.claude/.credentials.json`,
> or use `CLAUDE_CODE_OAUTH_TOKEN`.

## Configuration (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `CLAUDESCOPE_PORT` | `8080` | TCP port to serve on |
| `CLAUDESCOPE_BIND` | `0.0.0.0` | Bind address (all interfaces) |
| `CLAUDESCOPE_POLL_SECONDS` | `60` | Seconds between upstream polls (min 15; the endpoint rate-limits hard — keep ≥ 30) |
| `CLAUDESCOPE_PLAN` | `""` | Force the plan label shown on-device (e.g. `Max (20x)`) if upstream doesn't return it |
| `CLAUDE_CREDENTIALS_PATH` | `~/.claude/.credentials.json` | Where to read the Claude Code token |
| `CLAUDE_CODE_OAUTH_TOKEN` | — | Use this bearer token directly, skipping the credentials file |
| `CLAUDE_CODE_USER_AGENT` | `claude-code/1.0.0` | User-Agent sent upstream (keep the `claude-code/` prefix — other UAs hit an aggressive rate-limit bucket) |

Example — pin the plan label and slow the poll down:

```sh
CLAUDESCOPE_PLAN="Max (20x)" CLAUDESCOPE_POLL_SECONDS=90 python3 sidecar.py
```

## Keep it running

- **systemd (Linux):** point a simple unit at `python3 /path/to/sidecar.py` with
  `Restart=always` and `User=` set to the account that has Claude Code signed in.
- **launchd (macOS):** a LaunchAgent running the same command.
- **Windows:** Task Scheduler ("At log on", restart on failure), or `nssm` to run it as a service.

## Security notes

- The sidecar holds a **powerful account credential**. Run it on a machine you trust, as the user who
  owns the Claude login. It exposes **only** the read-only usage numbers over HTTP — never the token.
- It listens on your LAN unauthenticated (any device that can reach the port can read your usage
  percentages). That's fine for a home network; if you don't want that, bind to a specific interface
  with `CLAUDESCOPE_BIND` or put it behind your own auth/proxy.
- Nothing is written to Anthropic — the sidecar only issues read GETs to the usage endpoint.

## Troubleshooting

| Symptom (`error` field / stderr) | Fix |
|---|---|
| `no Claude token found` | Run Claude Code once so it writes `~/.claude/.credentials.json`, or set `CLAUDE_CODE_OAUTH_TOKEN`. |
| `401 Unauthorized` | Token expired — open Claude Code so it refreshes, or mint a new `claude setup-token`. |
| `429 rate-limited` | Raise `CLAUDESCOPE_POLL_SECONDS` (the endpoint is stingy; 60–120 s is plenty). |
| Device stuck on the splash | Confirm the Sidecar URL is reachable from the device's network and `curl` returns `"valid": true`. |
