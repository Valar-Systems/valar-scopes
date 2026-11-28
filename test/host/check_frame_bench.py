"""THE FRAME BENCH's static rows, C1 (docs/frame-time-bench.md, "C1's bench"), on the M3 tree. Each row has a plant
that must read exactly that row red (--selftest).
  FB1   no line naming the bench outside a `// THE FRAME BENCH` region (main.cpp, AircraftManager.cpp,
        ConfigurationWebServer.cpp)
  FB2   every region opens with #ifdef FRAME_BENCH and closes with #endif
  FB3   no NVS or file-system call in the instrument (the header and the regions)
  FB5   FrameBench.h is all under #ifdef FRAME_BENCH; FrameBenchTable.h includes no Arduino header
  M3-1  framebench::Present( appears exactly twice in main.cpp: in pushtask::Task between framebuf::FrameDone() and
        xSemaphoreGive(g_free[i]) (B), and after the loop's FrameDone() inside its `#if !defined(BLIPSCOPE_PUSH_TASK)`
        block (A); nowhere else, so never at the loop's hand-off in B
  M3-3P the push plant is vTaskDelay(pdMS_TO_TICKS(..)) in pushtask::Task, between tft.endWrite() and t2; no
        delayMicroseconds anywhere in pushtask::Task (a spin there would preempt the loop: p4's caution 2)
  C1-A  the (a) hook region exists once, in the cloud branch after the schema-mismatch print
  C1-W  the weather-field region exists once, in ConsumeWeather before `whole`, and sets `claimed = len`
  C1-M  MapEnabled()'s first statement is the map-off region
  C1-E  the enrichment scan skips the table's hexes before the cooldown check, and RequestCloudEnrich counts them
  C1-K  GetStoredString notes the bench's answers and the board's own (the record)
  MRK   "[frame-bench]" is in check-no-bench-hooks.sh's MARKERS
  C1-X  the v20 sibling carries step C, so C1's bench exemption is GONE (docs/frame-time-bench.md, the sibling's re-cut,
        FINDING 2): src/AircraftManager.h includes "MapSweep.h" (step C's one definer of BLIPSCOPE_MAP_STEP_C), and
        BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT appears in none of the bench's files. Every FRAME_BENCH build defined it, so
        on this tree it could only mask an FT image that lost step C, which must hit the refusal's #error instead
  M3-L  THE PACKAGE: the loop plant (framebench::DelayOn) appears exactly once in main.cpp, in a FRAME BENCH region
        whose END is followed at once by `pushtask::HandOver();` -- before the hand-over, outside the push's window
  M3-D  THE PACKAGE: framebench::DropThis(i) appears exactly once, in a FRAME BENCH region of pushtask::Task after its
        xQueueReceive and before its esp_cache_msync (before any push work); the region releases the buffer
        (xSemaphoreGive(g_free[i])) and continues
  FB-TU every macro FrameBench.h tests (#if/#ifdef/#ifndef/#elif) that a PROJECT header #defines (include/**) is
        included by FrameBench.h itself before its first test -- that header, or "variants/Variant.h" for one under
        include/variants/ -- so no file can see a different scene table by its own include order (C1's plants, which
        never fired because main.cpp kept image A's kScenes; docs/frame-time-bench.md, "THE PLANTS' CAUSE")
  FB-REC the v20 sibling's I-5 band holds only for C1's record: struct Rec, S(), Mux(), Ten(), Record() and the
        measuring loop SelfTime() in FrameBench.h are textually identical to 0d6308cb's (their whitespace-trimmed
        text's sha256 against REC_SHA). A change re-registers the band from the sibling's proof window
        (docs/frame-time-bench.md, "THE v20 SIBLING", Q-S3)
  FT-LAPS FT's rebuild row (docs/frame-time-bench.md, "THE REBUILD ROW"): include/MapCompose.h holds the four laps once
        each, inside FRAME BENCH regions, in Rebuild's order (Load, its lap, Build, its lap, the names, their lap) and
        Compose's lap right after Compose; AircraftManager.cpp takes the µs total around the one Frame() call and hands
        the record over inside the rebuild branch
  FT-PLANT the timer plant: D = 20,000 µs defined once; its wait once, gated by the plant flag, between the names and
        their lap (inside the graded figure, after Load), spun by Inputs::spin, which the bench wires to the CPU cycle
        counter (never micros, the timer the laps use: a wait on the timer it checks proves only that it fired); the
        flag taken and cleared at Rebuild's start
  FT-DREW Addition 2 per frame: step C's mapFrameDrawn enters Record's stateFlags as bit value 32, in a region
  FT-SWAP FT's install is MAP_BENCH's MapBenchAccept, a second path beside ConsumeBasemap's Accepted branch: it sets
        every draw-side field that branch sets (derived from both bodies: mapPkg, mapHeldLen, ++mapPkgSeq), after its
        copy into the landing buffer and maplayer::Load
  FT-ENV the two FT envs: cloud.prod, WX_LAYER_SHIPS, FRAME_BENCH, MAP_BENCH, FRAME_BENCH_FT and the row's flag; no
        WX_LOOK_AB, no -DBLIPSCOPE_MAP_STEP_C; common's scripts then pre:scripts/ft_prebuild.py; the row's pin list; and
        board_build.embed_files equal to the pins, in order
  FT-TAB FrameBenchFt.h's package table equals each row's pin list (point, variant, sha256 prefix), in order, with the
        symbols named for each embed file
  FT-SCENES each row's window as registered: KORD's A/A pair 1 at 10/25/50 mi, the plant (KORD at 25), every point's
        baseline then option at 10/25/50, pair 2 as pair 1 (FT-SEA 31 scenes, FT-HR 49)
  FT-TBL TBL-DEV per FT scene: FrameBench.h defines PrintHomeTable once, printing the one registered line from
        fbtable::Sum at the scene's STORED home (st.ftLat, st.ftLon: the doubles OnCloudPoll serves around), kLevel and
        kTblT, with no float conversion in its body (a CRC, never a coordinate); AircraftManager.cpp calls it once, in a
        region, on the line right after SetHome inside the branch where the scene's package validated
  TAG-SET S2b's map-rebuild tag (docs/frame-time-bench.md, "S2b's map-rebuild frames", re-registered as (B)), four
        clauses: `mapRebuilt = true` exactly once in src/ and include/, inside OnRebuild after its `scene < 0 || done`
        return; TakeMapRebuilt's body exactly reads it as 64 and clears it; its result enters the one framebench::Record(
        call, taken on the line before it, in a region; no other 64 flag write in the bench's files (the header, the
        sources' regions). Record() itself is untouched, so FB-REC still holds
  S2b's port (docs/frame-time-bench.md, "S2b's registration" and its P2 amendment): RP5's rows (b417d80cd4c5's
  check_frame_bench.py), re-pointed at where S2b's pieces live (include/FrameBenchS2b.h, OnCloudPollS2b), plus S2B-SCOPE:
  FB-R3  git tracks no recording and no generated recording header (framebench_rec_gen.h, *.fbrec, recordings/); the
         generator scripts/framebench_rec.py is code and is tracked
  FB-R4  no print in S2b's own text (its #if branches, FrameBenchS2b.h) carries a latitude or longitude value
  FB-R5  FrameBenchS2b.h's airport strings equal include/Airports.h's values at 2 dp, code by code
  FB-R6  the rung set is the single kRungs[] constant: one definition, and no other {25, 50, 100}-style list
  FB-R7  the replay's CRC is computed on the device over the SAME body it deserialized, and the served line prints it
  FB-R8  the generated header is included once, in FrameBenchS2b.h under FRAME_BENCH_S2B, which FrameBench.h includes
         only under `#elif defined(FRAME_BENCH_S2B)`; scripts/framebench_rec.py checks each pin before using it
  FB-R9  OnCloudPollS2b serves the replay in the settle too: no `s.measuring` test anywhere in it, and its REPLAY gate
         precedes the body's deserialize
  FB-R10 (Z): ApplyZoom( exactly once in the bench regions, after a scene's Initialise() and behind
         framebench::ViewRung(), and ViewRung answers a rung only through ShipsThenZooms(k), which is exactly
         REPLAY || LIVE_APT ((a)+'s E3, ruled 2026-11-17), below kShippingRadiusMi (re-registered 2026-11-18: the
         REPLAY-only text this row asked for was stale from 7382ef81 on)
  FB-R11 THE CUSTOMER DEFAULTS: kKeepKeys equals the keys whose parse in Initialise() keeps the current value on "",
         SnapshotKeep's argument passes their members in kKeepKeys' order, kOwnKeys is exactly the five identity keys,
         and OverrideS2b's two fall-throughs return CustomerDefault(key)
  S2B-SCOPE every line of FrameBench.h and the sources naming an S2b-only identifier lies inside an S2b preprocessor
         branch (`#if defined(FRAME_BENCH_S2B)`, its `#elif`, or the `#else` of `#if !defined(FRAME_BENCH_S2B)`): the
         text-level half of S2B-ISO, so no other env compiles an S2b line
  S2b's map built in (s2b_rerun_registration.amend3.txt; the owner, 2026-11-18 23:25Z: "S2b: built-in"):
  S2B-ENV  S2b's env adds MAP_BENCH, FT's pre-build step and its pin list, and embeds exactly its pins, in order; no
           FRAME_BENCH_FT, WX_LOOK_AB or BLIPSCOPE_MAP_STEP_C
  S2B-TAB  FrameBenchS2b.h's package names, sha256 prefixes and embed symbols equal s2b_pkg.pins, in order
  B-CAP    each pinned length is within the shipping query's cap, read from the image's own host row CA1
           (test_map_fetch.cpp): a fetched package over it is refused unread, so a built-in one over it is not S2b's
  B-LOAD   S2b's source branch in MapBenchAccept chooses the bytes and nothing else: no install, one refusal (no
           package); the copy, the gate and the install are FT-SWAP's, shared
  B-TICK   MapBenchTick returns at once under S2b, as under FT: MAP_BENCH's own phases never run
  B-NONE   Initialise's home-to-the-package's-centre region is compiled out under S2b; S2bPkgOf gives -1 to every
           kind but REPLAY and LIVE-APT; the scene driver sets the package once, right before Initialise
  B-CENTRE between the gate and the install, a package whose centre is not the scene's 2-dp home is refused
    python -I check_frame_bench.py <tree>             exit 0 every row ok; 1 a row off
    python -I check_frame_bench.py --selftest <tree>  the clean tree green; each plant reads exactly its row red"""
import os
import re
import subprocess
import sys

BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"
SOURCES = ["src/main.cpp", "src/AircraftManager.cpp", "src/ConfigurationWebServer.cpp"]
HEADER, TABLE, MARKS = "include/FrameBench.h", "include/FrameBenchTable.h", "scripts/check-no-bench-hooks.sh"
AMH, EXEMPT = "src/AircraftManager.h", "BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT"
MCH, AMC = "include/MapCompose.h", "src/AircraftManager.cpp"
FTH, INI = "include/FrameBenchFt.h", "platformio.ini"
FT_ROWS = {"SEA": ("blipscope-s3-128-framebench-v20-ftsea", "test/fixtures/frame-bench/ft_sea.pins", "FRAME_BENCH_FT_SEA"),
           "HR": ("blipscope-s3-128-framebench-v20-fthr", "test/fixtures/frame-bench/ft_hr.pins", "FRAME_BENCH_FT_HR")}
NVS = re.compile(r"Preferences|nvs_|SPIFFS|LittleFS|prefs\.(put|remove|clear)")
# TAG-SET (S2b, (B)): the set, the take's head and exact body, the one Record call and its take, a 64 flag write
TAG_TRUE = "mapRebuilt = true"
TAKE_HEAD = "inline uint8_t TakeMapRebuilt()"
TAKE_BODY = ["{", "State& s = S();", "const uint8_t v = s.mapRebuilt ? 64 : 0;", "s.mapRebuilt = false;", "return v;", "}"]
TAG_TAKE = "const uint8_t mapTag = framebench::TakeMapRebuilt();"
TAG_CALL = "framebench::Record(frameUs, trackedAircraft.size(), (uint8_t)(stateFlags | mapTag));"
FLAG64 = re.compile(r"\?\s*64\s*:|\|=?\s*64\b|\b64\s*\|")
# S2b's port (RP5's rows, re-pointed): its table header, Airports.h, the generator script
S2B_H, APH, REC_PY = "include/FrameBenchS2b.h", "include/Airports.h", "scripts/framebench_rec.py"
S2B_ENV, S2B_PINS = "blipscope-s3-128-framebench-v20-s2b", "test/fixtures/frame-bench/s2b_pkg.pins"
MFT = "test/host/test_map_fetch.cpp"
TRACKED_REC = re.compile(r"framebench_rec_gen\.h|\.fbrec$|(^|/)recordings/")
PRINT = re.compile(r"Serial\.(print|printf|println)\s*\(")
COORD = re.compile(r"kAptLat|kAptLon|kS2bHomeLat|kS2bHomeLon|latSent|lonSent|req->lat|req->lon|\blat0\b|\blon0\b|"
                   r"\.latitude|\.longitude")
# S2B-SCOPE: identifiers only S2b's text may name (comments stripped first)
S2B_ONLY = ["OnCloudPollS2b", "OverrideS2b", "SnapshotKeep", "CustomerDefault", "SelfTimeOverride", "ViewRung",
            "HomeName", "RecCrc32", "kRecPolls", "kRecN", "FrameBenchS2b.h", "CTRL_IL", "LIVE_APT", "LIVE_OWN",
            "keepTaken", "kKeepKeys", "kOwnKeys", "kNumKeep", "S().submits", "fb.submits", "sceneStartMs",
            "S2bPkgOf", "S2bPkgStart", "S2bPkgLen", "kS2bPkg"]


def nocomment(l):
    return l.split("//")[0]


def load(tree):
    files = {}
    for rel in SOURCES + [HEADER, TABLE, MARKS, AMH, MCH, FTH, INI, S2B_H, APH, REC_PY, S2B_PINS, MFT] + [r[1] for r in FT_ROWS.values()]:
        files[rel] = open(os.path.join(tree, rel), "rb").read().decode("utf-8").replace("\r\n", "\n").split("\n")
    # FB-TU: which project headers #define which macros (include/**, the bench's own two headers excluded)
    defs = {}
    inc = os.path.join(tree, "include")
    for root, _, names in os.walk(inc):
        for n in names:
            if not n.endswith((".h", ".hpp")):
                continue
            rel = os.path.relpath(os.path.join(root, n), inc).replace(os.sep, "/")
            if rel in ("FrameBench.h", "FrameBenchTable.h"):
                continue
            for l in open(os.path.join(root, n), "rb").read().decode("utf-8", "replace").split("\n"):
                m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)", l)
                if m:
                    defs.setdefault(m.group(1), set()).add(rel)
    files["__defs__"] = defs
    # FB-R3: git's own list of tracked files (None when git cannot be read: the row then reads red, never green)
    try:
        out = subprocess.run(["git", "-C", tree, "ls-files"], capture_output=True, text=True, check=True).stdout
        files["__tracked__"] = out.splitlines()
    except (OSError, subprocess.CalledProcessError):
        files["__tracked__"] = None
    # TAG-SET: the tag's set in every OTHER src/ and include/ file (the loaded ones are scanned from `files`, so a plant
    # in them is seen)
    loaded = set(SOURCES + [HEADER, TABLE, AMH, MCH, FTH])
    other = []
    for top in ("src", "include"):
        for root, _, names in os.walk(os.path.join(tree, top)):
            for n in names:
                if not n.endswith((".h", ".hpp", ".cpp", ".c", ".inc")):
                    continue
                rel = os.path.relpath(os.path.join(root, n), tree).replace(os.sep, "/")
                if rel in loaded:
                    continue
                text = open(os.path.join(root, n), "rb").read().decode("utf-8", "replace")
                other += [f"{rel}:{i + 1}" for i, l in enumerate(text.split("\n")) if TAG_TRUE in nocomment(l)]
    files["__tag_other__"] = other
    return files


TESTED = re.compile(r"\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)")


def fb_tu(f):
    """FB-TU: (ok, detail). A macro FrameBench.h tests and a project header defines must be included first."""
    h, defs = f[HEADER], f["__defs__"]
    first = {}
    for i, l in enumerate(h):
        m = TESTED.match(l)
        if m:
            for name in re.findall(r"[A-Za-z_]\w*", m.group(1)):
                if name != "defined" and name in defs:
                    first.setdefault(name, i)
    incs = {}
    for i, l in enumerate(h):
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', l)
        if m:
            incs.setdefault(m.group(1), i)
    bad, seen = [], []
    for name, i in sorted(first.items()):
        need = sorted({"variants/Variant.h" if d.startswith("variants/") else d for d in defs[name]})
        ok = any(incs.get(n, 10 ** 9) < i for n in need)
        seen.append(f"{name} (defined by {', '.join(sorted(defs[name]))}; needs {' or '.join(need)} before :{i + 1})")
        if not ok:
            bad.append(name)
    return (not bad, f"{len(first)} tested macro(s) defined by project headers; not included first: "
                     f"{bad if bad else 'none'}; {'; '.join(seen) if seen else 'none tested'}")


def regions(lines):
    out, start = [], None
    for i, l in enumerate(lines):
        if l.strip() == BEGIN:
            start = i
        elif l.strip() == END and start is not None:
            out.append((start, i))
            start = None
    return out


def inside(regs, i):
    return any(a <= i <= b for a, b in regs)


def func_span(lines, head):
    """[start, end) of the function whose definition line starts with `head`, by brace depth."""
    s = next((i for i, l in enumerate(lines) if l.startswith(head)), -1)
    if s < 0:
        return -1, -1
    depth, opened = 0, False
    for j in range(s, len(lines)):
        depth += lines[j].count("{") - lines[j].count("}")
        opened = opened or "{" in lines[j]
        if opened and depth == 0:
            return s, j + 1
    return s, len(lines)


def rows(f):
    res = {}
    bad1, bad2, bad3 = [], [], []
    for rel in SOURCES:
        L, regs = f[rel], regions(f[rel])
        for i, l in enumerate(L):
            if re.search(r"framebench::|FrameBench\.h", l) and not inside(regs, i):
                bad1.append(f"{rel}:{i + 1}")
        for a, b in regs:
            if L[a + 1].strip() != "#ifdef FRAME_BENCH" or L[b - 1].strip() != "#endif":
                bad2.append(f"{rel}:{a + 1}")
            bad3 += [f"{rel}:{i + 1}" for i in range(a, b) if NVS.search(L[i].split("//")[0])]
    bad3 += [f"{HEADER}:{i + 1}" for i, l in enumerate(f[HEADER]) if NVS.search(l.split("//")[0])]
    res["FB1"] = (not bad1, f"bench lines outside a region: {bad1[:3] or 0}")
    res["FB2"] = (not bad2, f"regions not opened by #ifdef FRAME_BENCH / closed by #endif: {bad2[:3] or 0}")
    res["FB3"] = (not bad3, f"NVS or file-system calls in the instrument: {bad3[:3] or 0}")
    code = [l.strip() for l in f[HEADER] if l.strip() and not l.strip().startswith("//")]
    pure = not any(re.search(r"#include\s*[<\"](Arduino|esp_|freertos)", l) for l in f[TABLE])
    fb5 = len(code) >= 3 and code[0] == "#pragma once" and code[1] == "#ifdef FRAME_BENCH" and code[-1].startswith("#endif")
    res["FB5"] = (fb5 and pure, f"header under #ifdef {fb5}; table pure {pure}")

    m = f["src/main.cpp"]
    pres = [i for i, l in enumerate(m) if "framebench::Present(" in l]
    ts, te = func_span(m, "void Task(void*)")
    in_task = [i for i in pres if ts <= i < te
               and any(m[j].strip() == "framebuf::FrameDone();" for j in range(max(ts, i - 4), i))
               and any(m[j].strip() == "xSemaphoreGive(g_free[i]);" for j in range(i, min(te, i + 6)))]
    in_loopA = [i for i in pres if not ts <= i < te
                and any(m[j].strip() == "#if !defined(BLIPSCOPE_PUSH_TASK)" for j in range(max(0, i - 5), i))
                and any(m[j].strip() == "framebuf::FrameDone();" for j in range(max(0, i - 4), i))
                and any(m[j].strip() == "#endif" and j + 1 < len(m) and "THE FRAME BENCH" not in m[j]
                        for j in range(i + 1, min(len(m), i + 5)))]
    res["M3-1"] = (len(pres) == 2 and len(in_task) == 1 and len(in_loopA) == 1,
                   f"Present calls {len(pres)}: in pushtask::Task after FrameDone {len(in_task)}, after the loop's "
                   f"FrameDone in its no-push-task block {len(in_loopA)}")
    task = m[ts:te] if ts >= 0 else []
    vt = [i for i, l in enumerate(task) if "vTaskDelay(pdMS_TO_TICKS(" in l and "PushDelayMs()" in l]
    ew = next((i for i, l in enumerate(task) if l.strip() == "tft.endWrite();"), -1)
    t2 = next((i for i, l in enumerate(task) if l.strip().startswith("const uint32_t t2 = micros();")), -1)
    spin = [i for i, l in enumerate(task) if "delayMicroseconds" in l]
    m33 = len(vt) == 1 and ew < vt[0] < t2 and not spin
    res["M3-3P"] = (m33, f"push plants by vTaskDelay {len(vt)}, between endWrite and t2 {m33}; spins in the task {len(spin)}")

    a = f["src/AircraftManager.cpp"]
    aregs = regions(a)
    hook = [i for i, l in enumerate(a) if "framebench::OnCloudPoll(" in l]
    mis = next((i for i, l in enumerate(a) if "blips schema mismatch; keeping current picture" in l), -1)
    res["C1-A"] = (len(hook) == 1 and inside(aregs, hook[0]) and 0 <= mis < hook[0] < mis + 8,
                   f"(a) hook calls {len(hook)}, after the schema-mismatch print {len(hook) == 1 and 0 <= mis < hook[0]}")
    cs, ce = func_span(a, "void AircraftManager::ConsumeWeather(")
    wf = [i for i in range(max(cs, 0), max(ce, 0)) if "framebench::WxField(" in a[i]]
    whole = next((i for i in range(max(cs, 0), max(ce, 0)) if "const bool whole =" in a[i]), -1)
    cl = any(a[j].strip() == "claimed = len;" for j in (range(wf[0], wf[0] + 3) if wf else []))
    res["C1-W"] = (len(wf) == 1 and wf[0] < whole and cl, f"weather-field calls {len(wf)}, before `whole` "
                                                          f"{len(wf) == 1 and wf[0] < whole}, sets claimed {cl}")
    ms, me = func_span(a, "bool AircraftManager::MapEnabled() const")
    body = [l.strip() for l in a[ms + 1:me] if l.strip() and l.strip() != "{"] if ms >= 0 else []
    mo = len(body) >= 3 and body[0] == BEGIN and body[1] == "#ifdef FRAME_BENCH" and "framebench::MapOff()" in body[2]
    res["C1-M"] = (mo, f"MapEnabled opens with the map-off region {mo}")
    sk = [i for i, l in enumerate(a) if "framebench::IsSceneHex(icao)) continue;" in l]
    cd = [i for i in sk if any("tracked.metadataRetryAfter" in a[j] for j in range(i, i + 4))]
    cnt = sum(1 for l in a if "framebench::IsSceneHex(icao24)) framebench::S().fbSubmits++;" in l)
    res["C1-E"] = (len(sk) == 1 and len(cd) == 1 and cnt == 1,
                   f"scan skips {len(sk)} (before the cooldown {len(cd)}); submit counters {cnt}")
    c = f["src/ConfigurationWebServer.cpp"]
    nk_b = sum(1 for l in c if "framebench::NoteKey(key, v, true);" in l)
    nk_o = sum(1 for l in c if "framebench::NoteKey(key, value.c_str(), false);" in l)
    res["C1-K"] = (nk_b == 1 and nk_o == 1, f"notes of the bench's answers {nk_b}, of the board's own {nk_o}")
    mk = any(l.startswith("MARKERS=(") and '"[frame-bench]"' in l and '"[s2b-map]"' in l for l in f[MARKS])
    res["MRK"] = (mk, f"[frame-bench] and [s2b-map] in MARKERS {mk}")
    h = f[AMH]
    step_c = sum(1 for l in h if l.strip().startswith('#include "MapSweep.h"'))
    named = sum(1 for rel in [AMH] + SOURCES + [HEADER, TABLE] for l in f[rel] if EXEMPT in l)
    res["C1-X"] = (step_c == 1 and named == 0, f"step C's MapSweep.h include {step_c} (want 1); the exemption named "
                                               f"{named} time(s) in the bench's files (want 0)")
    res["FB-TU"] = fb_tu(f)
    res["M3-L"], res["M3-D"] = m3_l(f), m3_d(f)
    res["FB-REC"] = fb_rec(f)
    res["FT-LAPS"], res["FT-PLANT"], res["FT-DREW"] = ft_laps(f), ft_plant(f), ft_drew(f)
    res["FT-SWAP"], res["FT-ENV"] = ft_swap(f), ft_env(f)
    res["FT-TAB"], res["FT-SCENES"] = ft_tab(f), ft_scenes(f)
    res["FT-TBL"] = ft_tbl(f)
    res["TAG-SET"] = tag_set(f)
    res.update(s2b_builtin(f))
    res.update(s2b_rows(f))
    return res


ASSIGN = re.compile(r"^\s*(?:\+\+\s*(\w+)|(\w+)\s*=)[^=]")


def assigned(lines):
    """The member names a block assigns (`x = ...` or `++x`), in order, comparisons excluded."""
    out = []
    for l in lines:
        m = ASSIGN.match(l)
        if m:
            out.append(m.group(1) or m.group(2))
    return out


def ft_swap(f):
    """FT-SWAP: FT's install is MAP_BENCH's MapBenchAccept, a second path beside ConsumeBasemap's Accepted branch; it
    must establish exactly what that branch does on the draw side (mapPkg, mapHeldLen, ++mapPkgSeq), after its own copy
    into the landing buffer and the gate. Both sets are derived from the code, never typed."""
    a = f[AMC]
    s, e = func_span(a, "void AircraftManager::MapBenchAccept(int which)")
    cs, ce = func_span(a, "void AircraftManager::ConsumeBasemap(")
    if s < 0 or cs < 0:
        return (False, "MapBenchAccept or ConsumeBasemap not found")
    acc = next((i for i in range(cs, ce) if "if (o == mappolicy::Outcome::Accepted) {" in a[i]), -1)
    acc_end = next((i for i in range(acc + 1, ce) if a[i].strip() == "}"), -1) if acc >= 0 else -1
    branch = set(assigned(a[acc + 1:acc_end])) if acc >= 0 and acc_end > acc else set()
    body = a[s:e]
    tail = assigned(body[-6:])   # the install is the function's last statements
    cpy = next((i for i, l in enumerate(body) if "memcpy(mapLanding, s, len);" in l), -1)
    gate = next((i for i, l in enumerate(body) if "maplayer::Load(mapLanding, len, &pk);" in l), -1)
    inst = next((i for i, l in enumerate(body) if l.strip() == "mapPkg = pk;"), -1)
    order = 0 <= cpy < gate < inst
    ok = branch == {"mapPkg", "mapHeldLen", "mapPkgSeq"} and set(tail) >= branch and order
    return (ok, f"the Accepted branch sets {sorted(branch)}; MapBenchAccept's install {sorted(set(tail) & branch)}; "
                f"copy -> gate -> install {order}")


def ini_env(lines, name):
    """The lines of [env:name] in platformio.ini, comments (';' to the end) stripped."""
    out, on = [], False
    for l in lines:
        if l.strip().startswith("[") and l.strip().endswith("]"):
            on = l.strip() == f"[env:{name}]"
            continue
        if on:
            t = l.split(";")[0].rstrip()
            if t.strip():
                out.append(t)
    return out


def pins_of(lines):
    return [tuple(l.split()[:3]) for l in lines if l.strip() and not l.lstrip().startswith("#")]


def ft_env(f):
    """FT-ENV: each FT env compiles the accepted flag set and nothing that would change what is measured: cloud.prod,
    WX_LAYER_SHIPS, FRAME_BENCH, MAP_BENCH, FRAME_BENCH_FT and its row's flag (and not the other's); no WX_LOOK_AB; no
    -DBLIPSCOPE_MAP_STEP_C (CD4: MapSweep.h only); the pre-build step after common's; its pin list; and embed_files equal
    to .pio/ftpkg/<POINT>-<variant>.bmpk for each pin, in the pin list's order."""
    bad = []
    for row, (env, pins, flag) in FT_ROWS.items():
        e = ini_env(f[INI], env)
        txt = "\n".join(e)
        other = [v[2] for k, v in FT_ROWS.items() if k != row][0]
        need = ["${cloud.prod}", "-DWX_LAYER_SHIPS", "-DFRAME_BENCH", "-DMAP_BENCH", "-DFRAME_BENCH_FT", "-D" + flag,
                "${common.extra_scripts}", "pre:scripts/ft_prebuild.py", f"custom_ft_pins = {pins}"]
        miss = [n for n in need if not any(l.strip() == n or l.strip().startswith(n + " ") for l in e)
                and n not in txt.split()]
        if not e:
            bad.append(f"{env} absent")
            continue
        if miss:
            bad.append(f"{env} lacks {miss}")
        if "WX_LOOK_AB" in txt or "BLIPSCOPE_MAP_STEP_C" in txt or other in txt:
            bad.append(f"{env} carries a forbidden flag")
        i = next((k for k, l in enumerate(e) if l.strip().startswith("board_build.embed_files")), -1)
        emb = [l.strip() for l in e[i + 1:] if l.startswith((" ", "\t"))] if i >= 0 else []
        want = [f".pio/ftpkg/{p}-{v}.bmpk" for p, v, _ in pins_of(f[pins])]
        if emb[:len(want)] != want or len(emb) < len(want) or (len(emb) > len(want) and emb[len(want)].startswith(".pio")):
            bad.append(f"{env} embed list is not its pins in order")
    return (not bad, "both FT envs as registered" if not bad else "; ".join(bad))


TAB_ROW = re.compile(r'\{"([A-Z]+)", "([A-Za-z0-9-]+)", "([0-9a-f]{12})", (\w+)Start, (\w+)End\}')
PKG_SYM = re.compile(r'FRAME_BENCH_FT_PKG\((\w+), "([A-Za-z0-9_]+)"\)')


def ft_section(h, flag):
    """FrameBenchFt.h's lines for one row: from its #if/#elif defined(flag) to the next #elif/#else."""
    s = next((i for i, l in enumerate(h) if re.match(r"#(el)?if defined\(" + flag + r"\)", l.strip())), -1)
    if s < 0:
        return []
    e = next((i for i in range(s + 1, len(h)) if h[i].startswith(("#elif", "#else"))), len(h))
    return h[s + 1:e]


def ft_tab(f):
    """FT-TAB: each row's package table in FrameBenchFt.h equals its pin list, entry for entry (point, variant, sha256
    prefix), in order, and each entry's symbols are declared for <POINT>_<variant>, the embed file's own name."""
    bad = []
    for row, (env, pins, flag) in FT_ROWS.items():
        sec = ft_section(f[FTH], flag)
        tab = [m.groups() for l in sec for m in [TAB_ROW.search(l)] if m]
        syms = {m.group(1): m.group(2) for l in sec for m in [PKG_SYM.search(l)] if m}
        want = [(p, v, s[:12]) for p, v, s in pins_of(f[pins])]
        if [(t[0], t[1], t[2]) for t in tab] != want:
            bad.append(f"{row}: the table is not the pins in order ({len(tab)} entries, {len(want)} pins)")
        for p, v, _, a, b in tab:
            if a != b or syms.get(a) != f"{p}_{v}":
                bad.append(f"{row}: {p} {v} names the wrong symbols")
    return (not bad, "both rows' tables equal their pins" if not bad else "; ".join(bad))


SCENE_ROW = re.compile(r"\{FT_K, (\w+), (\d+), (\w+)\}")


def ft_scenes(f):
    """FT-SCENES: each row's window as registered: KORD's A/A pair 1 (its package twice at 10, 25 and 50 mi), the
    plant (KORD's package at 25 mi), every point's baseline then option at 10, 25 and 50 mi, and pair 2 as pair 1.
    kFtAA is KORD's pinned package. FT-SEA 6 + 1 + 18 + 6 = 31 scenes; FT-HR 6 + 1 + 36 + 6 = 49."""
    bad = []
    for row, (env, pins, flag) in FT_ROWS.items():
        sec = ft_section(f[FTH], flag)
        pl = pins_of(f[pins])
        aa = next((int(m.group(1)) for l in sec for m in [re.search(r"constexpr uint8_t kFtAA = (\d+);", l)] if m), -1)
        if not (0 <= aa < len(pl)) or pl[aa][0] != "KORD":
            bad.append(f"{row}: kFtAA is not KORD's package")
            continue
        sc = [(aa if a == "kFtAA" else int(a), int(r), role) for l in sec for a, r, role in SCENE_ROW.findall(l)]
        pair = [(aa, r, "PAIR1") for r in (10, 10, 25, 25, 50, 50)]
        points = []
        for p, _, _ in pl:
            if p not in points:
                points.append(p)
        graded = []
        for p in points:
            idx = [i for i, x in enumerate(pl) if x[0] == p]
            base, opt = (idx[0], idx[1]) if len(idx) == 2 else (idx[0], idx[0])
            for r in (10, 25, 50):
                graded += [(base, r, "GRADED"), (opt, r, "GRADED")]
        want = pair + [(aa, 25, "PLANT")] + graded + [(a, r, "PAIR2") for a, r, _ in pair]
        if sc != want:
            bad.append(f"{row}: {len(sc)} scenes, not the registered {len(want)}")
    return (not bad, "both windows as registered (31 and 49 scenes)" if not bad else "; ".join(bad))


def lines_with(lines, needle):
    return [i for i, l in enumerate(lines) if needle in l]


def ft_laps(f):
    """FT-LAPS: the four laps once each, in FRAME BENCH regions, in Rebuild's order; the µs total around Frame()."""
    m, mr = f[MCH], regions(f[MCH])
    a, ar = f[AMC], regions(f[AMC])
    want = {"load call": "mapplaces::Load(pk, m.places);", "lap load": "m.lap.load = t - lapT;",
            "build call": "if (!in.build || !in.build(pk, k.view", "lap build": "m.lap.build = t - lapT;",
            "names call": "    DrawNames(m, pk, k, in.airports, in.ctx);", "lap names": "m.lap.names = t - lapT;",
            "compose call": "Compose(m.layer, wx, m.composite);", "lap compose": "m.lap.compose = in.clock() - lapC;"}
    at = {k: lines_with(m, v) for k, v in want.items()}
    once = all(len(v) == 1 for v in at.values())
    laps_in = once and all(inside(mr, at[k][0]) for k in at if k.startswith("lap"))
    order = once and at["load call"][0] < at["lap load"][0] < at["build call"][0] < at["lap build"][0] < \
        at["names call"][0] < at["lap names"][0] and 0 < at["lap compose"][0] - at["compose call"][0] <= 6
    t0 = lines_with(a, "const uint32_t ftT0 = micros();")
    fr = lines_with(a, "mapcompose::Frame(mapDraw, &mapPkg, k, wx, mapWxGen, in);")
    tt = lines_with(a, "const uint32_t ftTotal = micros() - ftT0;")
    orb = lines_with(a, "framebench::OnRebuild(rr);")
    pr = lines_with(a, '"[map] draw: rebuilt in %lu ms')
    total = len(t0) == len(fr) == len(tt) == len(orb) == len(pr) == 1 and inside(ar, t0[0]) and inside(ar, tt[0]) and \
        inside(ar, orb[0]) and t0[0] < fr[0] < tt[0] <= fr[0] + 5 and pr[0] < orb[0] <= pr[0] + 40
    return (laps_in and order and total, f"laps once each {once}, in regions {laps_in}, in Rebuild's order {order}; "
                                         f"the µs total around Frame() and the record in the rebuild branch {total}")


def ft_plant(f):
    """FT-PLANT: D = 20,000 µs once; the wait once, gated by the plant flag, between the names and their lap, spun by
    Inputs::spin; the bench's spin counts CPU cycles (ESP.getCycleCount at getCpuFrequencyMhz), never micros(), so the
    plant tests the lap timer's conversion and not only that it fired (the orchestrator's catch, option (a))."""
    m, mr = f[MCH], regions(f[MCH])
    a, ar = f[AMC], regions(f[AMC])
    d = [(i, re.search(r"constexpr uint32_t kBenchPlantUs = (\d+);", l)) for i, l in enumerate(m)]
    d = [(i, x.group(1)) for i, x in d if x]
    waits = lines_with(m, "in.spin(kBenchPlantUs);")
    field = lines_with(m, "void (*spin)(uint32_t us) = nullptr;")
    names, lap = lines_with(m, "    DrawNames(m, pk, k, in.airports, in.ctx);"), lines_with(m, "m.lap.names = t - lapT;")
    take, clear = lines_with(m, "const bool plant = m.benchPlant;"), lines_with(m, "m.benchPlant = false;")
    hook = lines_with(a, "in.spin = [](uint32_t us) {")
    body = a[hook[0] + 1:hook[0] + 5] if len(hook) == 1 else []
    cycles = any("ESP.getCycleCount()" in l for l in body) and any("getCpuFrequencyMhz()" in l for l in body) and \
        not any("micros(" in l or "millis(" in l for l in body)
    place = len(waits) == 1 and len(names) == len(lap) == 1 and names[0] < waits[0] < lap[0]
    ok = len(d) == 1 and d[0][1] == "20000" and inside(mr, d[0][0]) and place and inside(mr, waits[0]) and \
        m[waits[0]].strip().startswith("if (plant && in.spin)") and len(field) == 1 and inside(mr, field[0]) and \
        len(take) == len(clear) == 1 and inside(mr, take[0]) and take[0] < clear[0] and len(hook) == 1 and \
        inside(ar, hook[0]) and cycles
    return (ok, f"D defined {len(d)} time(s) = {d[0][1] if d else '-'} µs (want 20000); the gated spin {len(waits)}, "
                f"between the names and their lap {place}; the hook counts CPU cycles, not micros {cycles}")


def ft_drew(f):
    """FT-DREW: Addition 2 per frame, step C's mapFrameDrawn into Record's stateFlags (bit value 32), in a region."""
    a, ar = f[AMC], regions(f[AMC])
    s = lines_with(a, "mapDrew = mapFrameDrawn ? 32 : 0;")
    e = lines_with(a, "(nightNow ? 16 : 0) | mapDrew);")
    ok = len(s) == len(e) == 1 and inside(ar, s[0]) and inside(ar, e[0])
    return (ok, f"the bit set {len(s)} time(s), in Record's stateFlags {len(e)} time(s), both in regions {ok}")


FLOAT_FMT = re.compile(r"%[-+ #0-9.*]*[fFeEgGaA]")


def ft_tbl(f):
    """FT-TBL: TBL-DEV per FT scene, the one registered line, from the stored home, printed only where SetHome ran."""
    h = f[HEADER]
    s, e = func_span(h, "inline void PrintHomeTable(int scene)")
    body = h[s:e] if s >= 0 else []
    defs = len(lines_with(h, "inline void PrintHomeTable("))
    fmt = [l for l in body if 'Serial.printf("[frame-bench] ft S%d table sum %08lx\\n", scene,' in l]
    sm = [l for l in body if "fbtable::Sum(st.ftLat, st.ftLon, kLevel, (double)kTblT)" in l]
    floats = [l for l in body if FLOAT_FMT.search(l)]
    a, ar = f[AMC], regions(f[AMC])
    calls = lines_with(a, "framebench::PrintHomeTable(")
    home = lines_with(a, "framebench::SetHome(hp.lat0Micro / 1e6, hp.lon0Micro / 1e6);")
    beside = len(calls) == 1 and len(home) == 1 and calls[0] == home[0] + 1 and inside(ar, calls[0]) and \
        a[calls[0]].strip().startswith("framebench::PrintHomeTable(k);") and \
        a[home[0] - 1].rstrip().endswith("== bmpk::Status::kOk) {")
    ok = defs == 1 and len(fmt) == 1 and len(sm) == 1 and not floats and beside
    return (ok, f"defined {defs} (want 1); the registered line {len(fmt)}, the sum at the stored home {len(sm)}, float "
                f"formats {len(floats)} (want 0); called {len(calls)} time(s), beside SetHome in its branch {beside}")


# FB-REC: the record I-5's band was derived from (0d6308cb's FrameBench.h), by its spans' trimmed text.
REC_HEADS = ["struct Rec {", "inline State& S()", "inline portMUX_TYPE& Mux()", "inline uint16_t Ten(uint32_t us)",
             "inline void Record(uint32_t frameUs, size_t tracked, uint8_t stateFlags)", "inline void SelfTime()"]
REC_SHA = "425040a3999d2f46a3f400d8030d98b435340dc6f39a03a0ea37d795359145cb"   # 0d6308cb's, by rec_text()


def rec_text(h):
    """The record's spans, each found once by its head and cut by brace depth, joined; None if any is missing."""
    parts = []
    for head in REC_HEADS:
        if sum(1 for l in h if l.startswith(head)) != 1:
            return None
        s, e = func_span(h, head)
        parts.append("\n".join(l.rstrip() for l in h[s:e]))
    return "\n".join(parts)


def fb_rec(f):
    import hashlib
    t = rec_text(f[HEADER])
    if t is None:
        return (False, "a span of the record is missing or not unique")
    got = hashlib.sha256(t.encode("utf-8")).hexdigest()
    return (got == REC_SHA, f"{len(REC_HEADS)} spans, {len(t)} chars; sha256 {got[:12]} against 0d6308cb's {REC_SHA[:12]}")


def region_of(regs, i):
    return next(((a, b) for a, b in regs if a <= i <= b), None)


def m3_l(f):
    """M3-L: the loop plant immediately before the hand-over (THE PACKAGE)."""
    m, regs = f["src/main.cpp"], regions(f["src/main.cpp"])
    hits = [i for i, l in enumerate(m) if "framebench::DelayOn()" in l]
    ok, where = False, "none"
    if len(hits) == 1:
        r = region_of(regs, hits[0])
        nxt = r[1] + 1 if r else None
        ok = r is not None and nxt < len(m) and m[nxt].strip() == "pushtask::HandOver();"
        where = f":{hits[0] + 1}, region {'yes' if r else 'no'}, next after its END {m[nxt].strip() if r and nxt < len(m) else '-'}"
    return (ok, f"loop plant lines {len(hits)}; {where}")


def m3_d(f):
    """M3-D: the dropped-update plant before any push work in pushtask::Task (THE PACKAGE)."""
    m, regs = f["src/main.cpp"], regions(f["src/main.cpp"])
    ts, te = func_span(m, "void Task(void*)")
    hits = [i for i, l in enumerate(m) if "framebench::DropThis(i)" in l]
    recv = next((i for i in range(ts, te) if "xQueueReceive(g_queue" in m[i]), -1) if ts >= 0 else -1
    sync = next((i for i in range(ts, te) if "esp_cache_msync(" in m[i]), -1) if ts >= 0 else -1
    ok = False
    if len(hits) == 1 and ts <= hits[0] < te and recv < hits[0] < sync:
        r = region_of(regs, hits[0])
        body = [l.strip() for l in m[r[0]:r[1] + 1]] if r else []
        ok = r is not None and "xSemaphoreGive(g_free[i]);" in body and "continue;" in body
    return (ok, f"drop plant lines {len(hits)}; in pushtask::Task after xQueueReceive and before esp_cache_msync, "
                f"releasing the buffer and continuing: {ok}")


def tag_set(f):
    """TAG-SET (S2b, (B)): the map-rebuild tag's set, its take, its one way into the record, and no other 64."""
    h = f[HEADER]
    # (1) the set: once in src/ and include/, inside OnRebuild after its early return
    sets = [(rel, i) for rel in [HEADER, TABLE, AMH, MCH, FTH] + SOURCES
            for i, l in enumerate(f[rel]) if TAG_TRUE in nocomment(l)]
    n_sets = len(sets) + len(f["__tag_other__"])
    os_, oe = func_span(h, "inline void OnRebuild(const RebuildRec& r)")
    ret = next((i for i in range(max(os_, 0), max(oe, 0)) if h[i].strip() == "if (s.scene < 0 || s.done) return;"), -1)
    c1 = n_sets == 1 and len(sets) == 1 and sets[0][0] == HEADER and 0 <= ret < sets[0][1] < oe
    # (2) the take: one definition, exactly this body (reads the tag as 64, then clears it)
    heads = sum(1 for l in h if l.startswith(TAKE_HEAD))
    ts, te = func_span(h, TAKE_HEAD)
    body = [l.strip() for l in h[ts + 1:te] if l.strip()] if ts >= 0 else []
    c2 = heads == 1 and body == TAKE_BODY
    # (3) the one framebench::Record( call passes the take, taken on the line before it, in a region
    a = f[AMC]
    recs = [(rel, i) for rel in SOURCES for i, l in enumerate(f[rel]) if "framebench::Record(" in nocomment(l)]
    takes = [(rel, i) for rel in SOURCES for i, l in enumerate(f[rel]) if "TakeMapRebuilt(" in nocomment(l)]
    c3 = (len(recs) == 1 and recs[0][0] == AMC and nocomment(a[recs[0][1]]).strip() == TAG_CALL and len(takes) == 1
          and takes[0] == (AMC, recs[0][1] - 1) and nocomment(a[recs[0][1] - 1]).strip() == TAG_TAKE
          and inside(regions(a), recs[0][1]))
    # (4) no other 64 flag write in the bench's files: the header whole, the sources' regions (comments stripped)
    w64 = [(HEADER, i) for i, l in enumerate(h) if FLAG64.search(nocomment(l))]
    for rel in SOURCES:
        regs = regions(f[rel])
        w64 += [(rel, i) for i, l in enumerate(f[rel]) if inside(regs, i) and FLAG64.search(nocomment(l))]
    take_line = next((i for i in range(max(ts, 0), max(te, 0)) if h[i].strip() == TAKE_BODY[2]), -1)
    c4 = len(w64) == 1 and w64[0] == (HEADER, take_line)
    ok = c1 and c2 and c3 and c4
    return (ok, f"set {n_sets} time(s), in OnRebuild after its return {c1}; take defined {heads} time(s), body exact {c2}; "
                f"Record calls {len(recs)}, takes {len(takes)}, the call passes the take {c3}; 64 flag writes "
                f"{len(w64)}, only the take's {c4}")


def s2b_blocks(lines):
    """The line indices inside S2b's own preprocessor branches: `#if defined(FRAME_BENCH_S2B)` (or `#ifdef`), an
    `#elif defined(FRAME_BENCH_S2B)`, and the `#else` of `#if !defined(FRAME_BENCH_S2B)` (or `#ifndef`). Nested
    branches inside one of them count as S2b's too."""
    out, stack = set(), []   # each frame: [this branch's sense, the #if's own]; "pos" S2b, "neg" not S2b, None neither
    for i, l in enumerate(lines):
        m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", l)
        if m:
            d, rest = m.group(1), nocomment(m.group(2)).strip()
            if d in ("if", "ifdef", "ifndef"):
                pos = (d == "if" and rest == "defined(FRAME_BENCH_S2B)") or (d == "ifdef" and rest == "FRAME_BENCH_S2B")
                neg = (d == "if" and rest == "!defined(FRAME_BENCH_S2B)") or (d == "ifndef" and rest == "FRAME_BENCH_S2B")
                sense = "pos" if pos else ("neg" if neg else None)
                stack.append([sense, sense])
            elif d == "elif" and stack:
                stack[-1][0] = "pos" if rest == "defined(FRAME_BENCH_S2B)" else None
            elif d == "else" and stack:
                first = stack[-1][1]
                stack[-1][0] = "pos" if first == "neg" else ("neg" if first == "pos" else None)
            elif d == "endif" and stack:
                stack.pop()
            continue
        if any(fr[0] == "pos" for fr in stack):
            out.add(i)
    return out


def s2b_rows(f):
    """S2b's port: RP5's rows FB-R3 .. FB-R11 (b417d80cd4c5), re-pointed at FrameBenchS2b.h and OnCloudPollS2b, and
    S2B-SCOPE. Each (ok, detail)."""
    res = {}
    h, s2, a = f[HEADER], f[S2B_H], f[AMC]
    htext, s2text = "\n".join(h), "\n".join(s2)
    hb, ab = s2b_blocks(h), s2b_blocks(a)
    # FB-R3: nothing recorded or generated is tracked
    lst = f["__tracked__"]
    bad_t = [p for p in (lst or []) if TRACKED_REC.search(p)]
    res["FB-R3"] = (lst is not None and not bad_t,
                    "no tracked file listing to read" if lst is None else f"tracked recording files: {bad_t[:3] or 0}")
    # FB-R4: no print in S2b's own text carries a coordinate
    bad_p = []
    for rel, lines, idx in ((HEADER, h, hb), (AMC, a, ab), (S2B_H, s2, set(range(len(s2))))):
        for i in sorted(idx):
            l = nocomment(lines[i])
            if PRINT.search(l):
                stmt, k = l, i
                while ";" not in stmt and k + 1 < len(lines):
                    k += 1
                    stmt += nocomment(lines[k])
                if COORD.search(stmt):
                    bad_p.append(f"{rel}:{i + 1}")
    res["FB-R4"] = (not bad_p, f"S2b prints carrying a coordinate: {bad_p[:3] or 0}")
    # FB-R5: the airport strings equal Airports.h at 2 dp
    apts = {}
    for l in f[APH]:
        for m in re.finditer(r'\{\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*"([A-Z]{3})"\s*\}', l):
            apts.setdefault(m.group(3), (f"{float(m.group(1)):.2f}", f"{float(m.group(2)):.2f}"))
    arr = {}
    for name in ("kAptCode", "kAptLat", "kAptLon"):
        m = re.search(name + r"\[\]\s*=\s*\{([^}]*)\}", s2text)
        arr[name] = re.findall(r'"([^"]*)"', m.group(1)) if m else []
    mism = [c for c, la, lo in zip(arr["kAptCode"], arr["kAptLat"], arr["kAptLon"]) if apts.get(c) != (la, lo)]
    r5 = len(arr["kAptCode"]) == 9 == len(arr["kAptLat"]) == len(arr["kAptLon"]) and not mism
    res["FB-R5"] = (r5, "9 airports, each equal to Airports.h at 2 dp" if r5 else
                    f"airports {len(arr['kAptCode'])}, mismatched {mism[:3]}")
    # FB-R6: one kRungs[] and no other rung list
    krx = r"static const uint8_t kRungs\[\]\s*=\s*\{[^}]*\}"
    defs = len(re.findall(krx, s2text)) + len(re.findall(krx, htext))
    others = []
    for rel in SOURCES + [HEADER, S2B_H]:
        for i, l in enumerate(f[rel]):
            code = nocomment(l)
            if re.search(r"\{\s*25\s*,\s*50\s*,\s*100\s*\}|\{\s*10\s*,\s*25\s*,\s*50\s*\}", code) and "kRungs[]" not in code:
                others.append(f"{rel}:{i + 1}")
    res["FB-R6"] = (defs == 1 and not others, f"kRungs[] definitions {defs}; other rung lists {others[:3] or 0}")
    # FB-R7: the CRC over the body deserialized, and the served line prints it
    de = htext.count("deserializeJson(doc, rp[pick].body);")
    cr = htext.count("RecCrc32(rp[pick].body, strlen(rp[pick].body))")
    pr = htext.count('"[frame-bench] served A%u %d crc %08lx us %lu\\n", (unsigned)sc.home, rp[pick].seq,')
    res["FB-R7"] = (de == 1 and cr == 1 and pr == 1 and "(unsigned long)crc" in htext,
                    f"deserialize {de}, CRC over the same body {cr}, served print {pr}")
    # FB-R8: the generated header, once, in FrameBenchS2b.h under FRAME_BENCH_S2B; that header included only under
    # `#elif defined(FRAME_BENCH_S2B)`; the pre-build step checks each pin before using the file
    inc = [i for i, l in enumerate(s2) if '#include "framebench_rec_gen.h"' in l]
    s2code = [l.strip() for l in s2 if l.strip() and not l.strip().startswith("//")]
    s2gated = (len(s2code) >= 3 and s2code[0] == "#pragma once" and s2code[1] == "#if defined(FRAME_BENCH_S2B)"
               and s2code[-1].startswith("#endif"))
    hinc = [i for i, l in enumerate(h) if '#include "FrameBenchS2b.h"' in l]
    hgated = len(hinc) == 1 and hinc[0] > 0 and h[hinc[0] - 1].strip() == "#elif defined(FRAME_BENCH_S2B)"
    hgen = sum(1 for l in h if "framebench_rec_gen.h" in nocomment(l))
    sc = "\n".join(f[REC_PY])
    pin, use = sc.find("hashlib.sha256(blob).hexdigest() != want"), sc.find("json.loads(l)")
    res["FB-R8"] = (len(inc) == 1 and s2gated and hgated and hgen == 0 and 0 <= pin < use,
                    f"generated-header includes {len(inc)} (FrameBench.h {hgen}), FrameBenchS2b.h gated {s2gated}, "
                    f"included under its #elif {hgated}; pin check before use {0 <= pin < use}")
    # FB-R9: the replay serves in the settle too: no measuring test in OnCloudPollS2b; its REPLAY gate first
    fs, fe = func_span(h, "inline void OnCloudPollS2b(")
    body = h[fs:fe] if fs >= 0 else []
    meas = [i for i, l in enumerate(body) if "s.measuring" in nocomment(l)]
    gate = next((i for i, l in enumerate(body) if nocomment(l).strip() == "if (sc.kind != REPLAY) return;"), -1)
    deser = next((i for i, l in enumerate(body) if "deserializeJson(doc, rp[pick].body);" in l), -1)
    res["FB-R9"] = (fs >= 0 and not meas and 0 <= gate < deser,
                    f"OnCloudPollS2b found {fs >= 0}; measuring tests in it {len(meas)}; the REPLAY gate before the "
                    f"deserialize {0 <= gate < deser}")
    # FB-R10: the (Z) zoom, once, after a scene's Initialise(), behind ViewRung; ViewRung answers through
    # ShipsThenZooms, which is exactly REPLAY || LIVE_APT (E3)
    zc = [i for x, y in regions(a) for i in range(x, y) if "ApplyZoom(" in a[i]]
    z_ok = (len(zc) == 1 and any(a[j].strip() == "Initialise();" for j in range(max(0, zc[0] - 9), zc[0]))
            and any("framebench::ViewRung(" in a[j] for j in range(max(0, zc[0] - 4), zc[0])))
    vr = next((i for i, l in enumerate(h) if "inline uint8_t ViewRung(int k)" in l), -1)
    st = next((i for i, l in enumerate(h) if l.startswith("inline bool ShipsThenZooms(int k)")), -1)
    st_ok = st >= 0 and "{ return kScenes[k].kind == REPLAY || kScenes[k].kind == LIVE_APT; }" in h[st]
    vr_ok = st_ok and vr >= 0 and any("(ShipsThenZooms(k) && kScenes[k].rungMi < kShippingRadiusMi)" in h[j]
                                      for j in range(vr, min(len(h), vr + 5)))
    res["FB-R10"] = (z_ok and vr_ok, f"bench zoom calls {len(zc)}; after Initialise() behind ViewRung {z_ok}; "
                                     f"ViewRung through ShipsThenZooms = REPLAY || LIVE_APT (E3) {vr_ok}")
    # FB-R11: the customer defaults held to the parses
    ini = next((i for i, l in enumerate(a) if l.startswith("void AircraftManager::Initialise()")), -1)
    ini_end = next((i for i in range(ini + 1, len(a)) if a[i].startswith("void AircraftManager::")), len(a)) \
        if ini >= 0 else -1
    var_key, keep_pairs = {}, []
    for l in (a[ini:ini_end] if ini >= 0 else []):
        m = re.search(r'const String (\w+) = configServer\.GetStoredString\("([a-z0-9-]+)"\);', l)
        if m:
            var_key[m.group(1)] = m.group(2)
        m = re.search(r"if \(!(\w+)\.isEmpty\(\)\) (\w+) = ", l)
        if m and m.group(1) in var_key:
            keep_pairs.append((var_key[m.group(1)], m.group(2)))

    def str_list(name):
        line = next((l for l in s2 if f"static const char* const {name}[] = " in l), "")
        return re.findall(r'"([a-z0-9-]+)"', line.split("=", 1)[1]) if line else []

    keep_keys, own_keys = str_list("kKeepKeys"), str_list("kOwnKeys")
    snap = next((i for i, l in enumerate(a) if "const bool keepNow[framebench::kNumKeep] = {" in l), -1)
    snap_members = re.findall(r"\b(display\w+)\b", " ".join(a[snap:snap + 3])) if snap >= 0 else []
    member_of = dict(keep_pairs)
    ordered = [member_of.get(k) for k in keep_keys]
    so, se = func_span(h, "inline const char* OverrideS2b(")
    falls = sum(nocomment(l).count("return CustomerDefault(key);") for l in h[so:se]) if so >= 0 else 0
    r11 = (sorted(keep_keys) == sorted(k for k, _ in keep_pairs) and len(keep_pairs) == len(set(k for k, _ in keep_pairs))
           and ordered == snap_members and own_keys == ["cloud-url", "cloud-key", "cloud-key-fac", "opensky-id",
                                                        "opensky-secret"] and falls == 2)
    res["FB-R11"] = (r11, f"keep-current parses {len(keep_pairs)} {sorted(k for k, _ in keep_pairs)}; kKeepKeys "
                          f"{keep_keys}; snapshot order matches {ordered == snap_members}; kOwnKeys {own_keys}; "
                          f"fall-throughs {falls}")
    # S2B-SCOPE: every line naming an S2b-only identifier lies in an S2b branch (comments stripped)
    bad_s = []
    for rel in [HEADER] + SOURCES:
        lines = f[rel]
        idx = hb if rel == HEADER else (ab if rel == AMC else s2b_blocks(lines))
        bad_s += [f"{rel}:{i + 1}" for i, l in enumerate(lines)
                  if any(t in nocomment(l) for t in S2B_ONLY) and i not in idx]
    res["S2B-SCOPE"] = (not bad_s, f"S2b-only identifiers outside an S2b branch: {bad_s[:3] or 0}")
    return res


def enclosing_if(lines, i):
    """The #if/#ifdef line that encloses line i, by preprocessor depth (#else and #elif keep it), or ""."""
    depth = 0
    for k in range(i - 1, -1, -1):
        t = lines[k].strip()
        if t.startswith("#endif"):
            depth += 1
        elif t.startswith("#if"):
            if depth == 0:
                return t
            depth -= 1
    return ""


def s2b_builtin(f):
    """S2b's map built in (s2b_rerun_registration.amend3.txt): S2B-ENV, S2B-TAB, B-CAP, B-LOAD, B-TICK, B-NONE and
    B-CENTRE. Each (ok, detail), derived from the code, the pins and the image's own host row CA1, never typed."""
    res = {}
    pins = pins_of(f[S2B_PINS])
    # S2B-ENV
    e = ini_env(f[INI], S2B_ENV)
    txt = "\n".join(e)
    need = ["${cloud.prod}", "-DWX_LAYER_SHIPS", "-DFRAME_BENCH", "-DFRAME_BENCH_S2B", "-DMAP_BENCH",
            "${common.extra_scripts}", "pre:scripts/ft_prebuild.py", f"custom_ft_pins = {S2B_PINS}"]
    miss = [n for n in need if not any(l.strip() == n or l.strip().startswith(n + " ") for l in e)
            and n not in txt.split()]
    forbidden = [x for x in ("WX_LOOK_AB", "BLIPSCOPE_MAP_STEP_C", "FRAME_BENCH_FT") if x in txt]
    i = next((k for k, l in enumerate(e) if l.strip().startswith("board_build.embed_files")), -1)
    emb = [l.strip() for l in e[i + 1:] if l.startswith((" ", "\t"))] if i >= 0 else []
    want = [f".pio/ftpkg/{p}-{v}.bmpk" for p, v, _ in pins]
    emb_ok = emb[:len(want)] == want and (len(emb) == len(want) or not emb[len(want)].startswith(".pio"))
    res["S2B-ENV"] = (bool(e) and not miss and not forbidden and emb_ok and len(pins) == 2,
                      f"env present {bool(e)}; lacks {miss or 0}; forbidden {forbidden or 0}; embeds = pins {emb_ok}; "
                      f"pins {len(pins)}")
    # S2B-TAB: the header's names, sha256 prefixes and embed symbols equal the pins, in order
    h = f[S2B_H]
    nl = next((l for l in h if "kS2bPkgName[kS2bNumPkgs] = {" in l), "")
    sl = next((l for l in h if "kS2bPkgSha12[kS2bNumPkgs] = {" in l), "")
    names = re.findall(r'"([A-Za-z0-9-]+)"', nl.split("=", 1)[1]) if "=" in nl else []
    shas = re.findall(r'"([0-9a-f]{12})"', sl)
    syms = re.findall(r'asm\("_binary__pio_ftpkg_([A-Za-z0-9_]+)_bmpk_start"\)', "\n".join(h))
    want_names = [f"{p}-{v}" for p, v, _ in pins]
    want_syms = [f"{p}_{v}".replace("-", "_") for p, v, _ in pins]
    tab_ok = names == want_names and shas == [s[:12] for _, _, s in pins] and syms == want_syms and len(pins) == 2
    res["S2B-TAB"] = (tab_ok, f"names {names}; sha12 {shas}; start symbols {syms}")
    # B-CAP: each pinned length within the shipping query's cap, read from the image's own host row CA1
    m = f[MFT]
    ca1 = next((k for k, l in enumerate(m) if '{ "CA1", 100, &kMi,' in l), -1)
    capm = re.search(r"(\d+)", m[ca1 + 2]) if 0 <= ca1 < len(m) - 2 else None
    cap = int(capm.group(1)) if capm else -1
    lens = [int(l.split()[3]) for l in f[S2B_PINS] if l.strip() and not l.lstrip().startswith("#")]
    res["B-CAP"] = (cap > 0 and len(lens) == 2 and all(n <= cap for n in lens),
                    f"pinned lengths {lens}; the shipping query's cap {cap} (test_map_fetch CA1)")
    # B-LOAD: S2b's source branch in MapBenchAccept chooses the bytes and nothing else: no install, one refusal
    a = f[AMC]
    s, e2 = func_span(a, "void AircraftManager::MapBenchAccept(int which)")
    body = a[s:e2] if s >= 0 else []
    bs = next((k for k, l in enumerate(body) if l.strip() == "#elif defined(FRAME_BENCH_S2B)"), -1)
    be = next((k for k in range(bs + 1, len(body)) if body[k].strip().startswith("#")), -1) if bs >= 0 else -1
    br = [nocomment(l).strip() for l in body[bs + 1:be]] if bs >= 0 and be > bs else []
    assigns = [l for l in br if re.search(r"\b(mapPkg|mapHeldLen|mapPkgSeq|mapLanding|mapLandingCap)\b", l)]
    rets = [l for l in br if "return" in l]
    srcs = (any("framebench::S2bPkgStart(which)" in l for l in br)
            and any("framebench::S2bPkgLen(which)" in l for l in br))
    res["B-LOAD"] = (bool(br) and srcs and not assigns and rets == ["if (s == nullptr || len == 0) return;"],
                     f"S2b's source branch {len(br)} line(s); sources {srcs}; installs {assigns or 0}; returns {rets}")
    # B-TICK: MapBenchTick returns at once under S2b, as under FT
    ts, te = func_span(a, "void AircraftManager::MapBenchTick()")
    tb = a[ts:te] if ts >= 0 else []
    tick = any(l.strip() == "#if defined(FRAME_BENCH_FT) || defined(FRAME_BENCH_S2B)" and k + 1 < len(tb)
               and tb[k + 1].strip().startswith("return;") for k, l in enumerate(tb))
    res["B-TICK"] = (tick, f"MapBenchTick returns under S2b {tick}")
    # B-NONE: Initialise's home-to-centre region is compiled out under S2b; S2bPkgOf gives -1 to every kind but REPLAY
    # and LIVE-APT; the scene driver sets the package before Initialise, once
    li = next((k for k, l in enumerate(a) if l.strip() == "lat = pk.lat0Micro / 1e6;"), -1)
    guard = enclosing_if(a, li) if li >= 0 else ""
    guard_ok = guard.startswith("#if defined(MAP_BENCH) && !defined(FRAME_BENCH_S2B)")
    po, pe = func_span(h, "inline int S2bPkgOf(int k)")
    pb = [nocomment(l).strip() for l in h[po:pe]] if po >= 0 else []
    of_ok = ("if (sc.kind != REPLAY && sc.kind != LIVE_APT) return -1;" in pb
             and "return sc.home == 0 ? 0 : sc.home == FRAME_BENCH_REC_B ? 1 : -1;" in pb)
    w = [k for k, l in enumerate(a) if nocomment(l).strip() == "mapBench.which = framebench::S2bPkgOf(k);"]
    app_ok = (len(w) == 1 and a[w[0] - 1].strip() == "#if defined(MAP_BENCH)" and a[w[0] + 1].strip() == "#endif"
              and a[w[0] + 2].strip() == "Initialise();")
    res["B-NONE"] = (guard_ok and of_ok and app_ok,
                     f"Initialise's centre region off under S2b {guard_ok}; S2bPkgOf -1 for the rest {of_ok}; "
                     f"set before Initialise once {app_ok}")
    # B-CENTRE: after the gate and before the install, a centre that is not the scene's home refuses the package
    cen = [k for k, l in enumerate(body)
           if "lround(lat * 1e6) != (long)pk.lat0Micro || lround(lon * 1e6) != (long)pk.lon0Micro" in l]
    gi = next((k for k, l in enumerate(body) if "maplayer::Load(mapLanding, len, &pk);" in l), -1)
    ii = next((k for k, l in enumerate(body) if l.strip() == "mapPkg = pk;"), -1)
    cen_ok = (len(cen) == 1 and cen[0] + 2 < len(body) and body[cen[0] + 2].strip() == "return;"
              and 0 <= gi < cen[0] < ii and enclosing_if(body, cen[0]).startswith("#if defined(FRAME_BENCH_S2B)"))
    res["B-CENTRE"] = (cen_ok, f"the centre test {len(cen)} time(s), refusing, between the gate and the install, "
                               f"under S2b: {cen_ok}")
    return res


def report(f):
    res = rows(f)
    for k, (ok, d) in res.items():
        print(f"  {'ok  ' if ok else 'FAIL'} {k}: {d}")
    off = [k for k, (ok, _) in res.items() if not ok]
    print(f"{len(res)} rows, {len(off)} failed")
    return 0 if not off else 1


def selftest(tree):
    base = load(tree)
    if report(base) != 0:
        print("selftest FAILED: the clean tree is not green")
        return 1
    rc = 0

    def plant(name, fn, want):
        f = {k: (list(v) if isinstance(v, list) else v) for k, v in base.items()}
        fn(f)
        red = {k for k, (ok, _) in rows(f).items() if not ok}
        ok = red == want
        print(f"selftest {'ok' if ok else 'FAIL'}: {name} reads exactly {sorted(want)} red (got {sorted(red)})")
        return 0 if ok else 1

    def sub(rel, old, new, n=1):
        def fn(f):
            t = "\n".join(f[rel])
            assert t.count(old) >= 1, (rel, old[:60])
            f[rel] = t.replace(old, new, n).split("\n")
        return fn

    rc |= plant("FB1, a bench call outside a region", sub("src/main.cpp", "void loop()", "void loop() { framebench::S(); }\nvoid loopX()"), {"FB1"})
    rc |= plant("FB2, a region without its #ifdef", sub("src/ConfigurationWebServer.cpp", BEGIN + "\n#ifdef FRAME_BENCH\n#include", BEGIN + "\n#include"), {"FB2"})
    rc |= plant("FB3, Preferences in the header", lambda f: f[HEADER].insert(len(f[HEADER]) - 3, "Preferences p;"), {"FB3"})
    rc |= plant("FB5, an Arduino include in the table", lambda f: f[TABLE].insert(5, "#include <Arduino.h>"), {"FB5"})
    rc |= plant("M3-1, a present at the loop's hand-off (v19's anchor)",
                sub("src/main.cpp", "  appManager.RecordFrameUs(micros() - frameStartUs);\n",
                    BEGIN + "\n#ifdef FRAME_BENCH\n  framebench::Present(micros());\n#endif\n" + END +
                    "\n  appManager.RecordFrameUs(micros() - frameStartUs);\n"), {"M3-1"})
    rc |= plant("M3-3P, the push plant as a spin", sub("src/main.cpp", "vTaskDelay(pdMS_TO_TICKS(d));", "delayMicroseconds(d * 1000);"), {"M3-3P"})
    rc |= plant("C1-A, the (a) hook removed", sub("src/AircraftManager.cpp", "framebench::OnCloudPoll(", "(void)(", 1), {"C1-A"})
    rc |= plant("C1-W, `claimed` left unset", sub("src/AircraftManager.cpp", "        claimed = len;\n", ""), {"C1-W"})
    rc |= plant("C1-M, map off removed", sub("src/AircraftManager.cpp", "    if (framebench::MapOff()) return false;", "    (void)0;"), {"C1-M"})
    rc |= plant("C1-E, the scan's skip removed", sub("src/AircraftManager.cpp", "if (framebench::IsSceneHex(icao)) continue;", "(void)0;"), {"C1-E"})
    rc |= plant("C1-K, the board's own not noted", sub("src/ConfigurationWebServer.cpp", "framebench::NoteKey(key, value.c_str(), false);", "(void)0;"), {"C1-K"})
    rc |= plant("MRK, the marker dropped", sub(MARKS, ' "[frame-bench]"', ""), {"MRK"})
    rc |= plant("C1-X, the exemption's FRAME_BENCH define reintroduced",
                sub(AMH, "// THE MAP FETCH END\n// THE MAP DRAW BEGIN",
                    "// THE MAP FETCH END\n" + BEGIN + "\n#ifdef FRAME_BENCH\n#define BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT 1\n"
                    "#endif\n" + END + "\n// THE MAP DRAW BEGIN"), {"C1-X"})
    rc |= plant("C1-X, the exemption's term back on the refusal's #if",
                sub(AMH, "!defined(BLIPSCOPE_MAP_STEP_C)\n#error",
                    "!defined(BLIPSCOPE_MAP_STEP_C) && !defined(BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT)\n#error"), {"C1-X"})
    rc |= plant("C1-X, step C's header include removed",
                sub(AMH, '#include "MapSweep.h"', '// (step C\'s include, planted away)'), {"C1-X"})
    rc |= plant("FB-TU, the variant include removed (the plants' cause)",
                sub(HEADER, '#include "variants/Variant.h"', "// (the variant include, planted away)"), {"FB-TU"})
    old_site = ("    // THE FRAME BENCH BEGIN\n#ifdef FRAME_BENCH\n"
                "    if (framebench::DelayOn()) delayMicroseconds(framebench::PLANTED_DELAY_US);   // I-6's +10 ms, BEFORE "
                "the hand-over\n#endif\n    // THE FRAME BENCH END\n    pushtask::HandOver();\n")
    back = ("    pushtask::HandOver();\n    // THE FRAME BENCH BEGIN\n#ifdef FRAME_BENCH\n"
            "    if (framebench::DelayOn()) delayMicroseconds(framebench::PLANTED_DELAY_US);   // the OLD site\n"
            "#endif\n    // THE FRAME BENCH END\n")
    rc |= plant("M3-L, the loop plant back after the hand-over (the old site)", sub("src/main.cpp", old_site, back), {"M3-L"})

    def drop_after_push(f):
        t = "\n".join(f["src/main.cpp"])
        a = t.index("        // THE FRAME BENCH BEGIN\n#ifdef FRAME_BENCH\n        if (framebench::DropThis(i))")
        b = t.index("        // THE FRAME BENCH END\n", a) + len("        // THE FRAME BENCH END\n")
        region, t = t[a:b], t[:a] + t[b:]
        anchor = "        const uint32_t t1 = micros();\n"
        assert t.count(anchor) == 1
        f["src/main.cpp"] = t.replace(anchor, anchor + region).split("\n")
    rc |= plant("M3-D, the drop region moved after the push's first work", drop_after_push, {"M3-D"})
    rc |= plant("FB-REC, one extra statement in Record()",
                sub(HEADER, "    s.delayed = false;\n}", "    s.delayed = false;\n    s.overflowFrames += 0;\n}"), {"FB-REC"})
    rc |= plant("FT-LAPS, the Compose lap removed", sub(MCH, "m.lap.compose = in.clock() - lapC; ", ""), {"FT-LAPS"})
    rc |= plant("FT-LAPS, the µs total zeroed", sub(AMC, "const uint32_t ftTotal = micros() - ftT0;",
                                                    "const uint32_t ftTotal = 0;"), {"FT-LAPS"})

    def swap_laps(f):
        t = "\n".join(f[MCH])
        a, b = "m.lap.load = t - lapT; lapT = t; }   // lap 1", "m.lap.build = t - lapT; lapT = t; }   // lap 2"
        assert t.count(a) == 1 and t.count(b) == 1
        t = t.replace(a, "@@A@@").replace(b, "m.lap.load = t - lapT; lapT = t; }   // lap 2").replace(
            "@@A@@", "m.lap.build = t - lapT; lapT = t; }   // lap 1")
        assert t != "\n".join(f[MCH])
        f[MCH] = t.split("\n")
    rc |= plant("FT-LAPS, the Load and Build laps swapped", swap_laps, {"FT-LAPS"})
    rc |= plant("FT-PLANT, D at 5,000", sub(MCH, "kBenchPlantUs = 20000;", "kBenchPlantUs = 5000;"), {"FT-PLANT"})
    rc |= plant("FT-PLANT, the plant's gate removed", sub(MCH, "if (plant && in.spin) in.spin(kBenchPlantUs);",
                                                          "if (in.spin) in.spin(kBenchPlantUs);"), {"FT-PLANT"})
    rc |= plant("FT-PLANT, the spin on micros() (the timer it checks)",
                sub(AMC, "const uint32_t c0 = ESP.getCycleCount();\n        while ((uint32_t)(ESP.getCycleCount() - c0) < cycles) {}",
                    "const uint32_t c0 = micros();\n        while ((uint32_t)(micros() - c0) < us) {}"), {"FT-PLANT"})
    rc |= plant("FT-DREW, the bit dropped from stateFlags", sub(AMC, "(nightNow ? 16 : 0) | mapDrew);",
                                                                "(nightNow ? 16 : 0));"), {"FT-DREW"})
    rc |= plant("FT-SWAP, MapBenchAccept's ++mapPkgSeq dropped",
                sub(AMC, "    mapHeldLen = len;\n    ++mapPkgSeq;\n}\n", "    mapHeldLen = len;\n}\n"), {"FT-SWAP"})
    rc |= plant("FT-ENV, WX_LOOK_AB in the ftsea env", sub(INI, "    -DFRAME_BENCH_FT_SEA\n",
                                                           "    -DFRAME_BENCH_FT_SEA\n    -DWX_LOOK_AB\n"), {"FT-ENV"})
    rc |= plant("FT-ENV, an embed line dropped", sub(INI, "    .pio/ftpkg/KBOS-sea.bmpk\n", ""), {"FT-ENV"})
    rc |= plant("FT-TAB, a table sha prefix altered", sub(FTH, '"ee8c834bc81a"', '"ee8c834bc81b"'), {"FT-TAB"})
    rc |= plant("FT-SCENES, a pair-2 rung changed", sub(FTH, "{FT_K, kFtAA, 50, PAIR2}, {FT_K, kFtAA, 50, PAIR2},",
                                                        "{FT_K, kFtAA, 25, PAIR2}, {FT_K, kFtAA, 50, PAIR2},"), {"FT-SCENES"})
    rc |= plant("FB-REC, a changed line in its callee Ten()",
                sub(HEADER, "return t >= 0xFFFF ? 0xFFFF : (uint16_t)t;", "return t > 0xFFFF ? 0xFFFF : (uint16_t)t;"), {"FB-REC"})
    tbl_call = "                    framebench::PrintHomeTable(k);   // TBL-DEV at this scene's home: a CRC, never a coordinate\n"
    rc |= plant("FT-TBL, the per-scene call removed", sub(AMC, tbl_call, ""), {"FT-TBL"})
    rc |= plant("FT-TBL, the sum around Bend's strings, not the scene's stored home",
                sub(HEADER, "fbtable::Sum(st.ftLat, st.ftLon, kLevel, (double)kTblT)",
                    "fbtable::Sum(atof(kHomeLat), atof(kHomeLon), kLevel, (double)kTblT)"), {"FT-TBL"})
    rc |= plant("FT-TBL, the call outside the validated branch (a stale home's sum)",
                sub(AMC, "hp.lon0Micro / 1e6);\n" + tbl_call + "                }\n",
                    "hp.lon0Micro / 1e6);\n                }\n                framebench::PrintHomeTable(k);\n"), {"FT-TBL"})
    rc |= plant("FT-TBL, a coordinate printed beside the sum",
                sub(HEADER, "    const State& st = S();\n    Serial.printf(\"[frame-bench] ft S%d table sum",
                    "    const State& st = S();\n    Serial.printf(\"[frame-bench] ft home %.2f\\n\", st.ftLat);\n"
                    "    Serial.printf(\"[frame-bench] ft S%d table sum"), {"FT-TBL"})
    rc |= plant("TAG-SET P-TAG-EVERY, the take returns 64 whatever mapRebuilt says",
                sub(HEADER, "const uint8_t v = s.mapRebuilt ? 64 : 0;", "const uint8_t v = 64;"), {"TAG-SET"})
    rc |= plant("TAG-SET P-TAG-NOCLEAR, the take's clear removed", sub(HEADER, "    s.mapRebuilt = false;\n", ""),
                {"TAG-SET"})
    rc |= plant("TAG-SET P-TAG-NOPASS, the Record call passes stateFlags alone",
                sub(AMC, "(uint8_t)(stateFlags | mapTag));", "stateFlags);"), {"TAG-SET"})

    # S2b's port: RP5's plants (b417d80cd4c5), re-pointed, and S2B-SCOPE's. Each reads exactly its own row red.
    def tracked_plus(path):
        def fn(f):
            f["__tracked__"] = (f["__tracked__"] or []) + [path]
        return fn

    def tracked_again(path):
        def fn(f):
            f["__tracked__"] = sorted(set((f["__tracked__"] or []) + [path]))
        return fn

    rc |= plant("FB-R3, a recording tracked", tracked_plus("bench/framebench_rec_SFO.fbrec"), {"FB-R3"})
    rc |= plant("FB-R3, the generated header tracked", tracked_plus("include/framebench_rec_gen.h"), {"FB-R3"})
    rc |= plant("FB-R3 CONTROL, the generator script tracked", tracked_again(REC_PY), set())
    rc |= plant("FB-R4, a coordinate printed in S2b's replay serve",
                sub(HEADER, "    (void)lonSent;\n", "    (void)lonSent;\n    Serial.printf(\"%s\", latSent);\n"), {"FB-R4"})
    rc |= plant("FB-R5, one airport digit changed", sub(S2B_H, '"33.64"', '"33.65"'), {"FB-R5"})
    rc |= plant("FB-R6, a second rung list",
                sub(AMC, "        const uint32_t nowMs = millis();\n",
                    "        const uint32_t nowMs = millis();\n        static const uint8_t r[] = {25, 50, 100};\n"), {"FB-R6"})
    rc |= plant("FB-R7, the CRC over another body",
                sub(HEADER, "RecCrc32(rp[pick].body, strlen(rp[pick].body))", "RecCrc32(rp[0].body, strlen(rp[0].body))"),
                {"FB-R7"})
    rc |= plant("FB-R8, the pinned-sha check removed", sub(REC_PY, "hashlib.sha256(blob).hexdigest() != want", "False"),
                {"FB-R8"})
    rc |= plant("FB-R9, a measuring gate in the replay serve",
                sub(HEADER, "    if (sc.kind != REPLAY) return;\n", "    if (sc.kind != REPLAY || !s.measuring) return;\n"),
                {"FB-R9"})
    rc |= plant("FB-R10, ViewRung without ShipsThenZooms",
                sub(HEADER, "(ShipsThenZooms(k) && kScenes[k].rungMi < kShippingRadiusMi)",
                    "(kScenes[k].rungMi < kShippingRadiusMi)"), {"FB-R10"})
    rc |= plant("FB-R10, ShipsThenZooms admitting FIXED",
                sub(HEADER, "{ return kScenes[k].kind == REPLAY || kScenes[k].kind == LIVE_APT; }",
                    "{ return kScenes[k].kind == REPLAY || kScenes[k].kind == LIVE_APT || kScenes[k].kind == FIXED; }"),
                {"FB-R10"})
    rc |= plant("FB-R10, a second zoom call in the driver",
                sub(AMC, '                    ApplyZoom(i, radarzoom::Edge::None, "bench");\n',
                    '                    ApplyZoom(i, radarzoom::Edge::None, "bench");\n'
                    '                    ApplyZoom(0, radarzoom::Edge::None, "bench");\n'), {"FB-R10"})
    rc |= plant("FB-R11, a KEEP-CURRENT key dropped from kKeepKeys",
                sub(S2B_H, '{"infotext", "triangle", "airports", ', '{"infotext", "triangle", '), {"FB-R11"})
    rc |= plant("FB-R11, a drawing key added to kOwnKeys", sub(S2B_H, '"opensky-secret"};', '"opensky-secret", "trail"};'),
                {"FB-R11"})
    rc |= plant("S2B-SCOPE, an S2b call moved outside its branch",
                sub(AMC, "#if defined(FRAME_BENCH_S2B)\n                    framebench::SelfTimeOverride();",
                    "                    framebench::SelfTimeOverride();\n#if defined(FRAME_BENCH_S2B)"), {"S2B-SCOPE"})
    # S2b's map built in (amend3): each row's plant reads exactly that row red
    rc |= plant("S2B-ENV, MAP_BENCH dropped from S2b's env",
                sub(INI, "    -DFRAME_BENCH_REC_B=${sysenv.FRAME_BENCH_REC_B}\n    -DMAP_BENCH\n",
                    "    -DFRAME_BENCH_REC_B=${sysenv.FRAME_BENCH_REC_B}\n"), {"S2B-ENV"})
    rc |= plant("S2B-TAB, a sha256 prefix that is not the pin's",
                sub(S2B_H, 'kS2bPkgSha12[kS2bNumPkgs] = {"d', 'kS2bPkgSha12[kS2bNumPkgs] = {"0'), {"S2B-TAB"})
    rc |= plant("B-CAP, a pinned length one byte over the cap",
                lambda f: f.__setitem__(S2B_PINS, [(" ".join(l.split()[:3] + ["211409"]) if l.startswith("LAX ") else l)
                                                   for l in f[S2B_PINS]]), {"B-CAP"})
    rc |= plant("B-LOAD, an install inside S2b's source branch",
                sub(AMC, "    if (s == nullptr || len == 0) return;   // no package for this scene (B-NONE): no map, as registered\n",
                    "    if (s == nullptr || len == 0) return;   // no package for this scene (B-NONE): no map, as registered\n"
                    "    mapHeldLen = len;\n"), {"B-LOAD"})
    rc |= plant("B-TICK, MapBenchTick's phases left on under S2b",
                sub(AMC, "#if defined(FRAME_BENCH_FT) || defined(FRAME_BENCH_S2B)\n    return;",
                    "#if defined(FRAME_BENCH_FT)\n    return;"), {"B-TICK"})
    rc |= plant("B-NONE, Initialise's centre region compiled into S2b",
                sub(AMC, "#if defined(MAP_BENCH) && !defined(FRAME_BENCH_S2B)   // S2b keeps",
                    "#ifdef MAP_BENCH   // S2b keeps"), {"B-NONE"})
    rc |= plant("B-NONE, LIVE-OWN given a package",
                sub(S2B_H, "if (sc.kind != REPLAY && sc.kind != LIVE_APT) return -1;",
                    "if (sc.kind != REPLAY && sc.kind != LIVE_APT && sc.kind != LIVE_OWN) return -1;"), {"B-NONE"})
    rc |= plant("B-CENTRE, the off-centre package installed anyway",
                sub(AMC, "        return;\n    }\n#endif\n#endif\n    // THE FRAME BENCH END\n    mapPkg = pk;",
                    "    }\n#endif\n#endif\n    // THE FRAME BENCH END\n    mapPkg = pk;"), {"B-CENTRE"})
    def drop_s2b_map(f):   # position-free: the MARKERS line, wherever "[s2b-map]" sits on it
        idx = [i for i, l in enumerate(f[MARKS]) if l.startswith("MARKERS=(")]
        assert len(idx) == 1, "the MRK plant needs exactly one MARKERS line"
        before = f[MARKS][idx[0]]
        f[MARKS][idx[0]] = before.replace(' "[s2b-map]"', "")
        assert f[MARKS][idx[0]] != before, "the MRK plant did not apply"
    rc |= plant("MRK, [s2b-map] dropped from MARKERS", drop_s2b_map, {"MRK"})
    rc |= plant("S2B-SCOPE, an S2b package call outside its branch",
                sub(AMC, "        auto applyScene = [&](int k) {\n#if defined(FRAME_BENCH_FT)",
                    "        auto applyScene = [&](int k) {\n            (void)framebench::S2bPkgLen(0);\n"
                    "#if defined(FRAME_BENCH_FT)"), {"S2B-SCOPE"})
    print("selftest PASSED" if rc == 0 else "selftest FAILED")
    return rc


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        sys.exit(selftest(sys.argv[2]))
    sys.exit(report(load(sys.argv[1])))
