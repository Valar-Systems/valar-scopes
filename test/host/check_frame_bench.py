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
  C1-X  the step-C exemption: BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT defined exactly once, inside a FRAME_BENCH region of
        src/AircraftManager.h, and named exactly once more, on the step-C refusal's #if
  C1-L  a level and a weather answer per scene (docs/frame-time-bench.md, "C1-L"): Scene's two default members; the
        FRAME_PROFILE include, once, wrapping C1's five-line block; the (a) hook's loop on Level(); IsSceneHex on
        MaxLevel(); Override's wx-layer line on the scene's wx; Allocate's level refusal; the driver's two scene lines
        on the scene's level and weather; and kLevel in code only at its definition, Scene's default and Level()
    python -I check_frame_bench.py <tree>             exit 0 every row ok; 1 a row off
    python -I check_frame_bench.py --selftest <tree>  the clean tree green; each plant reads exactly its row red"""
import os
import re
import sys

BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"
SOURCES = ["src/main.cpp", "src/AircraftManager.cpp", "src/ConfigurationWebServer.cpp"]
HEADER, TABLE, MARKS = "include/FrameBench.h", "include/FrameBenchTable.h", "scripts/check-no-bench-hooks.sh"
AMH, EXEMPT = "src/AircraftManager.h", "BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT"
NVS = re.compile(r"Preferences|nvs_|SPIFFS|LittleFS|prefs\.(put|remove|clear)")


def load(tree):
    files = {}
    for rel in SOURCES + [HEADER, TABLE, MARKS, AMH]:
        files[rel] = open(os.path.join(tree, rel), "rb").read().decode("utf-8").replace("\r\n", "\n").split("\n")
    return files


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
    mk = any(l.startswith("MARKERS=(") and '"[frame-bench]"' in l for l in f[MARKS])
    res["MRK"] = (mk, f"[frame-bench] in MARKERS {mk}")
    h, hregs = f[AMH], regions(f[AMH])
    defs = [i for i, l in enumerate(h) if re.match(r"\s*#\s*define\s+" + EXEMPT + r"\b", l)]
    uses = [i for i, l in enumerate(h) if EXEMPT in l and i not in defs and not l.strip().startswith("//")]
    on_if = [i for i in uses if h[i].startswith("#if defined(BLIPSCOPE_MAP_FETCH)") and i + 1 < len(h)
             and h[i + 1].startswith('#error "step B draws the map without step C')]
    elsewhere = sum(1 for rel in SOURCES + [HEADER, TABLE] for l in f[rel] if EXEMPT in l)
    x_ok = len(defs) == 1 and inside(hregs, defs[0]) and len(uses) == 1 and len(on_if) == 1 and elsewhere == 0
    res["C1-X"] = (x_ok, f"exemption defines {len(defs)} (in a region {len(defs) == 1 and inside(hregs, defs[0])}); "
                         f"uses {len(uses)} (on the refusal's #if {len(on_if)}); in other files {elsewhere}")

    H = f[HEADER]
    code = [l.split("//")[0].rstrip() for l in H]
    ss, se = func_span(H, "struct Scene {")
    members = [l.strip() for l in code[ss:se]] if ss >= 0 else []
    l_members = "uint8_t level = kLevel;" in members and "bool wx = true;" in members
    inc = [i for i, l in enumerate(code) if l.strip() == '#include "FrameProfileScenes.h"']
    wrap = (len(inc) == 1 and code[inc[0] - 1].strip() == "#if defined(FRAME_PROFILE)"
            and [l.strip() for l in code[inc[0] + 1:inc[0] + 3]] == ["#else", "#if defined(BLIPSCOPE_PUSH_TASK)"]
            and [l.strip() for l in code[inc[0] + 6:inc[0] + 8]] == ["#endif", "#endif"])
    os_, oe = func_span(H, "inline void OnCloudPoll(")
    poll = code[os_:oe] if os_ >= 0 else []
    l_poll = ("const int n = Level();" in [l.strip() for l in poll] and "for (int i = 0; i < n; ++i) {" in
              [l.strip() for l in poll] and not any(re.search(r"\bkLevel\b", l) for l in poll))
    hs, he = func_span(H, "inline bool IsSceneHex(")
    hexb = " ".join(code[hs:he]) if hs >= 0 else ""
    l_hex = "< MaxLevel();" in hexb and not re.search(r"(?<!Max)Level\(\)", hexb)
    l_wx = any(l.strip() == 'if (strcmp(key, "wx-layer") == 0) return kScenes[S().scene].wx ? "true" : "false";'
               for l in code)
    al, ae = func_span(H, "inline bool Allocate()")
    alb = [l.strip() for l in code[al:ae]] if al >= 0 else []
    ref = next((i for i, l in enumerate(alb) if l == "if (!LevelsOk()) {"), -1)
    buf = next((i for i, l in enumerate(alb) if l == "if (s.buf) return true;"), -1)
    l_alloc = 0 <= ref < buf and "return false;" in alb[ref:buf]
    scl = [i for i, l in enumerate(a) if '"[frame-bench] scene S' in l and "level=%d wx=%s rung=%dmi settle" in l]
    lv = sum(1 for i in scl for j in range(i, i + 4) if re.search(r"\(int\)framebench::kScenes\[(0|next)\]\.level", a[j]))
    l_drv = len(scl) == 2 and lv == 2
    kl = [i for i, l in enumerate(code) if re.search(r"\bkLevel\b", l)]
    kl_ok = (len(kl) == 3 and code[kl[0]].strip().startswith("constexpr int kLevel = 40;")
             and code[kl[1]].strip() == "uint8_t level = kLevel;" and code[kl[2]].strip().startswith("inline int Level()"))
    kl_src = sum(1 for rel in SOURCES for l in f[rel] if re.search(r"\bkLevel\b", l.split("//")[0]))
    parts = {"members": l_members, "profile include": wrap, "hook on Level()": l_poll, "hex on MaxLevel()": l_hex,
             "wx answer": l_wx, "level refusal": l_alloc, "scene lines": l_drv, "kLevel only at its 3": kl_ok and kl_src == 0}
    res["C1-L"] = (all(parts.values()), "off: " + (", ".join(k for k, v in parts.items() if not v) or "none"))
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
        f = {k: list(v) for k, v in base.items()}
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
    rc |= plant("C1-X, the exemption defined outside its region",
                sub(AMH, "#endif\n" + END + "\n// THE MAP DRAW BEGIN", "#endif\n" + END +
                    "\n#define BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT 1\n// THE MAP DRAW BEGIN"), {"C1-X"})
    rc |= plant("C1-L, the served loop on kLevel", sub(HEADER, "for (int i = 0; i < n; ++i) {", "for (int i = 0; i < kLevel; ++i) {"), {"C1-L"})
    rc |= plant("C1-L, IsSceneHex on the current level", sub(HEADER, "< MaxLevel();", "< Level();"), {"C1-L"})
    rc |= plant("C1-L, the wx-layer answer fixed at true", sub(HEADER, 'return kScenes[S().scene].wx ? "true" : "false";', 'return "true";'), {"C1-L"})
    rc |= plant("C1-L, Allocate's level refusal removed", sub(HEADER, "if (!LevelsOk()) {", "if (false) {"), {"C1-L"})

    def unwrap(f):
        t = "\n".join(f[HEADER])
        old_a = '#if defined(FRAME_PROFILE)\n#include "FrameProfileScenes.h"   // p4\'s'
        assert t.count(old_a) == 1
        a0 = t.index(old_a)
        a1 = t.index("#if defined(BLIPSCOPE_PUSH_TASK)", a0)
        t = t[:a0] + t[a1:]
        assert t.count("#endif\n#endif\nconstexpr int kNumScenes") == 1
        f[HEADER] = t.replace("#endif\n#endif\nconstexpr int kNumScenes", "#endif\nconstexpr int kNumScenes").split("\n")
    rc |= plant("C1-L, the FRAME_PROFILE include removed", unwrap, {"C1-L"})
    rc |= plant("C1-L, a scene line printing kLevel", sub("src/AircraftManager.cpp", "(int)framebench::kScenes[0].level", "framebench::kLevel"), {"C1-L"})
    print("selftest PASSED" if rc == 0 else "selftest FAILED")
    return rc


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        sys.exit(selftest(sys.argv[2]))
    sys.exit(report(load(sys.argv[1])))
