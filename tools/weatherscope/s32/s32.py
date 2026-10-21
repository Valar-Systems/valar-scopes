"""Step 3.2's checks (docs/weatherscope-stage3.md, Addendum 6: predictions in A6.2, frozen in f254d5c).

    python -I tools/weatherscope/s32/s32.py sites    <clone>                    every plant's anchor is unique at HEAD (no build)
    python -I tools/weatherscope/s32/s32.py build    <clone> <ref> <label> <out> [--plant NAME]... [--epoch N] <env>...
    python -I tools/weatherscope/s32/s32.py s1       <out>                      S1's rows: calibration, 15/15, both sabotages
    python -I tools/weatherscope/s32/s32.py nonbuilt <clone>                    S1 for the shared files no build reads
    python -I tools/weatherscope/s32/s32.py scan     <image> s2|d2              one image's needle counts
    python -I tools/weatherscope/s32/s32.py s2d2     <out>                      S2 and D2, with their controls
    python -I tools/weatherscope/s32/s32.py d1       <clone> [--sabotage]       the Stored* naming rule
    python -I tools/weatherscope/s32/s32.py d1host   <clone> [--sabotage]       the feed-interface host test

Run every build from PowerShell (the pio esptool trap), in ONE clone for every round, with --epoch 1760000000
(S1 v2's SOURCE_DATE_EPOCH, A5.6). R1/R2 and the per-image comparison are s1.py's outside_list, imported unchanged.

THE PLANTS, named here before any measured build (A6.2 requires the per-file sites in this commit). Each replaces one
exact string that must occur exactly once in its file; the build refuses otherwise, and restores every file
byte-identical afterwards.

  S1's per-file sabotages: one UNGATED, runtime-visible value in each shared file the other envs compile, built in
  blipscope-s3-128 at the head; predicted to differ from the head's blipscope-s3-128 outside R1/R2 in each:
    pf-main            src/main.cpp           setup()'s "[boot] reset reason=%s\\n"  -> "[boot] RESET reason=%s\\n"
    pf-cws             ConfigurationWebServer /reset-wifi's reply "WiFi cleared - ..."  -> "WiFi CLEARED - ..."
    pf-buildidentity   include/BuildIdentity.h PrintBanner's "\\n[build] env=%s ..." -> "\\n[build] ENV=%s ..."
    pf-deviceidentity  include/DeviceIdentity.h Name()'s separator: DEVICE_PRODUCT_NAME "-"  -> "_"
    pf-factoryreset    include/FactoryReset.h TierName's "wifi"  -> "WIFI"
  src/SoakHarness.h has NO ungated runtime site: all of it sits inside #ifdef SOAK_TEST, main.cpp includes it only
  under SOAK_TEST, and no skus.yml env defines SOAK_TEST. Reported as such (A6.2's last clause); no build can see it.

  S1's full-matrix sabotage, unchanged from 3.1 (A5.2):
    cws-initialise     an ungated Serial.println at the top of ConfigurationWebServer::Initialise(); predicted to
                       change every env that compiles the file, with animtest-s3-128 and weatherscope-bench-s3-128
                       (neither compiles it) at 0 differing bytes.

  S2/D2's anchor, in the PRODUCT's own code (a control build):
    control            WeatherscopeManager::Update() gains an unreachable-in-practice but kept
                       `if (millis() == 0xFFFFFFFFul) Serial.println("S2D2 CONTROL local-url api.weather.gov");`
                       predicted: S2 finds local-url, and D2 finds weather.gov.

  S2's sabotage (A2.3): the guard term removed from the guard around the RADAR page (the #if directly above the
  radar's CONFIG_HTML), so the product compiles the radar form:
    s2-sabotage        predicted: S2 finds its strings and names them.

  D1's sabotage (no build): a comment naming wx::StoredWxFeed in WeatherscopeManager.cpp; predicted: the rule
  reports a second naming file, WeatherscopeManager.cpp. D1's host-test sabotage: -DWX_FEEDS_SABOTAGE, predicted
  to fail exactly "the alerts: both served, in order".
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "s1"))
import s1  # noqa: E402  (outside_list and git, unchanged from step 3.1)

EPOCH_NOTE = "S1 v2's SOURCE_DATE_EPOCH (A5.6)"
CWS = "src/ConfigurationWebServer.cpp"
GUARD_WITH = ("#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && "
              "!defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && "
              "!defined(FEATURE_SPEED) && !defined(FEATURE_WEATHERSCOPE)\n"
              "static const char CONFIG_HTML[] PROGMEM = R\"(\n")
GUARD_WITHOUT = GUARD_WITH.replace(" && !defined(FEATURE_WEATHERSCOPE)", "")
SAB_LINE = '    if (millis() == 0xFFFFFFFFul) Serial.println("S1 SABOTAGE");   // S1 SABOTAGE, never committed\n'

PLANTS = {
    "pf-main": [("src/main.cpp",
                 'Serial.printf("[boot] reset reason=%s\\n", ResetReasonName());',
                 'Serial.printf("[boot] RESET reason=%s\\n", ResetReasonName());')],
    "pf-cws": [(CWS, '"WiFi cleared - restarting into setup mode.', '"WiFi CLEARED - restarting into setup mode.')],
    "pf-buildidentity": [("include/BuildIdentity.h",
                          'Serial.printf("\\n[build] env=%s fw=v%d features=%s"',
                          'Serial.printf("\\n[build] ENV=%s fw=v%d features=%s"')],
    "pf-deviceidentity": [("include/DeviceIdentity.h",
                           'return String(DEVICE_PRODUCT_NAME "-") + suffix;',
                           'return String(DEVICE_PRODUCT_NAME "_") + suffix;')],
    "pf-factoryreset": [("include/FactoryReset.h", 'case Tier::Wifi:    return "wifi";', 'case Tier::Wifi:    return "WIFI";')],
    "cws-initialise": [(CWS, "void ConfigurationWebServer::Initialise() {\n",
                        "void ConfigurationWebServer::Initialise() {\n" + SAB_LINE)],
    "control": [("src/weatherscope/WeatherscopeManager.cpp", "void WeatherscopeManager::Update()\n{\n",
                 "void WeatherscopeManager::Update()\n{\n"
                 '    if (millis() == 0xFFFFFFFFul) Serial.println("S2D2 CONTROL local-url api.weather.gov");   // never committed\n')],
    "s2-sabotage": [(CWS, GUARD_WITH, GUARD_WITHOUT)],
}
PER_FILE = ["pf-main", "pf-cws", "pf-buildidentity", "pf-deviceidentity", "pf-factoryreset"]
NO_SITE = {"src/SoakHarness.h": "all of it is inside #ifdef SOAK_TEST, included only under SOAK_TEST; no skus.yml env defines it"}

S2_NEEDLES = [b"local-url", b"radius-unit", b"data-source", b"/api/v1/blipscope/blips"]
D2_NEEDLES = [b"weather.gov", b"noaa", b"ncep", b"mrms", b"amazonaws"]
PRODUCT, BENCH, RADAR = "weatherscope-s3-128", "weatherscope-bench-s3-128", "blipscope-s3-128"
NOT_COMPILING_CWS = ["animtest-s3-128", BENCH]
S2_ZERO_EDITIONS = ["claudescope-s3-146", "speedscope-s3-146", "missileer-s3-146"]
D1_RE = re.compile(r"Stored(Wx|Obs|Alert)Feed")
D1_DEFS = {"src/weatherscope/StoredFeeds.h", "src/weatherscope/StoredFeeds.cpp"}
D1_FACTORY = ["src/weatherscope/WxFeedFactory.cpp"]
D1_SAB_FILE = "src/weatherscope/WeatherscopeManager.cpp"


def nl_of(data):
    return "\r\n" if b"\r\n" in data else "\n"


def apply_plants(clone, names):
    """Apply every named plant; returns {path: original bytes}. Refuses unless each anchor occurs exactly once."""
    originals = {}
    for name in names:
        for rel, old, new in PLANTS[name]:
            path = os.path.join(clone, rel)
            data = originals.get(path) or open(path, "rb").read()
            originals.setdefault(path, data)
            cur = open(path, "rb").read()
            nl = nl_of(cur)
            text = cur.decode("utf-8")
            o, n = old.replace("\n", nl), new.replace("\n", nl)
            if text.count(o) != 1:
                restore(originals)
                sys.exit(f"REFUSED: plant {name}: its anchor occurs {text.count(o)} times in {rel}, not once")
            mutated = text.replace(o, n)
            assert mutated != text
            open(path, "wb").write(mutated.encode("utf-8"))
            assert open(path, "rb").read() != cur
            print(f"planted {name} in {rel}")
    return originals


def restore(originals):
    for path, data in originals.items():
        open(path, "wb").write(data)
        assert open(path, "rb").read() == data
    if originals:
        print("restored byte-identical: " + ", ".join(sorted(os.path.basename(p) for p in originals)))


def sites(clone):
    """No build: every plant's anchor occurs exactly once at HEAD, and the no-site file really has no reachable site."""
    ok = True
    for name, plants in PLANTS.items():
        for rel, old, _ in plants:
            data = open(os.path.join(clone, rel), "rb").read()
            n = data.decode("utf-8").count(old.replace("\n", nl_of(data)))
            print(f"  {name:18s} {rel}: anchor occurs {n} time(s) -> {'ok' if n == 1 else 'NOT UNIQUE'}")
            ok &= n == 1
    # SoakHarness.h: its Weatherscope term must sit inside the file's #ifdef SOAK_TEST ... #endif, main.cpp must include
    # it only under SOAK_TEST, and no skus.yml env (or what it extends) may define SOAK_TEST.
    lines = open(os.path.join(clone, "src", "SoakHarness.h"), "rb").read().decode("utf-8").splitlines()
    opens = [i for i, l in enumerate(lines) if l.strip() == "#ifdef SOAK_TEST"]
    term = [i for i, l in enumerate(lines) if "FEATURE_WEATHERSCOPE" in l]
    last_endif = max(i for i, l in enumerate(lines) if l.strip().startswith("#endif"))
    inside = len(opens) == 1 and len(term) == 1 and opens[0] < term[0] < last_endif
    print(f"  src/SoakHarness.h: #ifdef SOAK_TEST at line {opens[0] + 1 if opens else None}, the term at "
          f"{[t + 1 for t in term]}, the last #endif at {last_endif + 1} -> {'inside' if inside else 'NOT INSIDE'}")
    main = open(os.path.join(clone, "src", "main.cpp"), "rb").read().decode("utf-8").splitlines()
    inc = [i for i, l in enumerate(main) if '#include "SoakHarness.h"' in l]
    gated = len(inc) == 1 and main[inc[0] - 1].strip() == "#ifdef SOAK_TEST"
    print(f"  src/main.cpp includes it at {[i + 1 for i in inc]}, directly under #ifdef SOAK_TEST: {gated}")
    ini = open(os.path.join(clone, "platformio.ini"), "rb").read().decode("utf-8").replace("\r\n", "\n")
    sections = dict(re.findall(r"^\[(env:[^\]]+)\]\s*\n(.*?)(?=^\[|\Z)", ini, re.M | re.S))
    def defines_soak(sec, depth=0):
        body = sections.get(sec, "")
        if "-DSOAK_TEST" in body:
            return True
        ext = re.search(r"^extends\s*=\s*(\S+)", body, re.M)
        return bool(ext) and depth < 5 and defines_soak(ext.group(1), depth + 1)
    skus = open(os.path.join(clone, "skus.yml"), encoding="utf-8").read()
    envs = re.findall(r"\{\s*env:\s*([A-Za-z0-9_.-]+)", skus)
    soak = [e for e in envs if defines_soak("env:" + e)]
    control = [s for s in sections if "-DSOAK_TEST" in sections[s]]
    print(f"  skus.yml envs ({len(envs)}) defining SOAK_TEST, through extends: {soak}; "
          f"control, sections that do define it: {control}")
    ok &= inside and gated and not soak and bool(control)
    return 0 if ok else 1


def build(clone, ref, label, out, plants, envs, epoch):
    if s1.git(clone, "status", "--porcelain"):
        sys.exit("REFUSED: the clone's tree is not clean")
    s1.git(clone, "checkout", "-q", "--detach", ref)
    sha = s1.git(clone, "rev-parse", "HEAD")
    benv = dict(os.environ)
    if epoch is not None:
        benv["SOURCE_DATE_EPOCH"] = str(epoch)
    dest = os.path.join(out, label)
    os.makedirs(dest, exist_ok=True)
    manifest = {"ref": ref, "sha": sha, "plants": plants, "epoch": epoch, "envs": {}}
    originals = apply_plants(clone, plants)
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
            print(f"  {label} {env}: {'built' if ok else f'FAILED rc={rc} (see {log})'}; CWS compiled: {compiled_cws}")
            shutil.rmtree(bdir, ignore_errors=True)
    finally:
        restore(originals)
        assert not s1.git(clone, "status", "--porcelain"), "the clone is not clean after the build"
    json.dump(manifest, open(os.path.join(dest, "manifest.json"), "w"), indent=1)
    return 0 if all(e["built"] for e in manifest["envs"].values()) else 1


def load(out, label):
    m = json.load(open(os.path.join(out, label, "manifest.json")))
    if m.get("epoch") is None:
        sys.exit(f"REFUSED: {label} was not built with SOURCE_DATE_EPOCH")
    return m


def s1_rows(out):
    p = lambda label, env: os.path.join(out, label, f"{env}.bin")
    man = {l: load(out, l) for l in ["calib-a", "calib-b", "base", "head", "sabotage"] + PER_FILE}
    head_sha = man["head"]["sha"]
    for l in ["sabotage"] + PER_FILE:
        if man[l]["sha"] != head_sha:
            sys.exit(f"REFUSED: {l} was not built on the head")
    if not (man["calib-a"]["sha"] == man["calib-b"]["sha"] == man["base"]["sha"]):
        sys.exit("REFUSED: the calibration was not built on the base")
    epochs = {m["epoch"] for m in man.values()}
    if len(epochs) != 1:
        sys.exit(f"REFUSED: the rounds used different epochs {epochs}")
    print(f"base {man['base']['sha'][:7]}, head {head_sha[:7]}; SOURCE_DATE_EPOCH {epochs.pop()} ({EPOCH_NOTE})")
    eq, o, r1, r2, total = s1.outside_list(p("calib-a", RADAR), p("calib-b", RADAR))
    cal = eq and total == 0
    print(f"calibration ({RADAR} at the base, twice; predicted 0 differing bytes): "
          f"{'size differs' if not eq else f'{total} differing bytes'} -> {'as predicted' if cal else 'OFF-PREDICTION, STOP'}")
    if not cal:
        return 1
    others = sorted(man["base"]["envs"])
    ok_all = True
    print(f"S1 (predicted: identical outside R1 and R2 in 15/15; {len(others)} envs at the base):")
    n = 0
    for env in others:
        eq, o, r1, r2, total = s1.outside_list(p("base", env), p("head", env))
        good = eq and o == 0
        n += good
        print(f"  {env}: {'size differs' if not eq else f'{o} outside R1/R2 (R1 {r1}, R2 {r2}, total {total})'} -> "
              f"{'as predicted' if good else 'OFF-PREDICTION'}")
    rows_ok = n == len(others) == 15
    print(f"  -> {n}/{len(others)}: {'as predicted' if rows_ok else 'OFF-PREDICTION, STOP'}")
    ok_all &= rows_ok
    print("the full-matrix sabotage (predicted: outside R1/R2 where CWS compiled; animtest + the bench at 0 bytes):")
    n = 0
    for env in others:
        compiled = man["sabotage"]["envs"].get(env, {}).get("compiledConfigurationWebServer")
        eq, o, r1, r2, total = s1.outside_list(p("base", env), p("sabotage", env))
        good = ((not eq) or o > 0) if compiled else (eq and total == 0)
        n += good
        print(f"  {env}: CWS compiled {compiled}; {'size differs' if not eq else f'{o} outside R1/R2, {total} total'} -> "
              f"{'as predicted' if good else 'OFF-PREDICTION'}")
    not_comp = sorted(e for e in others if not man["sabotage"]["envs"].get(e, {}).get("compiledConfigurationWebServer"))
    split_ok = not_comp == sorted(NOT_COMPILING_CWS)
    print(f"  -> {n}/{len(others)}; not compiling it: {not_comp} (predicted {sorted(NOT_COMPILING_CWS)}) -> "
          f"{'as predicted' if n == len(others) and split_ok else 'OFF-PREDICTION'}")
    ok_all &= n == len(others) and split_ok
    print(f"the per-file sabotages (predicted: {RADAR} differs outside R1/R2 from the head's, in each):")
    n = 0
    for l in PER_FILE:
        eq, o, r1, r2, total = s1.outside_list(p("head", RADAR), p(l, RADAR))
        good = (not eq) or o > 0
        n += good
        print(f"  {l} ({PLANTS[l][0][0]}): {'size differs' if not eq else f'{o} outside R1/R2, {total} total'} -> "
              f"{'as predicted' if good else 'OFF-PREDICTION'}")
    for f, why in NO_SITE.items():
        print(f"  {f}: NO ungated runtime site ({why}); its S1 coverage is the full-image comparison alone, and no "
              f"skus.yml build compiles it")
    print(f"  -> {n}/{len(PER_FILE)} with a site, 1 file reported without one: "
          f"{'as predicted' if n == len(PER_FILE) else 'OFF-PREDICTION'}")
    ok_all &= n == len(PER_FILE)
    print("S1 rows:", "ALL AS PREDICTED" if ok_all else "OFF-PREDICTION: STOP")
    return 0 if ok_all else 1


def nonbuilt(clone):
    cmds = [[sys.executable, "-I", "scripts/skus.py", "--selftest"], [sys.executable, "-I", "scripts/skus.py", "--validate"],
            [sys.executable, "-I", "scripts/skus.py", "--check-readers"], ["node", "scripts/check_release_envs.mjs"]]
    ok = True
    for c in cmds:
        r = subprocess.run(c, cwd=clone, capture_output=True, text=True)
        print(f"$ {' '.join(c)} -> exit {r.returncode}")
        print(r.stdout + r.stderr)
        ok &= r.returncode == 0
    print("non-built shared files:", "as predicted (all exit 0)" if ok else "OFF-PREDICTION")
    return 0 if ok else 1


def counts(image, needles):
    data = open(image, "rb").read()
    return {n.decode(): data.count(n) for n in needles}


def s2d2(out):
    p = lambda label, env: os.path.join(out, label, f"{env}.bin")
    head, base = load(out, "head"), load(out, "base")
    ctl, sab = load(out, "control"), load(out, "s2sab")
    if not (ctl["sha"] == sab["sha"] == head["sha"]):
        sys.exit("REFUSED: the control or the sabotage was not built on the head")
    ok_all = True
    # the positive control first: S2's strings are radar-only IN FACT
    print("S2's positive control (predicted: all four present in the radar at the base, none in three editions):")
    r = counts(p("base", RADAR), S2_NEEDLES)
    good = all(v > 0 for v in r.values())
    print(f"  {RADAR}: {r} -> {'as predicted' if good else 'OFF-PREDICTION'}")
    ok_all &= good
    for env in S2_ZERO_EDITIONS:
        r = counts(p("base", env), S2_NEEDLES)
        good = all(v == 0 for v in r.values())
        print(f"  {env}: {r} -> {'as predicted' if good else 'OFF-PREDICTION'}")
        ok_all &= good
    r = counts(p("control", PRODUCT), S2_NEEDLES + D2_NEEDLES)
    good = r["local-url"] >= 1 and r["weather.gov"] >= 1
    print(f"the anchor (the product with one planted copy of each; predicted local-url and weather.gov found): {r} -> "
          f"{'as predicted' if good else 'OFF-PREDICTION: the scanner cannot see, STOP'}")
    if not good:
        return 1
    r = counts(p("head", PRODUCT), S2_NEEDLES)
    good = all(v == 0 for v in r.values())
    print(f"S2 (the product image; predicted 0 hits): {r} -> {'as predicted' if good else 'OFF-PREDICTION'}")
    ok_all &= good
    r = counts(p("s2sab", PRODUCT), S2_NEEDLES)
    found = [k for k, v in r.items() if v > 0]
    good = bool(found)
    print(f"S2's sabotage (the radar page's guard without the term; predicted: S2 finds and names its strings): {r}; "
          f"found {found} -> {'as predicted' if good else 'OFF-PREDICTION'}")
    ok_all &= good
    r = counts(p("head", PRODUCT), D2_NEEDLES)
    good = all(v == 0 for v in r.values())
    print(f"D2 (the product image, case-sensitive lowercase; predicted 0 hits): {r} -> {'as predicted' if good else 'OFF-PREDICTION'}")
    ok_all &= good
    print(f"  (recorded, not graded: the bench image {counts(p('head', BENCH), D2_NEEDLES + [b'MRMS'])})")
    print("S2/D2 rows:", "ALL AS PREDICTED" if ok_all else "OFF-PREDICTION: STOP")
    return 0 if ok_all else 1


def d1_files(clone):
    hits = []
    for root, _, files in os.walk(os.path.join(clone, "src")):
        for f in files:
            path = os.path.join(root, f)
            if D1_RE.search(open(path, "rb").read().decode("utf-8", "replace")):
                hits.append(os.path.relpath(path, clone).replace(os.sep, "/"))
    return sorted(hits)


def d1(clone, sabotage):
    originals = {}
    if sabotage:
        path = os.path.join(clone, D1_SAB_FILE)
        originals[path] = open(path, "rb").read()
        nl = nl_of(originals[path])
        open(path, "wb").write(originals[path] + ("// wx::StoredWxFeed  (D1 SABOTAGE, never committed)" + nl).encode())
        assert open(path, "rb").read() != originals[path]
        print(f"planted a mention of wx::StoredWxFeed in {D1_SAB_FILE}")
    try:
        hits = d1_files(clone)
    finally:
        restore(originals)
    namers = [h for h in hits if h not in D1_DEFS]
    print(f"files in src/ naming a Stored* feed: {hits}")
    print(f"besides the definitions ({sorted(D1_DEFS)}): {namers}")
    if not sabotage:
        good = namers == D1_FACTORY
        print(f"D1 (predicted: exactly {D1_FACTORY}) -> {'as predicted' if good else 'OFF-PREDICTION'}")
    else:
        good = namers == sorted(D1_FACTORY + [D1_SAB_FILE])
        print(f"D1's sabotage (predicted: the rule reports {D1_SAB_FILE} as a second namer) -> "
              f"{'as predicted' if good else 'OFF-PREDICTION'}")
    return 0 if good else 1


def find_cxx():
    for cand in [os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe"),
                 shutil.which("g++"), shutil.which("clang++")]:
        if cand and os.path.isfile(cand):
            return cand
    sys.exit("REFUSED: no host C++ compiler (run.sh's search order)")


def d1host(clone, sabotage):
    cxx = find_cxx()
    flags = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]  # run.sh's FLAGS
    tmp = tempfile.mkdtemp(prefix="s32-d1host-")
    exe = os.path.join(tmp, "test_wx_feeds.exe")
    cmd = [cxx, *flags, *(["-DWX_FEEDS_SABOTAGE"] if sabotage else []), "-I" + os.path.join(clone, "include"),
           "-I" + os.path.join(clone, "src", "weatherscope"), os.path.join(clone, "test", "host", "test_wx_feeds.cpp"),
           "-o", exe]
    c = subprocess.run(cmd, capture_output=True, text=True)
    if c.returncode != 0:
        print(c.stdout + c.stderr)
        sys.exit("the host test did not compile: the RIG, not a result")
    r = subprocess.run([exe, os.path.join(clone, "test", "fixtures", "wx")], capture_output=True, text=True)
    print(r.stdout + r.stderr, end="")
    shutil.rmtree(tmp, ignore_errors=True)
    fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
    if not sabotage:
        good = r.returncode == 0 and not fails
        print(f"D1 host test (predicted: exit 0, no FAIL): exit {r.returncode}, {len(fails)} FAIL -> "
              f"{'as predicted' if good else 'OFF-PREDICTION'}")
    else:
        good = r.returncode == 1 and fails == ["FAIL the alerts: both served, in order"]
        print(f"D1 host test's sabotage (predicted: exit 1, exactly the in-order alerts case): exit {r.returncode}, "
              f"{fails} -> {'as predicted' if good else 'OFF-PREDICTION'}")
    return 0 if good else 1


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ""
    if cmd == "sites" and len(argv) == 3:
        return sites(argv[2])
    if cmd == "build" and len(argv) >= 7:
        plants, epoch, rest = [], None, []
        it = iter(argv[2:])
        for a in it:
            if a == "--plant":
                name = next(it)
                if name not in PLANTS:
                    sys.exit(f"unknown plant {name}")
                plants.append(name)
            elif a == "--epoch":
                epoch = int(next(it))
            else:
                rest.append(a)
        return build(rest[0], rest[1], rest[2], rest[3], plants, rest[4:], epoch)
    if cmd == "s1" and len(argv) == 3:
        return s1_rows(argv[2])
    if cmd == "nonbuilt" and len(argv) == 3:
        return nonbuilt(argv[2])
    if cmd == "scan" and len(argv) == 4:
        print(counts(argv[2], S2_NEEDLES if argv[3] == "s2" else D2_NEEDLES))
        return 0
    if cmd == "s2d2" and len(argv) == 3:
        return s2d2(argv[2])
    if cmd == "d1" and len(argv) in (3, 4):
        return d1(argv[2], "--sabotage" in argv)
    if cmd == "d1host" and len(argv) in (3, 4):
        return d1host(argv[2], "--sabotage" in argv)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
