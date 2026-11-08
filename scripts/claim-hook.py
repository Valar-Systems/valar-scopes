#!/usr/bin/env python3
"""A Claude Code PreToolUse hook (docs/session-claims.md, Q-CL1 ruled 2026-11-07): refuse `wrangler deploy` and `wrangler secret
put` unless the caller holds the deploy claim. NEVER refuse a rollback: a lock must never stand between production and its
rollback, so a rollback goes ahead, is logged beside the claims, and the session is told to announce it.

The caller names itself with BLIPSCOPE_SESSION=<name> in the command, or in its environment. Exit 2 refuses the tool call, and
what this prints on stderr is shown to the session. Anything that isn't one of the gated commands passes untouched.
"""
import importlib.util
import json
import os
import re
import sys
from datetime import datetime, timezone

GATED = re.compile(r'\bwrangler(?:\.cmd)?\s+(?:deploy\b|secret\s+(?:put|bulk)\b|versions\s+deploy\b)')
ROLLBACK = re.compile(r'\bwrangler(?:\.cmd)?\s+rollback\b')
DRY = re.compile(r'--dry-run\b')
NAMED = re.compile(r'\bBLIPSCOPE_SESSION=([A-Za-z0-9][A-Za-z0-9._-]{0,63})\b')


def claims():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location('session_claim', os.path.join(here, 'session-claim.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def decide(command, env_session):
    """(exit code, message). 0 lets the command run; 2 refuses it."""
    if 'wrangler' not in command:
        return 0, ''
    sc = claims()
    if ROLLBACK.search(command):
        m = NAMED.search(command)
        who = m.group(1) if m else (env_session or 'unnamed')
        os.makedirs(sc.claims_dir(), exist_ok=True)
        with open(os.path.join(sc.claims_dir(), 'rollbacks.log'), 'a', encoding='utf-8') as f:
            f.write('%s %s %s\n' % (datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'), who, ' '.join(command.split())[:200]))
        return 0, 'claim-hook: a rollback is never refused. It is logged; announce it through Daniel.'
    if not GATED.search(command) or DRY.search(command):
        return 0, ''
    m = NAMED.search(command)
    who = m.group(1) if m else env_session
    if not who:
        return 2, ('claim-hook: REFUSED. `wrangler deploy` and `wrangler secret put` need the caller\'s deploy claim. Name the '
                   'session (BLIPSCOPE_SESSION=<name> in the command) and take the claim: python scripts/session-claim.py take deploy '
                   '--covers <worker>:<env> --until +45m')
    cur = sc.read_claim('deploy')
    if cur is None:
        return 2, 'claim-hook: REFUSED. Nobody holds the deploy claim, so %s does not. Take it first.' % who
    if 'unreadable' in cur:
        return 2, 'claim-hook: REFUSED. The deploy claim is unreadable: a person must look at %s' % sc.claim_path('deploy')
    if sc.expired(cur):
        return 2, 'claim-hook: REFUSED. The deploy claim is EXPIRED (%s). It is never overwritten.' % sc.say('deploy', cur)
    if cur['session'] != who:
        return 2, 'claim-hook: REFUSED. The deploy claim is held by %s.' % sc.say('deploy', cur)
    return 0, ''


def main():
    try:
        event = json.load(sys.stdin)
    except ValueError:
        return 0
    command = (event.get('tool_input') or {}).get('command') or ''
    code, msg = decide(command, os.environ.get('BLIPSCOPE_SESSION', '').strip() or None)
    if msg:
        print(msg, file=sys.stderr)
    return code


if __name__ == '__main__':
    sys.exit(main())
