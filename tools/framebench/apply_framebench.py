"""Applies THE FRAME BENCH's hook regions to a tree (the v19 tag's, or a v20 candidate's), by counted anchors, keeping
each file's own line endings. include/FrameBench.h, scripts/framebench_host.py and test/host/check_frame_bench.py are
separate files. Refuses on any anchor that does not match exactly once; writes nothing until every anchor has matched.
    python -I apply_framebench.py <tree>"""
import os
import sys

tree = sys.argv[1]
BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"


def region(indent, body):
    lines = [indent + BEGIN, "#ifdef FRAME_BENCH"] + body + ["#endif", indent + END]
    return "\n".join(lines) + "\n"


EDITS = {
    "src/main.cpp": [
        ("#include <esp_task_wdt.h>\n", "after",
         region("", ['#include "FrameBench.h"   // the frame-time bench (docs/frame-time-bench.md)'])),
        ("  appManager.RecordFrameUs(micros() - frameStartUs);\n", "before",
         region("  ", ["  // present to present, then CTRL+10ms's planted delay inside the frame span (I-6)",
                       "  framebench::Present(micros());",
                       "  if (framebench::DelayOn()) delayMicroseconds(framebench::PLANTED_DELAY_US);"])),
    ],
    "src/AircraftManager.cpp": [
        ('#include "AircraftManager.h"\n', "after",
         region("", ['#include "FrameBench.h"   // the frame-time bench (docs/frame-time-bench.md)'])),
        ("void AircraftManager::RecordFrameUs(uint32_t frameUs)\n{\n", "after",
         region("    ", [
             "    {",
             "        // The frame bench's recorder and scene driver, on the loop task (docs/frame-time-bench.md).",
             "        framebench::State& fb = framebench::S();",
             "        static bool announced = false;",
             "        if (!announced) {",
             "            announced = true;",
             '            Serial.printf("[frame-bench] the frame bench: %d scenes, settle %lu s, measure %lu s\\n",',
             "                          framebench::kNumScenes, (unsigned long)(framebench::SETTLE_MS / 1000),",
             "                          (unsigned long)(framebench::MEASURE_MS / 1000));",
             "        }",
             "        framebench::Record(frameUs, trackedAircraft.size());",
             "        const uint32_t nowMs = millis();",
             "        if (!fb.done) {",
             "            if (fb.scene < 0) {",
             "                if (nowMs >= framebench::START_AFTER_MS && framebench::Allocate()) {",
             "                    framebench::SelfTime();",
             "                    fb.scene = 0;",
             "                    framebench::PrepareScene(0);",
             "                    fb.measuring = false;",
             "                    fb.phaseStartMs = nowMs;",
             '                    Serial.printf("[frame-bench] scene S0 %s home=%s level=%u rung=%umi settle\\n",',
             "                                  framebench::kKindName[framebench::kScenes[0].kind],",
             "                                  framebench::kHomeName[framebench::kScenes[0].home],",
             "                                  (unsigned)framebench::kScenes[0].level, (unsigned)framebench::kScenes[0].rung);",
             "                    Initialise();",
             "                }",
             "            } else if (!fb.measuring) {",
             "                if (nowMs - fb.phaseStartMs >= framebench::SETTLE_MS && fb.dumpScene < 0) {",
             "                    fb.measuring = true;",
             "                    fb.phaseStartMs = nowMs;",
             "                    fb.n = 0;",
             "                    fb.overflowFrames = 0;",
             '                    Serial.printf("[frame-bench] scene S%d measure\\n", fb.scene);',
             "                }",
             "            } else if (nowMs - fb.phaseStartMs >= framebench::MeasureMs(fb.scene)) {",
             "                framebench::EndMeasure();",
             "                const int next = fb.scene + 1;",
             "                if (next < framebench::kNumScenes) {",
             "                    fb.scene = next;",
             "                    framebench::PrepareScene(next);",
             "                    fb.phaseStartMs = nowMs;",
             '                    Serial.printf("[frame-bench] scene S%d %s home=%s level=%u rung=%umi settle\\n", next,',
             "                                  framebench::kKindName[framebench::kScenes[next].kind],",
             "                                  framebench::kHomeName[framebench::kScenes[next].home],",
             "                                  (unsigned)framebench::kScenes[next].level, (unsigned)framebench::kScenes[next].rung);",
             "                } else {",
             "                    fb.done = true;",
             '                    Serial.println("[frame-bench] run done; the board\'s own settings again");',
             "                }",
             "                Initialise();",
             "            }",
             "        }",
             "        framebench::PumpDump();",
             "    }"])),
        ("        // predicate, so nothing undrawn can be tapped.\n        if (ZoomCulled(x, y)) continue;\n", "after",
         region("        ", ["        if (firstPass) framebench::S().drawn++;   // DRAWN (A2): this frame's drawn aircraft"])),
    ],
    "src/ConfigurationWebServer.cpp": [
        ('#include "ConfigurationWebServer.h"\n', "after",
         region("", ['#include "FrameBench.h"   // the frame-time bench (docs/frame-time-bench.md)'])),
        ("const String ConfigurationWebServer::GetStoredString(const char* key)\n{\n", "after",
         region("    ", ["    // The scene's settings, held in RAM: read here instead of NVS, never written anywhere.",
                         "    if (const char* v = framebench::Override(key)) return String(v);"])),
    ],
}

INI_ENV = """
; THE FRAME BENCH BEGIN
; docs/frame-time-bench.md: bench-only and slug-less, never published. The bench host's address comes from the
; UNTRACKED env var FRAME_BENCH_HOST through scripts/framebench_host.py; it is never in a tracked file or printed.
[env:blipscope-s3-128-framebench]
extends = env:blipscope-s3-128
build_flags =
    ${s3128_hw.build_flags}
    -DBLIPSCOPE_EXPECT_BIG_NVS
    ${cloud.prod}
    -DFRAME_BENCH
extra_scripts =
    ${common.extra_scripts}
    pre:scripts/framebench_host.py
; THE FRAME BENCH END
"""

MARKER_OLD = '"[wx-key]" )'
MARKER_NEW = '"[wx-key]" "[frame-bench]" )'

out = {}
for rel, edits in EDITS.items():
    p = os.path.join(tree, rel)
    raw = open(p, "rb").read()
    crlf = b"\r\n" in raw
    s = raw.decode("utf-8").replace("\r\n", "\n")
    for anchor, where, text in edits:
        c = s.count(anchor)
        if c != 1:
            sys.exit(f"REFUSED: {rel}: an anchor matches {c} times: {anchor[:70]!r}")
        s = s.replace(anchor, anchor + text if where == "after" else text + anchor)
    out[p] = (s.replace("\n", "\r\n") if crlf else s).encode("utf-8")

p = os.path.join(tree, "platformio.ini")
raw = open(p, "rb").read()
crlf = b"\r\n" in raw
s = raw.decode("utf-8").replace("\r\n", "\n")
if "[env:blipscope-s3-128-framebench]" in s:
    sys.exit("REFUSED: platformio.ini already holds the framebench env")
s = s.rstrip("\n") + "\n" + INI_ENV
out[p] = (s.replace("\n", "\r\n") if crlf else s).encode("utf-8")

p = os.path.join(tree, "scripts/check-no-bench-hooks.sh")
s = open(p, "rb").read().decode("utf-8")
if s.count(MARKER_OLD) != 1:
    sys.exit(f"REFUSED: check-no-bench-hooks.sh's MARKERS line matched {s.count(MARKER_OLD)} times")
out[p] = s.replace(MARKER_OLD, MARKER_NEW).encode("utf-8")

for p, b in out.items():
    open(p, "wb").write(b)
print(f"applied: {sum(len(e) for e in EDITS.values())} regions in {len(EDITS)} sources, the ini env, the MARKERS entry")
