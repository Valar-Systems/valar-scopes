"""Runs test_remote_stream.cpp against SABOTAGED copies of src/remote/SseStream.h and src/remote/EsphomeModel.h.
Each plant must fail EXACTLY the set of named checks frozen in docs/remote-edition.md section 5 (3d73ff0).

    python -I test/host/sabotage_remote_stream.py          (CXX=... to pick the compiler; MinGW by default)

Same method as sabotage_remote_policy.py: the real headers are never edited; both are copied into a temporary
directory searched first and the plant goes into one copy; each anchor must occur exactly once and the copy must
differ; the real files' hashes are compared before and after; the unplanted copies run first as the control
(21 checks, 0 failures). ArduinoJson comes from .pio/libdeps, and its version must equal platformio.ini's pin.
Exit: 0 every plant failed exactly its frozen set; 1 a plant missed or overshot; 2 the rig.
"""
import glob, hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass run.sh makes over every driver

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FILES = {"SseStream.h": os.path.join(ROOT, "src", "remote", "SseStream.h"),
         "EsphomeModel.h": os.path.join(ROOT, "src", "remote", "EsphomeModel.h")}
TEST = os.path.join(ROOT, "test", "host", "test_remote_stream.cpp")
FIX = os.path.join(ROOT, "test", "fixtures", "remote")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
CHECKS = 21

# name: (file, [(anchor, replacement)], the frozen failing set)
SABOTAGES = {
    "SX1": ("SseStream.h",
            [("                st = St::Broken;   // not hex, too long, or empty: never resync\n                return;\n",
              "                st = St::Ext;   // SABOTAGE SX1: skip ahead to the next line\n                break;\n")],
            {"K2"}),
    "SX2": ("SseStream.h",
            [("                bad = true;   // this line was cut off: the event it belongs to is dropped whole\n",
              "                Field();   // SABOTAGE SX2: deliver the truncated line\n")],
            {"W4"}),
    "SX3": ("EsphomeModel.h",
            [("    if (data.find(\"\\\"domain\\\":\") == std::string::npos) m.bareSeen = true;   // the dump has finished\n",
              "    m.bareSeen = true;   // SABOTAGE SX3: any state event counts\n")],
            {"M2"}),
    "SX4": ("EsphomeModel.h",
            [("        const float pos = o[\"position\"].as<float>();\n        if (!(pos >= 0.0f && pos <= 1.0f)) { ++m.rejects; return; }\n",
              "        const float raw = o[\"position\"].as<float>();\n        const float pos = raw < 0.0f ? 0.0f : (raw > 1.0f ? 1.0f : raw);   // SABOTAGE SX4: clamp\n")],
            {"M5"}),
    "SX5": ("EsphomeModel.h",
            [("    else if (std::strcmp(opStr, \"CLOSING\") == 0) op = coverpolicy::Op::Closing;\n    else { ++m.rejects; return; }\n",
              "    else if (std::strcmp(opStr, \"CLOSING\") == 0) op = coverpolicy::Op::Closing;\n    else op = coverpolicy::Op::Idle;   // SABOTAGE SX5\n")],
            {"M6"}),
    "SX6": ("EsphomeModel.h",
            [("    m.stallGuard.based = false;   // re-baselined: trips while we were not listening never fire\n    m.vibration.based = false;\n",
              "    // SABOTAGE SX6: the counters keep their old baselines across a reconnect\n")],
            {"M8"}),
    "SX7": ("EsphomeModel.h",
            [("    if (tamper && !m.tamperWas) m.tamperAcked = false;   // a new episode is pending again\n",
              "    // SABOTAGE SX7: a new episode stays acknowledged\n")],
            {"M9"}),
    "SX8": ("EsphomeModel.h",
            [("        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-' || u == '.' || u == '_' || u == '~') {\n",
              "        if (u != 0) {   // SABOTAGE SX8: no percent-encoding\n")],
            {"M11"}),
    "SX9": ("EsphomeModel.h",
            [("    if (!Relevant(data)) return;\n",
              "    if (false && !Relevant(data)) return;   // SABOTAGE SX9: parse everything\n")],
            {"M12"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def read(p):
    return open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")


def pin():
    m = re.search(r"bblanchon/ArduinoJson @ ([0-9.]+)", read(os.path.join(ROOT, "platformio.ini")))
    return m.group(1) if m else ""


def run(td, name, texts, ajson):
    inc = os.path.join(td, name)
    os.makedirs(inc, exist_ok=True)
    for fn, t in texts.items():
        open(os.path.join(inc, fn), "w", encoding="utf-8", newline="\n").write(t)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "src", "remote"),
                        "-I" + os.path.join(ROOT, "include"), "-I" + ajson, TEST, "-o", exe],
                       capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        return None
    r = subprocess.run([exe, FIX, pin()], capture_output=True, text=True)
    m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
    if r.returncode not in (0, 1) or not m or "RIG" in r.stdout:
        print(f"{name}: the suite did not run cleanly (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        return None
    failed = set(re.findall(r"^  FAIL  (\S+)", r.stdout, re.M))
    if int(m.group(1)) != CHECKS or int(m.group(2)) != len(failed):
        print(f"{name}: {m.group(0)} but {len(failed)} FAIL line(s) parsed: the RIG")
        return None
    return failed


def main():
    originals = {fn: read(p) for fn, p in FILES.items()}
    if sc.wants_anchors():
        entries = [(name, f"src/remote/{fn}", originals[fn], anchor)
                   for name, (fn, edits, _) in SABOTAGES.items() for anchor, _ in edits]
        return sc.anchors_mode("sabotage_remote_stream.py", entries)
    ajson = sorted(glob.glob(os.path.join(ROOT, ".pio", "libdeps", "*", "ArduinoJson", "src")))
    if not ajson:
        print("no ArduinoJson under .pio/libdeps (run pio run once): the RIG")
        return 2
    before = {fn: sha(p) for fn, p in FILES.items()}
    print(f"compiler: {subprocess.run([CXX, '--version'], capture_output=True, text=True).stdout.splitlines()[0]}")
    rc = 0
    with tempfile.TemporaryDirectory() as td:
        control = run(td, "control", originals, ajson[0])
        if control is None or control:
            print(f"CONTROL  not clean: {sorted(control) if control else 'did not run'} -- refusing every plant")
            return 2
        print(f"CONTROL  {CHECKS} checks, 0 failures")
        for name, (fn, edits, want) in SABOTAGES.items():
            texts = dict(originals)
            text = texts[fn]
            for anchor, repl in edits:
                n = text.count(anchor)
                if n != 1:
                    print(f"{name}  REFUSED: anchor occurs {n} times in {fn} (the RIG)")
                    return 2
                text = text.replace(anchor, repl)
            if text == texts[fn]:
                print(f"{name}  REFUSED: the copy of {fn} is byte-identical (the RIG)")
                return 2
            texts[fn] = text
            got = run(td, name, texts, ajson[0])
            if got is None:
                return 2
            ok = got == want
            print(f"{name}  {'PASS' if ok else 'FAIL'}  failed {sorted(got)}"
                  + ("" if ok else f"  (unexpected {sorted(got - want)}, missing {sorted(want - got)})"))
            if not ok:
                rc = 1
    after = {fn: sha(p) for fn, p in FILES.items()}
    if after != before:
        print("a REAL header changed during the run: the RIG")
        return 2
    print("real headers unchanged (" + ", ".join(f"{fn} {before[fn][:12]}" for fn in FILES) + ")")
    print("SABOTAGE PASSED" if rc == 0 else "SABOTAGE FAILED")
    return rc


if __name__ == "__main__":
    sys.exit(main())
