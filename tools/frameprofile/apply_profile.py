"""Applies THE FRAME PROFILE's regions (docs/frame-profile.md, P1 / A0a; section 0 governs) to a tree that already
carries THE FRAME BENCH's C1 regions, by counted anchors, keeping each file's own line endings. include/FrameProfile.h
(and, later, include/FrameProfileScenes.h) are separate files. Refuses on any anchor that does not match exactly once,
and writes nothing until every anchor has matched. Every region is `#ifdef FRAME_PROFILE`, so without the flag the
preprocessed tokens are unchanged (PLACED, section 0 (b)).
    python -I apply_profile.py <tree>
Exit 0 applied; 1 refused (nothing written); 2 usage."""
import os
import sys

if len(sys.argv) != 2:
    print(__doc__)
    sys.exit(2)
tree = sys.argv[1]
BEGIN, END = "// THE FRAME PROFILE BEGIN", "// THE FRAME PROFILE END"


def region(indent, body):
    return "\n".join([indent + BEGIN, "#ifdef FRAME_PROFILE"] + body + ["#endif", indent + END]) + "\n"


def ini_region(body):
    return "\n".join(["; THE FRAME PROFILE BEGIN"] + body + ["; THE FRAME PROFILE END"]) + "\n"


FB_INCLUDE = '#include "FrameBench.h"   // the frame-time bench, C1 (docs/frame-time-bench.md)\n#endif\n// THE FRAME BENCH END\n'
INC = region("", ['#include "FrameProfile.h"   // the frame profile, P1 (docs/frame-profile.md)'])
GATE = "                if (nowMs - fb.phaseStartMs >= framebench::SettleMs(fb.scene) && fb.dumpScene < 0) {\n"
INFO = ("    const int n = LabelLayout(tracked, x, y, idx, lx, ly, lw);\n"
        "    for (int k = 0; k < n; ++k)\n"
        "        backbuffer.drawString(AIRCRAFT_INFO_FIELDS[idx[k]].format(tracked), lx[k], ly[k]);\n")
BOX = ("    const int n = LabelLayout(tracked, x, y, idx, lx, ly, lw);\n"
       "    if (n == 0) return false;\n")

# (anchor, where, text): "after" / "before" insert text beside the anchor; "replace" puts text in the anchor's place.
EDITS = {
    "src/main.cpp": [
        (FB_INCLUDE, "after", INC),
        ("  const uint32_t frameStartUs = micros();\n", "after",
         region("  ", ["  frameprofile::Start(frameStartUs);"])),
        ("  LGFX_Sprite& drawBuf = pushtask::TakeForDrawing();\n", "before",
         region("  ", ["  frameprofile::Mark(frameprofile::B_UPD);"])),
        ("  LGFX_Sprite& drawBuf = pushtask::TakeForDrawing();\n", "after",
         region("  ", ["  frameprofile::Mark(frameprofile::B_WAIT);"])),
        ("    framebuf::SetPhase(framebuf::kPhaseCleared);   // diag only (A9.22)\n", "after",
         region("    ", ["    frameprofile::Mark(frameprofile::B_UNDER);"])),
        ("    appManager.Draw(canvas, firstPass);\n", "before",
         region("    ", ["    frameprofile::Mark(frameprofile::B_SWEEP);"])),
        ("    appManager.Draw(canvas, firstPass);\n", "after",
         region("    ", ["    frameprofile::Mark(frameprofile::B_DRAW);"])),
        ("    pushtask::HandOver();\n", "after",
         region("    ", ["    frameprofile::Mark(frameprofile::B_HAND);"])),
        ("  board::DisplayFlush(tft);\n", "after",
         region("  ", ["  frameprofile::Mark(frameprofile::B_FLUSH);"])),
    ],
    "src/AircraftManager.cpp": [
        (FB_INCLUDE, "after", INC),
        ("        framebench::Record(frameUs, trackedAircraft.size(), stateFlags);\n", "after",
         region("        ", ["        frameprofile::Commit(frameUs);   // the phases, beside the bench's record"])),
        ("                    framebench::SelfTime();\n", "after",
         region("                    ", ["                    if (frameprofile::Allocate()) frameprofile::SelfTime();   // PC-1"])),
        ("                framebench::EndMeasure();\n", "after",
         region("                ", ["                frameprofile::EndMeasure(fb.scene);"])),
        ("        framebench::PumpDump();\n", "after",
         region("        ", ["        frameprofile::PumpDump();"])),
        # The next measure waits for the profile's dump as well as the bench's, so no dump line lands in a span.
        (GATE, "replace",
         "                // THE FRAME PROFILE BEGIN\n#ifdef FRAME_PROFILE\n"
         + GATE.replace("fb.dumpScene < 0) {", "fb.dumpScene < 0 && frameprofile::DumpIdle()) {")
         + "#else\n" + GATE + "#endif\n                // THE FRAME PROFILE END\n"),
        ("void AircraftManager::DrawRadar(BandCanvas& backbuffer, bool firstPass)\n{\n", "after",
         region("    ", ["    frameprofile::Mark(frameprofile::B_RADAR);   // static: from here to the contact loop"])),
        ("    for (auto& [icao, tracked] : trackedAircraft) {\n        if (tracked.state.onGround) continue;\n\n"
         "        // The scene is rendered once per band", "before",
         region("    ", ["    frameprofile::LoopBegin();   // the contact loop: contacts, outline, labels"])),
        ("    // THE FRAME IS FINISHED BEING WRITTEN ON -- harvest it.", "before",
         region("    ", ["    frameprofile::LoopEnd();"])),
        ("            const bool outline = WxOutlineNow();\n", "after",
         region("            ", ["            const uint32_t fpOutline0 = frameprofile::Cyc();"])),
        ("            if (displayTriangles) DrawAircraftTriangle(backbuffer, x, y, tracked, markerColor);\n", "before",
         region("            ", ["            frameprofile::AddCyc(frameprofile::A_OUTLINE, fpOutline0);"])),
        (INFO, "replace",
         region("    ", ["    const uint32_t fpLayout0 = frameprofile::Cyc();"])
         + INFO.split("\n")[0] + "\n"
         + region("    ", ["    frameprofile::AddCyc(frameprofile::A_LAYOUT, fpLayout0);",
                         "    const uint32_t fpDraw0 = frameprofile::Cyc();",
                         "    frameprofile::PlantLabels();   // P-CTRL: +5.0 ms inside lbl_draw, on its scene only"])
         + "\n".join(INFO.split("\n")[1:3]) + "\n"
         + region("    ", ["    frameprofile::AddCyc(frameprofile::A_DRAW, fpDraw0);"])),
        (BOX, "replace",
         region("    ", ["    const uint32_t fpBox0 = frameprofile::Cyc();"])
         + BOX.split("\n")[0] + "\n"
         + region("    ", ["    frameprofile::AddCyc(frameprofile::A_LAYOUT, fpBox0);"])
         + BOX.split("\n")[1] + "\n"),
        ("    // The B1 fix: Rasterise records each row's span, and Blend visits only that (docs/wx-device.md).\n",
         "before", region("    ", ["    frameprofile::Mark(frameprofile::B_SWR);"])),
        ("    // The sprite stores LovyanGFX's swap565_t: logical RGB565 byte-swapped (colortype.hpp), so swap in and out.\n",
         "before", region("    ", ["    frameprofile::Mark(frameprofile::B_SWB);"])),
        ("    DrawRadarSweepBeam(canvas, cx, cy, radius, angle);   // the beam and its head, as today\n", "before",
         region("    ", ["    frameprofile::Mark(frameprofile::B_SWM);"])),
        ("    DrawRadarSweepBeam(canvas, cx, cy, radius, angle);   // the beam and its head, as today\n", "after",
         region("    ", ["    frameprofile::Mark(frameprofile::B_SWE);"])),
    ],
    "platformio.ini": [
        ("    -DFRAME_BENCH\n; THE FRAME BENCH END\n", "after", ini_region([
            "; docs/frame-profile.md section 0: P1 / A0a's per-phase profile. C1's image B set (the push task, the weather",
            "; layer compiled in as v20 ships it, FRAME_BENCH) plus FRAME_PROFILE. Bench-only and slug-less, never published.",
            "[env:blipscope-s3-128-frameprofile]",
            "extends = env:blipscope-s3-128",
            "build_flags =",
            "    ${s3128_hw.build_flags}",
            "    -DBLIPSCOPE_EXPECT_BIG_NVS",
            "    ${cloud.prod}",
            "    -DWX_LAYER_SHIPS",
            "    -DFRAME_BENCH",
            "    -DFRAME_PROFILE"])),
    ],
    "scripts/check-no-bench-hooks.sh": [
        ('"[m3-bench]" "[frame-bench]" )\n', "replace", '"[m3-bench]" "[frame-bench]" "[frame-profile]" )\n'),
    ],
}

out = {}
for rel, edits in EDITS.items():
    path = os.path.join(tree, rel)
    raw = open(path, "rb").read()
    crlf = b"\r\n" in raw
    s = raw.decode("utf-8").replace("\r\n", "\n")
    if "THE FRAME PROFILE BEGIN" in s:
        sys.exit(f"REFUSED: {rel} already carries THE FRAME PROFILE")
    # Count every anchor against the ORIGINAL text first, so an edit cannot make a later anchor match.
    for anchor, where, text in edits:
        c = s.count(anchor)
        if c != 1:
            sys.exit(f"REFUSED: {rel}: an anchor matches {c} times: {anchor[:70]!r}")
    for anchor, where, text in edits:
        if where == "after":
            s = s.replace(anchor, anchor + text, 1)
        elif where == "before":
            s = s.replace(anchor, text + anchor, 1)
        else:
            s = s.replace(anchor, text, 1)
    out[path] = s.replace("\n", "\r\n").encode("utf-8") if crlf else s.encode("utf-8")
for path, b in out.items():
    open(path, "wb").write(b)
    print(f"applied: {os.path.relpath(path, tree)}")
sys.exit(0)
