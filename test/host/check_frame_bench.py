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
    mk = any(l.startswith("MARKERS=(") and '"[frame-bench]"' in l for l in f[MARKS])
    res["MRK"] = (mk, f"[frame-bench] in MARKERS {mk}")
    h = f[AMH]
    step_c = sum(1 for l in h if l.strip().startswith('#include "MapSweep.h"'))
    named = sum(1 for rel in [AMH] + SOURCES + [HEADER, TABLE] for l in f[rel] if EXEMPT in l)
    res["C1-X"] = (step_c == 1 and named == 0, f"step C's MapSweep.h include {step_c} (want 1); the exemption named "
                                               f"{named} time(s) in the bench's files (want 0)")
    res["FB-TU"] = fb_tu(f)
    res["M3-L"], res["M3-D"] = m3_l(f), m3_d(f)
    res["FB-REC"] = fb_rec(f)
    return res


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
    rc |= plant("FB-REC, a changed line in its callee Ten()",
                sub(HEADER, "return t >= 0xFFFF ? 0xFFFF : (uint16_t)t;", "return t > 0xFFFF ? 0xFFFF : (uint16_t)t;"), {"FB-REC"})
    print("selftest PASSED" if rc == 0 else "selftest FAILED")
    return rc


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        sys.exit(selftest(sys.argv[2]))
    sys.exit(report(load(sys.argv[1])))
