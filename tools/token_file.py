#!/usr/bin/env python3
"""token_file.py -- where the Python instruments get their Cloudflare tokens (docs/token-file-python.md). It is the
Python half of proxy/scripts/token-file.ts, with the same rule and the same refusals:

  bash:        export BLIPSCOPE_TOKEN_FILE=~/.config/blipscope/tokens
  PowerShell:  $env:BLIPSCOPE_TOKEN_FILE = "$HOME\\.config\\blipscope\\tokens"

Set it PER SHELL. The file lives OUTSIDE any git working tree and holds one key=value per line (blank lines and #
comments ignored). Refused, each with a message that names the variable, the path, the key and a line number, NEVER a
line's content or a value: the variable unset or blank; the file missing; the path inside this repo or any git working
tree; on POSIX, a file readable by group or others (on Windows only a note, as in token-file.ts); a line that is not
key=value; a key twice; the key missing or empty; a purpose this helper does not know.

The instruments run under `python -I`, which drops the script's folder from sys.path, so each loads this file by path:

    import importlib.util as _ilu
    _s = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
    TOKEN_FILE = _ilu.module_from_spec(_s); _s.loader.exec_module(TOKEN_FILE)
    tok = TOKEN_FILE.token("analytics-read")

  python3 tools/token_file.py --check       the file the variable names: booleans and key NAMES only, never a value
  python3 tools/token_file.py --selftest    T1-T10 (docs/token-file-python.md section 3)
"""
import os
import stat
import sys
import tempfile

TOKEN_FILE_ENV = "BLIPSCOPE_TOKEN_FILE"
# The keys these readers ask for. records-device and test-device are ids, not credentials, and are in token-file.ts's
# list too; tools/wx/pd6_fleet.py reads them to leave the bench out of a fleet count.
PURPOSES = ("analytics-read", "observability-read", "r2-wx-read-id", "r2-wx-read-secret", "records-device", "test-device")
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FORMAT_HELP = (f"{TOKEN_FILE_ENV} names a file OUTSIDE the repo, set per shell "
               f"(bash: export {TOKEN_FILE_ENV}=~/.config/blipscope/tokens; "
               f"PowerShell: $env:{TOKEN_FILE_ENV} = \"$HOME\\.config\\blipscope\\tokens\"). "
               f"Format: one key=value per line; blank lines and # comments ignored.")


class TokenFileError(Exception):
    pass


def inside_repo_or_git_tree(p, repo_root=REPO_ROOT):
    """True when p is inside this repo, or inside ANY git working tree (one `git add -A` away)."""
    norm = lambda s: (os.path.normcase(os.path.abspath(s))).rstrip("\\/")
    a, root = norm(p), norm(repo_root)
    if a == root or a.startswith(root + os.sep):
        return True
    d = os.path.dirname(os.path.abspath(p))
    while True:
        if os.path.exists(os.path.join(d, ".git")):
            return True
        up = os.path.dirname(d)
        if up == d:
            return False
        d = up


def read_text(what, env=None, repo_root=REPO_ROOT, warn=lambda m: print(m, file=sys.stderr)):
    env = os.environ if env is None else env
    path = (env.get(TOKEN_FILE_ENV) or "").strip()
    if not path:
        raise TokenFileError(f"{TOKEN_FILE_ENV} is not set, so there is no {what}. {FORMAT_HELP}")
    if not os.path.exists(path):
        raise TokenFileError(f"{TOKEN_FILE_ENV} points at {path}, which does not exist. {FORMAT_HELP}")
    if inside_repo_or_git_tree(path, repo_root):
        raise TokenFileError(f"{TOKEN_FILE_ENV} points at {path}, which is inside a git working tree; move it out of the repo.")
    if os.name == "nt":
        warn(f"note: {path}: file permissions are not checked on Windows; keep it readable by you alone.")
    else:
        mode = os.stat(path).st_mode
        if mode & 0o077:
            raise TokenFileError(f"{path} is readable by others (mode {stat.S_IMODE(mode):o}); run: chmod 600 {path}")
    with open(path, encoding="utf-8") as fh:
        return fh.read(), path


def parse(text, purpose, where):
    """The value for one key. Raises naming the key and line numbers, never content."""
    if purpose not in PURPOSES:
        raise TokenFileError(f"unknown purpose {purpose!r}; known: {', '.join(PURPOSES)}")
    found, found_at = None, 0
    for i, raw in enumerate(text.lstrip("﻿").replace("\r\n", "\n").split("\n")):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        eq = line.find("=")
        if eq <= 0:
            raise TokenFileError(f"{where}: line {i + 1} is not key=value (content not shown). {FORMAT_HELP}")
        if line[:eq].strip() != purpose:
            continue
        if found is not None:
            raise TokenFileError(f"{where}: key \"{purpose}\" appears twice (lines {found_at} and {i + 1}). Keep one.")
        found, found_at = line[eq + 1:].strip(), i + 1
    if found is None:
        raise TokenFileError(f"{where}: no \"{purpose}\" line. This script needs {purpose}=<token>. {FORMAT_HELP}")
    if not found:
        raise TokenFileError(f"{where}: \"{purpose}\" is empty (line {found_at}).")
    return found


def read_token(purpose, env=None, repo_root=REPO_ROOT):
    text, path = read_text(f"{purpose} token", env, repo_root)
    return parse(text, purpose, path)


def token(purpose):
    """For the instruments: the value, or exit non-zero with the message (never a value)."""
    try:
        return read_token(purpose)
    except TokenFileError as e:
        raise SystemExit(f"token file: {e}")


def tokens(*purposes):
    """Several keys from one read of the file: {purpose: value}, or exit non-zero."""
    try:
        text, path = read_text(" + ".join(purposes) + " tokens")
        return {p: parse(text, p, path) for p in purposes}
    except TokenFileError as e:
        raise SystemExit(f"token file: {e}")


def check():
    """Booleans and key NAMES only, never a value."""
    path = (os.environ.get(TOKEN_FILE_ENV) or "").strip()
    print(f"{TOKEN_FILE_ENV} set: {bool(path)}")
    if not path:
        return 1
    exists = os.path.exists(path)
    print(f"file exists: {exists}")
    if not exists:
        return 1
    outside = not inside_repo_or_git_tree(path)
    print(f"outside every git tree: {outside}")
    if os.name == "nt":
        print("permissions: not checkable on Windows (a note, as in token-file.ts)")
    else:
        print(f"permissions owner-only: {not (os.stat(path).st_mode & 0o077)}")
    text = open(path, encoding="utf-8").read()
    keys, malformed = {}, 0
    for raw in text.lstrip("﻿").replace("\r\n", "\n").split("\n"):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        eq = line.find("=")
        if eq <= 0:
            malformed += 1
            continue
        k = line[:eq].strip()
        keys[k] = keys.get(k, 0) + 1
    print(f"malformed lines: {malformed}")
    ok = outside and malformed == 0
    for p in PURPOSES:
        n = keys.get(p, 0)
        print(f"key {p}: present once: {n == 1}")
        ok = ok and n == 1
    print("CHECK", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


def selftest():
    fails, skips = [], []
    planted = "PLANTED-SECRET-7f3a91"

    def expect_refusal(name, env, purpose="analytics-read", root=REPO_ROOT, msgs=None):
        try:
            read_token(purpose, env, root)
            ok, msg = False, "(no refusal)"
        except TokenFileError as e:
            ok, msg = True, str(e)
        except Exception as e:   # anything but the helper's own refusal (a FileNotFoundError, say) is not a refusal
            ok, msg = False, type(e).__name__
        if msgs is not None:
            msgs.append(msg)
        print(("PASS " if ok else "FAIL ") + name)
        if not ok:
            fails.append(name)

    msgs = []
    with tempfile.TemporaryDirectory() as outer:
        clean = os.path.join(outer, "clean")
        os.mkdir(clean)
        def plant(dirname, body, mode=0o600):
            p = os.path.join(dirname, "tokens")
            with open(p, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(body)
            os.chmod(p, mode)
            return p
        good = plant(clean, f"# a comment\n\nanalytics-read={planted}\nobservability-read=OBS-PLANT\n")
        expect_refusal("T1: the variable unset is refused", {}, msgs=msgs)
        expect_refusal("T2: a missing file is refused", {TOKEN_FILE_ENV: os.path.join(clean, "nope")}, msgs=msgs)
        tree = os.path.join(outer, "tree")
        os.makedirs(os.path.join(tree, ".git"))
        expect_refusal("T3: a file inside a git working tree is refused", {TOKEN_FILE_ENV: plant(tree, f"analytics-read={planted}\n")}, msgs=msgs)
        if os.name == "nt":
            print("SKIP T4: POSIX permissions are not checkable on Windows (CI runs T4 on Linux)")
            skips.append("T4")
        else:
            d4 = os.path.join(outer, "t4"); os.mkdir(d4)
            expect_refusal("T4: a file readable by others (0644) is refused", {TOKEN_FILE_ENV: plant(d4, f"analytics-read={planted}\n", 0o644)}, msgs=msgs)
        d5 = os.path.join(outer, "t5"); os.mkdir(d5)
        expect_refusal("T5: a bare line (no =) is refused", {TOKEN_FILE_ENV: plant(d5, f"{planted}\nanalytics-read=x\n")}, msgs=msgs)
        d6 = os.path.join(outer, "t6"); os.mkdir(d6)
        expect_refusal("T6: a key twice is refused", {TOKEN_FILE_ENV: plant(d6, f"analytics-read={planted}\nanalytics-read={planted}2\n")}, msgs=msgs)
        d7 = os.path.join(outer, "t7"); os.mkdir(d7)
        expect_refusal("T7: the key missing is refused", {TOKEN_FILE_ENV: plant(d7, f"observability-read={planted}\n")}, msgs=msgs)
        d8 = os.path.join(outer, "t8"); os.mkdir(d8)
        expect_refusal("T8: the key empty is refused", {TOKEN_FILE_ENV: plant(d8, "analytics-read=\n")}, msgs=msgs)
        try:
            got = read_token("analytics-read", {TOKEN_FILE_ENV: good})
        except TokenFileError:
            got = None
        ok = got == planted
        print(("PASS " if ok else "FAIL ") + "T9: a well-formed file outside any git tree returns exactly the planted value")
        if not ok:
            fails.append("T9")
    leaked = [m for m in msgs if planted in m or "OBS-PLANT" in m]
    ok = not leaked and len(msgs) >= 7
    print(("PASS " if ok else "FAIL ") + f"T10: no refusal message carries a planted value ({len(msgs)} messages scanned whole)")
    if not ok:
        fails.append("T10")
    print("SELFTEST " + ("PASSED" if not fails else f"FAILED: {fails}") + (f" (skipped: {skips})" if skips else ""))
    return 0 if not fails else 1


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    if "--check" in sys.argv:
        sys.exit(check())
    print(__doc__)
    sys.exit(2)
