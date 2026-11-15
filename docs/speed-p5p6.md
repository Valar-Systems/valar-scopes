# Speed P5 (W, the weather cut) and P6 (L1, the label-layout cache): registration, the non-numeric parts

**Status:** a registration. No code. Written 2026-11-13 by Weatherscope, which carries P5 and P6 under the owner's
staffing 2 (docs/speed-plan.md 2b on `p4/big-screens-plan`).
- Frozen here: every row, control, plant and the flag design.
- **The gain predictions are NOT here.** They wait for A0a (docs/frame-profile.md's P1 at 20, 40 and 60, with the
  `outline`, `lbl_layout`/`lbl_draw` and `sw_raster`/`sw_blend`/`sw_beam` splits). They are frozen in an addendum to
  this file before any code is written.
- Full rigour (the owner, 2026-11-13 01:17Z): both are firmware, and both run on E2:08.

## 0. Common to both

**The tree.** map-client's `bench/frame-bench-c1` at **`bfcb47e01c3d767b770ee4c26d18676e7c590e33`**, frozen
(map-client, after 03:48Z: no further push to that branch). C1's acknowledged images were built from it (c1-A
`5eb7c4ad525d`, c1-B `1132863bd0df`, tree `efe82e49c8b6`). Its parent is p4's M3 tree `3bc7ec26fe28`, not main.
- It is M3 plus the step-C bench exemption, map off. Weather compiles there under `${cloud.prod}` + `WX_LAYER_SHIPS`;
  main and the v19 bench refuse it.
- Everything here is **measured WITHOUT step C**, as C1 is.
- P5's and P6's bench branches start from that sha, and every reading names it.

**Three constraints of that tree, each a row** (map-client):
- **The kind tables.** `kKindName` and `kPushDelayMs` (`include/FrameBench.h:43-44`, 4 entries each) are looked up by
  `Kind`, and `PushDelayMs()` indexes the delays by the scene's kind. On image B a missing entry becomes a vTaskDelay
  in the push task.
  - Every new kind gets an entry in BOTH, with 0 delay.
  - A `static_assert` makes both arrays' length equal the kind count, so a kind without an entry fails the build.
  - `DelayOn()` adds the loop's +10 ms only for `C1_LOOP10`; no new kind touches it.
- **`kScenes` is compiled twice:** image B's list with `BLIPSCOPE_PUSH_TASK`, and image A's (`{C1}`) without. P5's
  and P6's kinds go into image B's list, the shipping shape with the push task. Image A is not used.
- **The step-C exemption** (`BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT`, a FRAME_BENCH region in `src/AircraftManager.h`) is
  on that branch, and CI refuses it on every PR since #691 (main `b7e7fdd99884`).
  - So a bench branch is never a PR. It is pushed as a record only.
  - **The winning path lands in its own change from main,** without the exemption and without the old path. I-3 is
    read on that change.

**The switch, inside one image (the orchestrator's ruling, after 03:44Z).**
- Each bench image gains scene kinds in `include/FrameBench.h`'s `kScenes`: the same scene with the OLD path and with
  the NEW path, alternated in one session (OLD, NEW, OLD, NEW).
- Being within one session, this meets the A/B/A rule (the orchestrator, 00:11:47Z). The OLD-to-OLD spread is the
  noise the delta is read against.
- The OLD path lives ONLY inside `// THE FRAME BENCH` regions under `#ifdef FRAME_BENCH`. The shipping code is the NEW
  path alone.
- If either step ever needs two images, it registers A/B/A with E = max(N, U), as M3-3 does.

**The identity checks** (map-client's frame-time bench, I-2 to I-4, applied as they are):
- **I-2:** the bench image is the tree plus the instrument. Every `FRAME_BENCH` line lies inside a region, and the
  regions hold no NVS write and print no coordinate or name.
- **I-3:** the regions add nothing to the shipping image. `blipscope-s3-128` built from the step's branch equals the
  same branch's `blipscope-s3-128` with every region's text removed: 0 bytes outside the five regions, by the image
  comparator.
- **I-4:** the step's own boot marker (`[speed-p5]`, `[speed-p6]`) joins `check-no-bench-hooks.sh`'s MARKERS beside
  `[frame-bench]`.
  - The scan refuses the bench image (the positive control) and passes the shipping image.

**The frame-time margin (the owner's standing rule; frozen now):** on the step's targeted phase, NEW is not slower than
OLD by more than max(N, 0.3 ms), where N is the run's own OLD-to-OLD spread. Beyond it, the step stops. Every other
phase moves by at most max(N, 0.5 ms). The gain itself is predicted from A0a.

**The screen indicator stays out of every cache (the orchestrator's ruling, after 21:41Z):**
- map-client's frame-trust test reads `DrawScreenIndicator`'s dots (`SCREEN_SIZE - 16`, y = 224) as proof that a
  `/diag/fb` grab is finished.
- **Row (both steps):** no cached layer (a weather base, a static layer, an outline mask, a label cache) ever holds the
  indicator's pixels. It is asserted on the cache itself: at the dots' positions, the clear colour or the layer's own,
  never a dot's colour.
- **Plant:** the indicator baked into the cache. It must read red.
- If a change ever needs the indicator inside a cache, map-client registers a new last-drawn witness first.

**Bench windows, E2:08:** P5 about 60 min, P6 about 45 min, each requested when its images are frozen.
- Never inside the held windows (11-14 00:00-01:30Z, 11-15 00:00-03:00Z).
- They run no-hands, under the owner's 04:29Z terms: the write ranges against the partition table, both images by
  sha256, E2:08 restored and back on Wi-Fi with its settings, v19 first.

**The escape hatch:** any reading outside its band stops the step. It is reported, and nothing is fixed before the
ruling.

## 1. P5, W: the weather cut

The order, from the record (docs/wx-device.md; told to p4 at 20:39Z): the outline, then the blend, then the clear.
- **The clear:** +2.1 ms. A memcpy of 5.08 ms against the 3.01 ms fill.
- **The sweep:** +4.6 ms.
- **The outline:** 0.1625 ms per contact, so about 6.5 ms at 40.

### 1a. The outline: one dilated edge per contact instead of 8 marker rasters

**What changes.**
- **Today** (`AircraftManager.cpp`, the `if (outline)` loop): the marker is drawn 8 times in black at the 1 px
  offsets, then once in colour.
- **The cut:**
  - take the marker's pixel set M once, on a small scratch, by the same LovyanGFX call;
  - dilate it by the 8-neighbourhood;
  - write black on (D ∖ M), clipped exactly as LovyanGFX clips: the sprite's bounds, and the band offset if banding
    returns;
  - then draw the marker as today.
- **Per contact, immediately before that contact's own marker, in the same loop.** That is today's order, so a later
  contact's edge still covers an earlier contact's marker where 8 copies do.

**The host rows,** the cloud reviewer's words, forwarded by the owner at 20:41Z: "The host comparison must include two
scenes where a faster outline is most likely to differ from 8 copies:
- planes whose markers and outlines overlap, since outlines drawn in a separate pass before the markers change pixels
  there;
- markers clipped at the disc's edge, at several headings.

Pass: byte-identical to the 8-copy image in both, at 40 and 60 aircraft."

**The scenes, from the disc-edge read (main `4b00c97b3dbd`).**
- Nothing in the radar's path sets a clip rectangle. LovyanGFX clips at the sprite's 0..239.
- `ZoomCulled` (`AircraftManager.cpp:5450`) drops a whole contact by its centre, only when zoomed in.
- **H-OVERLAP:** pairs and triples of contacts whose markers and edges overlap, in both draw orders.
- **H-EDGE:** markers centred within 4 px of the tangent points (0,119), (238,119), (119,0), (119,238), and just past
  them (partly off the sprite), at headings 0, 45, 90, … 315, for every kind (the darts by weight class, the rotor
  cross, the balloon ring, the dot).
- **H-CULL (zoomed in):** a contact centred just inside the cull circle with its body reaching past it, and one just
  outside (no pixels from either path).
- **All three** at 40 and 60 aircraft. Byte-identical to the 8-copy image, compared whole.

**The host raster.**
- LovyanGFX's `fillTriangle`, `fillCircle`, `drawLine` and `drawCircle`, ported line for line from the pinned
  `.pio/libdeps` copy, as `test_wx_outline.cpp` already ports `fillCircle`.
- The marker's own vertex arithmetic is read from `DrawAircraftTriangle`.

**The plants** (each must read red, exactly on the row named):

| plant | red on |
|---|---|
| P5-ONEPASS: all edges in one pass before all markers (the reviewer's) | H-OVERLAP |
| P5-NOCLIP: the mask's writes clipped one pixel short of LovyanGFX | H-EDGE |
| P5-DILATE4: a 4-neighbour dilation instead of 8 | H-OVERLAP, H-EDGE (each with a dart) |
| P5-CULLSKIP: the edge drawn for a culled contact | H-CULL |

**The device anchor** (the orchestrator's row): the host port must equal the device, not only the cut.
- A fixed bench scene with **today's 8-copy outline**, overlapping and edge-clipped markers included. The sweep and the
  weather layer are off in that scene, bench-only, so the frame holds still.
- It is read whole by `/diag/fb` three times, each Torn false, with the late-drawn elements (the indicator) present.
- The host port, given the same positions, headings and kinds, must reproduce those pixels exactly within 2 px of every
  marker.
- A port that differs cannot stand in for the device, and the step stops.

**The switch:** scene kinds `OUTLINE_8COPY` and `OUTLINE_MASK`. Once the mask ships, the 8-copy loop exists only
inside a FRAME_BENCH region.

**Frame-time row:**
- the `outline` phase (A0a's split), 8COPY against MASK, alternated at 40, under the margin above;
- the gain predicted from A0a's outline slope (the addendum).

### 1b. The sweep blend: exact, cheaper

- **The cut:** the blend's per-pixel work (about 0.7 µs a pixel, from the record) done as table lookups or fixed
  point. The output must be byte-identical.
- **Host row:** W5a's comparison (span-only `Blend` against `BlendRows`, at all 3,600 angles, both looks, over a frame
  of varied pixels and codes), the new blend against the old one, byte for byte.
- **Plant:** P5-ROUND, one rounding step changed. W5a must read red.
- **Frame-time row:** `sw_blend` (A0a's split), OLD against NEW, alternated at 40, on a dry layer (the record's rule:
  the blend's work depends on the codes beneath it).

### 1c. The dirty-region clear (after M3)

- **The cut:** restore only last frame's dirty regions from the layer, not the whole 115 KB.
  - **Per backbuffer:** with M3's second buffer, each buffer keeps its own set from the frame before it.
- **The indicator's band is in EVERY frame's restore set.**
- **Host row:** a sequence of frames on two alternating buffers, the restored canvas byte-identical to a full clear.
- **Plants:**

  | plant | red on |
  |---|---|
  | P5-INDSKIP: a restore that leaves the indicator's band out | a frame stopped before `DrawScreenIndicator` still shows dots |
  | P5-ONEBUF: one dirty set shared by both buffers | the host sequence |

- **Frame-time row:** `under` (A0a's), OLD against NEW, alternated at 40.

## 2. P6, L1: the label-layout cache

**What changes.**
- **Today,** per contact per frame:
  - `AircraftLabelBox` (`:4247`, first pass) and `DrawAircraftInfo` (`:5662`) each run `LabelLayout`;
  - `LabelLayout` formats every enabled field (Arduino `String` allocations) and calls `textWidth`;
  - `DrawAircraftInfo` formats the chosen fields once more for `drawString`.
- **The cut:**
  - per contact, the chosen fields' strings and widths are cached, and kept until any value they were formatted from
    changes;
  - the placement (`ChordWidthPx`, the clamp, the row's y) is recomputed every frame from the cached widths, because
    it depends on the position;
  - `drawString` renders as today;
  - the box and the draw share the one cache.

**The equality row, inside one image (the bench).**
- Inside a FRAME_BENCH region, each contact's label is laid out by BOTH paths on the same frame: the cache, and today's
  `LabelLayout`. The bench counts the mismatches in index, string, x, y or width.
- **Predicted: 0 mismatches over the whole scene,** with at least one change of value per contact in the scene (the
  fixed traffic's altitudes and speeds change). That change is the control that the cache is ever invalidated.
- **The same row on the host,** over a recorded field sequence.

**The plants:**

| plant | red on |
|---|---|
| P6-NEVER: the cache never invalidated | mismatches above 0 |
| P6-WIDTH: a stale width kept across a text change | mismatches above 0 |
| P6-POS: the placement cached with the strings | mismatches above 0 as contacts move |

**The indicator row:** the label cache holds strings and widths, never pixels, so it cannot hold the indicator. That is
stated, and asserted by the cache's type.

**The switch:** scene kinds `LABELS_TODAY` and `LABELS_CACHE`. Once the cache ships, today's path exists only inside a
FRAME_BENCH region.

**Frame-time row:**
- `lbl_layout` (A0a's split), TODAY against CACHE, alternated at 40, under the margin;
- `lbl_draw` must not move beyond max(N, 0.5 ms), because the render is untouched;
- the gain predicted from A0a's `lbl_layout` (the addendum).

## 3. What waits for A0a, and nothing else

The addendum, before any code, carries:
- each frame-time row's predicted gain, from A0a's `outline`, `sw_blend`, `under` and `lbl_layout` at 20, 40 and 60;
- each row's band;
- the bench scenes' settle and measure, as C1's.

If A0a moves a premise here (for instance, the outline isn't the largest weather share at 40), the order is re-ruled
before anything is built.

## 4. Amendment 1: the pin moves (ruled 2026-11-14, before any reading)

**What was found.** map-client found that the C1-lineage bench's planted delays (a push-task delay of 10 or 60 ms,
and the loop's +10 ms) never fired: a header-defined macro is included in a different order in `main.cpp` than in
the driver. P5's and P6's OLD paths live in FRAME_BENCH regions of that same tree.

**The ruling** (the orchestrator, 2026-11-14, after 22:18:15Z): "before any P5/P6 reading, REBASE onto map-client's
fixed tree once its include-order fix lands, and keep your same-order check as a row that stops the run if it fails."

Nothing has been read at the old pin, so this amendment comes before any reading.

**What changes:**
- Section 0's tree (`bfcb47e01c3d`) is no longer the base for any reading. P5's and P6's bench branches start from
  map-client's fixed tree. Its sha is written here before any reading, and every reading names it.
- Section 0's three constraints (the kind tables, `kScenes` compiled twice, the step-C exemption) are re-read on the
  fixed tree. Any that moved is re-stated here before the addendum.

**New row S0, same order (a stop row).** Run on each bench image before its first frame-time reading.
- **The check reads the built image, not the source:** every FRAME_BENCH-gated piece a P5/P6 row depends on (the OLD
  path, the kind tables, image B's scene list) is compiled into the translation unit that runs it, under the same
  bench macros the driver sees.
- **Its control:** a plant, the include order reversed in a copy, must read S0 red.
- Pass: the run continues. Fail: the run stops and is reported, and nothing is fixed before a ruling. The check
  cannot tell, or the control does not go red: "instrument", stop.

### 4a. Amendment 1a: the gains wait for an A0a read on the fixed tree (ruled 2026-11-14, before any reading)

**The ruling** (the orchestrator, 2026-11-14, after 22:21:08Z): wait for an A0a read on the fixed tree, not one at
`bfcb47e01c3d`. That sha also carries C1's first defect. Its (a) hook aliased every row to one, so the board tracked
ONE aircraft while being fed 40. A frame-time read through it measured a 1-aircraft scene, unless its reader checked
drawn == N on every frame.

**What changes:**
- Section 3's addendum takes its gains only from an A0a read on the fixed tree named in section 4. An A0a read at
  `bfcb47e01c3d` is not used.
- **New row S1, drawn == N (a stop row).** On every frame of every frame-time reading, the count of contacts actually
  drawn equals the scene's N (20, 40 or 60).
  - One frame off: the reading stops and is reported.
  - If the fixed tree's bench prints no drawn count per frame, S1 is re-stated here before the addendum, so it is
    never skipped.
  - **Its control:** a plant that aliases the rows to one must read S1 red.

### 4b. Which fixed tree (agreed 2026-11-14, before any reading)

**The ruling** (the orchestrator, 2026-11-14, after 22:23:19Z): P5/P6's base is the same fully fixed levels tree that
A0a moves onto, with both the hex fix and the include-order fix. Then the predicted gains and the readings come from
one tree, and S0 and S1 match A0a's stop rows.

- **The tree: map-client's `bench/frame-bench-levels` at `a0583e88b1424a03bee03fcacf2fb29bef75e25e`** (written
  2026-11-15, before any reading). It is C1-L (`40f02c0fa812`) plus three commits:
  - `a9894185b2a8`, the hex fix (the same fix as c1's `a599fcaaffc6`);
  - `d813ea42be08`, the include-order fix: `FrameBench.h` includes `variants/Variant.h` itself (c1's `68cae820a44e`);
  - `a0583e88b142`, check_frame_bench's FB-TU row (c1's `bcc4414ca870`).
- **It is A0a's tree.** The source is p4's RE-PIN in `docs/frame-profile.md` section 0, on `p4/big-screens-plan` at
  `1e34762aee25`: "A0a's tree is map-client's `bench/frame-bench-levels` at `a0583e88b142...`". The profile is
  `bench/frame-profile-p1-final` at `1d9e91740e1e`. The orchestrator named this sha as P5/P6's base on 2026-11-15
  after 19:48Z.
- **Checked by me, read-only, 19:49:14Z (date -u):**
  - ls-remote has `-levels` at `a0583e88b142` and `-p1-final` at `1d9e91740e1e`;
  - `a0583e88b142` is an ancestor of `1d9e91740e1e`, with two commits between (`ed0430d50e6c`, `1d9e91740e1e`);
  - `1e34762aee25` is on `p4/big-screens-plan`.
  - The stale `bench/frame-profile-p1-levels` (`6979d2d7ff5b`, on the old C1-L) is not A0a's pin.
- **Not on this tree:** c1's later `0d6308cb77b6` ("THE PACKAGE"). It moves the loop plant before HandOver, adds
  M3-2's per-scene hand-over count and the owner's dropped-update plant. It is C1's proof instrument, nobody has said
  it is coming to levels, and it is not part of this base.
- **What this sets, under sections 4 and 4a:**
  - Every P5/P6 bench branch starts from `a0583e88b142`, and every reading names it.
  - Section 0's three constraints (the kind tables, `kScenes` compiled twice, the step-C exemption) are re-read on
    this tree before the addendum. Any that moved is re-stated here.
  - The addendum's gains come only from an A0a read on this tree.
  - S0 stands as registered. FB-TU is a source check, so it does not replace S0's read of the built image.
  - S1 stands as registered in 4a. This tree carries no per-scene count of its own (THE PACKAGE's is not on it). So
    p4 is asked whether A0a's read on `1d9e91740e1e` prints a drawn count per frame. If no sha with one is named, S1
    is re-stated here before the addendum. **(Wrong; corrected in 4c.)**
- **No P5/P6 image is built** until this section is pushed and the orchestrator has read it.

### 4c. Section 0 re-read on `a0583e88b142`, and a correction to 4b (2026-11-15, before any reading)

Read from the tree's source with `git show`, 19:53:58Z (date -u). Nothing was built. Both heads were unchanged at the
end of the read.

**The correction.** 4b's S1 bullet says this tree carries no per-scene count of its own. I wrote that before reading
the tree, and it mixed up two counts. THE PACKAGE adds a per-SCENE count (M3-2's hand-over count). This tree already
carries a per-FRAME drawn count:
- `framebench::S().drawn++` at `src/AircraftManager.cpp:4339`, inside a FRAME_BENCH region. It runs once per contact
  on the first pass, after `ZoomCulled`; the default view culls nothing.
- `Record()` stores it as `Rec.drawn`, byte 5 of each 8-byte record (`fr10` bytes 0-1, `p2p10` 2-3, `tracked` 4,
  `drawn` 5, `flags` 6, `pres` 7). It is clamped at 255, and N is at most 60.
- The `[frame-bench] dump S<k> <pos> <hex>` lines print each record's 8 bytes. The scene line prints the scene's
  `level`, which is its N.
- The profile at `1d9e91740e1e` keeps the same counter, at `src/AircraftManager.cpp:4400`.

So S1 reads `Rec.drawn` against N on every dumped frame of every reading, as 4a registered it, and the bench already
prints the count. The question to p4 narrows to one thing: does A0a's reader also check drawn == N on every frame, so
that the two stop rows match?
- S1 counts contacts that reach the draw step after the cull, not pixels.
- A row-aliasing plant (40 fed, 1 tracked) reads drawn = 1, not N, so S1's control still goes red.
- **The clamp cannot manufacture a pass** (the orchestrator's point after reading 4c, written here before any reading;
  the addendum's S1 row cites this bullet). `Record()` clamps the count: `r.drawn = s.drawn > 255 ? 255 : s.drawn`
  (`FrameBench.h:297`). A clamp could fake drawn == N only if N were 255, and it never is here:
  - every scene's level, its N, is 1..`kTableMax` = 60;
  - `LevelsOk()` (`FrameBench.h:158-163`) refuses any scene outside that, and its caller (line 442) then starts no
    scene.
  - So a runaway count reads 255 ≠ N, which is red.
  - `s.drawn` itself is a `uint16_t` that is zeroed every frame (lines 109 and 304). A wrap back onto N would take
    65,536 draws in one frame.
- **p4's answer (2026-11-15 21:4xZ): S1 matches a row of A0a's, registered but not yet run.**
  - A0a's reader is not written yet, so nothing has run it.
  - Its registered row is `docs/frame-profile.md:32-33` on `p4/big-screens-plan`, added in the RE-PIN `1e34762aee25`
    and unchanged at `882918177ae6`: "When the reader is written, it grades drawn == the scene's level on every
    measured frame, as a STOP row with its plant." It grades drawn, not tracked.
  - I checked the line read-only at 21:45:12Z (date -u).
  - The addendum cites it as "registered, not yet run" until p4 sends the reader's sha and its plant reading red.
    Those come from p4 when the reader lands.

**Section 0's three constraints, re-read:**
1. **The kind tables: unchanged; only their line numbers moved.**
   - `kKindName` and `kPushDelayMs` are now at `include/FrameBench.h:56-57` (they were at 43-44), still 4 entries each.
   - `PushDelayMs()` still indexes the delays by kind (lines 268-271).
   - `DelayOn()` still adds the loop's +10 ms only for `C1_LOOP10` (lines 273-276).
   - The tree's only `static_assert` is on `Rec`'s size, as it was at `bfcb47e01c3d`. So the `static_assert` on both
     arrays' length is still P5/P6's to add, as section 0 says.
2. **`kScenes`: now chosen three ways, and each scene carries a level.**
   - `FRAME_PROFILE` takes the list from p4's `FrameProfileScenes.h`. Otherwise `BLIPSCOPE_PUSH_TASK` gives image B's
     list, and otherwise image A's `{C1}`.
   - `FrameBench.h` now includes `variants/Variant.h` itself (line 34), so every translation unit sees one list. S0
     still reads this from the image.
   - C1-L's `Scene` carries `level` (the (a) table's n: default 40, at most `kTableMax` 60) and `wx` (default on).
   - So P5's and P6's kinds still go into image B's list, and their bench envs define no `FRAME_PROFILE`. Their N
     (20, 40 or 60) is the scene's `level`, which is within `kTableMax`.
3. **The step-C exemption: unchanged.** `BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT` is still defined at
   `src/AircraftManager.h:50`, so a bench branch is still never a PR.

None of the three blocks P5/P6, and section 0 stands with these readings.
