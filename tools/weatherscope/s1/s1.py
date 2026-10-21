"""S1, the shared-code invariant (docs/weatherscope-stage3.md 1.2; step 3.1's predictions in Addendum 5).

    python -I tools/weatherscope/s1/s1.py build    <clone> <ref> <label> <out> [--sabotage] [--bench-oldname] [--epoch N] <env>...
    python -I tools/weatherscope/s1/s1.py compare  <out> <step>      (S1 v1, A5.2; its result is A5.4)
    python -I tools/weatherscope/s1/s1.py compare2 <out>             (S1 v2, A5.6: explicit R1/R2 list, calibration first)

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
    return subprocess.run(["git", "-C", clone, *args], capture_output=True, text=True, check=True).stdout.strip()


def build(clone, ref, label, out, sabotage, envs, epoch=None, bench_oldname=False):
    if git(clone, "status", "--porcelain"):
        sys.exit("REFUSED: the clone's tree is not clean")
    git(clone, "checkout", "-q", "--detach", ref)
    sha = git(clone, "rev-parse", "HEAD")
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
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
