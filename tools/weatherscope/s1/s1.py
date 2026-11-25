"""S1, the shared-code invariant (docs/weatherscope-stage3.md 1.2; step 3.1's predictions in Addendum 5).

    python -I tools/weatherscope/s1/s1.py build    <clone> <ref> <label> <out> [--sabotage] [--bench-oldname] [--epoch N] <env>...
    python -I tools/weatherscope/s1/s1.py compare  <out> <step>      (S1 v1, A5.2; its result is A5.4)
    python -I tools/weatherscope/s1/s1.py compare2 <out>             (S1 v2, A5.6: explicit R1/R2 list, calibration first)
    python -I tools/weatherscope/s1/s1.py selftest <old s1.py>       (checkout_lf's rows, offline, in temporary repos)

CHECKOUT (2026-11-22, the s1.py LF PR): every git call runs with -c core.autocrlf=false -c core.eol=lf, and build()
checks out through checkout_lf, which refuses a clone already checked out CRLF, a tree that is not clean, and a
checkout that lands any file CRLF over an LF index. selftest grades it against the old s1.py given as its argument.

S1 v2 (docs/weatherscope-stage3.md A5.6): every measured build gets --epoch (SOURCE_DATE_EPOCH) after a warm-up
build of every env; --bench-oldname builds the head's bench under its old env name (A5.4's D2 method); compare2
grades the calibration (0 bytes) and then base vs head, the sabotage and the bench, allowing differences ONLY in
R1 (app_elf_sha256) and R2 (the checksum and appended SHA-256).

BUILD, in ONE clone for every round, so no path compiled into an image differs between base and head (run it from
PowerShell, not Git Bash: the pio esptool trap). Refuses unless the clone's tree is clean. Checks out <ref> detached.
With --sabotage, inserts Addendum 5's S1 sabotage at the top of ConfigurationWebServer::Initialise(), asserts it
applied, and restores the file afterwards (asserting that too). For each env: deletes .pio/build/<env>, runs
`pio run -e <env>`, copies firmware.bin to <out>/<label>/<env>.bin, records whether the build compiled
ConfigurationWebServer.cpp (its object file exists), then deletes .pio/build/<env>, so one env's build at a time is
on disk. Writes <out>/<label>/manifest.json (ref, sha, per-env result).

COMPARE grades step 3.1's S1 rows with stampdiff.py (Stage 1's accountant, committed unchanged):
  S1          base vs head, every other env: stamps only, 14/14
  sabotage    base vs sabotage: a non-stamp difference in EVERY env whose sabotage build compiled
              ConfigurationWebServer.cpp, and stamps only in every env that did not
  bench       base weatherscope-s3-128 vs head weatherscope-bench-s3-128: stamps only, and no time strings
Exit 0 only when every row holds.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STAMPDIFF = os.path.join(HERE, "stampdiff.py")
SAB_ANCHOR = "void ConfigurationWebServer::Initialise() {\n"
SAB_LINE = '    if (millis() == 0xFFFFFFFFul) Serial.println("S1 SABOTAGE");   // S1 SABOTAGE, never committed\n'
CWS = os.path.join("src", "ConfigurationWebServer.cpp")
BENCH_BASE, BENCH_HEAD = "weatherscope-s3-128", "weatherscope-bench-s3-128"


def git(clone, *args):
    # Per command, and writing no config: the machine's own core.autocrlf (system-wide true on Git for Windows) must
    # not decide the bytes compiled. Both flags are needed: under `* text=auto` a text file's working-tree EOL comes
    # from core.eol when autocrlf is false, and core.eol's default, native, is CRLF on Git for Windows.
    return subprocess.run(["git", "-c", "core.autocrlf=false", "-c", "core.eol=lf", "-C", clone, *args],
                          capture_output=True, text=True, check=True).stdout.strip()


def eol_offenders(clone):
    """Paths whose index is LF but whose working-tree bytes are CRLF or mixed (`git ls-files --eol` reads the bytes,
    whatever the stat cache says). Files committed CRLF (i/crlf) and -text files are left alone."""
    bad = []
    for entry in git(clone, "ls-files", "--eol", "-z").split("\0"):
        if "\t" not in entry:
            continue
        info, path = entry.split("\t", 1)
        fields = info.split()
        i = next((f[2:] for f in fields if f.startswith("i/")), "")
        w = next((f[2:] for f in fields if f.startswith("w/")), "")
        attr = next((f[5:] for f in fields if f.startswith("attr/")), "")
        if i == "lf" and w in ("crlf", "mixed") and "-text" not in attr:
            bad.append(path)
    return bad


def checkout_lf(clone, ref):
    """Checks out <ref> detached, LF, and returns its sha. Refuses a clone already checked out CRLF, a tree that is not
    clean, and a checkout that lands any file CRLF over an LF index."""
    bad = eol_offenders(clone)
    if bad:
        sys.exit(f"REFUSED: the clone is checked out CRLF ({len(bad)} file(s)); re-create it with "
                 f"git -c core.autocrlf=false -c core.eol=lf clone\n  " + "\n  ".join(bad[:5]))
    if git(clone, "status", "--porcelain"):
        sys.exit("REFUSED: the clone's tree is not clean")
    git(clone, "checkout", "-q", "--detach", ref)
    bad = eol_offenders(clone)
    if bad:
        sys.exit(f"REFUSED: {len(bad)} file(s) checked out CRLF over an LF index\n  " + "\n  ".join(bad[:5]))
    return git(clone, "rev-parse", "HEAD")


def build(clone, ref, label, out, sabotage, envs, epoch=None, bench_oldname=False):
    sha = checkout_lf(clone, ref)
    benv = dict(os.environ)
    if epoch is not None:
        benv["SOURCE_DATE_EPOCH"] = str(epoch)    # S1 v2 (A5.6): fixes __DATE__/__TIME__ in every compile
    ini = os.path.join(clone, "platformio.ini")
    ini_original = None
    if bench_oldname:                             # A5.4's D2 method, committed for S1 v2
        ini_original = open(ini, "rb").read()
        t = ini_original.decode("utf-8")
        assert t.count("[env:" + BENCH_HEAD + "]") == 1 and t.count("[env:" + BENCH_BASE + "]") == 0, "bench rename anchor"
        open(ini, "wb").write(t.replace("[env:" + BENCH_HEAD + "]", "[env:" + BENCH_BASE + "]").encode("utf-8"))
        assert open(ini, "rb").read() != ini_original
        print(f"bench env header renamed to [env:{BENCH_BASE}] for this build")
    cws = os.path.join(clone, CWS)
    original = None
    if sabotage:
        original = open(cws, "rb").read()
        text = original.decode("utf-8")
        nl = "\r\n" if "\r\n" in text else "\n"            # the clone may check out CRLF (core.autocrlf)
        anchor, line = SAB_ANCHOR.replace("\n", nl), SAB_LINE.replace("\n", nl)
        assert text.count(anchor) == 1, "sabotage anchor is not unique"
        mutated = text.replace(anchor, anchor + line)
        assert mutated != text and mutated.count("S1 SABOTAGE") == 2
        open(cws, "wb").write(mutated.encode("utf-8"))
        print(f"sabotage planted in {CWS}")
    dest = os.path.join(out, label)
    os.makedirs(dest, exist_ok=True)
    manifest = {"ref": ref, "sha": sha, "sabotage": sabotage, "epoch": epoch, "bench_oldname": bench_oldname, "envs": {}}
    try:
        for env in envs:
            bdir = os.path.join(clone, ".pio", "build", env)
            shutil.rmtree(bdir, ignore_errors=True)
            log = os.path.join(dest, f"{env}.log")
            with open(log, "w") as lf:
                rc = subprocess.run(["pio", "run", "-e", env], cwd=clone, stdout=lf, stderr=subprocess.STDOUT,
                                    env=benv).returncode
            fw = os.path.join(bdir, "firmware.bin")
            ok = rc == 0 and os.path.exists(fw)
            compiled_cws = os.path.exists(os.path.join(bdir, "src", "ConfigurationWebServer.cpp.o"))
            if ok:
                shutil.copyfile(fw, os.path.join(dest, f"{env}.bin"))
            manifest["envs"][env] = {"built": ok, "rc": rc, "compiledConfigurationWebServer": compiled_cws}
            print(f"  {label} {env}: {'built' if ok else f'FAILED rc={rc} (see {log})'}; "
                  f"ConfigurationWebServer.cpp compiled: {compiled_cws}")
            shutil.rmtree(bdir, ignore_errors=True)
    finally:
        if sabotage:
            open(cws, "wb").write(original)
            assert open(cws, "rb").read() == original
            print(f"sabotage removed; {CWS} restored byte-identical")
        if bench_oldname:
            open(ini, "wb").write(ini_original)
            assert open(ini, "rb").read() == ini_original
            print("platformio.ini restored byte-identical")
        assert not git(clone, "status", "--porcelain"), "the clone is not clean after the build"
    json.dump(manifest, open(os.path.join(dest, "manifest.json"), "w"), indent=1)
    return 0 if all(e["built"] for e in manifest["envs"].values()) else 1


def stampdiff(a, b):
    r = subprocess.run([sys.executable, "-I", STAMPDIFF, a, b], capture_output=True, text=True)
    stamps_only = r.returncode == 0
    time_strings = "compile-time string" in r.stdout
    last = (r.stdout.strip().splitlines() or ["?"])[-1]
    return stamps_only, time_strings, r.stdout, last


def compare(out, step):
    base = json.load(open(os.path.join(out, "base", "manifest.json")))
    head = json.load(open(os.path.join(out, "head", "manifest.json")))
    sab = json.load(open(os.path.join(out, "sabotage", "manifest.json")))
    print(f"base {base['sha'][:7]}, head {head['sha'][:7]}, sabotage on {sab['sha'][:7]}")
    if sab["sha"] != head["sha"]:
        sys.exit("REFUSED: the sabotage round was not built on the head")
    others = sorted(e for e in base["envs"] if e != BENCH_BASE)
    ok_all = True
    detail = open(os.path.join(out, f"compare-{step}.txt"), "w")

    def pair(a_label, b_label, env_a, env_b):
        a, b = os.path.join(out, a_label, f"{env_a}.bin"), os.path.join(out, b_label, f"{env_b}.bin")
        if not (os.path.exists(a) and os.path.exists(b)):
            return None
        s, t, full, last = stampdiff(a, b)
        detail.write(f"=== {a_label}/{env_a} vs {b_label}/{env_b}\n{full}\n")
        return s, t, last

    print(f"S1 (predicted: stamps only in {len(others)}/{len(others)}):")
    s1_ok = 0
    for env in others:
        r = pair("base", "head", env, env)
        good = bool(r and r[0])
        s1_ok += good
        print(f"  {env}: {'stamps only' if good else 'NOT stamps only' if r else 'MISSING image'} ({r[2] if r else '-'})")
    print(f"  -> {s1_ok}/{len(others)} stamps only: {'as predicted' if s1_ok == len(others) else 'OFF-PREDICTION, STOP'}")
    ok_all &= s1_ok == len(others)

    print("S1's sabotage (predicted: non-stamp where ConfigurationWebServer.cpp compiled, stamps only elsewhere):")
    sab_ok, n_comp = 0, 0
    for env in others:
        compiled = sab["envs"].get(env, {}).get("compiledConfigurationWebServer")
        n_comp += bool(compiled)
        r = pair("base", "sabotage", env, env)
        if not r:
            print(f"  {env}: MISSING image")
            continue
        want_stamps_only = not compiled
        good = r[0] == want_stamps_only
        sab_ok += good
        print(f"  {env}: compiled CWS {compiled}; {'stamps only' if r[0] else 'non-stamp difference'} -> "
              f"{'as predicted' if good else 'OFF-PREDICTION'}")
    not_comp = sorted(e for e in others if not sab["envs"].get(e, {}).get("compiledConfigurationWebServer"))
    split_ok = not_comp == ["animtest-s3-128"]
    print(f"  -> {sab_ok}/{len(others)} as predicted; {n_comp} envs compiled ConfigurationWebServer.cpp; the ones that "
          f"did not: {not_comp} (predicted ['animtest-s3-128']) -> {'as predicted' if split_ok else 'OFF-PREDICTION'}")
    ok_all &= sab_ok == len(others) and split_ok

    r = pair("base", "head", BENCH_BASE, BENCH_HEAD)
    good = bool(r and r[0] and not r[1])
    print(f"bench image ({BENCH_BASE} at base vs {BENCH_HEAD} at head; predicted stamps only, no time strings): "
          f"{r[2] if r else 'MISSING image'} -> {'as predicted' if good else 'OFF-PREDICTION'}")
    ok_all &= good
    detail.close()
    print("S1 rows:", "ALL AS PREDICTED" if ok_all else "OFF-PREDICTION: STOP")
    return 0 if ok_all else 1


R1 = (0xB0, 0xB0 + 32)   # esp_app_desc_t.app_elf_sha256 (A5.6): hashes the whole ELF, debug sections included


def outside_list(a_path, b_path):
    """S1 v2 (A5.6): (size_equal, bytes differing OUTSIDE R1 and R2, R1 differs, R2 differs, total differing)."""
    a, b = open(a_path, "rb").read(), open(b_path, "rb").read()
    if len(a) != len(b):
        return False, None, None, None, None
    n = len(a)
    r2 = (n - 33, n)          # the image checksum byte and the appended SHA-256
    diff = [i for i in range(n) if a[i] != b[i]]
    inside = lambda i, r: r[0] <= i < r[1]
    out = [i for i in diff if not inside(i, R1) and not inside(i, r2)]
    return True, len(out), any(inside(i, R1) for i in diff), any(inside(i, r2) for i in diff), len(diff)


def compare2(out):
    """Grades A5.6's rows, in order; the calibration first, and a calibration miss stops everything after it."""
    p = lambda label, env: os.path.join(out, label, f"{env}.bin")
    man = {l: json.load(open(os.path.join(out, l, "manifest.json")))
           for l in ("calib-a", "calib-b", "base2", "head2", "sabotage2", "bench-oldname2")}
    for l, m in man.items():
        if m.get("epoch") is None:
            sys.exit(f"REFUSED: {l} was not built with SOURCE_DATE_EPOCH")
    print(f"base2 {man['base2']['sha'][:7]}, head2 {man['head2']['sha'][:7]}, sabotage2 on {man['sabotage2']['sha'][:7]}, "
          f"bench-oldname2 on {man['bench-oldname2']['sha'][:7]}; SOURCE_DATE_EPOCH {man['base2']['epoch']}")
    if not (man["sabotage2"]["sha"] == man["head2"]["sha"] == man["bench-oldname2"]["sha"]):
        sys.exit("REFUSED: the sabotage or bench round was not built on the head")
    eq, o, r1, r2, total = outside_list(p("calib-a", "blipscope-s3-128"), p("calib-b", "blipscope-s3-128"))
    cal_ok = eq and total == 0
    print(f"C-S1v2 calibration (blipscope-s3-128 at the base, twice; predicted 0 differing bytes): "
          f"{'size differs' if not eq else f'{total} differing bytes'} -> {'as predicted' if cal_ok else 'OFF-PREDICTION, STOP'}")
    if not cal_ok:
        return 1
    others = sorted(e for e in man["base2"]["envs"] if e != BENCH_BASE)
    ok_all, s1_ok = True, 0
    print(f"S1 (predicted: identical outside R1 and R2 in {len(others)}/{len(others)}):")
    for env in others:
        eq, o, r1, r2, total = outside_list(p("base2", env), p("head2", env))
        good = eq and o == 0
        s1_ok += good
        print(f"  {env}: {'size differs' if not eq else f'{o} bytes outside R1/R2'}"
              f"{'' if not eq else f' (R1 differs: {r1}, R2 differs: {r2}, total {total})'} -> "
              f"{'as predicted' if good else 'OFF-PREDICTION'}")
    print(f"  -> {s1_ok}/{len(others)}: {'as predicted' if s1_ok == len(others) else 'OFF-PREDICTION, STOP'}")
    ok_all &= s1_ok == len(others)
    print("S1's sabotage (predicted: outside R1/R2 where ConfigurationWebServer.cpp compiled; animtest-s3-128 at 0 bytes):")
    sab_ok = 0
    for env in others:
        compiled = man["sabotage2"]["envs"].get(env, {}).get("compiledConfigurationWebServer")
        eq, o, r1, r2, total = outside_list(p("base2", env), p("sabotage2", env))
        if compiled:
            good = (not eq) or o > 0
        else:
            good = eq and total == 0
        sab_ok += good
        print(f"  {env}: compiled CWS {compiled}; {'size differs' if not eq else f'{o} outside R1/R2, {total} total'} -> "
              f"{'as predicted' if good else 'OFF-PREDICTION'}")
    not_comp = sorted(e for e in others if not man["sabotage2"]["envs"].get(e, {}).get("compiledConfigurationWebServer"))
    split_ok = not_comp == ["animtest-s3-128"]
    print(f"  -> {sab_ok}/{len(others)}; not compiling it: {not_comp} (predicted ['animtest-s3-128']) -> "
          f"{'as predicted' if sab_ok == len(others) and split_ok else 'OFF-PREDICTION'}")
    ok_all &= sab_ok == len(others) and split_ok
    eq, o, r1, r2, total = outside_list(p("base2", BENCH_BASE), p("bench-oldname2", BENCH_BASE))
    good = eq and o == 0
    print(f"bench under its old name vs the base's bench (predicted identical outside R1/R2): "
          f"{'size differs' if not eq else f'{o} outside R1/R2 (R1 {r1}, R2 {r2}, total {total})'} -> "
          f"{'as predicted' if good else 'OFF-PREDICTION'}")
    ok_all &= good
    print("S1 v2 rows:", "ALL AS PREDICTED" if ok_all else "OFF-PREDICTION: STOP")
    return 0 if ok_all else 1


# ---- selftest: checkout_lf's rows, offline (no pio, no network, no board), each in its own temporary repo ------------

class RigError(Exception):
    """The selftest's own setup failed: not a row result."""


_FLAGS_NEEDLE = '"-c", "core.autocrlf=false", "-c", ' + '"core.eol=lf", '     # in git(); built so each needle
                                                                                 # occurs once in this file
_EOL_NEEDLE = '"-c", ' + '"core.eol=lf", '
_GUARD_NEEDLE = "def eol_offenders(clone):" + "\n"
_GUARD_NEUTERED = _GUARD_NEEDLE + "    return []   # SELFTEST PLANT: the guard neutered\n"
_SETUP_FLAGS = ["-c", "core.autocrlf=false", "-c", "core.eol=lf"]
_OLDCLONE_LINE = ("REFUSED: the clone is checked out CRLF (3 file(s)); re-create it with "
                  "git -c core.autocrlf=false -c core.eol=lf clone")
_POST_LINE = "REFUSED: 1 file(s) checked out CRLF over an LF index"
_DIRTY_LINE = "REFUSED: the clone's tree is not clean"

# The predictions (registration 351774478223, section 3 and Amendment 1), as whole dicts. `cr` is the CR count in
# a.txt, the file the head changes (2 lines at head, 1 at base); `c_cr` is c.txt's.
SELFTEST_ROWS = [
    ("L-RED",       {"refused": False, "first_line": None, "cr": 2}),
    ("L-FIX",       {"refused": False, "first_line": None, "cr": 0, "sha_is_head": True}),
    ("L-NOENV",     {"refused": False, "first_line": None, "cr": 0}),
    ("L-SAB-FLAGS", {"refused": True, "first_line": _POST_LINE, "cr": 2}),
    ("L-SAB-BOTH",  {"refused": False, "first_line": None, "cr": 2}),
    ("L-EOL",       {"refused": True, "first_line": _POST_LINE, "cr": 2}),
    ("L-DELIB",     {"refused": False, "first_line": None, "cr": 0, "c_cr": 2}),
    ("L-DIRTY",     {"refused": True, "first_line": _DIRTY_LINE, "cr": 0, "head_unmoved": True}),
    ("L-S32",       {"checkout_args": 0, "checkout_lf_calls": 1, "s1_checkout_lf_calls": 1}),
    ("L-OLDCLONE",  {"refused": True, "first_line": _OLDCLONE_LINE, "paths": [".gitattributes", "a.txt", "b.txt"],
                     "head_unmoved": True, "cr": 1}),
]


def _load_module(name, path):
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _plant_module(name, plants, tmp):
    """This file's source with each (old, new) applied; refuses unless every anchor occurs exactly once, and asserts
    the written module differs from the source."""
    src = open(os.path.abspath(__file__), "rb").read()
    text = src.decode("utf-8")
    for old, new in plants:
        if text.count(old) != 1:
            raise RigError(f"plant {name}: its anchor occurs {text.count(old)} times, not once")
        mutated = text.replace(old, new)
        if mutated == text:
            raise RigError(f"plant {name}: the replacement changed nothing")
        text = mutated
    path = os.path.join(tmp, name + ".py")
    open(path, "wb").write(text.encode("utf-8"))
    if open(path, "rb").read() == src:
        raise RigError(f"plant {name}: the planted file equals the source")
    return _load_module(name, path)


def _sg(repo, *args):
    """The rig's own git: fixed flags, independent of the code under test."""
    r = subprocess.run(["git", *_SETUP_FLAGS, "-C", repo, *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise RigError(f"setup git {' '.join(args)}: exit {r.returncode}: {r.stderr.strip()}")
    return r.stdout.strip()


def _write(repo, rel, data):
    open(os.path.join(repo, rel), "wb").write(data)


def _cr(repo, rel):
    return open(os.path.join(repo, rel), "rb").read().count(b"\r")


def _repo(root, name, delib=False):
    """`* text=auto`; a.txt and b.txt committed LF at base; head changes a.txt only; checked out at base, LF."""
    d = os.path.join(root, name)
    os.makedirs(d)
    _sg(d, "init", "-q", "-b", "main")
    _write(d, ".gitattributes", b"* text=auto\n" + (b"c.txt -text\n" if delib else b""))
    _write(d, "a.txt", b"alpha\n")
    _write(d, "b.txt", b"bravo\n")
    if delib:
        _write(d, "c.txt", b"charlie\r\ndelta\r\n")
    _sg(d, "add", "-A")
    _sg(d, "commit", "-q", "-m", "base")
    base = _sg(d, "rev-parse", "HEAD")
    _sg(d, "checkout", "-q", "-b", "work")
    _write(d, "a.txt", b"alpha\nbeta\n")
    _sg(d, "commit", "-q", "-am", "head")
    head = _sg(d, "rev-parse", "HEAD")
    _sg(d, "checkout", "-q", "main")
    if _cr(d, "a.txt") or _cr(d, "b.txt") or _sg(d, "status", "--porcelain") or _sg(d, "rev-parse", "HEAD") != base:
        raise RigError(f"{name}: the repo is not at base, LF and clean after setup")
    if delib and _cr(d, "c.txt") != 2:
        raise RigError(f"{name}: c.txt does not hold its 2 committed CRs")
    return d, base, head


def _attempt(fn, repo, ref):
    try:
        return {"refused": False, "first_line": None, "sha": fn(repo, ref), "rest": []}
    except SystemExit as e:
        lines = str(e.code).splitlines()
        return {"refused": True, "first_line": lines[0] if lines else "", "sha": None,
                "rest": [l.strip() for l in lines[1:]]}


def selftest(old_path):
    import hashlib
    import stat
    import tempfile
    sys.dont_write_bytecode = True
    old_bytes = open(old_path, "rb").read()
    blob = hashlib.sha1(b"blob %d\0" % len(old_bytes) + old_bytes).hexdigest()
    gv = subprocess.run(["git", "--version"], capture_output=True, text=True).stdout.strip()
    print(f"s1 selftest: old s1.py {old_path} (git blob {blob[:12]}); {gv}; python {sys.version.split()[0]}")
    snapshot = dict(os.environ)
    root = tempfile.mkdtemp(prefix="s1-selftest-")
    results, verdicts = {}, {}
    rig = None
    try:
        home = os.path.join(root, "home")
        os.makedirs(home)
        gcfg = os.path.join(root, "gitconfig-empty")
        open(gcfg, "wb").close()
        base_env = {k: v for k, v in snapshot.items() if not k.upper().startswith("GIT_")}
        base_env.update({"GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": gcfg, "HOME": home,
                         "GIT_AUTHOR_NAME": "s1 selftest", "GIT_AUTHOR_EMAIL": "selftest@example.invalid",
                         "GIT_COMMITTER_NAME": "s1 selftest", "GIT_COMMITTER_EMAIL": "selftest@example.invalid"})
        crlf_env = dict(base_env, GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true")

        def use(env):
            os.environ.clear()
            os.environ.update(env)

        def cfg(key):
            r = subprocess.run(["git", "-C", root, "config", "--get", key], capture_output=True, text=True)
            return r.returncode, r.stdout.strip()

        # The isolation control: no autocrlf or eol anywhere in the base env, and autocrlf=true in the planted env.
        use(base_env)
        iso = (cfg("core.autocrlf"), cfg("core.eol"))
        use(crlf_env)
        planted = cfg("core.autocrlf")
        print(f"isolation control: base env core.autocrlf {iso[0]}, core.eol {iso[1]}; planted env core.autocrlf "
              f"{planted} (predicted (1, ''), (1, ''); (0, 'true'))")
        if iso != ((1, ""), (1, "")) or planted != (0, "true"):
            raise RigError("the isolation control failed: the rows would not run in the env they name")

        old = _load_module("s1_old_selftest", os.path.abspath(old_path))
        sab_flags = _plant_module("s1_plant_flags", [(_FLAGS_NEEDLE, "")], root)
        sab_both = _plant_module("s1_plant_both", [(_FLAGS_NEEDLE, ""), (_GUARD_NEEDLE, _GUARD_NEUTERED)], root)
        sab_eol = _plant_module("s1_plant_eol", [(_EOL_NEEDLE, "")], root)
        print("plants applied and asserted: flags out of git(); flags out + the guard neutered; core.eol out")

        def old_replay(repo, ref):    # build()'s status-then-checkout, through the OLD blob's own git()
            if old.git(repo, "status", "--porcelain"):
                sys.exit(_DIRTY_LINE)
            old.git(repo, "checkout", "-q", "--detach", ref)
            return old.git(repo, "rev-parse", "HEAD")

        def row_checkout(name, fn, env, delib=False, dirty=False):
            use(base_env)
            repo, b, h = _repo(root, name, delib)
            if dirty:
                _write(repo, "b.txt", b"bravo\ncharlie\n")
            use(env)
            a = _attempt(fn, repo, h)
            use(base_env)
            at = _sg(repo, "rev-parse", "HEAD")
            got = {"refused": a["refused"], "first_line": a["first_line"], "cr": _cr(repo, "a.txt")}
            return got, a, at, b, h

        def run_row(name):
            if name == "L-RED":
                got, *_ = row_checkout(name, old_replay, crlf_env)
            elif name == "L-FIX":
                got, a, at, b, h = row_checkout(name, checkout_lf, crlf_env)
                got["sha_is_head"] = a["sha"] == h and at == h
            elif name == "L-NOENV":
                got, *_ = row_checkout(name, checkout_lf, base_env)
            elif name == "L-SAB-FLAGS":
                got, *_ = row_checkout(name, sab_flags.checkout_lf, crlf_env)
            elif name == "L-SAB-BOTH":
                got, *_ = row_checkout(name, sab_both.checkout_lf, crlf_env)
            elif name == "L-EOL":
                got, *_ = row_checkout(name, sab_eol.checkout_lf, crlf_env)
            elif name == "L-DELIB":
                got, *_ = row_checkout(name, checkout_lf, crlf_env, delib=True)
                got["c_cr"] = _cr(os.path.join(root, name), "c.txt")
            elif name == "L-DIRTY":
                got, a, at, b, h = row_checkout(name, checkout_lf, crlf_env, dirty=True)
                got["head_unmoved"] = at == b
            elif name == "L-S32":
                src = open(os.path.join(HERE, "..", "s32", "s32.py"), "rb").read().decode("utf-8")
                got = {"checkout_args": src.count('"checkout"'), "checkout_lf_calls": src.count("checkout_lf("),
                       "s1_checkout_lf_calls": src.count("s1.checkout_lf(")}
            elif name == "L-OLDCLONE":
                use(base_env)
                src, b, h = _repo(root, name + "-src")
                dst = os.path.join(root, name)
                use(crlf_env)            # a clone CREATED under autocrlf=true, with no flags: its tree lands CRLF
                r = subprocess.run(["git", "clone", "-q", src, dst], capture_output=True, text=True)
                use(base_env)
                if r.returncode != 0:
                    raise RigError(f"L-OLDCLONE's clone: exit {r.returncode}: {r.stderr.strip()}")
                if _cr(dst, "a.txt") != 1 or _cr(dst, "b.txt") != 1 or _cr(dst, ".gitattributes") != 1:
                    raise RigError("L-OLDCLONE: the plant did not plant (the clone's tree is not CRLF)")
                a = _attempt(checkout_lf, dst, h)
                got = {"refused": a["refused"], "first_line": a["first_line"], "paths": a["rest"],
                       "head_unmoved": _sg(dst, "rev-parse", "HEAD") == b, "cr": _cr(dst, "a.txt")}
            return got

        for name, want in SELFTEST_ROWS:
            try:
                got = run_row(name)
            except RigError:
                raise
            except subprocess.CalledProcessError as e:
                got = {"error": f"git {' '.join(e.cmd[1:])}: exit {e.returncode}: {(e.stderr or '').strip()[:200]}"}
            except Exception as e:      # noqa: BLE001 -- outside the set, reported as the row's result
                got = {"error": repr(e)[:300]}
            results[name] = got
            verdicts[name] = got == want
            print(f"{name:12s} got  {got}")
            print(f"{'':12s} want {want} -> {'as predicted' if verdicts[name] else 'OFF-PREDICTION'}")
    except RigError as e:
        rig = str(e)
    finally:
        os.environ.clear()
        os.environ.update(snapshot)

        def onexc(func, path, _exc):
            os.chmod(path, stat.S_IWRITE)
            func(path)
        try:
            try:
                shutil.rmtree(root, onexc=onexc)
            except TypeError:
                shutil.rmtree(root, onerror=onexc)
        except OSError as e:
            print(f"(cleanup: could not remove {root}: {e})")
    if rig is not None:
        print(f"RIG: {rig} -- not a result; stop")
        return 2
    n_ok = sum(verdicts.values())
    print(f"selftest: {n_ok}/{len(SELFTEST_ROWS)} rows as predicted -> "
          f"{'ALL AS PREDICTED' if n_ok == len(SELFTEST_ROWS) else 'OFF-PREDICTION: STOP'}")
    return 0 if n_ok == len(SELFTEST_ROWS) else 1


def main(argv):
    if len(argv) >= 7 and argv[1] == "build":
        sabotage = "--sabotage" in argv
        bench_oldname = "--bench-oldname" in argv
        epoch = None
        rest = []
        it = iter(argv[2:])
        for a in it:
            if a == "--epoch":
                epoch = int(next(it))
            elif a not in ("--sabotage", "--bench-oldname"):
                rest.append(a)
        clone, ref, label, out, envs = rest[0], rest[1], rest[2], rest[3], rest[4:]
        return build(clone, ref, label, out, sabotage, envs, epoch, bench_oldname)
    if len(argv) == 4 and argv[1] == "compare":
        return compare(argv[2], argv[3])
    if len(argv) == 3 and argv[1] == "compare2":
        return compare2(argv[2])
    if len(argv) == 3 and argv[1] == "selftest":
        return selftest(argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
