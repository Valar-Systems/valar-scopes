#!/usr/bin/env python3
"""Session claims: who is deploying, and who is merging (docs/session-claims.md, ruled 2026-11-07).

Two claims, each a local file beside the board registry (~/.config/blipscope/claims/deploy.json and merge.json), one holder
each, independent of each other: a long deploy claim never stops a merge. The behaviour is board-port.py's reservations
(exclusive create; an unreadable claim is still a claim; only the holder releases), plus an expected end: an EXPIRED claim
stops everyone until its holder releases it or a person deletes the file. Nothing here overwrites one.

    BLIPSCOPE_SESSION=<name> python3 scripts/session-claim.py take deploy --covers proxy:staging,builder:staging --until +45m
    BLIPSCOPE_SESSION=<name> python3 scripts/session-claim.py check deploy --covers proxy:staging
    BLIPSCOPE_SESSION=<name> python3 scripts/session-claim.py extend deploy --until +30m
    BLIPSCOPE_SESSION=<name> python3 scripts/session-claim.py release deploy
    python3 scripts/session-claim.py show
    python3 scripts/session-claim.py --selftest     # CL1-CL9, in a temporary folder
    python3 scripts/session-claim.py --sabotage     # each plant must turn exactly its rows red

Exit codes: 0 yes; 2 usage (including BLIPSCOPE_SESSION unset); 3 held by another session, or not held; 4 expired or
unreadable. `--until` takes an ISO time in UTC (2026-11-08T03:00:00Z) or +<n>m / +<n>h, at most 24 h ahead.
"""
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta, timezone

CLAIMS = ('deploy', 'merge')
MAX_AHEAD = timedelta(hours=24)


def claims_dir():
    return os.environ.get('BLIPSCOPE_CLAIMS_DIR') or os.path.join(os.path.expanduser('~'), '.config', 'blipscope', 'claims')


def claim_path(claim):
    return os.path.join(claims_dir(), claim + '.json')


def utcnow():
    return datetime.now(timezone.utc).replace(microsecond=0)


def iso(t):
    return t.strftime('%Y-%m-%dT%H:%M:%SZ')


def parse_time(s):
    m = re.fullmatch(r'\+(\d+)([mh])', s or '')
    if m:
        n = int(m.group(1))
        return utcnow() + (timedelta(minutes=n) if m.group(2) == 'm' else timedelta(hours=n))
    try:
        return datetime.strptime(s, '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=timezone.utc)
    except (TypeError, ValueError):
        return None


def session():
    s = os.environ.get('BLIPSCOPE_SESSION', '').strip()
    return s if re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}', s) else None


def read_claim(claim):
    """None when there is no claim. An unreadable or malformed file is still a claim: never treated as free."""
    try:
        with open(claim_path(claim), encoding='utf-8') as f:
            c = json.load(f)
    except FileNotFoundError:
        return None
    except (OSError, ValueError) as e:
        return {'unreadable': '%s' % e.__class__.__name__}
    if not isinstance(c, dict) or not all(isinstance(c.get(k), str) for k in ('session', 'since', 'until')) \
            or not isinstance(c.get('covers'), list):
        return {'unreadable': 'not a claim'}
    return c


def expired(c):
    t = parse_time(c.get('until', ''))
    return t is None or t <= utcnow()


def describe(c):
    if 'unreadable' in c:
        return 'UNREADABLE (%s): a person must look at %s' % (c['unreadable'], '%s')
    return '%s, covering %s, since %s until %s (host %s)' % (c['session'], ','.join(c['covers']) or '-', c['since'], c['until'],
                                                            c.get('host', '?'))


def say(claim, c):
    d = describe(c)
    return d % claim_path(claim) if '%s' in d else d


def take(claim, covers, until):
    who = session()
    if not who:
        print('BLIPSCOPE_SESSION is unset or malformed: a claim names its session', file=sys.stderr)
        return 2
    end = parse_time(until)
    if end is None or end <= utcnow() or end - utcnow() > MAX_AHEAD:
        print('--until must be a UTC time or +<n>m/+<n>h, in the future and at most 24 h ahead', file=sys.stderr)
        return 2
    c = {'session': who, 'covers': covers, 'since': iso(utcnow()), 'until': iso(end), 'host': socket.gethostname()}
    os.makedirs(claims_dir(), exist_ok=True)
    # The claim is written whole to a temporary file first, then published by a hard link, which fails when the name exists.
    # An exclusive create followed by a write would leave a moment when another session reads an empty file: found by CL3.
    fd, tmp = tempfile.mkstemp(dir=claims_dir(), prefix='.%s.' % claim)
    with os.fdopen(fd, 'w', encoding='utf-8') as f:
        json.dump(c, f, indent=1)
    try:
        os.link(tmp, claim_path(claim))
        won = True
    except FileExistsError:
        won = False
    finally:
        os.remove(tmp)
    if not won:
        cur = read_claim(claim) or {'unreadable': 'vanished while read'}
        if 'unreadable' in cur or expired(cur):
            print('%s: %s claim %s -- not overwritten' % (claim, 'EXPIRED' if 'unreadable' not in cur else 'an', say(claim, cur)),
                  file=sys.stderr)
            return 4
        if cur['session'] == who:
            print('%s: already yours (%s)' % (claim, say(claim, cur)))
            return 0
        print('%s: HELD by %s' % (claim, say(claim, cur)), file=sys.stderr)
        return 3
    print('%s: taken by %s' % (claim, say(claim, c)))
    return 0


def check(claim, covers):
    who = session()
    if not who:
        print('BLIPSCOPE_SESSION is unset or malformed', file=sys.stderr)
        return 2
    cur = read_claim(claim)
    if cur is None:
        print('%s: not held by anyone, so not by %s' % (claim, who))
        return 3
    if 'unreadable' in cur:
        print('%s: %s' % (claim, say(claim, cur)))
        return 4
    if expired(cur):
        print('%s: EXPIRED, %s' % (claim, say(claim, cur)))
        return 4
    if cur['session'] != who:
        print('%s: HELD by %s' % (claim, say(claim, cur)))
        return 3
    missing = [x for x in covers if x not in cur['covers']]
    if missing:
        print('%s: yours, but it does not cover %s (%s)' % (claim, ','.join(missing), say(claim, cur)))
        return 3
    print('%s: yours (%s)' % (claim, say(claim, cur)))
    return 0


def extend(claim, until):
    who = session()
    end = parse_time(until)
    if not who or end is None or end <= utcnow() or end - utcnow() > MAX_AHEAD:
        print('extend needs BLIPSCOPE_SESSION and a --until in the future, at most 24 h ahead', file=sys.stderr)
        return 2
    cur = read_claim(claim)
    if cur is None or 'unreadable' in cur or expired(cur) or cur['session'] != who:
        print('%s: only its holder extends it, before it expires (%s)' % (claim, say(claim, cur) if cur else 'none'), file=sys.stderr)
        return 4 if cur and ('unreadable' in cur or expired(cur)) else 3
    cur['until'] = iso(end)
    fd, tmp = tempfile.mkstemp(dir=claims_dir(), prefix='.%s.' % claim)
    with os.fdopen(fd, 'w', encoding='utf-8') as f:
        json.dump(cur, f, indent=1)
    os.replace(tmp, claim_path(claim))
    print('%s: extended (%s)' % (claim, say(claim, cur)))
    return 0


def release(claim):
    who = session()
    if not who:
        print('BLIPSCOPE_SESSION is unset or malformed', file=sys.stderr)
        return 2
    cur = read_claim(claim)
    if cur is None:
        print('%s: not held' % claim)
        return 0
    if 'unreadable' in cur:
        print('%s: REFUSING to release %s' % (claim, say(claim, cur)), file=sys.stderr)
        return 4
    if cur['session'] != who:
        print('%s: REFUSING to release: held by %s. Only the holder releases it; if it is abandoned, a person deletes %s'
              % (claim, say(claim, cur), claim_path(claim)), file=sys.stderr)
        return 3
    os.remove(claim_path(claim))
    print('%s: released by %s' % (claim, who))
    return 0


def show():
    for claim in CLAIMS:
        cur = read_claim(claim)
        state = 'free' if cur is None else 'UNREADABLE' if 'unreadable' in cur else 'EXPIRED' if expired(cur) else 'held'
        print('%s: %s%s' % (claim, state, '' if cur is None else ' -- ' + say(claim, cur)))
    return 0


def opt(argv, flag):
    if flag in argv:
        i = argv.index(flag)
        if i + 1 < len(argv):
            return argv[i + 1]
    return None


def main(argv):
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 2
    if argv[0] == '--selftest':
        return selftest()
    if argv[0] == '--sabotage':
        return sabotage()
    if argv[0] == 'show':
        return show()
    if len(argv) < 2 or argv[0] not in ('take', 'check', 'extend', 'release') or argv[1] not in CLAIMS:
        print(__doc__)
        return 2
    verb, claim = argv[0], argv[1]
    covers = [x for x in (opt(argv, '--covers') or '').split(',') if x]
    if verb == 'take':
        return take(claim, covers, opt(argv, '--until'))
    if verb == 'check':
        return check(claim, covers)
    if verb == 'extend':
        return extend(claim, opt(argv, '--until'))
    return release(claim)


# ---- the selftest: CL1-CL9 (docs/session-claims.md), in a temporary folder ----------------------------------------------------
def run(args, who='alpha', folder=None, quiet=True):
    env = dict(os.environ, BLIPSCOPE_CLAIMS_DIR=folder)
    env.pop('BLIPSCOPE_SESSION', None)
    if who is not None:
        env['BLIPSCOPE_SESSION'] = who
    r = subprocess.run([sys.executable, os.path.abspath(__file__)] + args, env=env, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def file_bytes(folder, claim):
    try:
        with open(os.path.join(folder, claim + '.json'), 'rb') as f:
            return f.read()
    except FileNotFoundError:
        return None


def plant(folder, claim, body):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, claim + '.json'), 'w', encoding='utf-8') as f:
        f.write(body if isinstance(body, str) else json.dumps(body))


def selftest(script=None):
    """Each row prints PASS or FAIL with its numbers; exit 0 only when every row passes."""
    me = script or os.path.abspath(__file__)
    failed = []
    def row(name, ok, detail):
        print('%s %s: %s' % ('PASS' if ok else 'FAIL', name, detail))
        if not ok:
            failed.append(name)
    def call(args, who, folder):
        env = dict(os.environ, BLIPSCOPE_CLAIMS_DIR=folder)
        env.pop('BLIPSCOPE_SESSION', None)
        if who is not None:
            env['BLIPSCOPE_SESSION'] = who
        r = subprocess.run([sys.executable, me] + args, env=env, capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr
    future = iso(utcnow() + timedelta(hours=1))
    past = iso(utcnow() - timedelta(hours=1))
    other = {'session': 'other-session', 'covers': ['proxy:staging'], 'since': past, 'until': future, 'host': 'h'}
    stale = {'session': 'alpha', 'covers': ['proxy:staging'], 'since': past, 'until': iso(utcnow() - timedelta(minutes=1)), 'host': 'h'}
    with tempfile.TemporaryDirectory() as root:
        d = os.path.join(root, 'cl1')
        rc, out = call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
        c = json.loads(file_bytes(d, 'deploy') or b'{}')
        fields = all(k in c for k in ('session', 'covers', 'since', 'until', 'host'))
        utc = all(str(c.get(k, '')).endswith('Z') for k in ('since', 'until'))
        row('CL1', rc == 0 and fields and utc, 'take when free: exit %d, five fields %s, UTC %s' % (rc, fields, utc))

        d = os.path.join(root, 'cl2'); plant(d, 'deploy', other); before = file_bytes(d, 'deploy')
        rc, out = call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
        row('CL2', rc == 3 and 'other-session' in out and file_bytes(d, 'deploy') == before,
            "another session's claim planted, then take: exit %d, names it %s, bytes unchanged %s"
            % (rc, 'other-session' in out, file_bytes(d, 'deploy') == before))

        d = os.path.join(root, 'cl3'); os.makedirs(d)
        env = dict(os.environ, BLIPSCOPE_CLAIMS_DIR=d)
        procs = []
        for i in range(20):
            e = dict(env, BLIPSCOPE_SESSION='s%02d' % i)
            procs.append(subprocess.Popen([sys.executable, me, 'take', 'merge', '--covers', 'pr:1', '--until', '+10m'], env=e,
                                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        codes = [p.wait() for p in procs]
        row('CL3', codes.count(0) == 1 and codes.count(3) == 19, '20 takes at once: %d exit 0, %d exit 3, others %s'
            % (codes.count(0), codes.count(3), sorted(set(x for x in codes if x not in (0, 3)))))

        d = os.path.join(root, 'cl4'); call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
        a, _ = call(['check', 'deploy', '--covers', 'proxy:staging'], 'alpha', d)
        b, _ = call(['check', 'deploy', '--covers', 'proxy:staging'], 'beta', d)
        n, _ = call(['check', 'merge', '--covers', 'pr:1'], 'alpha', d)
        u, _ = call(['check', 'deploy', '--covers', 'proxy:staging'], None, d)
        row('CL4', (a, b, n, u) == (0, 3, 3, 2), 'check: holder %d, another %d, no claim %d, session unset %d' % (a, b, n, u))

        d = os.path.join(root, 'cl5'); call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
        rc, _ = call(['check', 'deploy', '--covers', 'builder:staging'], 'alpha', d)
        row('CL5', rc == 3, 'a claim on proxy:staging, check builder:staging: exit %d' % rc)

        d = os.path.join(root, 'cl6'); plant(d, 'deploy', stale); before = file_bytes(d, 'deploy')
        codes6 = [call(['check', 'deploy', '--covers', 'proxy:staging'], w, d)[0] for w in ('alpha', 'beta')]
        codes6 += [call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], w, d)[0] for w in ('alpha', 'beta')]
        row('CL6', codes6 == [4, 4, 4, 4] and file_bytes(d, 'deploy') == before,
            'an expired claim planted; check and take as holder and as another: %s, bytes unchanged %s'
            % (codes6, file_bytes(d, 'deploy') == before))

        d = os.path.join(root, 'cl7'); call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
        before = file_bytes(d, 'deploy')
        b, _ = call(['release', 'deploy'], 'beta', d)
        kept = file_bytes(d, 'deploy') == before
        a, _ = call(['release', 'deploy'], 'alpha', d)
        row('CL7', b == 3 and kept and a == 0 and file_bytes(d, 'deploy') is None,
            'release: another %d with the file unchanged %s; holder %d, file gone %s' % (b, kept, a, file_bytes(d, 'deploy') is None))

        res8 = []
        for name, body in (('empty', ''), ('not JSON', '{not json')):
            d = os.path.join(root, 'cl8-' + name.replace(' ', '')); plant(d, 'deploy', body); before = file_bytes(d, 'deploy')
            rc, _ = call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
            rc2, _ = call(['check', 'deploy', '--covers', 'proxy:staging'], 'alpha', d)
            res8.append((name, rc, rc2, file_bytes(d, 'deploy') == before))
        row('CL8', all(r[1] == 4 and r[2] == 4 and r[3] for r in res8), 'an empty file and a non-JSON one: %s' % res8)

        d = os.path.join(root, 'cl9'); call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
        rc, _ = call(['take', 'merge', '--covers', 'pr:1', '--until', '+10m'], 'beta', d)
        row('CL9', rc == 0, 'deploy claim held by another session, take the merge claim: exit %d' % rc)

        # CL10-CL12: the three places that read a claim, each against a stub that counts what would have run.
        base = os.path.dirname(os.path.dirname(me))
        bash = find_bash()
        stub = os.path.join(root, 'bin'); os.makedirs(stub)
        calls = os.path.join(root, 'calls.txt')
        for tool in ('npx', 'gh', 'wrangler'):
            with open(os.path.join(stub, tool), 'w', newline='\n') as f:
                f.write('#!/usr/bin/env bash\necho "%s $*" >> "%s"\nexit 0\n' % (tool, calls.replace('\\', '/')))
            os.chmod(os.path.join(stub, tool), 0o755)
        def wired(cmd, who, folder, extra=None):
            env = dict(os.environ, BLIPSCOPE_CLAIMS_DIR=folder, PATH=stub + os.pathsep + os.environ.get('PATH', ''))
            env.pop('BLIPSCOPE_SESSION', None); env.pop('DEPLOY_CLAIM_CHECK_ONLY', None); env.pop('MERGE_CLAIM_CHECK_ONLY', None)
            if who is not None:
                env['BLIPSCOPE_SESSION'] = who
            env.update(extra or {})
            if os.path.exists(calls):
                os.remove(calls)
            r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
            n = sum(1 for _ in open(calls)) if os.path.exists(calls) else 0
            return r.returncode, r.stdout + r.stderr, n
        def states(tag):
            out = []
            for name, who, body in (('none', 'alpha', None), ('another', 'alpha', 'other'), ('expired', 'alpha', 'stale'),
                                    ('unset', None, None)):
                d = os.path.join(root, '%s-%s' % (tag, name)); os.makedirs(d)
                if body:
                    planted = dict(other if body == 'other' else stale)
                    planted['covers'] = ['proxy:staging', 'pr:1']
                    plant(d, 'deploy' if tag == 'cl10' else 'merge', planted)
                out.append((name, who, d))
            return out

        deploy_sh = os.path.join(base, 'proxy', 'scripts', 'deploy.sh')
        why = {'none': 'not held by anyone', 'another': 'HELD by', 'expired': 'EXPIRED', 'unset': 'BLIPSCOPE_SESSION is unset'}
        res = []
        for name, who, d in states('cl10'):
            rc, out, n = wired([bash, deploy_sh, 'staging'], who, d)
            res.append((name, rc != 0 and 'REFUSING TO DEPLOY' in out and why[name] in out and n == 0))
        d = os.path.join(root, 'cl10-holder'); call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
        rc, out, n = wired([bash, deploy_sh, 'staging'], 'alpha', d, {'DEPLOY_CLAIM_CHECK_ONLY': '1'})
        control = rc == 0 and 'held for proxy:staging' in out and n == 0
        row('CL10', all(ok for _, ok in res) and control, 'deploy.sh refuses with 0 stub calls: %s; the holder passes the check: %s'
            % (res, control))

        merge_sh = os.path.join(base, 'scripts', 'merge-by-rule.sh')
        fast = {'MERGE_CLAIM_CHECK_ONLY': '1', 'MERGE_CLAIM_WAIT_S': '2', 'MERGE_CLAIM_POLL_S': '1'}
        res = []
        for name, who, d in states('cl11'):
            if name == 'none':
                continue  # with no claim held the driver takes it: that is the control below, not a refusal
            rc, out, n = wired([bash, merge_sh, '1', 'abcdef012345', 'some-branch'], who, d, fast)
            res.append((name, rc != 0 and 'held for pr:1' not in out and why[name] in out and n == 0))
        d = os.path.join(root, 'cl11-free'); os.makedirs(d)
        rc, out, n = wired([bash, merge_sh, '1', 'abcdef012345', 'some-branch'], 'alpha', d, fast)
        control = rc == 0 and 'held for pr:1' in out and n == 0 and file_bytes(d, 'merge') is None
        row('CL11', all(ok for _, ok in res) and control, 'the merge driver refuses with 0 gh calls: %s; with the claim free it takes it, '
            'and it is gone afterwards: %s' % (res, control))

        hook = os.path.join(base, 'scripts', 'claim-hook.py')
        d = os.path.join(root, 'cl12'); os.makedirs(d)
        def h(command, who=None):
            env = dict(os.environ, BLIPSCOPE_CLAIMS_DIR=d)
            env.pop('BLIPSCOPE_SESSION', None)
            if who:
                env['BLIPSCOPE_SESSION'] = who
            r = subprocess.run([sys.executable, hook], input=json.dumps({'tool_name': 'Bash', 'tool_input': {'command': command}}),
                               env=env, capture_output=True, text=True)
            return r.returncode
        got = [h('npx wrangler deploy --env staging'),
               h('BLIPSCOPE_SESSION=alpha npx wrangler deploy --env staging'),
               h('npx wrangler secret put X --env staging', 'alpha'),
               h('npx wrangler deploy --dry-run --env staging', 'alpha'),
               h('ls -la')]
        plant(d, 'deploy', dict(other, covers=['proxy:staging']))
        got.append(h('BLIPSCOPE_SESSION=alpha npx wrangler deploy --env staging'))
        plant(d, 'deploy', dict(stale, covers=['proxy:staging']))
        got.append(h('BLIPSCOPE_SESSION=alpha npx wrangler deploy --env staging'))
        os.remove(os.path.join(d, 'deploy.json'))
        call(['take', 'deploy', '--covers', 'proxy:staging', '--until', '+10m'], 'alpha', d)
        got.append(h('BLIPSCOPE_SESSION=alpha npx wrangler deploy --env staging'))
        os.remove(os.path.join(d, 'deploy.json'))
        plant(d, 'deploy', dict(other, covers=['proxy:production']))
        got.append(h('npx wrangler rollback --env production', 'alpha'))
        logged = os.path.exists(os.path.join(d, 'rollbacks.log'))
        want = [2, 2, 2, 0, 0, 2, 2, 0, 0]
        row('CL12', got == want and logged, 'the hook: %s against %s (unnamed, no claim, secret put, dry run, ls, another session, '
            'expired, holder, rollback held by another); the rollback logged: %s' % (got, want, logged))
    print('selftest: %s' % ('every row passes' if not failed else 'FAILED %s' % ', '.join(failed)))
    return 0 if not failed else 1


# ---- the plants: each must turn exactly its registered rows red ---------------------------------------------------------------
PLANTS = {
    'S-CL-NOEXCL': (['        os.link(tmp, claim_path(claim))\n',
                     '        os.replace(tmp, claim_path(claim)); open(tmp, "w").close()  # SABOTAGE S-CL-NOEXCL: overwrites\n'],
                    # CL11 too, registered before the PR: the merge driver goes through `take`, so it proceeds over another's claim
                    {'CL2', 'CL3', 'CL6', 'CL8', 'CL11'}),
    'S-CL-EXPIRY': (["        if 'unreadable' in cur or expired(cur):\n            print('%s: %s claim %s -- not overwritten'",
                     "        if 'unreadable' in cur:  # SABOTAGE S-CL-EXPIRY\n            print('%s: %s claim %s -- not overwritten'"],
                    # CL11 too, for the same reason: `take` then calls an expired claim of the caller's "already yours"
                    {'CL6', 'CL11'}),
    'S-CL-NOOWNER': (["    if cur['session'] != who:\n        print('%s: REFUSING to release: held by",
                      "    if False:  # SABOTAGE S-CL-NOOWNER\n        print('%s: REFUSING to release: held by"],
                     {'CL7'}),
    'S-CL-NOCOVER': (['    missing = [x for x in covers if x not in cur[\'covers\']]\n',
                      '    missing = []  # SABOTAGE S-CL-NOCOVER\n'],
                     {'CL5'}),
    'S-CL-DEPLOY': (['if ! "$CLAIM_PY" "$CLAIM_TOOL" check deploy --covers "$CLAIM_TARGET"; then\n',
                     'if false; then  # SABOTAGE S-CL-DEPLOY\n'],
                    {'CL10'}),
    'S-CL-MERGE': (['    "$CLAIM_PY" "$CLAIM" take merge --covers "pr:$PR" --until +10m; rc=$?\n',
                    '    rc=0  # SABOTAGE S-CL-MERGE\n'],
                   {'CL11'}),
    'S-CL-HOOK': (["GATED = re.compile(r'\\bwrangler(?:\\.cmd)?\\s+(?:deploy\\b|secret\\s+(?:put|bulk)\\b|versions\\s+deploy\\b)')\n",
                   "GATED = re.compile(r'(?!x)x')  # SABOTAGE S-CL-HOOK\n"],
                  {'CL12'}),
}
PLANT_FILE = {'S-CL-DEPLOY': 'proxy/scripts/deploy.sh', 'S-CL-MERGE': 'scripts/merge-by-rule.sh', 'S-CL-HOOK': 'scripts/claim-hook.py'}


def find_bash():
    if sys.platform == 'win32':  # Git's bash: the one System32 holds is WSL's, a different machine
        for p in (r'C:\Program Files\Git\bin\bash.exe', r'C:\Program Files\Git\usr\bin\bash.exe'):
            if os.path.exists(p):
                return p
    return shutil.which('bash') or 'bash'


WIRED = ('scripts/session-claim.py', 'scripts/claim-hook.py', 'scripts/merge-by-rule.sh', 'scripts/ci-wait.sh', 'proxy/scripts/deploy.sh')


def sabotage():
    """Each plant goes into a COPY of the five files, laid out as in the repo, and that copy's selftest runs."""
    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ok = True
    with tempfile.TemporaryDirectory() as d:
        for name, ((a, b), want) in PLANTS.items():
            rel = PLANT_FILE.get(name, 'scripts/session-claim.py')
            tree = os.path.join(d, name)
            for w in WIRED:
                os.makedirs(os.path.dirname(os.path.join(tree, w)), exist_ok=True)
                shutil.copyfile(os.path.join(base, w), os.path.join(tree, w))
            src = open(os.path.join(tree, rel), encoding='utf-8', newline='').read()
            if src.count(a) != 1:
                print('%s: ANCHOR found %d times in %s, need 1' % (name, src.count(a), rel)); ok = False; continue
            planted = src.replace(a, b)
            with open(os.path.join(tree, rel), 'w', encoding='utf-8', newline='') as f:
                f.write(planted)
            r = subprocess.run([sys.executable, os.path.join(tree, 'scripts', 'session-claim.py'), '--selftest'], capture_output=True, text=True)
            red = set(re.findall(r'^FAIL (CL\d+):', r.stdout, re.M))
            verdict = 'AS PREDICTED' if red == want else 'OFF'
            print('%s: %s, failing {%s} against {%s}' % (name, verdict, ', '.join(sorted(red)), ', '.join(sorted(want))))
            ok = ok and red == want
    print('sabotage: %s' % ('every plant as predicted' if ok else 'OFF'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
