"""DRAW-PIN (docs/frame-time-bench.md, "DRAW-PIN: the re-read rule's 'differs in the draw path' as a check"): the map's
draw path as a textual pin, so "the step C that merges differs from 3e9d6ec9f449 in the draw path" is this script's exit
1, never a reading.

THE PARTS, each normalised CRLF -> LF after every `// THE FRAME BENCH BEGIN` ... `// THE FRAME BENCH END` region is
removed from its file:
  the four headers, whole: include/MapCompose.h, include/MapPlaces.h, include/MapLayer.h, include/MapSweep.h;
  in src/AircraftManager.cpp, by signature, from the signature line to its matching brace (string, char and comment text
  skipped while counting): DrawWeatherUnder, DrawMapBase, MapAirportBoxes, DrawSweepOverMap;
  the gate line: the one line holding `if (mapFrameDrawn) return DrawSweepOverMap(`.
THE PIN: sha256 over each part's name and sha256, in that order. Textual and conservative: a comment-only edit inside a
part reads CHANGED (a false alarm, never a miss). Out of scope, named: include/MapAirportsKey.h (it decides whether a
rebuild happens, not its cost or pixels; S2b's tag row measures that) and the rest of AircraftManager.cpp.

    python -I scripts/draw_pin.py --repo <git dir> --rev <sha>     the pin at a revision, against the registered value
    python -I scripts/draw_pin.py --repo <git dir> --selftest      the seven plants, each against its own tree
EXIT 0 UNCHANGED; 1 CHANGED (the parts named); 2 the rig (a signature or the gate line found other than once, a region
unbalanced or nested, a file unreadable or not UTF-8); 3 no registered value yet. Prints shas and part names only."""
import hashlib
import re
import subprocess
import sys

HEADERS = ["include/MapCompose.h", "include/MapPlaces.h", "include/MapLayer.h", "include/MapSweep.h"]
AMCPP = "src/AircraftManager.cpp"
BODIES = [("DrawWeatherUnder", "bool AircraftManager::DrawWeatherUnder("),
          ("DrawMapBase", "bool AircraftManager::DrawMapBase("),
          ("MapAirportBoxes", "int AircraftManager::MapAirportBoxes("),
          ("DrawSweepOverMap", "bool AircraftManager::DrawSweepOverMap(")]
GATE = "if (mapFrameDrawn) return DrawSweepOverMap("
BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"
FILES = HEADERS + [AMCPP]

BASE_REV = "3e9d6ec9f449"
# Computed at BASE_REV (3e9d6ec9f449b308df690d8f2edcbf024774e304) by this script, 2026-11-15, then frozen with it.
REGISTERED_PIN = "21fc376c8a2a57b86cf5dbbab724b01579241590e036b2053884da919aed61e4"
REGISTERED_PARTS = {   # name -> sha256, the same run
    "MapCompose.h": "8f1cefd40a53c840c78862896974f49c8696b32099ff0bd5fb9c8f1767a6eb24",
    "MapPlaces.h": "7c40624960a2088f45ed54af5206c9ee977507de843a1b31ccdf4346ba52e1ad",
    "MapLayer.h": "68e82c2bfcdc9292e698378396d92129ab34672fc5809beffb3583f27bea1fd0",
    "MapSweep.h": "fc69563a1eea3be98c68de2448fb3aca3ac2af144db5a2a8b35410cb31e7f52e",
    "DrawWeatherUnder": "5fb58241dde6e54e2a5be093af9df76f0c5b59ab8677e9642d1c8971034a75be",
    "DrawMapBase": "3854ed90252f35faef0509f2132f91e0ad7ca8bac57ab19f72ca1b5dbf8e8833",
    "MapAirportBoxes": "2b6307d968870bb39a300d045a1b3300147cc8f637ae0ae72282243e6c369aff",
    "DrawSweepOverMap": "2fd410c5b0b636ade4d53832a8de97b3ec96be6a517e989f7390e033e10bdad3",
    "gate": "443dac1dfda5a05add6249374bd35551d7013fd75656304e1548cf7f606d721c",
}


class Rig(Exception):
    pass


def sha(b):
    return hashlib.sha256(b).hexdigest()


def read_rev(repo, rev, path):
    r = subprocess.run(["git", "-C", repo, "show", f"{rev}:{path}"], capture_output=True)
    if r.returncode != 0:
        raise Rig(f"{path} at {rev}: git show exit {r.returncode}")
    return r.stdout


def text_of(raw, path):
    try:
        return raw.decode("utf-8").replace("\r\n", "\n")
    except UnicodeDecodeError as e:
        raise Rig(f"{path}: not UTF-8 ({e.reason})")


def strip_bench(text, path):
    out, inside = [], False
    for ln in text.split("\n"):
        if BEGIN in ln:
            if inside:
                raise Rig(f"{path}: a FRAME BENCH region opens inside another")
            inside = True
            continue
        if END in ln:
            if not inside:
                raise Rig(f"{path}: a FRAME BENCH END with no BEGIN")
            inside = False
            continue
        if not inside:
            out.append(ln)
    if inside:
        raise Rig(f"{path}: a FRAME BENCH region never closes")
    return "\n".join(out)


def body(text, name, sig):
    starts = [m.start() for m in re.finditer(r"(?m)^" + re.escape(sig), text)]
    if len(starts) != 1:
        raise Rig(f"{AMCPP}: {name}'s signature found {len(starts)} time(s), not once")
    i, n, depth, opened = starts[0], len(text), 0, False
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                raise Rig(f"{AMCPP}: {name}: an unterminated comment")
            i = j + 2
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == chr(92) else 1
            if j >= n:
                raise Rig(f"{AMCPP}: {name}: an unterminated literal")
            i = j + 1
            continue
        if c == "{":
            depth += 1
            opened = True
        elif c == "}":
            depth -= 1
            if opened and depth == 0:
                return text[starts[0]:i + 1]
        i += 1
    raise Rig(f"{AMCPP}: {name}: no matching brace")


def parts_of(files):
    """files: path -> raw bytes. Returns [(name, sha256)] in the pin's order."""
    texts = {p: strip_bench(text_of(files[p], p), p) for p in FILES}
    parts = [(p.split("/")[-1], texts[p]) for p in HEADERS]
    parts += [(name, body(texts[AMCPP], name, sig)) for name, sig in BODIES]
    gate = [ln for ln in texts[AMCPP].split("\n") if GATE in ln]
    if len(gate) != 1:
        raise Rig(f"{AMCPP}: the gate line found {len(gate)} time(s), not once")
    parts.append(("gate", gate[0]))
    return [(name, sha(t.encode("utf-8"))) for name, t in parts]


def pin_of(parts):
    return sha("".join(f"{name}\t{h}\n" for name, h in parts).encode("utf-8"))


def files_at(repo, rev):
    return {p: read_rev(repo, rev, p) for p in FILES}


def plant(files, path, old, new):
    f = dict(files)
    t = f[path].decode("utf-8").replace("\r\n", "\n")
    assert t.count(old) == 1, f"the plant's anchor occurs {t.count(old)} time(s) in {path}, need exactly 1"
    u = t.replace(old, new)
    assert u != t, "the plant did not apply"
    f[path] = u.encode("utf-8")
    return f


def verdict(base_files, planted):
    """('UNCHANGED', set()) / ('CHANGED', the parts that moved) / ('RIG', None), the planted tree against its own."""
    try:
        a, b = dict(parts_of(base_files)), dict(parts_of(planted))
    except Rig:
        return "RIG", None
    moved = {k for k in a if a[k] != b[k]}
    return ("CHANGED", moved) if moved else ("UNCHANGED", set())


STEPC_AIRPORTS = "c96bd0b04182"   # step C with the airports addition
SIBLING = "6f0cdd1381eb"          # the v20 sibling FT ran on


def selftest(repo):
    c96, sib = files_at(repo, STEPC_AIRPORTS), files_at(repo, SIBLING)
    first_sweep = c96["include/MapSweep.h"].decode("utf-8").replace("\r\n", "\n").split("\n", 1)[0] + "\n"
    plants = [
        ("P-IN", c96, AMCPP, "    k.airportsGen = mapAirportsGen;\n", "    k.airportsGen = mapAirportsGen + 0;\n",
         ("CHANGED", {"DrawMapBase"})),
        ("P-BENCH", sib, AMCPP, "        rr.cpuMhz = getCpuFrequencyMhz();\n", "        rr.cpuMhz = 0;\n",
         ("UNCHANGED", set())),
        ("P-OUT", c96, AMCPP, "            cloudAirportsRetryMs = 0; // back to the default rule\n",
         "            cloudAirportsRetryMs = 0; // back to the default rule (the plant)\n", ("UNCHANGED", set())),
        ("P-HDR", c96, "include/MapSweep.h", first_sweep, first_sweep[:-1] + " (the plant)\n",
         ("CHANGED", {"MapSweep.h"})),
        ("P-GATE", c96, AMCPP, "return DrawSweepOverMap(canvas, cx, cy, radius, angle);   // step C: the map's frame (S7)\n",
         "return DrawSweepOverMap(canvas, cx, cy, radius, angle);   // step C: the map's frame (S7, the plant)\n",
         ("CHANGED", {"gate"})),
        ("P-RIG-SIG", c96, AMCPP, "bool AircraftManager::DrawMapBase(BandCanvas& canvas, const uint16_t* wx)\n",
         "bool AircraftManager::DrawMapBaseRenamed(BandCanvas& canvas, const uint16_t* wx)\n", ("RIG", None)),
        ("P-RIG-REGION", sib, AMCPP, "        framebench::OnRebuild(rr);\n#endif\n        // THE FRAME BENCH END\n",
         "        framebench::OnRebuild(rr);\n#endif\n", ("RIG", None)),
    ]
    ok = True
    for name, tree, path, old, new, want in plants:
        got = verdict(tree, plant(tree, path, old, new))
        good = got[0] == want[0] and (want[1] is None or got[1] == want[1])
        moved = "" if got[1] is None else " {" + ", ".join(sorted(got[1])) + "}"
        print(f"  {'ok  ' if good else 'OFF '} {name}: {got[0]}{moved} (want {want[0]}"
              f"{'' if want[1] is None else ' {' + ', '.join(sorted(want[1])) + '}'})")
        ok &= good
    print("DRAW-PIN SELFTEST: 7 plants AS PREDICTED" if ok else "DRAW-PIN SELFTEST: NOT AS PREDICTED")
    return 0 if ok else 1


def main(argv):
    a = {argv[i]: argv[i + 1] for i in range(0, len(argv) - 1) if argv[i] in ("--repo", "--rev")}
    try:
        if "--selftest" in argv:
            return selftest(a["--repo"])
        parts = parts_of(files_at(a["--repo"], a["--rev"]))
    except Rig as e:
        print(f"RIG: {e}")
        return 2
    except (KeyError, IndexError):
        print(__doc__)
        return 2
    for name, h in parts:
        print(f"part {name} {h[:12]}")
    pin = pin_of(parts)
    print(f"pin {pin}")
    if not REGISTERED_PIN:
        print("no registered value yet")
        return 3
    if pin == REGISTERED_PIN:
        print(f"UNCHANGED against {BASE_REV}")
        return 0
    moved = [name for name, h in parts if REGISTERED_PARTS.get(name) != h]
    print(f"CHANGED against {BASE_REV}: {', '.join(moved)}")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
