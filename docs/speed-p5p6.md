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

### 4d. Amendment 4d: code before the addendum (ruled 2026-11-16, before any reading)

**The ruling.** The orchestrator, after 04:49Z on 2026-11-16, carrying the owner's overnight order ("5. weatherscope:
the weather cut and label cache", with nobody to stop and wait for him): "P5/P6 build no image before the addendum,
and the addendum waits for an A0a reading. So overnight, do what doesn't depend on A0a's numbers: the code, host tests
and selftests for the weather cut and the label cache, on bench branches from a0583e88b142, committed and pushed behind
your gates. ... No board use."

**What changes.** The header and section 3 say the addendum comes "before any code is written". That no longer holds.
Code, host tests and selftests may now be written, committed and pushed on bench branches from `a0583e88b142` before
the addendum.

**What does not change, and why the early code cannot bias any reading:**
- **No image is built and no board is used before the addendum,** so every frame-time reading still comes after it.
- **The gains are still predicted in the addendum,** from A0a's measured `outline`, `sw_blend`, `under` and
  `lbl_layout` splits, before any image is built. They come from A0a's numbers, not from this code.
- **Every host row's outcome is already frozen in sections 1 and 2:** byte-identical images, 0 mismatches, and each
  plant red on its named row. So no host result can be fitted after it is seen. A host result outside them stops that
  row and is reported, and nothing is fixed before a ruling.
- **If A0a moves a premise** (section 3), the order is re-ruled, and code already written may be set aside. That is
  the cost of not waiting, and it is accepted.
- **Each bench branch is pushed as a record only,** never as a PR (section 0). Each winner lands later in its own change
  from main.
- **A compile-only firmware build** (`pio run` of a bench env, never flashed, never read for time) has been asked
  about, because it produces an image. Until it is ruled, no `pio run`: the firmware call sites stay thin wrappers
  around header-only functions that the host tests compile.
- **Ruled, after 04:55Z (the orchestrator):** a compile-only build is not a reading, because it is never flashed and
  never read. Its ELF is deleted or ignored, and no size line comes from it. **But not yet, for disk:** no `pio run`
  until 5c's A8 window closes AND map-server's step-8 dry runs finish. Then it goes only if C:'s free space, minus the
  build's size plus 0.5 GiB, still clears 30.0 GiB, read immediately before.

### 4e. P5 1a's host rows and plants, as built (2026-11-16, before any run)

**Where:** bench branch `bench/speed-p5`, from `a0583e88b142`.
- **The host side, run first:** `include/OutlineMask.h`; `test/host/LgfxPort.h`, LovyanGFX 1.2.21's integer raster
  ported line for line; `test/host/test_p5_outline.cpp`; `test/host/sabotage_p5_outline.py`; and a `run.sh` block.
- **The firmware call sites,** not yet compiled (4d): `src/AircraftManager.{h,cpp}`, and `include/FrameBench.h`'s
  kinds `OUTLINE_8COPY`/`OUTLINE_MASK` with the length `static_assert` section 0 asks for.

**Three design facts the rows rest on:**
- **Exact by construction, and the guard that keeps it so.** The mask writes black on (D ∖ M), and that equals the 8
  copies whenever the marker drawn at an offset is the centre's marker moved by it. LovyanGFX's integer raster
  (fillTriangle, drawLine including its clip walk, drawCircle, fillCircle) is shift-invariant. The float-to-`int32_t`
  truncation of a dart's vertices is not: it fails across zero, at the sprite's top and left, and can fail under float
  rounding. So `DrawEdge` builds the integer vertices at all 8 offsets first, and uses the mask only when every one is
  the centre's moved by its offset. Any other contact gets today's 8 copies. Each row prints its mask and fallback
  counts.
- **One compiled body.** `DrawAircraftTriangle`'s arithmetic moves unchanged into `outlinemask::Heading` and `Build`
  (noinline), and `DrawAircraftTriangle` calls them. So the colour marker, the bench's 8-copy path, the guard and the
  mask take their vertices from the same instructions, whatever the compiler fuses.
  - **Open, for the winner's own change:** whether moving that arithmetic moves any shipping pixel compared with
    main. That is a device question, read there.
- **The scratch.** On the device, M is drawn by LovyanGFX's own calls on a 32 × 32 1-bit `LGFX_Sprite` (read MSB
  first). On the host it comes from the port. The edge is written through the canvas's `drawFastHLine`, so it is
  clipped exactly as LovyanGFX clips.

**The rows (the ids the driver reads):**
- `CMP control`: a single changed pixel reads 1.
- `H-OVERLAP@40` and `@60`: pairs and triples, both draw orders, well inside the sprite.
- `H-EDGE@40` and `@60`: every kind (light, heavy, glider, rotor, balloon, dot) at headings 0, 45, … 315, centred 4 px
  inside to 6 px past each tangent point. Tangent offsets are −4, 0 and +4; one contact per point per frame, with
  non-overlapping fillers inside.
- `H-CULL@40` and `@60`: zoomed in. Contacts at radius 116 to 122 on the diagonals, both just inside and culled, with
  fillers inside.
- `H-CULL-ORDER`: read from `src/AircraftManager.cpp`. Every outline call lies after the radar loop's cull and before
  the contact's own colour marker, and the bench's 8-copy loop lies in the same span.
- Every raster row compares whole frames against today's 8 copies.

**The plants and their red sets, restated before any run:**

| plant | planted in | red on exactly |
|---|---|---|
| P5-ONEPASS: all edges in one pass, then all markers (the reviewer's) | the harness's NEW loop | `H-OVERLAP@40`, `@60` |
| P5-NOCLIP: the edge clipped to 1..238 | `OutlineMask.h` | `H-EDGE@40`, `@60` |
| P5-DILATE4: a 4-neighbour dilation | `OutlineMask.h` | `H-OVERLAP`, `H-EDGE`, `H-CULL`, each `@40` and `@60` |
| P5-CULLSKIP: the edge drawn before the cull | `AircraftManager.cpp` | `H-CULL-ORDER` |
| P5-NOGUARD: no shift check (new) | `OutlineMask.h` | `H-EDGE@40`, `@60` |

Three changes from section 1a's table:
- **DILATE4 adds H-CULL.** No non-empty pixel set has the same 4- and 8-neighbour dilation. So the plant turns every row
  that draws a marker through the mask red, and H-CULL draws its just-inside contacts.
- **CULLSKIP is read on H-CULL-ORDER,** because the raster rows cannot see AircraftManager's loop.
- **NOGUARD is new.** It is the control that the guard is ever needed: without it, a guard that never fires would be
  untested.

**Predictions for the first run** (WSL `Ubuntu-24.04`, g++ 13.3, run.sh's flags), frozen here:
1. **test_p5_outline:** 8 ok, exit 0.
   - Every raster row: 0 differing px.
   - Fallbacks: 0 in both H-OVERLAP and both H-CULL rows, above 0 in both H-EDGE rows.
   - Culled above 0 in both H-CULL rows.
   - H-CULL-ORDER: 1 outline call, 0 outside its span, and the 8-copy loop inside it.
2. **sabotage_p5_outline.py:**
   - every plant red on exactly its set, "P5 OUTLINE PLANTS AS PREDICTED", exit 0;
   - `--anchors`: 5 plants, 5 anchors, each present exactly once;
   - `--control`: test_p5_outline at 8 ok.
3. **test_wx_outline and sabotage_wx_outline:** unchanged, at 3 ok and as predicted. The 8-copy loop's text is kept
   word for word in the bench region.
4. **check_frame_bench.py:** every row ok, and its `--selftest` green. `lint_driver_counts.py` flags nothing in the
   new driver.

**Outside the set:**
- A raster row with differing pixels stops 1a. It is reported, and nothing is fixed before a ruling.
- A fallback count other than predicted is reported. The rows grade pixels, not counts.
- If NOGUARD's red set differs from its prediction, the guard's reach is reported, not re-predicted.
- Anything else: the instrument is not understood, so stop.

**WD5o on this branch:** test_wx_outline still reads the 8-copy loop, now the bench's OLD path, as "the outline". That
the mask equals that loop is test_p5_outline's job. The winner's own change from main moves WD5o onto the mask.

### 4f. P5 1a's first host run, read (2026-11-16 05:19-05:20Z, WSL `Ubuntu-24.04`, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`)

Read against 4e's predictions, which were pushed at `896f290df424` before the run. The code read is
`bench/speed-p5` at `11ef2d24079e` (pushed 05:23:53Z, identical to the tree the run read).

**As predicted:**
- **test_p5_outline:** 8 ok, exit 0.
  - Every raster row: 0 differing px (H-OVERLAP over 12 frames each; H-EDGE over 1,584; H-CULL over 336).
  - Fallbacks: H-OVERLAP 0, 0; H-EDGE 730, 731.
  - H-CULL: culled 1,968 and near-circle drawn 2,064 in each row.
  - H-CULL-ORDER: 1 call, 0 outside its span, the 8-copy loop inside it.
- **sabotage_p5_outline.py:**
  - ONEPASS, NOCLIP, DILATE4 and CULLSKIP each red on exactly their sets;
  - `--anchors`: 5 plants and 5 anchors, each present once;
  - `--control`: 8 ok;
  - lint_driver_counts: 0 flagged.
- **WD5o:** 3 ok, and sabotage_wx_outline as predicted.
- **check_frame_bench.py:** 15 rows, 0 failed, and its selftest PASSED, so C1-L and FB-TU held.

**Off the predicted set, reported and not re-predicted** (4e's branch for both is "report"):
1. **H-CULL's fallbacks read 1 and 1, not 0.** Both are the same filler: a light dart at heading exactly 180 (up 0) at
   (17, 60).
   - Float `sin(π)` is about −8.7e-8, so its tip's x is `17 − 5.2e-7`, which rounds to 17.0 and truncates to 17.
   - At offset −1 it is `16 − 5.2e-7`. Below a power of two the float spacing halves, so this one rounds to
     15.9999995 and truncates to 15, not 16.
   - The guard saw that, and the contact got today's 8 copies. The pixels are equal.
2. **NOGUARD read red on H-CULL@40 and @60 as well as both H-EDGE rows.** It is the same contact: without the guard,
   its pixels differ.
   - So the guard is needed inside the disc too, wherever a vertex sits just below a power of two, not only at the
     sprite's top and left as 4e's prediction assumed.
   - The red set is therefore the set of rows with a fallback, which is what the plant exists to show.

**My own rig miss:** the standalone WD5o compile failed once ("cannot open output file"), because its WSL build
directory from the first call no longer existed. It was re-run with the directory made, and read 3 ok. The code was
not involved.

**Finding, recorded by the orchestrator (after 05:25Z):**
- **The guard is load-bearing everywhere,** not only at the sprite's edges. A vertex just below a power of two can
  truncate differently at an offset, as the heading-180 dart at x 17 did. H-CULL's fallback row is recorded OFF and
  not re-predicted.
- **Carried into the addendum:** the device's OUTLINE_MASK scenes will include fallbacks, so the gain is predicted on
  the mask path plus its fallback rate, never on the mask alone.

### 4g. P6's host rows and plants, as built (2026-11-16, before any run)

**Where:** bench branch `bench/speed-p6`, from `a0583e88b142`.
- **The host side, run first:**
  - `include/LabelCache.h`;
  - `LabelKey()` in `src/AircraftInfoFields.{h,cpp}`;
  - the host's stand-ins: `test/host/p6_fake/TrackedAircraft.h` and `test/host/p6_shim/Arduino.h`;
  - `test/host/test_p6_labels.cpp` and `test/host/sabotage_p6_labels.py`;
  - a `run.sh` block.
- **The firmware call sites,** not yet compiled (4d): `AircraftManager::LabelLayout` and `DrawAircraftInfo`; the
  `TrackedAircraft::labelCache` member; FrameBench's kinds `LABELS_TODAY`, `LABELS_CACHE` and `LABELS_CHECK`, with the
  length `static_assert`; and the `[speed-p6]` marker in `check-no-bench-hooks.sh` (I-4).

**The design facts the rows rest on:**
- **The key, exact.** It holds every input of every info field: the strings length-prefixed, the floats by their
  bits, the ints, and the enabled-field mask.
  - `LabelKey` builds it beside the formatters it covers. Raw values are used, never rounded ones, so a wrong key can
    only over-invalidate (one refill), never keep a stale text.
  - An input past the 192-byte key, or a chosen line past 39 characters, makes the contact UNCACHED. It is then laid
    out by `LayoutToday`, exactly as before, every frame.
- **One layout rule.** `LayoutToday` is the old `LabelLayout` body, moved unchanged. It serves as the bench's OLD path
  and as the cache's fallback. `PlaceCached` repeats its loop on cached widths at this frame's (x, y), so the
  placement is never cached.
- **The box and the draw share the one entry.** Both go through `LabelLayout`, and so does the tap hit-test.
- **Memory.** The entry (predicted 336 bytes) lives in PSRAM. Each contact pays one pointer of internal heap, through
  `labelcache::Slot`, which owns the entry and frees it with the contact. A copy starts empty, which is correct
  because the key includes `icao24`.
  - **The device's heap reading is a row for the addendum,** not a host one.
- **The equality row on the device** runs in its own scene kind, `LABELS_CHECK`, which is untimed: the check costs a
  second layout, so it never runs in a timed scene. It prints `[speed-p6] labels S<k> checked <n> mismatches <m>`,
  and is predicted at 0 mismatches.

**The rows (the ids the driver reads):**
- `CMP control`: one changed x reads a mismatch.
- `P6-COVER`: the real formatters and `LabelKey`, compiled against the stand-in.
  - Each of the 16 members it reads is moved through several values, and wherever any field's text moves, the key
    must move.
  - The control: every member moves some text.
  - The members are generated from the same lists as the stand-in, so a member added there is moved here.
- `P6-SEQ@20`, `@40` and `@60`: a recorded 400-frame sequence.
  - Every input changes while its field is shown alone: each input's period is under the mask's 20 frames.
  - Contacts drift, the rim's drops and clamps included. The enabled mask cycles (each field alone, the defaults,
    all, and pairs).
  - Enrichment arrives, strings go empty, a route turns Local. Contact 0's inputs overflow the key, and contact 1's
    operator overflows a line.
  - Each frame does the box and then the draw through the cache, compared with `LayoutToday`: 0 mismatches in
    count, index, string, x, y or width.
  - The control: every contact's texts change at least once, and both refills and uncached are above 0.
- `P6-SHARE`: on every frame whose box refreshed, the draw's lookup is a hit.

**The plants and their red sets:**

| plant | planted in | red on exactly |
|---|---|---|
| P6-NEVER: never invalidated | `LabelCache.h` | `P6-SEQ@20`, `@40`, `@60` |
| P6-WIDTH: a stale width kept across a text change | `LabelCache.h` | `P6-SEQ@20`, `@40`, `@60` |
| P6-POS: the first frame's placement reused | `LabelCache.h` | `P6-SEQ@20`, `@40`, `@60` |
| P6-KEYDROP: the squawk left out of `LabelKey` (new) | `AircraftInfoFields.cpp` | `P6-COVER` and `P6-SEQ@20`, `@40`, `@60` |
| P6-NOSHARE: every lookup refills (new) | `LabelCache.h` | `P6-SHARE` |

Section 2's three plants read "mismatches above 0". That is the SEQ rows here. KEYDROP shows the key's coverage can be
broken, and NOSHARE shows the sharing row can fail.

**Predictions for the first run** (WSL `Ubuntu-24.04`, g++ 13.3, run.sh's flags and its staged build), frozen here:
1. **test_p6_labels:** 6 ok, exit 0. The entry is 336 bytes.
   - P6-COVER: 16 members, 0 with the key unmoved, 0 silent.
   - Each SEQ row: 0 mismatches, hits above 0, control "yes".
   - P6-SHARE: 0 misses.
2. **sabotage_p6_labels.py:**
   - each plant red on exactly its set, exit 0;
   - `--anchors`: 5 plants, 6 anchors (POS has two);
   - `--control`: 6 ok;
   - `lint_driver_counts` flags nothing.
3. **check_frame_bench.py on `bench/speed-p6`:** 15 rows, 0 failed, and its `--selftest` PASSED. `test_label_lines`
   is unchanged.

**Outside the set:**
- A SEQ mismatch or a COVER violation stops P6. It is reported, and nothing is fixed before a ruling.
- An entry size other than 336 is reported (it is a layout fact, not a grade).
- A plant off its set is reported, not re-predicted.
- Anything else: the instrument is not understood, so stop.

### 4h. P6's first host run, read (2026-11-16 05:44Z, WSL `Ubuntu-24.04`, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`)

Read against 4g's predictions, pushed at `4239b4684ed6` before the run. The code read is `bench/speed-p6` at
`1d692d7b772a` (pushed 05:46:53Z, the tree the run read).

**As predicted:**
- **test_p6_labels:** 6 ok, exit 0. The entry is 336 bytes.
  - P6-COVER: 16 members, 136 text moves, 0 with the key unmoved, 0 silent.
  - SEQ@20, @40, @60: 0 mismatches over 8,000, 16,000 and 24,000 contact-frames, with the control "yes" in each.
    Uncached was 840 in each row (contacts 0 and 1).
  - P6-SHARE: 46,740 frames whose box refreshed, 0 draw misses.
- **sabotage_p6_labels.py:**
  - NEVER, WIDTH and POS each red on exactly the three SEQ rows; NOSHARE red on exactly P6-SHARE;
  - `--anchors`: 5 plants and 6 anchors, each present once;
  - `--control`: 6 ok;
  - lint: 0 flagged.
- **check_frame_bench.py:** 15 rows, 0 failed, and its selftest PASSED. test_label_lines: "all good".

**Off the predicted set, reported and not re-predicted:**
- **P6-KEYDROP read red on P6-COVER only, not on the three SEQ rows.**
- **Why: the sequence moves a float input on every frame.** Velocity changes by 0.37 m/s a frame, for example. So the
  key changes every frame whatever the squawk does, and every frame's box refills.
  - The visible sign is in the same readings: hits equal refills in every SEQ row. Each frame's one hit is the draw,
    sharing the box's refill.
  - So a key that leaves the squawk out never serves a stale text in SEQ.
- **What each row can see, as built:**
  - SEQ grades refills and placement: NEVER, WIDTH and POS each read red.
  - COVER grades the key's coverage, and it caught the drop.
  - SHARE grades sharing.
  - No row here holds a key across frames.
- **Proposed, not done, awaiting a ruling:** a SEQ variant where the numeric inputs HOLD between "merges", every few
  frames, as on the device. Cross-frame hits then happen, and a key omission would show in SEQ as well as COVER.
- **The device's LABELS_CHECK row already has that property,** because real values change only on merges.

**My own rig miss, again:** one test_label_lines compile failed ("cannot open output file"), because WSL's `/tmp` had
been cleared between calls, as in 4f. It was re-run with the directory made, and read "all good". From now on every
WSL call makes its directory itself.

### 4i. P6-SEQH: the inputs held across frames (ruled 2026-11-16 after 05:48Z, before any run)

**The ruling** (the orchestrator): "YES to SEQH ... SEQ has NEVER exercised a cache HIT. A bug that returns stale
widths ON A HIT, which is the path the device actually lives on, is invisible to SEQ today." It asked for rows with
inputs held and changed occasionally, registered first; KEYDROP predicted red there; and a new plant, a hit that
returns the previous entry's width.

**One correction, stated so the red sets below follow from it.** SEQ does exercise hits, but only WITHIN a frame:
the draw's lookup after the box's refill, which is P6-SHARE's count. What SEQ never exercises is a key held ACROSS
frames, the path the device lives on. So:
- the orchestrator's plant (HITWIDTH) reads red on SEQ too;
- a second new plant (HELDHIT) is a bug only a key held across frames can show, and it must stay green on SEQ.

**The rows: `P6-SEQH@20`, `@40` and `@60`.** Each is 800 frames: 20 masks of 40 frames each.
- **Every contact gets a full first fill.** After that, only events change it: one per 3 to 5 frames, and each event
  changes exactly ONE input.
- **During a field's solo window,** every other event changes one of that field's own inputs. So every input changes
  alone while its field is drawn.
- **Contacts move every frame,** so a hit's placement is recomputed at a new (x, y).
- **Contact 0 overflows the key, and contact 1's operator overflows a line,** as in SEQ.
- **Each frame does the box and then the draw, compared with `LayoutToday`:** 0 mismatches.
- **The control:** every contact's texts change; box hits are above 0 (keys held across frames); refills and uncached
  are both above 0.

**The plants and their red sets, for all nine rows:**

| plant | red on exactly |
|---|---|
| P6-NEVER | the SEQ rows and the SEQH rows |
| P6-WIDTH | the SEQ rows and the SEQH rows |
| P6-POS | the SEQ rows and the SEQH rows |
| P6-KEYDROP | `P6-COVER` and the SEQH rows. SEQ stays green: 4h's finding. |
| P6-NOSHARE | `P6-SHARE` and the SEQH rows. SEQH's control needs held-key hits, and NOSHARE has none. |
| P6-HITWIDTH (the orchestrator's, new): a hit returns the previous fill's widths | the SEQ rows and the SEQH rows |
| P6-HELDHIT (new): from the second CONSECUTIVE hit on, the widths drift | the SEQH rows only |

**Predictions** (WSL `Ubuntu-24.04`, g++ 13.3, run.sh's flags and staged build), frozen here:
1. **test_p6_labels:** 9 ok, exit 0.
   - CMP control, P6-COVER, the SEQ rows and P6-SHARE are unchanged from 4h (their code did not change).
   - Each SEQH row: 0 mismatches over 16,000, 32,000 and 48,000 contact-frames, box hits above 0, control "yes".
2. **sabotage_p6_labels.py:**
   - each of the 7 plants red on exactly its set above, exit 0;
   - `--anchors`: 7 plants, 12 anchors;
   - `--control`: 9 ok;
   - lint: 0 flagged.

**Outside the set:**
- A SEQH mismatch stops P6. It is reported, and nothing is fixed before a ruling.
- A plant off its set is reported, not re-predicted.
- Anything else: stop.

### 4j. P6-SEQH, read (2026-11-16 05:54-05:55Z, WSL `Ubuntu-24.04`, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`)

Read against 4i's predictions, pushed at `ec8f5833c887` before the run. The code read is `bench/speed-p6` at
`4050a5357d8c`. **Every prediction held. Nothing is off the set.**
- **test_p6_labels:** 9 ok, exit 0.
  - CMP control, P6-COVER, the SEQ rows and P6-SHARE match 4h, number for number.
  - SEQH@20, @40, @60: 0 mismatches over 16,000, 32,000 and 48,000 contact-frames.
    - Box hits (keys held across frames): 11,352, 23,295 and 35,302.
    - Refills: 3,808, 7,865 and 11,858. Uncached: 1,680 in each.
    - The fewest text changes of any contact: 142, 138 and 137. The control read "yes" in each.
- **sabotage_p6_labels.py:** all 7 plants red on exactly their sets, "P6 LABEL PLANTS AS PREDICTED", exit 0.
  - HELDHIT read red on SEQH alone, and KEYDROP on COVER and SEQH.
  - `--anchors`: 7 plants and 12 anchors, each present once.
  - `--control`: 9 ok.
  - lint: 0 flagged.
- **So the cache's held-key hit path, the one the device lives on, is now graded on the host.**

### 4k. P5 1b's host rows and plants, as built (2026-11-16, before any run)

**Where:** `bench/speed-p5`, on top of 1a.
- **The host side:** `include/WxSweepLut.h`, `test/host/test_p5_blend.cpp`, `test/host/sabotage_p5_blend.py`, and a
  `run.sh` block.
- **The firmware call site,** not yet compiled (4d): `DrawSweepOverWeather` blends through the table. Today's
  `wxsweep::Blend` runs only in a FRAME_BENCH region, behind the new kind `SWEEP_OLD`. `SWEEP_LUT` is the table. Both
  are in `SPEED_P5_BENCH`'s image B list after 1a's scenes, and the addendum re-states that list, with section 1b's
  dry layer, before any reading.

**The design: exact by construction.**
- A wedge pixel's colour is `Lit565(code, pct, SliceLevel(mark))`. Its inputs:
  - `mark` is 1..18, because Rasterise writes nothing else;
  - `pct` is fixed for the frame;
  - `code` is the layer's, 0-15 plus `wxlayer::kOffDisc`, which is 255.
- **The table** holds `Lit565` itself for every mark and every code 0-15: 19 × 16 × 2 B = 608 B, built by the same two
  functions. It is rebuilt only when `pct` changes (`For`).
- **The guard:** a code past the table, or a mark past 18, takes `Lit565` directly. That is exact, never masked into
  range.
- **Why a separate header:** `include/WxSweep.h` is untouched. Its `Blend` is the reference and the bench's OLD path,
  and `sabotage_wx_sweep.py`'s plants and its span-loop anchor live there. A W5c row inside `test_wx_sweep.cpp` would
  turn red under that driver's S-w, because S-w is planted in `BlendOne`, which the table does not use. That would
  break its exact sets.

**The rows** (section 1b's "W5a's comparison ... the new blend against the old one, byte for byte"):
- `CMP control`: one changed pixel reads a difference.
- `W5c@A` and `W5c@B`: each look. At all 3,600 angles, the table blend against `wxsweep::Blend`: the same frame, count
  and cleared slice.
  - The frame uses varied pixels, codes 0-39, and kOffDisc (255) on a diagonal lattice, so both the table and its
    direct path run.
  - The control: both of those paths blend more than 0 pixels.
- `W5c-PCT`: ONE table across 360 frames whose look alternates A, B, A, … every third frame. A table built for the
  wrong percent would show here.

**The plants and their red sets:**

| plant | planted in `WxSweepLut.h` | red on exactly |
|---|---|---|
| P5-ROUND (section 1b's): one rounding step changed | the table's level rounded, not truncated | `W5c@A`, `W5c@B`, `W5c-PCT` |
| P5-CODEGUARD (new): the guard off | a code masked into the table (`& 15`) | `W5c@A`, `W5c@B`, `W5c-PCT` |
| P5-STALE (new): the table never rebuilt | `For` builds only once | `W5c-PCT` |

Section 1b names the row that must read red under P5-ROUND as W5a. Here that comparison is W5c, because W5a itself
compares `Blend` with `BlendRows`, and the table touches neither.

**Predictions for the first run** (WSL `Ubuntu-24.04`, g++ 13.3, run.sh's flags), frozen here:
1. **test_p5_blend:** 4 ok, exit 0.
   - W5c@A and W5c@B: 0 of 3,600 angles differ, with the control "yes".
   - W5c-PCT: 0 of 360 frames differ.
2. **sabotage_p5_blend.py:**
   - each plant red on exactly its set, exit 0;
   - `--anchors`: 3 plants, 3 anchors, each present exactly once;
   - `--control`: 4 ok;
   - lint: 0 flagged.
3. **Unchanged:**
   - test_wx_sweep: 5 ok, and sabotage_wx_sweep as predicted, with its anchors present (WxSweep.h is untouched);
   - test_p5_outline: 8 ok;
   - check_frame_bench.py on `bench/speed-p5`: 15 rows, 0 failed, and its selftest PASSED.

**Outside the set:**
- A W5c difference stops 1b. It is reported, and nothing is fixed before a ruling.
- A plant off its set is reported, not re-predicted.
- Anything else: stop.

### 4l. P5 1b's first host run, read (2026-11-16 06:13Z, WSL `Ubuntu-24.04`, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`)

Read against 4k's predictions, pushed at `b8989f3dbcf0` before the run. The code read is `bench/speed-p5` at
`b7ba59602fb2` (pushed 06:15:05Z, the tree the run read). **Every prediction held. Nothing is off the set.**
- **test_p5_blend:** 4 ok, exit 0.
  - W5c@A and W5c@B: 0 of 3,600 angles differ. 10,369,762 pixels came from the table and 15,832,839 through the
    direct path, so the control read "yes".
  - W5c-PCT: 0 of 360 frames differ, with one table and the look alternating 40% and 25%.
- **sabotage_p5_blend.py:** ROUND and CODEGUARD each red on exactly W5c@A, W5c@B and W5c-PCT; STALE on W5c-PCT alone.
  "P5 BLEND PLANTS AS PREDICTED", exit 0.
  - `--anchors`: 3 plants and 3 anchors, each present exactly once.
  - `--control`: 4 ok.
  - lint: 0 flagged.
- **Unchanged:**
  - test_wx_sweep: all checks passed.
  - sabotage_wx_sweep: as predicted, "real include/WxSweep.h unchanged: True"; `--anchors`: 4 plants and 4 anchors,
    each present exactly once.
  - test_p5_outline: all checks passed.
  - check_frame_bench.py: 15 rows, 0 failed, and its selftest PASSED.

### 4m. P5 1c's host core: rows and plants, as built (2026-11-16, before any run)

**Where:** `bench/speed-p5`, on top of 1b: `include/DirtyClear.h`, `test/host/test_p5_clear.cpp`,
`test/host/sabotage_p5_clear.py`, and a `run.sh` block.

**NOT in this step: the firmware wiring.** The dirty restore is exact only if every write over the layer either marks
its box or forces a full restore. So wiring it means four things:
- `BandCanvas` marks every primitive it forwards;
- every direct buffer write adds its spans (the sweep's, and anything that draws through `canvas.sprite()`);
- every full-restore event calls `All()`: a layer rebuild, the layer turning on or off, another face or a card, the
  M3 test frame;
- a device check scene compares the dirty restore with a full clear on every frame.

The host cannot see whether that list is complete, and a missed path is a ghost on the glass. So the wiring waits for
an audit of every write path in the radar frame, sent to the orchestrator for a ruling before any of it is written.

**The design** (the header's comment has it in full):
- Each buffer keeps a dirty set: per row, one span, the union of every mark since that buffer's last restore.
- `Restore` copies those spans back from the layer, plus the indicator's band on every frame.
- It is a FULL copy when the buffer is fresh or the layer's generation moved.
- The marks are conservative boxes, one per LovyanGFX primitive.

**The rows:**
- `CMP control`: one changed pixel reads 1.
- `MARK-COVER`: 4,000 primitives (triangles, lines, circles, discs, rects with negative sizes, fast hlines), on screen,
  partly off and wholly off. Every pixel the ported raster writes must lie inside its mark.
- `SEQ2`: 600 frames on two alternating buffers.
  - Each frame draws 40 moving primitives through a marking canvas, then the indicator WITHOUT a mark (the worst
    case).
  - The layer is rebuilt every 97 frames.
  - After every `Restore` the buffer must equal the layer, pixel for pixel; after the frame it must equal a
    full-clear twin.
  - The control: both full and partial restores happen.
- `IND-STOP`: 200 frames, with every fifth frame from frame 5 stopped before the indicator, and primitives kept off the
  band. On a stopped frame, the band's pixels must equal the full clear's.

**The plants and their red sets:**

| plant | planted in | red on exactly |
|---|---|---|
| P5-INDSKIP (section 1c's): the band left out of the restore | `DirtyClear.h` | `SEQ2`, `IND-STOP` |
| P5-ONEBUF (section 1c's): one set for both buffers | SEQ2's wiring, in the test (the caller owns the sets) | `SEQ2` |
| P5-LAYERGEN (new): a rebuilt layer not forcing a full restore | `DirtyClear.h` | `SEQ2` |
| P5-MARKSHORT (new): a triangle's mark one column short | `DirtyClear.h` | `MARK-COVER`, `SEQ2` |

Section 1c reads INDSKIP on "a frame stopped before DrawScreenIndicator still shows dots", which is IND-STOP. SEQ2 reads
it too, because the unmarked dots survive the restore there.

**Predictions for the first run** (WSL `Ubuntu-24.04`, g++ 13.3, run.sh's flags), frozen here:
1. **test_p5_clear:** 4 ok, exit 0.
   - MARK-COVER: 4,000 primitives, 0 pixels outside their mark.
   - SEQ2: 0 pixels off the layer after Restore, and 0 off the twin. Full restores 14: both buffers' first use, plus
     both buffers after each of the 6 rebuilds. Partial restores 586.
   - IND-STOP: 39 stopped frames, 0 band pixels differ.
2. **sabotage_p5_clear.py:**
   - each plant red on exactly its set, exit 0;
   - `--anchors`: 4 plants, 5 anchors (ONEBUF has two), each present exactly once;
   - `--control`: 4 ok;
   - lint: 0 flagged.

**Outside the set:**
- A pixel off in SEQ2 or IND-STOP, or one outside its mark, stops 1c. It is reported, and nothing is fixed before a
  ruling.
- A full or partial count other than 14 and 586 is reported.
- A plant off its set is reported, not re-predicted.
- Anything else: stop.

### 4n. P5 1c's host core, read (2026-11-16 06:24Z, WSL `Ubuntu-24.04`, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`)

Read against 4m's predictions, pushed at `1afb7f40877f` before the run. The code read is `bench/speed-p5` at
`4f06c3a24f96`. **Every prediction held. Nothing is off the set.**
- **test_p5_clear:** 4 ok, exit 0.
  - MARK-COVER: 4,000 primitives wrote 392,624 pixels, and 0 lay outside their mark.
  - SEQ2: 0 pixels off the layer after a Restore, 0 off the twin after a frame, with 14 full and 586 partial restores.
  - IND-STOP: 39 stopped frames, 0 band pixels differ.
- **sabotage_p5_clear.py:** each plant red on exactly its set, "P5 CLEAR PLANTS AS PREDICTED", exit 0.
  - INDSKIP: SEQ2 and IND-STOP. ONEBUF: SEQ2. LAYERGEN: SEQ2. MARKSHORT: MARK-COVER and SEQ2.
  - `--anchors`: 4 plants and 5 anchors, each present exactly once.
  - `--control`: 4 ok.
  - lint: 0 flagged.
- **Ungraded reading:** a partial restore copied 10,469 pixels on average, against 57,600 for a full clear, for this
  host scene of 40 primitives. It says nothing about the device's `under`, which the addendum predicts from A0a.

### 4o. P5 1c's firmware wiring and its static row (ruled 2026-11-16 after 06:27Z; registered before the code)

**The ruling** (the orchestrator): "YES, write the wiring (uncompiled until the disk condition clears), registered as
its OWN amendment first, with your audit as its list". Two additions:
- "A STATIC SOURCE ROW, so the audit is a check that runs, not a list that's remembered", with a plant of "one
  BandCanvas method forwarding without marking";
- the M3 dependency, named.

**The wiring, which is the audit's list:**
1. **`BandCanvas`:** each of its 15 drawing methods marks the canvas's dirty set (when it has one) before it forwards.
   - `fillScreen` calls `All()`.
   - `drawString` marks `x ± width, y ± fontHeight`, which covers any text datum.
   - `fillArc` and `drawArc` mark the outer radius's box.
   - A canvas with no set (every other edition, every other face) marks nothing, as now.
2. **`DrawSweepOverWeather`:** marks the wedge's per-row spans (`rowLo`/`rowHi`), the only direct pixel writes in the
   radar-weather frame. Its beam is drawn through the canvas.
3. **`DrawWeatherUnder`'s flat path:** `dirtyclear::Restore` from the layer, for the buffer's own set. Today's whole
   memcpy becomes the bench's `CLEAR_OLD` path, in a FRAME_BENCH region.
4. **Every other write calls `All()`:**
   - the non-flat `wxSprite.pushSprite`;
   - `CheckLayerCopy`'s push (bench F1);
   - the detail card's `photoSprite.pushSprite`;
   - and any frame that `DrawWeatherUnder` does not clear flat: main's `fillScreen`, through (1).
5. **`RebuildWeatherLayer`** bumps `wxLayerGen` before its first write to the layer, so both buffers restore in full.
6. **One set per M3 buffer.** main.cpp chooses the set by `&drawBuf == pushtask::g_buf[0]`, and an image without the
   push task uses one set for its one buffer.
   - The indicator's band is `SCREEN_SIZE_DIV_2 ± ((SCREEN_COUNT - 1) * 6 + 4)` by `SCREEN_SIZE - 16 ± 5`, which
     covers DrawScreenIndicator's dots.
7. **The device's backstop, `CLEAR_CHECK`:** an untimed scene that, after each Restore, counts the buffer's pixels
   that differ from the layer. It prints `[speed-p5] clear S<k> frames <n> mismatched-px <m>`, predicted 0.
   - `[speed-p5]` joins `check-no-bench-hooks.sh`'s MARKERS (I-4).
   - `CLEAR_OLD`, `CLEAR_DIRTY` and `CLEAR_CHECK` join FrameBench's kinds and `SPEED_P5_BENCH`'s list. The
     addendum re-states that list.

**THE M3 DEPENDENCY, named.** "One set per buffer, keyed by pushtask's buffer index" relies on p4's M3 push-task
design, which is not settled (tonight's soak). **If M3's buffer scheme changes, this wiring is re-read before any device
run.**

**The static row: `test/host/check_p5_writes.py`.** It reads the source, and it is the audit as a check that runs.
- **`W-CANVAS`:** every method in `include/BandCanvas.h` that forwards a drawing call (`s_.fill*`, `draw*`, `push*`,
  `write*`) marks or calls `All()` in its body. Methods that do not draw are named in an exempt list.
- **`W-DIRECT`:** every direct write to a canvas buffer in `src/AircraftManager.cpp` and `src/main.cpp` is a
  `.sprite()` handle, `getBuffer()` or `pushSprite(&` onto a canvas sprite. Each one's function must mark spans or call
  `All()`. Read-only sites, and writes to sprites other than the canvas, are named in an allowlist, each with its
  reason.
- **`W-GEN`:** every function that writes into the layer (`wxSprite.fill*`, `draw*`, `write*`) bumps `wxLayerGen`.
- **The control:** each row finds more than 0 sites, or the check reads the rig (exit 2).

**Its plants** (`--selftest`), each red on exactly its row:

| plant | where | red on |
|---|---|---|
| P5-UNMARKED (the orchestrator's): `BandCanvas::drawLine` forwarding without marking | `BandCanvas.h` | `W-CANVAS` |
| P5-RAWWRITE: the photo push without its `All()` | `AircraftManager.cpp` | `W-DIRECT` |
| P5-NOGEN: the generation bump removed | `AircraftManager.cpp` | `W-GEN` |

**Predictions** (WSL, run.sh's flags; the firmware itself is not compiled, 4d):
1. `check_p5_writes.py`: every row ok, exit 0; `--selftest`: each plant red on exactly its row.
2. Unchanged:
   - test_p5_outline at 8 ok, test_p5_blend at 4 ok, test_p5_clear at 4 ok;
   - check_frame_bench.py at 15 rows, 0 failed, its selftest PASSED. FB1 holds: every new FRAME_BENCH line is in a
     region.

**Outside the set:**
- An unmarked write found on the wired tree is reported, and the wiring is fixed only after a ruling.
- A plant off its row is reported.
- Anything else: stop.

**Two corrections and three counts, written after the code and BEFORE any run:**
- **`BandCanvas` has 14 drawing methods, not 15.** The audit's message and the list above miscounted, and the 15th
  was a setter. The 14: `fillScreen`, `drawString`, `drawLine`, `drawCircle`, `fillCircle`, `fillTriangle`,
  `fillRect`, `drawRect`, `fillRoundRect`, `drawRoundRect`, `drawFastHLine`, `drawFastVLine`, `fillArc` and `drawArc`.
- **`CheckLayerCopy` now takes the canvas and calls `DirtyAll()` itself.** With the `All()` in its caller, the static
  row would have rested on a claim about another function. That is CLAUDE.md's "a justification is a claim about
  code you are not editing".
- **The predicted counts** (found by `grep`, not by running the check):
  - `W-CANVAS`: 14 drawing methods, none unmarked.
  - `W-DIRECT`: 20 sites, 8 in marking functions and 12 allowed, none unmarked.
    - The 8 are `CheckLayerCopy` (2), `DrawWeatherUnder` (3), `DrawSweepOverWeather` (2) and the photo push (1).
    - The 12 are the outline scratch's read, the photo sprite's 5 null tests, main.cpp's 3 reads (M3-9, /diag/fb
      and M3-1b), and the boot and splash screens' 3 null tests.
  - `W-GEN`: 1 function writes the layer (`RebuildWeatherLayer`), and it bumps.

### 4p. P5 1c's wiring and static row, read (2026-11-16 06:40Z, WSL `Ubuntu-24.04`, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`)

Read against 4o's predictions and counts, pushed at `ef4c6d49323c` and `5268bd56eb3b` before the run. The code read is
`bench/speed-p5` at `979d3e15e6b4`. **Every 4o prediction held.**
- **check_p5_writes.py:**
  - `W-CANVAS`: 14 drawing methods, none unmarked.
  - `W-DIRECT`: 20 sites, 8 in marking functions and 12 allowed, none unmarked.
  - `W-GEN`: 1 function writes the layer, and none lacks the bump. Exit 0.
  - `--selftest` PASSED: UNMARKED red on exactly W-CANVAS, RAWWRITE on exactly W-DIRECT, NOGEN on exactly W-GEN.
- **Unchanged:**
  - test_p5_outline, test_p5_blend and test_p5_clear: all checks passed;
  - sabotage_p5_blend and sabotage_p5_clear: as predicted, and the clear driver's `--anchors` reads 4 plants and 5
    anchors, each present exactly once;
  - check_frame_bench.py: 15 rows, 0 failed, and its selftest PASSED.

**Open, for a ruling: sabotage_p5_outline.py reads "1 NOT AS PREDICTED" on every run.**
- It is 4f's finding, unchanged: NOGUARD red on H-CULL@40 and @60 as well as both H-EDGE rows. That finding stood,
  recorded OFF and not re-predicted, so the driver still expects H-EDGE alone.
- The wiring did not cause it. The other four plants read exactly as before.
- The cost of leaving it is a driver that is red on every run of this branch, and a red that is always there trains
  people to read past it.
- **Proposed:** the driver's NOGUARD set becomes the recorded one (H-CULL and H-EDGE, each `@40` and `@60`), with a
  comment naming 4f's finding as its source, so any further change reads red. **Not done before the ruling.**

### 4q. The outline driver encodes 4f's finding (ruled 2026-11-16 after 06:44Z, before the change)

**The ruling** (the orchestrator): "YES, update NOGUARD's expected set to the recorded 4f set (H-CULL and H-EDGE, @40
and @60), with a comment naming 4f's finding and its mechanism. ... This isn't a re-fit." It came with two conditions:
- 4e's ORIGINAL prediction stays in the record unedited, with 4f beside it as the finding;
- the updated driver must be shown to still go red, by a planted one-row change to NOGUARD's set.

**So:**
- **4e's table above is untouched.** Its NOGUARD row still reads `H-EDGE@40, @60`, and 4f, recorded beside it, is the
  finding.
- **The change:** `sabotage_p5_outline.py`'s NOGUARD set becomes `H-EDGE@40, @60, H-CULL@40, @60`. Its comment names
  4f and the mechanism: float `sin(π)` is about −8.7e-8, so `16 − 5.2e-7` truncates to 15, which the guard refuses.

**Predictions, frozen here:**
1. **The proof that it can still go red.** A copy of the updated driver, with NOGUARD's set planted one row short
   (`H-CULL@60` removed), reads "P5 OUTLINE PLANTS: 1 NOT AS PREDICTED" and exits 1. NOGUARD is the one FAIL line.
2. **The updated driver itself** reads "P5 OUTLINE PLANTS AS PREDICTED" and exits 0, with all five plants on exactly
   their sets. Its `--anchors` reads 5 plants and 5 anchors, each present once, and its `--control` reads 8 ok.
3. **Outside the set:** anything else is reported, and stops the change.

### 4r. The outline driver's change, read (2026-11-16 06:47-06:48Z, WSL `Ubuntu-24.04`, g++ 13.3.0)

Read against 4q's predictions, pushed at `e78525270e5b` before the change. The code read is `bench/speed-p5` at
`9a531c0e5496`. **Both predictions held.**
1. **The proof copy, one row short:** "P5 OUTLINE PLANTS: 1 NOT AS PREDICTED", exit 1. NOGUARD was the one FAIL line:
   red on H-CULL@40, @60 and H-EDGE@40, @60 against a planted want without H-CULL@60.
2. **The updated driver:** "P5 OUTLINE PLANTS AS PREDICTED", exit 0, with all five plants on exactly their sets.
   - `--anchors`: 5 plants and 5 anchors, each present once.
   - `--control`: 8 ok.
   - lint: 0 flagged.

4e's original NOGUARD row is unedited above, and 4f stands beside it as the finding.

### 4s. The build addendum: compile-only builds, before any build (2026-11-16, for the orchestrator's acceptance)

This is NOT section 3's gains addendum, which still waits for an A0a reading. It registers COMPILE-ONLY builds: no
board, no flash, no frame time.

**Why sizes are read now.** The 04:55Z ruling said a compile-only build gives "no size line". The orchestrator's
request of 2026-11-16 (~12:2xZ) asks for predicted sizes or bands, with the sections named, so that newer instruction
governs here. The sizes are predicted below, before any build.

**The trees** (each read by `ls-remote` at 13:0xZ):
- the base, `a0583e88b142` (A0a's tree, 4b);
- P5, `bench/speed-p5` at `9a531c0e5496`;
- P6, `bench/speed-p6` at `4050a5357d8c`.

**The builds:** all in ONE detached build worktree, so every image shares one path. The trees are checked out by sha,
and pio runs from PowerShell, because of the MSYS esptool trap.

| build | env | tree | extra |
|---|---|---|---|
| B0 | `blipscope-s3-128` | base | |
| B1 | `blipscope-s3-128` | P5 | |
| B2 | `blipscope-s3-128` | P6 | |
| B3 | `blipscope-s3-128-framebench-c1b` | base | |
| B4 | `blipscope-s3-128-framebench-c1b` | P5 | `PLATFORMIO_BUILD_FLAGS=-DSPEED_P5_BENCH` |
| B5 | `blipscope-s3-128-framebench-c1b` | P6 | `PLATFORMIO_BUILD_FLAGS=-DSPEED_P6_BENCH` |
| B6 | `missileer-s3-128` | base | |
| B7 | `missileer-s3-128` | P5 | BandCanvas.h is shared by every edition |

- The shipping env has no `WX_LAYER_SHIPS` ("v19 ships without weather"). The weather code still compiles under
  `FEATURE_CLOUD_FEED`, which `${cloud.prod}` defines, but it is off at run time.
- **The protocol:**
  - a fresh C: reading before each build, stopping under 30.2 GiB;
  - after each build, `firmware.bin` and `firmware.elf` go to a frozen art dir with a manifest line: build, env, sha,
    extra flags, `.bin` size and sha256, the ELF's section sizes, and the I-4 result;
  - then `.pio/build` is deleted.
  - Each build is a background process registered with bgcheck, and is confirmed stopped afterwards.

**The predictions,** as deltas within my own path (B1, B2 against B0; B4, B5 against B3; B7 against B6). The sections
are read with `xtensa-esp32s3-elf-size -A`.
- **Every build compiles** (exit 0).
- **`.iram0.text` delta: exactly 0 in every pair.** No IRAM code was added.
- **`.dram0.data` delta: exactly 0 in every pair.** The new constants are `const`, so they go to `.flash.rodata`.
- **`.dram0.bss`:**
  - **B1 − B0: +4,484 to +5,200 B.**
    - The dirty sets: 2 × 1,936 B.
    - The sweep table: 612 B.
    - `OutlineScratch`'s function-local static object and its guard: the rest of the band.
    - **This is new INTERNAL RAM.** `appManager` is a plain global. It is recorded for the device's heap row, and
      moving the sets to PSRAM is an option for a ruling.
  - **B4 − B3:** the same band, plus 8 B for CLEAR_CHECK's two counters: +4,492 to +5,208 B.
  - **B2 − B0: −16 to +16 B** (P6 adds no static; each entry is in PSRAM at run time).
  - **B5 − B3: −16 to +24 B** (LABELS_CHECK's two counters).
  - **B7 − B6: 0 exactly.**
- **The `.bin` size** (flash: text, rodata and data):
  - **B1 − B0: +4 KiB to +40 KiB.**
    - About 175 draw-call sites gain a null-checked mark.
    - Plus the new code: DrawEdge's two instantiations, Heading and Build, OutlineScratch, the LUT blend, Restore.
  - **B4 − B3: +4 KiB to +44 KiB.**
  - **B2 − B0 and B5 − B3: +2 KiB to +16 KiB** (LabelCache's templates, LabelKey and Slot).
  - **B7 − B6: +0.5 KiB to +12 KiB.** The marks' null checks are compiled into every edition's call sites, though no
    other edition attaches a set.
- **I-4, read on each `.bin`** (`scripts/check-no-bench-hooks.sh`, run by each image's OWN tree's copy, because the
  marker lists differ by tree). Corrected before acceptance and before any build; see below.
  - B0, B1, B2, B6 and B7 pass (exit 0).
  - B3 is refused (exit 1), naming exactly `[frame-bench]`.
  - B4 is refused, naming exactly `[frame-bench]` and `[speed-p5]`.
  - B5 is refused, naming exactly `[frame-bench]` and `[speed-p6]`.
  - So the extra flag is proved to have reached each image, read from the image rather than the command.
  - **The correction:** the frozen text said `--expect "[speed-p5]"` would find the marker in B4. But `--expect`
    requires its marker present AND every other marker absent, and B4 also carries `[frame-bench]`, so it would exit 1
    on a correct image. Found by reading the script at 13:0xZ, before acceptance and before any build.

**ACCEPTED** (the orchestrator, at `5bd088f82737`). The I-4 correction above landed after that sha, at `a42131fd8256`,
and before any build. It is frozen here with the additions below, before B0:
1. **Negative marker checks.** They are read as raw byte counts in each `.bin` (`grep -c -a -F`), independent of any
   script's marker list.
   - `[speed-p5]` is absent from B0, B1, B2, B3, B5, B6 and B7, and present only in B4.
   - `[speed-p6]` is absent from all but B5.
   - `[frame-bench]` is present only in B3, B4 and B5.
   - The anchor `[build] env=` is present in all eight; if it is missing, that image reads BLIND.
   - **A bench marker in a shipping image (B1 or B2) is a STOP.**
2. **bgcheck runs from the PARENT,** after each build's root has ended, never inside the registered shell.
3. **A section outside its band is REPORTED** with the section, not interpreted, and the remaining builds finish. No
   image goes near a board.
4. **No per-function size reading from these ELFs.** Function sizes jitter with link-time relaxation. Any
   function-level claim needs an object-level (`.o`) compare.

**The rulings on my three points:**
- The sizes are section-level bands, read as readings, not gates.
- **The ~4.5 KB of new internal `.bss` stays internal for now.** The marks run per draw call, and PSRAM would charge
  frame time. The deciding instrument is the device's heap row: internal free, the largest block, and the
  handshake's trial-alloc margin, registered in the board addendum. The sets move to PSRAM only if that row crosses its
  floor, and then as their own A/B with frame time.
- **Open merge condition, for BandCanvas.h's marks in every edition.** Before any merge, one of two must hold:
  - the marks compile to nothing in editions that attach no dirty set (B7 − B6 `.bin` ≈ 0); or
  - a Missileer frame-time row.

**What the builds show, and what they do NOT:**
- **They show:** every firmware call site written tonight compiles, in the shipping shape and with the bench regions.
  They also show what each step costs in each section, and that the bench markers are present only where they belong.
- **They do NOT show:**
  - pixels or correctness on a device: the device anchor, the scratch's 1-bit layout, CLEAR_CHECK and LABELS_CHECK
    all need a board;
  - any frame time or gain, which is section 3's addendum, from A0a;
  - the run-time heap;
  - the weather paths running (the shipping images have the layer off);
  - I-3, that the regions add nothing (that needs a stripped twin);
  - any edition but radar and Missileer.

**Outside the set:**
- A build that fails to compile stops. It is reported, with the first error, and nothing is fixed before a ruling.
- A section outside its band is reported with the section that moved. The images stay compile checks.
- An I-4 result other than predicted stops.
- A C: reading under 30.2 GiB stops before the next build.

### 4t. B0 read; B1 failed to compile (4s's first OFF); the fix, for re-acceptance (2026-11-16 13:1x-13:2xZ)

**B0** (`blipscope-s3-128` at the base `a0583e88b142`) read as predicted:
- compiled in 204 s;
- `.bin` 1,934,320 B, sha256 `daffbe391966…`;
- `.iram0.text` 79,887; `.dram0.data` 36,400; `.dram0.bss` 38,584; `.flash.text` 1,286,624; `.flash.rodata` 529,908;
- I-4 passed; the counts for `[speed-p5]`, `[speed-p6]` and `[frame-bench]` were 0, 0 and 0, and the anchor 1;
- `.pio/build` deleted; bgcheck from the parent: PASS.
- **B0 stands**, because the base tree is untouched. p4's 1,934,304 B was built in a different path, so the 16 B
  difference is not graded.

**B1** (`blipscope-s3-128` at P5 `9a531c0e5496`) **FAILED to compile.** That is 4s's first OFF, against "all 8
compile".
- The one error in the log: `src/AircraftManager.h:1400:30: error: 'SCREEN_SIZE' was not declared in this scope`, in
  P5 1c's inline `WxDirty()`. Only `AircraftManager.cpp.o` failed.
- The series STOPPED before B2.
- **The cause, recorded as a lesson:** 1c's wiring was pushed uncompiled, and host tests cannot see a device header's
  include set.
- **My protocol slip:** the reparse scan before deleting the failed build's `.pio/build` did not run (wrong path), and
  the delete did not wait on it. Only my own rebuildable objects were deleted. Now the delete waits on the scan's exit.

**The fix (ruled):** `AircraftManager.h:1400` reads `s.Init(variant::SCREEN_SIZE, variant::SCREEN_SIZE)`.
- `include/Layout.h:11` is `constexpr int SCREEN_SIZE = variant::SCREEN_SIZE;`, with no scaling and no offset, so the
  value is the same.
- **So no prediction in 4s moves:** not the `.bss` bands, not I-4, not the marker counts.
- On the fixed tree, P5's host tests, its three drivers, check_p5_writes (14 / 20, 8 + 12 / 1, selftest PASSED) and
  check_frame_bench (15/0) all re-ran as before. The four gates read 0.
- **P5 is now `bench/speed-p5` at `73dc246b79cb`,** read back by ls-remote at 13:22:49Z.

**The P6 read-only recheck** for the same class (an inline in a header using a name its own includes don't bring in).
Every header P6 changed, with what its new code uses and where that comes from:
- `include/FrameBench.h`: `InScene`, `S()`, `State` and `kScenes` from the same header; `Serial` from `<Arduino.h>`.
- `include/LabelCache.h`: the fixed-width ints and `size_t` from `<cstdint>`/`<cstddef>`; `memcpy`/`strlen`/`memcmp`
  from `<cstring>`; `is_trivially_copyable` from `<type_traits>`; `nothrow` and placement `new` from `<new>`;
  `labellines::` from `LabelLines.h`; `heap_caps_*` from `<esp_heap_caps.h>`. It also compiled standalone on the host
  under `-Werror`.
- `src/AircraftInfoFields.h`: `labelcache::KeyBuf` from `LabelCache.h`; `TrackedAircraft` from
  `models/TrackedAircraft.h`.
- `src/AircraftManager.h`: `labelcache::Entry` through `models/TrackedAircraft.h`.
- `src/models/TrackedAircraft.h`: `labelcache::Slot` from `LabelCache.h`.
- **Found: none.** P6 is unchanged at `4050a5357d8c`.
- **One related check:** `LabelCache.h` puts the entry in PSRAM only under `ESP_PLATFORM`. Without it the code would
  compile and quietly use internal RAM. The flag is defined for these builds, in the Arduino-ESP32 libs'
  `esp32s3/flags/defines` and in the platform's `builder/frameworks/_bare.py` CPPDEFINES.

**For re-acceptance:** 4s at P5 `73dc246b79cb` and P6 `4050a5357d8c`, with the base unchanged. Nothing builds before
then. The series restarts at B1, and B0 stands. A second compile failure in 4s goes to the orchestrator, then to the
owner.

### 4u. B1 read; B2 failed to compile (4s's SECOND compile failure, for the orchestrator and the owner) (2026-11-16 13:26-13:34Z)

4s was re-accepted at base `a0583e88b142`, P5 `73dc246b79cb` and P6 `4050a5357d8c`.

**B1** (`blipscope-s3-128` at P5 `73dc246b79cb`) **compiled**, in 193 s:
- `.bin` 1,936,992 B, sha256 `78c33676c242…`;
- I-4 passed; the marker counts were 0, 0 and 0, and the anchor 1;
- the reparse scan read 0 links before `.pio/build` was deleted; bgcheck from the parent: PASS.

Its deltas against B0, reported with the sections and NOT interpreted (as ruled):

| section | B1 − B0 | 4s predicted | |
|---|---|---|---|
| `.bin` | +2,672 B | +4 KiB to +40 KiB | **outside (below)** |
| `.iram0.text` | 0 | exactly 0 | as predicted |
| `.dram0.data` | +608 B | exactly 0 | **outside** |
| `.dram0.bss` | +3,880 B | +4,484 to +5,200 B | **outside (below)** |
| `.flash.text` | +1,548 B | (inside `.bin`) | |
| `.flash.rodata` | +524 B | (inside `.bin`) | |

**B2** (`blipscope-s3-128` at P6 `4050a5357d8c`) **FAILED to compile:** pio exit 1 after 166 s.
- The log's two errors: `src/AircraftManager.cpp:5734:56` and `5735:41`, `invalid use of non-static data member
  'AircraftManager::tft'`. Only `AircraftManager.cpp.o` failed.
- P6's local adapter `struct Fields`, inside `LabelLayout`, calls `tft.textWidth(s)` and `tft.fontHeight()`. But `tft`
  is a MEMBER (`LGFX& tft;`, `AircraftManager.h:992`), and a local class has no object to reach it through.
- 4t's P6 recheck covered only header inlines' include sets, so it could not see this class: a .cpp local class using
  an enclosing member. Only compiling sees it.
- The log is kept at `logs/B2.log`. The failed build dir was deleted after the scan read 0 links. bgcheck: PASS.
- **The series is STOPPED.** It goes to the orchestrator and then the owner, as ruled.
- **Proposed, NOT applied:** `m.tft.textWidth(s)` and `m.tft.fontHeight()`, through the adapter's existing
  `const AircraftManager& m`. It is the same object, so P6's behavior and 4s's predictions don't move.
- **Asked:** may B3, B4, B6 and B7, which don't touch P6's tree, run while P6 waits?

### 4v. B3, B4, B6 and B7 read; B2 and B5 wait for the owner (2026-11-16 13:36-13:51Z)

The orchestrator approved running them: "a stop stops only its dependents". The P6 fix went to the owner, with the
orchestrator recommending yes. All four COMPILED. Each build's directory was deleted only after a reparse scan read 0
links, and bgcheck from the parent read PASS each time.

| build | `.bin` | sha256 | `.iram0.text` | `.dram0.data` | `.dram0.bss` | `.flash.text` | `.flash.rodata` |
|---|---|---|---|---|---|---|---|
| B0 | 1,934,320 | `daffbe391966` | 79,887 | 36,400 | 38,584 | 1,286,624 | 529,908 |
| B1 | 1,936,992 | `78c33676c242` | 79,887 | 37,008 | 42,464 | 1,288,172 | 530,432 |
| B3 | 1,965,808 | `1358c324a42e` | 79,887 | 36,432 | 44,024 | 1,312,088 | 535,912 |
| B4 | 1,971,520 | `a73de5971eb8` | 79,887 | 37,040 | 48,272 | 1,316,356 | 536,744 |
| B6 | 1,733,392 | `dd80671aa0b6` | 79,795 | 24,176 | 66,472 | 1,224,544 | 403,376 |
| B7 | 1,734,480 | `868903acd0af` | 79,795 | 24,176 | 66,472 | 1,225,504 | 403,504 |

**The deltas against 4s's bands**, reported and not interpreted:

| pair | `.bin` | `.iram0.text` | `.dram0.data` | `.dram0.bss` | `.flash.text` | `.flash.rodata` |
|---|---|---|---|---|---|---|
| B1 − B0 | +2,672: **outside (below)** | 0 ✓ | +608: **outside** | +3,880: **outside (below)** | +1,548 | +524 |
| B4 − B3 | +5,712 ✓ | 0 ✓ | +608: **outside** | +4,248: **outside (below)** | +4,268 | +832 |
| B7 − B6 | +1,088 ✓ | 0 ✓ | 0 ✓ | 0 ✓ (the exact one) | +960 | +128 |

**I-4,** each image scanned by its own tree's script: B0, B1, B6 and B7 pass. B3 is refused naming exactly
`[frame-bench]`, and B4 exactly `[frame-bench]` and `[speed-p5]`. All as predicted.

**The negative checks,** by raw byte count:
- `[speed-p5]` appears only in B4 (count 1), so nothing leaked into the shipping image B1.
- `[speed-p6]` is 0 in all six. B5, where it is predicted, is not built yet.
- `[frame-bench]` appears only in B3 and B4 (17 each).
- The anchor `[build] env=` appears in all six, once each.
- All as predicted.

**The open merge condition** (4s): B7 − B6 `.bin` reads +1,088 B, so the marks do NOT compile to nothing in an edition
that attaches no set. Before any merge, the condition stands: that, or a Missileer frame-time row.

**Not graded:** B0 and B3 differ from p4's sizes (1,934,304 and 1,965,792) by +16 B each, because they were built in a
different path.

**Artifacts:** `scratchpad/art-p5p6-20261116` holds each build's `.bin`, `.elf`, `sections.txt` and log (B2's log
included). The interim `manifest.txt` sha256 is `f0be96eacd3e…`, over six lines. It is frozen when B2 and B5 have run.
`wt-build/.pio/libdeps` is kept for them, and removed at the end.

### 4w. A reading for the next registration, not a re-grade of 4s (2026-11-16 13:5xZ)

4s's grades are untouched: B1 − B0's and B4 − B3's `.dram0.data` and `.dram0.bss` stay **outside**, as 4u and 4v
recorded. This section explains where the bytes went, so the next registration predicts them correctly. It changes no
verdict.

**Method.** I diffed B0's ELF against B1's as a multiset of (type, name, size).
- It covers every `d`/`D`/`b`/`B` symbol (`xtensa-esp32s3-elf-nm -S -C`, read-only, over the archived `.elf` files).
- Duplicate names (`TAG`, `PIo2`, `s_platform`, defined in many translation units) are counted as a multiset, not
  joined by name. A join by name cross-multiplies them into false rows.

**The diff has exactly two rows:**

| symbol | type | B0 | B1 | change |
|---|---|---|---|---|
| `appManager` | `B` (.bss) | 6,864 | 10,744 | +3,880, which is the whole `.dram0.bss` delta |
| `AircraftManager::DrawSweepOverWeather(...)::sweepLut` | `d` (.data) | none | 612 | new; `.dram0.data` moved +608, with 4 B absorbed by padding |

- 3,880 + 612 = **4,492 B**, which is exactly the lower edge of 4s's `.bss` band (+4,492..+5,208).
- So the internal static total landed on the predicted edge. It was split across two sections instead of all landing
  in `.bss`.
- **Why:** `wxsweeplut::Table` declares `int pct = -1`. That non-zero initializer puts the function-static `sweepLut` in
  `.data`.

**Not yet diffed:** B4 − B3's `.bss` is +4,248, which is 368 B more than B1 − B0's +3,880. The difference is
presumably the bench-only FrameBench P5 kinds, but that is **not established**. Before any registration predicts it,
it gets diffed by symbol the same way.

**The design choice, left open for the next registration.** It has two options:
- **(a)** Predict internal static memory as `.bss` + `.data` together.
- **(b)** Zero-initialize the LUT so it lands in `.bss`.

Either way the 612 B stays in internal DRAM. Option (b) only moves it between sections, and it removes the 612 B
initializer copy from the flash image.

### 4x. The P6 fix, B2 and B5 read, the manifest frozen, and a correction to 4w (2026-11-16 15:02-15:20Z)

**The P6 fix.** The owner's word came at 15:02Z, relayed by the orchestrator: "weatherscope: fix and continue".
- `bench/speed-p6` is at **`139670a097798690fefa7fb1bfa682d54b29f15b`**, one commit on `4050a5357d8c`. It touches one
  file and two lines: `AircraftManager.cpp:5734-5735` now call `m.tft.textWidth(s)` and `m.tft.fontHeight()` through
  the adapter's `const AircraftManager& m`.
- Before editing, I read B2's first log whole. It held exactly 2 errors, both on these two lines.
- That build had compiled all five TUs that include a P6 header. The link was not ruled out, because the build never
  reached it.
- **Compile proof** (the orchestrator's 13:34Z rule): `blipscope-s3-128` at `139670a09779` exited 0, LINKED, with 0
  error lines. It ran in the background, registered, and the parent bgcheck read PASS.
  - Its image is not B2: it is not archived, not in the manifest and not graded. Its log is
    `compile-proof/P6FIX.log`.
- **Host:** WSL, g++ 13.3, run.sh's staged build. `test_p6_labels` read 9 ok, exit 0, and `sabotage_p6_labels.py` read
  all 7 plants red on exactly their sets, as in 4j.
  - One rig miss: the first `wsl` call had its `/mnt` path rewritten by Git Bash and ran nothing. It was re-run with
    `MSYS_NO_PATHCONV=1`.
- The four gates exited 0 and the push went through. The orchestrator **RE-ACCEPTED 4s** at base `a0583e88b142`, P5
  `73dc246b79cb` and P6 `139670a09779`, before B2.
- **Disclosed before B2 ran:** I had seen the compile proof's `.bin` size (1,935,792 B). 4s's predictions for B2 and B5
  were frozen long before that and are unchanged.
- B2's first failure log is preserved as `logs/B2-fail-4050a5357d8c.log`, with the same sha256 as the original,
  because `build_one.sh` truncates `logs/B2.log`.

**B2 and B5**, both compiled. Each build directory was deleted after a reparse scan read 0 links, and the parent
bgcheck read PASS after each.

| build | `.bin` | sha256 | `.iram0.text` | `.dram0.data` | `.dram0.bss` | `.flash.text` | `.flash.rodata` |
|---|---|---|---|---|---|---|---|
| B2 | 1,935,792 | `7d4421abf912` | 79,887 | 36,400 | 38,584 | 1,287,936 | 530,076 |
| B5 | 1,967,808 | `b0e14e261583` | 79,887 | 36,432 | 44,032 | 1,313,796 | 536,204 |

| pair | `.bin` | `.iram0.text` | `.dram0.data` | `.dram0.bss` | `.flash.text` | `.flash.rodata` |
|---|---|---|---|---|---|---|
| B2 − B0 | +1,472: **outside (below** +2 KiB) | 0 ✓ | 0 ✓ | 0 ✓ (−16..+16) | +1,312 | +168 |
| B5 − B3 | +2,000: **outside (below** +2 KiB) | 0 ✓ | 0 ✓ | +8 ✓ (−16..+24) | +1,708 | +292 |

Both outside readings are reported with their section and not interpreted, under addition 3.

- **I-4:** B2 passes. B5 is refused naming exactly `[frame-bench]` and `[speed-p6]`, so the extra flag reached that
  image. Both as predicted.
- **Not graded:** the compile proof and B2 share a size but not a sha256 (`03e94c663f86` and `7d4421abf912`). The proof
  image was not kept, so the differing bytes cannot be accounted for and nothing is claimed about them.

**The negative checks over all eight images** (raw byte counts, addition 1) all held:
- `[speed-p5]` appears only in B4.
- `[speed-p6]` appears only in B5.
- `[frame-bench]` appears only in B3, B4 and B5.
- The anchor `[build] env=` appears once in each of the eight, so none reads BLIND.
- B1 and B2, the shipping images, carry no bench marker: no STOP.

**4s is complete.** Every build compiled, after the two compile failures recorded in 4t and 4u.

**Readings outside their bands, all reported and not interpreted:**
- B1 − B0: `.bin`, `.data`, `.bss`.
- B4 − B3: `.data`, `.bss`.
- B2 − B0: `.bin`.
- B5 − B3: `.bin`.

**The merge condition** stands (4v): B7 − B6 is +1,088 B.

**The manifest is FROZEN** at 8 lines, in append order B0, B1, B3, B4, B6, B7, B2, B5.
- Its sha256 is `f95ca6a59a190ac7839d5babe84e0e26e9c2dd11e27622eb4d055a8254947418`. 4v's interim six-line sha is
  superseded.
- `art-p5p6-20261116/SHA256SUMS` lists all 35 files in the art dir: 8 × `.bin`/`.elf`/`sections.txt`, 9 logs, the
  manifest and the compile proof's log. It verified OK, and its sha256 is
  `bc134065ec32f78e0b5aa5b970c70c9e4de7aabc18981306a020f372639e67c8`.
- `wt-build/.pio/libdeps` was deleted after a reparse scan read 0 links. C: was at 33.83 GiB afterwards.

**A correction to 4w, written here because 4w is never edited.** 4w says:

> 3,880 + 612 = **4,492 B**, which is exactly the lower edge of 4s's `.bss` band (+4,492..+5,208).

That cites the wrong band:
- +4,492..+5,208 is **B4 − B3's** band.
- **B1 − B0's** band, which is the pair 4w's diff covers, is +4,484..+5,200 (4s, above).
- So 4,492 sits **inside** B1 − B0's band, 8 B above its lower edge.
- The section-level sum, `.bss` +3,880 plus `.data` +608 = 4,488, is inside too, as the orchestrator's 13:3xZ
  observation said.

The error was mine. The orchestrator's 14:0xZ reply repeated it without checking which band, and recorded that as its
own. Nothing else in 4w moves, and 4s's grades are untouched.

### 4y. B4 − B3 by symbol: where 4w's extra 368 B went (2026-11-16 21:13Z), a reading for the next registration

4s's grades are untouched, as in 4w. This reading only settles what 4w left open: "B4 − B3's `.bss` is +4,248, which is
368 B more than B1 − B0's +3,880 … it gets diffed by symbol the same way."

**The artifacts held still.**
- All 35 `SHA256SUMS` entries verify.
- `SHA256SUMS` and `manifest.txt` hash to the values 4x froze (`bc134065ec32…`, `f95ca6a59a19…`).

**The method is 4w's:**
- a multiset diff of (type, name, size) over every `d`/`D`/`b`/`B` symbol from `xtensa-esp32s3-elf-nm -S -C`;
- then `objdump -t -C` for each changed symbol's SECTION.

**The anchor control ran first.** The same script on B0 → B1 reproduces 4w's two rows exactly: `appManager` `B`
6,864 → 10,744, and `sweepLut` new, `d` 612.

**Predicted before reading B3 → B4 (in the session, at 21:1xZ):**
- **(i)** `appManager` +3,880 plus separate bench-only `.bss` symbols of +368; or
- **(ii)** `appManager` alone at +4,248.
- Anything else would be recorded as read.

**B3 → B4:** 2,692 → 2,697 symbols; 6 gone, 11 new.

| symbol | section | B3 | B4 |
|---|---|---|---|
| `appManager` | `.dram0.bss` | 7,912 | 11,792 (**+3,880**, the same as B1 − B0) |
| `(anonymous)::TheOutlineScratch()::scratch` | `.dram0.bss` | none | **348** |
| guard variable for that `scratch` | `.dram0.bss` | none | **8** |
| `AircraftManager::DrawSweepOverWeather(...)::sweepLut` | `.dram0.data` | none | 612 |
| `(anonymous)::OutlineScratch::Row(int) const::rev` | `.flash.rodata` | none | 16 |
| `outlinemask::kOff` | `.flash.rodata` | none | 16 |
| `framebench::kKindName` | `.flash.rodata` | 16 | 44 |
| `framebench::kPushDelayMs` | `.flash.rodata` | 16 | 44 |
| `framebench::kScenes` (×3, one per TU) | `.flash.rodata` | 12 each | 39 each |

**What it means:**
- **`.dram0.bss` +4,248 is branch (i).**
  - It breaks down as `appManager` +3,880, the outline scratch +348, and its static-initialisation guard +8. That makes
    +4,236 in symbols, plus 12 B of section alignment.
  - The 368 B extra over B1 − B0 is the outline scratch (356 B) plus alignment.
  - The scratch is absent from B0 → B1, so it compiles only into the framebench env.
- **`.dram0.data` +608 is identical to B1 − B0.** It is `sweepLut`'s 612 B, with 4 B absorbed.
- **The other `d`-typed growth, +169 B, is in FLASH.** That covers `rev`, `kOff` and the three `framebench` tables.
  `.flash.rodata` is +832 for the section.
  - `nm` types these symbols `d`, yet they sit in `.flash.rodata`.
  - So **an `nm` type letter is not a statement about internal RAM**. Only the section read decides that.

**For the next registration (no verdict changes here):**
- The framebench env's internal static memory = the shipping env's (+3,880 `.bss`, +612 `.data`) plus the outline
  scratch (356 B `.bss`, plus alignment).
- Its flash-resident tables are not internal memory.
- 4w's design choice stays open: (a) predict `.bss` + `.data` together, or (b) zero-initialise the LUT.

### 5. Board addendum 1: CLEAR_CHECK, LABELS_CHECK and the heap row on E2:08, from 4s's frozen images (registered 2026-11-16 ~22:0xZ, before any board contact)

**What the reviewer ruled (2026-11-16 21:2x-21:4xZ, relayed):**
- **Scope (A).** This addendum covers only the rows 4s's FROZEN images can run:
  - `CLEAR_CHECK` on B4;
  - `LABELS_CHECK` on B5;
  - the heap reading from `[health]` (B3 as the base, then B4 and B5), including the LabelCache-in-PSRAM prediction.
  - **Addendum 2** (later) holds the device anchor, the scratch self-check and the handshake trial-alloc margin. The
    frozen images have no anchor scene, no self-check and no margin print. They need code, the anchor's host comparison
    tool (red first), and new images built when C: is at or above 30 GiB.
- **The board is E2:08** (the Blipscope bench), in a delegated no-hands window on the 11-12 terms:
  - the write ranges are printed against the partition table first;
  - no factory image, full erase or NVS write;
  - both images are sha256-identified before the window;
  - E2:08 is restored to v19 at the end;
  - v19 has priority;
  - anything needing the owner's hands means stop and report.
  - **Not 6E:64:** these are Blipscope radar images, and CLAUDE.md forbids them there.
- **Every esptool call** goes through `scripts/app-only-write.py` from #735 (after it merges). That is the private
  esptool 5.2.0 copy, behind its tool guard, with `--stub-version 2` on every call. No raw call is made, except the
  recovery read-mac below, which uses the same private copy.
- **Serial, option (b):** one bench-trace per image.
  - It opens only after the board's NETWORK report confirms that image's boot, never on a timer after a reset.
  - It closes before the next write. A closed trace leaves the S3 in the ROM, which is where `app-only-write` wants it.
- **The seal:** each trace is sha256'd at close and read only through `tools/speed-p5p6/trace_filter.py` (below). The
  `[frame-bench]` records stay unread until the gains addendum (section 3, after A0a).
- **The restore** is v19's release asset, with success read over the network.
- **No new build:** the images come from 4s's frozen manifest.

**M3's state, read before any device run (4o's dependency).**
- `p4/m3-push-task` is at `9e5198a43d6d` (2026-11-16 05:14Z).
- Its push task still holds TWO buffers, `LGFX_Sprite* g_buf[2] = {&backbuffer, &backbuffer2}`, exactly as in this
  base, `a0583e88b142`.
- Its one commit not in the base changes 38 lines of `src/main.cpp`, and none of them names `g_buf`, `g_draw`, `g_free`
  or a back buffer. It is M3-9's self-check: a CPU view against a PSRAM view, before the DMA.
- M3 is not on main.
- **So the buffer scheme did not move.** 4o's wiring (one dirty set per buffer, chosen by `&drawBuf ==
  pushtask::g_buf[0]`) stands, and no re-read is needed.

**The images, identified now.**
- B3, B4 and B5 are 4s's FROZEN bins. All 35 `SHA256SUMS` entries verify, and both frozen checksums match 4x.
- v19 is the release asset `firmware-s3-128.bin`, downloaded and checked against the release's sha256 digest.
- Each sits in its own build dir beside one `partitions.bin`.

| image | env (source sha) | bytes | sha256 | `ota_0` write range |
|---|---|---|---|---|
| B3 | `blipscope-s3-128-framebench-c1b` (`a0583e88b142`) | 1,965,808 | `1358c324a42ef878e9596e8f4d76e43b9898768527812ca248a46b8c570aeccb` | 0x20000..0x1FFFFF |
| B4 | the same, `-DSPEED_P5_BENCH` (`73dc246b79cb`) | 1,971,520 | `a73de5971eb8e4ad3ff780139dcb0f0adb4f8a592b1c6e2d63d6a72912b0e7a0` | 0x20000..0x201FFF |
| B5 | the same, `-DSPEED_P6_BENCH` (`139670a09779`) | 1,967,808 | `b0e14e2615838182e78d4c08250f21b7bd2f2ffe294b249849e300425978a343` | 0x20000..0x200FFF |
| v19 (restore) | `blipscope-s3-128` (the v19 release) | 1,931,840 | `f6d73e39163177b0dc25516a104bb02021d2048a94f82a66fc42a90f0a2615be` | 0x20000..0x1F7FFF |

**The partition table** is the same CSV at every commit involved: `partitions-s3-16mb-bignvs.csv`, blob
`3beaadbc7ab8`, at v19, `a0583e88b142`, `73dc246b79cb`, `139670a09779` and main.
- `partitions.bin` was generated from that CSV with the framework's `gen_esp32part.py --flash-size 16MB`.
- It is **byte-equal to an independent copy**: the `partitions.bin` of an existing build of
  `blipscope-s3-128-wxlook`. Both have sha256 `28f336fe57a9991a5f39db735295a978e0a6aee5710f5e7107ac44bdd812d29b`,
  3,072 B.
- `app-only-write` still refuses on its own if the board's table differs. That refusal is the executing guard.
- The table:

| partition | offset | size |
|---|---|---|
| `nvs` | 0x9000 | 84 KiB |
| `otadata` | 0x1E000 | 8 KiB |
| `app0`/ota_0 | 0x20000 | 6,400 KiB |
| `app1`/ota_1 | 0x660000 | 6,400 KiB |
| `spiffs` | 0xCA0000 | 3,392 KiB |
| `coredump` | 0xFF0000 | 64 KiB |

- Every write lands inside `ota_0` (which ends at 0x65FFFF). NVS, otadata, the bootloader and the table are never
  written: `app-only-write` writes one region, after its two reads.

**NVS.**
- From v19 to each image's source, `ConfigMigration` is unchanged, and **no** NVS-writing call is added. The one
  `.clear(` in the diff is a JSON document's.
- FrameBench's header says it writes no NVS.
- The restore's network success (below) is also the test that the settings survived: v19 rejoins Wi-Fi without the
  portal.

**Tools, and the STOP lines.**
1. **Reserve the board** with `board-port.py --reserve blipscope-bench --by weatherscope --for "speed-p5p6 addendum 1"`.
   Release it at the end.
2. **Resolve the port** from E2:08's USB serial immediately before EVERY write (`board-port.py`, never a COM number).
   Print the serial beside the port.
3. **Before the first write,** `app-only-write.py --check-tool` must print esptool **5.2.0**, tree
   **`84c878702a70`…**, and `CHECK-TOOL PASSED`.
   - Anything else means STOP: nothing is written.
   - The shared `~/.platformio` (now 5.3.0) is never used for E2:08.
4. **The stub actually used** cannot be read from esptool's output. #735's own note: "nothing in esptool's output says
   which ran".
   - So it is recorded as the pin in every argv: `--stub-version 2`.
   - `app-only-write.py --selftest` must print `SELFTEST PASSED` immediately before the first write. Its rows check
     every argv a run builds.
5. **The environment.** This session's PowerShell tool is failing. So every `app-only-write` call and every
   bench-trace runs in a WMI-created PowerShell child (`Win32_Process.Create`), the form the reviewer accepted for the
   esptool repair.
   - Each child first asserts MSYSTEM is empty and the PATH has 0 msys/mingw/`Git\usr\bin` entries.
   - If not, it exits 7 having touched nothing.

**The board's network report.**
- An HTTP GET of its config page at its mDNS name: the product name plus the MAC's last three octets, `.local`.
- No LAN address is ever written down.
- The page shows `firmware v<N>` and the build summary `<env> · v<N> · <features>`.
- **The test images' env is `blipscope-s3-128-framebench-c1b`, and v19's is `blipscope-s3-128`.** So the env field is
  what tells them apart. All four images say `firmware v19`.

**The sequence.** Times are minutes from the window's start; the nominal total is ~78 min.

| step | what | budget |
|---|---|---|
| 0 | Reserve. Two network reads 60 s apart, which must both show `firmware v19` and one build summary, the **baseline string `S19`** recorded for the restore. Then `--check-tool` and `--selftest`. | 0-3 |
| 1 | **B3:** resolve the port, `app-only-write --build-dir <B3>`. Network-confirm the boot (the framebench env, within 180 s). Open the trace for **6 min**. Close, seal, filter. | 3-14 |
| 2 | **B4:** the same, with the trace open **40 min**. CLEAR_CHECK is scene 9 of 13 and ends ~34.5 min after boot (90 s start, then 100 s settle and 120 s measure per scene). | 14-60 |
| 3 | **B5:** the same, with the trace open **8 min**. LABELS_CHECK is scene 1 of 5 and ends ~5.2 min after boot. | 60-73 |
| 4 | **Restore v19:** `app-only-write --build-dir <v19>`. Two network reads 60 s apart, both within 300 s of the write's end. Release the board. | 73-78 |

- **HARD END: start + 95 min.** At the hard end, whatever is unread stays unread. Any open trace is closed, and step 4
  runs.
- **v19 priority:** anything of v19's preempts this. The window then goes straight to step 4.
- **Recovery** (a write fails, or a boot is not confirmed within 180 s):
  - Restore v19 through `app-only-write`.
  - If that cannot reach the chip, run ONE `read-mac` through the private copy (`python -I -m esptool --chip esp32s3
    --port <P> --stub-version 2 read-mac`), which brings an S3 out of the ROM, then retry the restore once.
  - Anything beyond that needs hands: STOP and report.
- **Data:**
  - The raw traces stay private in the session's handoff folder. They carry Wi-Fi lines and the sealed records.
  - Only the filter's output enters this record, and it does so after the four gates.

**The seal: `tools/speed-p5p6/trace_filter.py`.** This is code, not a promise.
- **It prints:**
  - the raw file's sha256;
  - bench-trace's own lines;
  - `[speed-p5]`, `[speed-p6]`, `[build]` and `[boot]` lines, whole;
  - and, from the main `[health]` line, ONLY `heap free`, `largest`, `free8`, `psram_free` and `tlsOk`.
- **It withholds:**
  - **The frame-time fields of the main `[health]` line** (`frame avg`, `p95`, `max`, …). They are dropped because they
    are gains material too.
  - **Every other timing `[health]` line, whole:** `sweep=`, `clear=`, `outline=`, `sweepparts=` and `sweepscan=`.
  - **Everything else, `[frame-bench]` included.** It is only counted, by tag.
- `[health] ALLOC FAILED` and preventive-reboot lines pass whole.
- **Its selftest** puts each withheld string into a synthetic trace as a CONTROL, then asserts its absence from the
  output. Read: 29 rows ok and 0 FAIL, with exactly 10 lines printed.
- **Two sabotages each read red:**
  - a copy that passes `[health]` whole: 7 FAIL rows;
  - a copy that passes `[frame-bench]`: 3 FAIL rows.

**Predictions.**

| row | graded on | predicted |
|---|---|---|
| T-TOOL | step 0 | `CHECK-TOOL PASSED` at 5.2.0 / `84c878702a70`; `SELFTEST PASSED` |
| W-n (each of the 4 writes) | `app-only-write`'s output | `checks ok: table matches (6 entries), board boots ota_0, image <bytes> B` (the bytes as in the table above). Then write attempt 1 verified ("Hash of data verified."), `reset: ok`, exit 0. |
| N-BOOT (B3, B4, B5) | the network | the page shows the env `blipscope-s3-128-framebench-c1b` within 180 s of the write's end |
| TR (each trace) | bench-trace | exit 0, `VERDICT: clean`. A contaminated trace (exit 3) voids that image's rows, the window continues, and there is no re-run in it. |
| R-CLEAR (B4) | the filter's output | exactly one `[speed-p5] clear S8 frames <n> mismatched-px 0`, with n ≥ 100 |
| R-LABELS (B5) | the filter's output | exactly one `[speed-p6] labels S0 checked <n> mismatches 0`, with n ≥ 100 |
| H-TLS | each image's first 10 heap lines (received ≥ 1 s after attach, so the open's stale burst is skipped) | `tlsOk=1` on all 30 lines |
| H-NOISE (the escape hatch) | the same lines | each image's `free8` max − min is ≤ 2,048 B. Otherwise the heap rows read **UNTRUSTWORTHY at this scale**: no verdict, report, decide. |
| H-P5 | the medians over those 10 lines | `free8`(B4) − `free8`(B3) **∈ [−6,904, −2,808]**, and `heap free` the same band. That is −4,856 ± 2,048: B4 adds .dram0.bss +4,248 and .dram0.data +608 of internal static (4y). `psram_free`(B4) − (B3) ∈ [−4,096, +4,096]. |
| H-P6, LabelCache in PSRAM | the same medians | `free8`(B5) − `free8`(B3) **∈ [−2,216, +2,048]**: statics +8 B, one pointer per contact (40 × 4 B), then ± 2,048. AND `psram_free`(B5) − (B3) **∈ [−24,576, −6,720]**: at least 20 entries of 336 B, and at most 40 at ≤ 512 B each, plus 4,096. If the entries were internal, `free8` would fall by ≥ 6,720 and fail its band: the two halves discriminate. |
| H-LARGEST | | `largest` is recorded, NOT graded ("kept, no longer trusted") |
| RESTORE | the network | `firmware v19` with the build summary equal to `S19`, on two reads 60 s apart, both within 300 s of the restore write's end. The reads arriving at all show the Wi-Fi rejoin without the portal. |

**Outside the set.**
- **A refusal** (exit 6 or 7) **on the first write** means STOP. Nothing was written, and there is no restore.
- **A refusal on a later write** means recovery, then STOP.
- **A failed write** (exit 8), **or a boot not confirmed,** means recovery, then STOP.
- **A row line absent** by its trace's end voids that row (an instrument failure), with no re-run in this window.
- **`mismatched-px` > 0 or `mismatches` > 0** is a **RED reading**: a finding about P5 1c's restore or P6's cache, not
  an instrument failure. It is reported as read.
- **A heap median outside its band** is reported as read. No band is moved.
- **The restore reads not arriving** means recovery, and the owner is told.
- **Anything none of these covers:** the instrument is unreliable at this scale. Stop and decide.

**What this addendum does NOT show:**
- frame time or any gain (section 3, after A0a; those records stay sealed);
- the device anchor, the scratch self-check, or the trial-alloc margin (addendum 2);
- any edition but radar.

**Earliest start:** after #735 merges (its sha is then named here) AND the reviewer's acceptance of this section. The
slot is the reviewer's to book, around E2:08's FT-HR (12:54Z), S2b (17:50Z) and the owner's sitting (22:00Z), with v19
priority.

### 5a. Each band's derivation, and a correction to section 5's H-P6 bands (2026-11-16 ~22:1xZ, before anything ran)

**Why this section exists.** The reviewer accepted section 5 (`8b0f9cbc87d2`) except for one point: every band must be
derivable as written. H-P5 checks out (−4,856 ± 2,048). But section 5's H-P6 `free8` band `[−2,216, +2,048]` is not
C ± 2,048 for any C, and its `psram_free` band `[−24,576, −6,720]` adds its allowance on one side only.
- **The cause was mine.** I put the allowance on −168 at one edge and on 0 at the other, and I bounded the entry count
  instead of deriving it.
- **The rule:** a band that cannot be derived is a band chosen after the fact.
- **Nothing has run,** so this re-registers both. Section 5 stays as frozen; this section supersedes its two H-P6 bands
  and adds one row (H-NOISE-PS). Every other row stands.

**New readings from the frozen ELFs, read-only.** gdb, `xtensa-esp32s3-elf-gdb -batch -nx`, on 4s's archived files:
- `sizeof(labelcache::Entry)` = **336** on the device (B5). This is the same as the host's figure.
- `sizeof(labelcache::Slot)` = **4**.
- `sizeof(TrackedAircraft)` = **1,036** in B3 and B4, and **1,040** in B5. P6 adds the 4-byte slot to every contact
  record; P5 adds nothing.
- `appManager.trackedAircraft` is a **`std::map<String, TrackedAircraft>`**: one heap node per contact, through
  `std::allocator`.

**The pricing rule** (the TLSF model, reproduced to the byte on every earlier Weatherscope reading). A block of n bytes
costs `roundclass(n + 12) + 4` in a free-size delta, where the class width is `2^(floor(log2 m) - 5)` for m ≥ 128.
- **One label-cache entry:** 336 + 12 = 348 rounds to 352 (width 8), plus 4, so **356 B**.
- **One map node:** about 1,070 B, where the width is 32. A record 4 B larger therefore moves every node either **0 or
  one 32-B class**, and all 40 nodes move the same way, because they are the same size. Either 0 or 40 × 32 = 1,280 B.
- Whether the nodes sit in internal RAM or PSRAM depends on the malloc threshold, which is not read here. The bands
  below cover both.

**The two allowances, chosen BEFORE any reading.** No `[health]` series exists for these images.
- **J = 2,048 B** for internal heap.
- **J_ps = 4,096 B** for PSRAM, wider because the poll's JSON documents land there.
- Each band is the derived expected value or range, widened by its allowance on BOTH sides.
- **H-NOISE and the new H-NOISE-PS** check that each allowance is not smaller than the line-to-line noise.

**The derivations.**

| row | expected change (from which reading) | allowance | band |
|---|---|---|---|
| H-P5 `free8` and `heap free` (B4 − B3) | **−4,856**: 4y's section deltas, `.dram0.bss` +4,248 and `.dram0.data` +608, both carved out of internal DRAM before the heap starts. `TrackedAircraft` is unchanged (1,036). | ± 2,048 | **[−6,904, −2,808]**, unchanged from section 5 |
| H-P5 `psram_free` (B4 − B3) | **0**: P5's sets are internal statics, and no record grows | ± 4,096 | **[−4,096, +4,096]**, unchanged |
| H-P6 `free8` and `heap free` (B5 − B3) | **−8 to −1,288**. −8 is the statics (4x, B5 − B3 `.dram0.bss` +8). Down to −1,288 if the map nodes are internal and each steps one class. | ± 2,048 | **[−3,336, +2,040]**, SUPERSEDING `[−2,216, +2,048]` |
| H-P6 `psram_free` (B5 − B3) | **−14,240 to −15,520**. −14,240 is 40 entries × 356 B: k = 40, the scene's level (`kLevel`), with every contact on screen and every key and line inside the cache's limits. Down to −15,520 if the nodes are in PSRAM and each steps one class. | ± 4,096 | **[−19,616, −10,144]**, SUPERSEDING `[−24,576, −6,720]` |

**The two halves still discriminate.**
- If the entries were INTERNAL, `free8` would move about −14,248 (below −3,336) and `psram_free` about 0 to −1,280
  (above −10,144). Both bands fail.
- If `free8` is in band and `psram_free` is out of band, the entries are in PSRAM but k ≠ 40. That is reported as
  read. No band moves.

**The other floors' derivations:**
- **R-CLEAR and R-LABELS, n ≥ 100.** Each check scene runs 220 s (100 s settle + 120 s measure), and at any running
  frame rate (≥ 1 fps) that gives ≥ 220. The floor only separates "ran" from "did not run". It is not a rate claim.
- **H-TLS, `tlsOk=1`.** CanHandshake's trial allocation succeeds on v19's internal heap today. The largest internal
  cost here is B4's −4,856 B.
  - **What's missing:** no headroom figure is on record, so the prediction rests only on the handshake working today.
  - **A `tlsOk=0` line is a finding** for the heap row, not an instrument failure.

**One added row.** **H-NOISE-PS:** each image's `psram_free` max − min over its 10 graded lines is ≤ 4,096 B.
Otherwise the `psram_free` halves of H-P5 and H-P6 read **UNTRUSTWORTHY at this scale** (no verdict, report,
decide), exactly as H-NOISE does for `free8`.

**Premise, stated:** arduino-esp32's `ESP.getFreeHeap()` returns internal free, so `heap free` shares `free8`'s center.
If B3's `heap free` and `free8` differ by more than J, `heap free` is recorded, not graded.

**Unchanged:** everything else in section 5. That covers the images and their shas, the table, the tools and STOP
lines, the sequence and timeline, the hard end at start + 95, the seal (`trace_filter.py` `eb31148e50ec`), the
restore, and the outside-the-set lines.

### 5b. #735's merge named, and the recovery moved onto its guarded path (2026-11-16 ~23:4xZ, before anything ran)

**#735 merged at 23:41Z.** Main is `0fc8c1d319cb`, and `scripts/app-only-write.py` there is
blob `fa9d102be291`. I read both myself at 23:43Z, and they match the reviewer's relay.
- Its recorded constants are as section 5 registered: `ESPTOOL_VERSION = '5.2.0'`, `ESPTOOL_TREE_SHA =
  '84c878702a70…'`, and `STUB_VERSION = '2'`.
- **The window runs that file and no other:** `scripts/app-only-write.py`, together with `board-port.py` and
  `bench-trace.ps1`, from a clean checkout at `0fc8c1d319cb`.
- Step 0's `--check-tool` and `--selftest` run there.

**One change to section 5's recovery, and only that.** Section 5 registered the recovery read-mac as a raw call into the
private copy (`python -I -m esptool … read-mac`). The merged script now has that recovery itself:
`app-only-write.py --read-mac --port <P>` (#735's A9).
- It runs through the SAME tool guard as every write.
- It prints one line, never esptool's output (so never the MAC).
- **So the recovery uses it, and no raw esptool call remains anywhere in this window.** That is the reviewer's ruling
  ("via app-only-write (`--read-mac`, the writes) rather than a raw call") applied to the last step that bypassed it.
- Everything else in sections 5 and 5a stands.

### 6. Board addendum 1, read (E2:08, 2026-11-16 23:45:46Z to 2026-11-17 00:55:19Z)

**The window.**
- E2:08 was reserved by weatherscope at 23:45:46Z and released at 00:55Z. The hard end was 01:20:46Z.
- Every esptool call went through `app-only-write.py` at main `0fc8c1d319cb` (blob `fa9d102be291`), from a clean
  checkout.
- Every write and trace ran in a WMI-created PowerShell child. Each one read MSYSTEM '' and 0 msys PATH entries.
- Only the filter's output (`trace_filter.py` `eb31148e50ec`) and the seals were read. No `[frame-bench]` record and
  no `[health]` timing was read.

**Step 0, T-TOOL: as predicted.**
- Two network reads, 61 s apart (23:45:46Z and 23:46:47Z), both read `firmware v19 | 'blipscope-s3-128 · v19 · cloud'`.
  That is **S19**.
- `--check-tool`: esptool 5.2.0, tree `84c878702a70…`, `CHECK-TOOL PASSED`.
- `--selftest`: `SELFTEST PASSED` (16 ok). It ran again right before B5, and both passed again.

**An instrument slip, harmless (recorded as such by the reviewer).** At 23:47:07Z a B3 launch passed its arguments joined
by `_`. `app-only-write` exited **2 (usage) before any esptool call**, and a live read right after it showed v19.
`py_child.ps1` now joins on `::`. It was proven with an argv-printing script (argc 4) before the real launch.

**The writes and boots: all as predicted.**

| image | write (`app-only-write`) | N-BOOT (network) |
|---|---|---|
| B3 | `checks ok: table matches (6 entries), board boots ota_0, image 1965808 B`; attempt 1 verified; reset ok; ended 23:48:35Z | framebench env at +18 s |
| B4 | the same, at `1971520 B`; ended 23:56:03Z | at +10 s |
| B5 | the same, at `1967808 B`; ended 00:43:20Z | at +10 s |
| v19 (restore) | the same, at `1931840 B`; ended 00:52:47Z | see RESTORE |

**The traces.**
- **B3:** a 6-min trace, `VERDICT: clean`, exit 0. Raw seal `4f6a977898c7`.
- **B4:** a 40-min trace that **ENDED ABNORMALLY at about 00:21:10Z**, 24.5 min in.
  - The child left no rc file, no end or verdict line, and no READ ERROR. Its processes were gone, and the board fell
    into the ROM (a closed port).
  - **The reviewer recorded it as an INSTRUMENT FAILURE: the trace child was killed from outside.** The most likely
    mechanism is a closed console tab: a WMI child launched without `CREATE_NO_WINDOW` is handed to Windows Terminal as
    a visible tab. That is not proven.
  - Raw seal `846df63f2800`.
- **B5:** an 8-min trace, `VERDICT: clean`, exit 0. Raw seal `915d2acfd206`.

**The rows.**

| row | read | verdict |
|---|---|---|
| R-CLEAR (B4) | no `[speed-p5]` line by the trace's end (CLEAR_CHECK was due about 00:30:35Z) | **VOID**, an instrument failure. No re-run in this window; it goes to addendum 2's window. |
| R-LABELS (B5) | `[speed-p6] labels S0 checked 232672 mismatches 0` (00:48:30Z), exactly one line | **AS PREDICTED** |
| H-TLS | `tlsOk=1` on all 20 graded lines of B3 and B5 | as predicted, for those two images |
| H-NOISE | `free8` spread over the 10 graded lines: B3 3,672, B5 2,356 (allowance 2,048) | **FIRED: UNTRUSTWORTHY at this scale** |
| H-NOISE-PS | `psram_free` spread: B3 67,592, B5 69,016 (allowance 4,096) | **FIRED: UNTRUSTWORTHY** |
| H-P6 | medians B5 − B3: `free8` −44, `heap free` −44, `psram_free` **−14,240** | **no verdict** (the noise rows fired). The values are recorded only. |
| H-P5 | B4's 50 heap lines stay SEALED, as the reviewer ruled | not graded |
| H-LARGEST | medians: B3 18,420, B5 29,684 | recorded, not graded |
| RESTORE | `firmware v19 | 'blipscope-s3-128 · v19 · cloud'` at 00:53:17Z and 00:54:18Z, 61 s apart, at +30 s and +91 s after the write | **AS PREDICTED**: S19 matched twice, Wi-Fi rejoined without the portal |

**What it means.**
- **P6's label cache equals a fresh layout on the device:** 0 mismatches over 232,672 checks. That is the
  merge-relevant equality row for P6.
- **P5's dirty restore (CLEAR_CHECK) is still unread on a device.** It needs addendum 2's window.
- **The heap instrument's first lines carry a start-up transient.** Both images' first one or two `[health]` lines
  read before the heap settles, then the values sit flat (B3's free8 at 75,764-75,784; B5's at 75,732).
  - So "the first 10 lines after attach" is too early a sample. The frozen escape caught exactly that, and no band
    was moved.
  - H-P6's psram median, −14,240, is the derived center to the byte (40 entries × 356 B). It is recorded, NOT
    graded: a value read through an instrument its own noise row refused is not evidence.
  - An after-transient sample is a NEW registration in addendum 2, not a re-grade of this one.
- **The launcher fix**, also for addendum 2: WMI children launched with `CREATE_NO_WINDOW`, proven hidden by a dry run
  (a new console count of +0, against a control's +1) before any window.

**Cleanup:** E2:08 was released and is on v19. 0 window processes were left, and bgcheck read PASS with 0 running.

### 6a. S1's status moves (p4, 2026-11-17 00:49Z), outside the window

The gains addendum's S1 row is drawn == the scene's level on every measured frame, a STOP row (4c, `aa006602280e`). It
is now **implemented with a red plant, and not yet run on a board**.
- p4's A0a reader landed at `tools/frameprofile/a0a_read.py` on `bench/frame-profile-p1-final`, commit `ab7ddec8981d`
  (blob `6fdfa05938c6`), as its row I-4 FIX-n: every measured frame's drawn (Rec byte 5) equals the level.
- Its plant, "one frame drawn 19 at level 20", reads `STOP at I-4`. The clean control reads `READ AS GRADED`: 14 of 14
  cases as predicted.
- No A0a window has happened yet, so the gains addendum (section 3) still waits on an A0a reading.

### 7. Board addendum 2: the anchor, the scratch self-check, the margin and the heap, re-run (registered 2026-11-17 ~01:3xZ, before any code)

**What the reviewer ruled (01:1x-01:2xZ), and the one choice left to me.**
1. **The device anchor runs as two sub-scenes, ANCHOR_BG and ANCHOR_8COPY.**
   - ANCHOR_BG serves an EMPTY table, so its grab IS the device's own background.
   - ANCHOR_8COPY serves level-L contacts with today's 8-copy outline.
   - **Both scenes:**
     - run at the pinned table time kTblT, so the contacts hold still;
     - have `FrameBench::Override` turn wx-layer, the sweep's key and every `info-*` key off (the map is already
       bench-off).
   - **The criterion:** the host composites its port's `RenderOld` onto the BG grab. ANCHOR_8COPY's grab must equal
     that composite **pixel-exact over every marker's footprint dilated by 2 px**.
2. **The scratch self-check:** 128 bytes compared against a host-generated table.
3. **The margin probe, option (i), my choice.** It runs ONLY in a dedicated **MARGIN scene, placed LAST in every image**.
   - It never runs during a heap-graded or frame-timed scene.
   - No image in this addendum carries gains scenes, so A0a's comparisons are untouched.
4. **The heap sample:** skip every `[health]` line received < 120 s after attach, then grade the next 10. This is a NEW
   rule for a new run; tonight's section 6 is re-graded by nothing.
5. **The images:** B3', B4' and B5', built anew. **B4' is a different image from section 6's B4.** So its CLEAR_CHECK is
   a NEW reading, and section 6's R-CLEAR stays VOID.
6. **The launcher:** map-client's branches (i)-(v) for `CREATE_NO_WINDOW`. Its line is frozen in a later section, once
   map-client's numbers are read. No window starts before that.

**A consequence I am registering rather than hiding: comparable heap needs identical scenes.**
- 5a's bands assume B4 − B3 and B5 − B3 differ only in statics. That holds only if the graded `[health]` lines come
  from the SAME scene kind in every image.
- So every image starts with **two C1 scenes** (C1 is the bench's own scene: weather on, sweep on, labels on). They
  cover boot + 90 s to boot + 530 s, which spans the graded window (attach + 120 s to attach + 420 s, attach being about
  boot + 20 s).
- The cost: the window is about 66 min, not 45.

**The scene lists.** Scenes start 90 s after boot, then 100 s settle and 120 s measure per scene, as now.

| image | branch | scenes | its rows end |
|---|---|---|---|
| B3' | `bench/speed-base` (new, from `a0583e88b142`) | {C1, C1, MARGIN} | MARGIN by boot + 750 s |
| B4' | `bench/speed-p5` | {C1, C1, ANCHOR_BG, ANCHOR_8COPY, CLEAR_CHECK, MARGIN} | CLEAR_CHECK at about boot + 1,190 s; MARGIN by boot + 1,410 s |
| B5' | `bench/speed-p6` | {C1, C1, LABELS_CHECK, MARGIN} | LABELS_CHECK at boot + 750 s; MARGIN by boot + 970 s |

**The code plan.** Bench branches only; nothing is PR'd.
- **A base commit on `bench/speed-base`, cherry-picked onto `bench/speed-p5` and `bench/speed-p6`:**
  - the MARGIN kind;
  - its probe, which runs only while a MARGIN scene measures: the largest L in [16,717, 196,608] at 64 B steps with
    `heaphealth::CanAllocate(L)`, printed `[speed-a2] heap margin S<k> p<i> <L − 16717>`, with 3 probes 30 s apart;
  - **a NEW tag, `[speed-a2]`,** because the probe runs in all three images.
    - `[speed-p5]` must stay P5-only for I-4, and `[frame-bench]` is withheld whole by the seal.
    - `[speed-a2]` joins `check-no-bench-hooks.sh`'s MARKERS (I-4) and `trace_filter.py`'s pass-whole list.
    - The filter change is its own commit, with a selftest row (a `[speed-a2]` line passes whole; a `[frame-bench]` line
      is still withheld) and a new sha, before the window.
  - the per-image A2 scene lists.
- **On `bench/speed-p5` only:**
  - **The two ANCHOR kinds:**
    - the pinned time;
    - the Override keys;
    - an empty table for ANCHOR_BG;
    - at each anchor scene's settle start, one `[speed-p5] anchor S<k> c<i> x <x> y <y> trk <deg×10> cat <c>` line per
      drawn contact, in draw order, screen pixels only.
  - **The scratch self-check at boot:**
    - a fixed pattern drawn into the 32 × 32 1-bit sprite through the mask's own calls;
    - its 128 bytes compared against a table generated by the host port;
    - printed `[speed-p5] scratch bytes 128 mismatches <m>`.
- **The host side:**
  - The port's `OldContact`/`RenderOld` moves VERBATIM out of `test_p5_outline.cpp` into one header, shared by that test
    and by a new `tools/speed-p5p6/anchor_compare`.
  - `run.sh`'s test_p5_outline counts must read identical before and after the move. That proves the move changed
    nothing.
  - The scratch table's generator is a host program over the same port.
- **L for the anchor** is chosen BEFORE the build, from the host table at kTblT: the smallest L in 20..60 with ≥ 1
  overlapping pair AND ≥ 1 marker clipped by the disc edge (20 if it qualifies). It is written into a later section,
  with its contact list.

**Predictions: the host side, red first, each before its real input.**

| row | predicted |
|---|---|
| A-CMP control | a composite that differs from the device frame in 1 pixel inside the region reads mismatch **1** |
| A-SHIFT plant | one host marker shifted 1 px reads mismatch **> 0** |
| A-SELF | the port composited onto a synthetic BG, compared with itself, reads mismatch **0** |
| A-REGION | a pixel 3 px outside every footprint is NOT compared (changing it reads 0) |
| PORT-MOVE | test_p5_outline's printed counts are identical before and after the header move |
| S-TABLE plant | the device-side comparator, host-compiled, with one bit of the table flipped, reads **1** |

**Predictions: the builds** (compile-only, in a hidden WMI PowerShell child, the tool versions recorded).
- All three link.
- I-4:
  - `[speed-a2]` is in all three of B3', B4' and B5';
  - `[speed-p5]` is in B4' only (the anchor, scratch and clear strings);
  - `[speed-p6]` is in B5' only;
  - `[frame-bench]` is in all three.
- The `.dram0` sections are read by section with `objdump -t`, never by nm letter (11-16's ruling).
- **The heap bands** for this run are DERIVED from these ELFs by 5a's method (the section deltas plus gdb sizes plus
  the TLSF rule) and frozen in a section BEFORE the window. They are not guessed now.

**Predictions: the window** (E2:08, the 11-12 terms, everything else as sections 5 and 5b).

| row | image | predicted |
|---|---|---|
| R-ANCHOR | B4' | ≥ 1 overlapping pair and ≥ 1 edge-clipped marker read from the anchor lines (else **VOID**). Then `/diag/fb` 3 grabs per scene, each `Torn: false` and byte-equal within its scene, and the indicator's pixels present. Then **mismatch 0** between ANCHOR_8COPY and the port composited onto ANCHOR_BG, over the 2-px-dilated footprints. |
| R-SCRATCH | B4' | exactly one `[speed-p5] scratch bytes 128 mismatches 0` |
| R-CLEAR' | B4' | exactly one `[speed-p5] clear S4 frames <n> mismatched-px 0`, n ≥ 100. A NEW reading. |
| R-LABELS' (control) | B5' | exactly one `[speed-p6] labels S2 checked <n> mismatches 0`, n ≥ 100 |
| R-MARGIN | all | 3 probes per image, every margin **> 0**. The values and B4' − B3' are recorded, not graded. |
| H-MARGIN-APART | all | every graded `[health]` line precedes its image's first `[speed-a2] heap margin` line (asserted from the timestamps) |
| H-NOISE, H-NOISE-PS | all | as 5a, on the after-transient sample. Firing means no verdict, as before. |
| H-P5', H-P6' | B4', B5' | the bands derived from the ELFs (above), frozen before the window |
| RESTORE | | S19 twice, 60 s apart, as section 5 |

**The grabs' timing.** It is relative to the boot that N-BOOT confirms, and it never reads a sealed line.
- **ANCHOR_BG** runs boot + 530..750 s. Grab at + 650, + 670 and + 690 s, all in its measure part.
- **ANCHOR_8COPY** runs boot + 750..970 s. Grab at + 870, + 890 and + 910 s.
- A grab that is torn is retried once, 5 s later. A second torn grab VOIDs the row.

**The window:** step 0 (3 min), B3' (about 15 min), B4' (about 26 min), B5' (about 18 min), then the restore (4 min).
That is **about 66 min, with the hard end at start + 85**.
- Earliest start: the images built and identified, the heap bands and L frozen, the launcher's line frozen (after
  map-client), and the reviewer's acceptance.
- It is booked by the reviewer, outside Tuesday's daylight E2:08 bookings.

**Outside the set**, as sections 5 and 5a: stop, report, and no fix before a ruling. Plus:
- a trace that ends without its end lines means VOID and an immediate restore (the 11-17 ruling);
- anything none of these covers means the instrument is unreliable at this scale: stop and decide.

### 7a. The launcher's line, frozen from map-client's numbers, and my own dry run's predictions (2026-11-17 ~01:2xZ, before the run)

**map-client's measurement**, read at 01:07-01:09Z on this machine, three launchers each, interleaved with controls:
- `Win32_ProcessStartup.CreateFlags = CREATE_NO_WINDOW` (0x08000000) is **rejected**: `Create` returns 21 and nothing is
  created.
- **`ShowWindow = [uint16]0` works:** ReturnValue 0, **+0** OpenConsole/WindowsTerminal processes, and the child's
  redirected log written.
- The flagless control reads **+1** every time, so an unflagged WMI child gets a visible tab.
- **That proves the mechanism EXISTS. It does not prove that it killed section 6's B4 trace,** which stays "most likely,
  not proven".

**The line.** Every WMI child is created with `ProcessStartupInformation = New-CimInstance -ClassName
Win32_ProcessStartup -Namespace root/cimv2 -ClientOnly -Property @{ ShowWindow = [uint16]0 }`.
- Any nonzero ReturnValue is refused with exit 10: nothing is launched, and no waiter is left to time out.
- The launcher keeps a `-Visible` switch for the control below ONLY.
- The files and their sha256: `launch.ps1` `f007aa63b5c9`, the harness `console_dry.ps1` `56b81883c9f7`, the child
  runner `py_child.ps1` `b7d63fc7636f`.

**The dry run, my launcher's own shape** (`cmd /c powershell -File py_child.ps1`, running an argv-printing script).
- Each run counts NEW OpenConsole/WindowsTerminal process ids for 8 s after `Create`. map-client's grading: conhost
  counts are noisy.
- It then waits up to 30 s for the child's rc and reads its log.

| run | predicted |
|---|---|
| HIDDEN-1, HIDDEN-2 | launch exit 0 (ReturnValue 0); new OpenConsole/WT ids **0**; log `argc 2`; rc 0 |
| CONTROL (`-Visible`) | launch exit 0; new ids **≥ 1**; log `argc 2`; rc 0. It proves the counter can see a tab. |
| BAD-DIR (a non-existent `CurrentDirectory`) | ReturnValue nonzero; launch **exit 10**, `REFUSED`; no log; rc none |

**Outside the set** means stop, report, and use no WMI child for a build or a window until it is ruled.

### 7b. The launcher's dry run, read (2026-11-17 01:14-01:16Z)

| run | read | against 7a |
|---|---|---|
| HIDDEN-1 | ReturnValue 0; new OpenConsole/WT ids **0**; log `argc 2`; rc 0 | as predicted, except the launch-exit field (below) |
| HIDDEN-2 | the same | the same |
| CONTROL (`-Visible`) | ReturnValue 0; new ids **1**; log `argc 2`; rc 0 | as predicted: the counter sees a tab |
| BAD-DIR | `REFUSED: Create returned 8`; launch **exit 10**; no log; rc none | as predicted |

**One field outside its wording, stated rather than folded in.** 7a predicted "launch exit 0" for the three successful
launches, and all three read it **EMPTY**.
- The launcher's success path ran off the script's end without `exit 0`, so PowerShell leaves `$LASTEXITCODE` unset.
  `bench-trace.ps1` documents the same trap.
- ReturnValue 0 and the children's rc 0 still showed the launches succeeding. The empty field is an instrument slip,
  not a reading against the launcher.
- **The fix** is one explicit `exit 0` at the end of `launch.ps1`, which is now sha256 `d04a1c87f589`.
- **Re-proven with that exact file** (01:15:58-01:16:29Z):
  - HIDDEN-3: ReturnValue 0, launch **exit 0**, new ids **0**, `argc 2`, rc 0;
  - BAD-DIR-2: exit 10, REFUSED.

**So the launcher every build and window uses is `launch.ps1` `d04a1c87f589`:** hidden by `ShowWindow = 0`, refusing a
nonzero ReturnValue with exit 10, and exiting 0 explicitly. bgcheck read PASS (0 running) after both runs.

### 7c. The seal gains `[speed-a2]` (2026-11-17 ~01:3xZ), its own commit as section 7 ruled

`tools/speed-p5p6/trace_filter.py` now passes `[speed-a2]` lines whole (addendum 2's margin probe). Everything else it
withholds is unchanged. Its sha256 moves from `eb31148e50ec` to `6741186f03b0`.

- **Selftest: 30 ok, 0 FAIL.** One row was added: a synthetic `[speed-a2] heap margin S2 p0 50048` line passes whole.
  The printed count is now exactly 11.
- **Three sabotages, each red:**
  - HEALTH-WHOLE (`[health]` passed whole): 7 FAIL;
  - FB-PASS (`[frame-bench]` passed): 3 FAIL;
  - **A2-DROP (`[speed-a2]` removed from the pass list): 3 FAIL.** The keep row fails, the withheld counts change,
    and 10 lines are printed instead of 11.

Section 6's traces were filtered by `eb31148e50ec` and stay so. The new sha applies to addendum 2's window only.

### 7d. The anchor, ruled (2026-11-17 ~01:3xZ) and its input format frozen (~01:5xZ), before any device code

**The reviewer's rulings on the four premises my reading of the code found false.**

| premise | what the code does | ruled |
|---|---|---|
| A1: the anchor runs with wx-layer OFF | the 8 copies draw only when `WxOutlineNow()`, which is `WxEnabled()`, so with the layer off the anchor tests no outline | **(1): wx-layer ON** in both anchor scenes. The fixed one-cell field is rebuilt identically in both, so it cancels in the composite, and the outline is drawn by today's production path. (2), forcing `WxOutlineNow()`, is REFUSED: it is a bench branch in a function with other callers. |
| A2: at the pinned time the contacts hold still | the table is served at `time(nullptr)`, and `PredictPosition` dead-reckons every contact by its ground speed from `millis()` | **ACCEPTED:** in the anchor scenes `OnCloudPoll` computes positions at `kTblT` and serves **gs = 0**, plus the A-STILL row |
| A3: the anchor has an edge-clipped marker | at the 25 mi rung the table's 1..8 mi radii put every contact within ~38 px of the centre | **(a):** the anchor scenes override `radius` to a small rung, with L and the rung chosen on the host and frozen before the window. (b), the edge untested on glass, is the fallback only if (a) cannot be met, named as such. |
| A4: the compared region holds only the outline and the marker | 8 kinds of per-contact overlay are drawn after the marker | **ACCEPTED:** each anchor line carries `ovl`, and anchor_compare refuses on a nonzero bit |

**The correction to section 7, written here rather than edited there.** Section 7 item 1 says `FrameBench::Override`
turns wx-layer off in both anchor scenes. **That is superseded for the anchor scenes by ruling (1): wx-layer is ON.**
With it off, the row tested nothing. Everything else in item 1 stands.

**A2, stated as the reviewer asked.**
- `OnCloudPoll` in an anchor scene computes each row at `kTblT` and serves its ground speed as **0**. The wire's `t`
  stays `time(nullptr)`, as today, so the stale ladder is untouched. The track is still served, so the marker's heading
  is unchanged.
- `PredictPosition`'s displacement is velocity × dt, so it is 0.
- **What I read:** velocity feeds the radar draw only through FAST (highlight, which is overridden off). It is also read
  by `velocity > 1` checks outside the radar loop.
- **If gs = 0 changes any OTHER drawn element inside a footprint, it shows as a mismatch:** the alarming direction,
  never a false PASS.

**The Override set in the two anchor scenes** (`nullptr`, the board's own setting, everywhere else):

| key | answer | why |
|---|---|---|
| `wx-layer` | `true` | ruling (1) |
| `scanline` | `false` | the sweep off: blip brightness 1, nothing repainted between grabs |
| `infotext`, `trail`, `highlight`, `lookup` | `false` | A4's overlays that have a switch |
| `radar-up` | `0` | **ADDED, for acceptance:** the host's L/rung pick projects with up 0, so the device must too |
| `radius` | the frozen rung | A3 (a) |
| `triangle` | `true` | **ADDED, for acceptance:** so the anchor tests the darts (kTri + kLine), not dots only |
| `latitude`, `longitude`, `radius-unit`, `data-source`, `local-*` | as every scene | unchanged |

**The anchor line, frozen.** One summary line per print, then one line per drawn contact, in draw order:

    [speed-p5] anchor S<k> p<n> contacts <m>
    [speed-p5] anchor S<k> p<n> c<i> x <x> y <y> trk <t> cat <c> dot <0|1> up <u> col <hhhh> ovl <hhh>

- `p0` and `p1` are the two prints; `c<i>` counts from 0 in draw order.
- `x`, `y` are the screen pixels the marker is drawn at. They are not coordinates.
- `trk` is `lroundf(trueTrack × 10)`; `up` is `radarUpDeg`; `cat` is the emitter category; `dot` is 1 when
  triangles are off.
- `col` is the RGB565 that LovyanGFX's own rgb888 → rgb565 conversion gives for `markerColor`, as 4 hex digits.
  - The device commit names the function. It must be the one the 16-bit backbuffer's writer uses.
  - The host draws with `col`. A conversion that is not the writer's shows as a mismatch on every marker pixel.
- `ovl` is 3 hex digits, the device's OWN predicates for this contact at this frame, one bit each:

| bit | overlay | the device's predicate |
|---|---|---|
| 0 | special diamond | `SpecialClassOf(tracked) != None` |
| 1 | NEW flag | `logbookEnabled && tracked.claimable` |
| 2 | overhead | `showOverhead && IsOverhead(tracked)` |
| 3 | highlight | `displayHighlight` and the contact is NEAR, HIGH or FAST |
| 4 | watchlist | `MatchesWatchlist(tracked)` |
| 5 | follow | the effective target is set and `MatchesFollow(tracked)` |
| 6 | pin | `icao == pinnedIcao` |
| 7 | emergency | `isEmergencySquawk(squawk)` |
| 8 | trail | `displayTrails` (**ADDED**: drawn under the marker, in the footprint) |
| 9 | label | `displayInfoText` (**ADDED**: the label can sit in a neighbour's footprint) |

Bits 8 and 9 are additions to the reviewer's 8, for acceptance. Their switches are overridden off, so the bits make
the override itself observable rather than assumed.

**A-STILL's two prints: a deviation from the ruling's wording, for acceptance.** The ruling says "settle start AND
measure end". I propose **measure start and measure start + 110 s** instead.
- At settle start, the scene's first poll may not have landed. That would put the PREVIOUS scene's contacts, or none,
  in `p0`, and fail A-STILL for a reason that is not motion.
- Measure start is 100 s of settle later. The second print at + 110 s is still in the measure, after the last grab, and
  before the scene switch can change the table under it.
- In boot terms:
  - ANCHOR_BG: `p0` at + 630 s, grabs at + 650/670/690 s, `p1` at + 740 s;
  - ANCHOR_8COPY: `p0` at + 850 s, grabs at + 870/890/910 s, `p1` at + 960 s.
  - So both prints bracket all three grabs.
- **A-STILL:** `p0` and `p1` are identical except for the pass tag, the summary line included.

**anchor_compare's file mode, frozen** (`tools/speed-p5p6/anchor_compare`, on `bench/speed-p5`):

    anchor_compare <bg.fb> <8copy.fb> <anchor-lines> <S_bg> <S_8copy>

- `<anchor-lines>` is the seal's printed output, which passes `[speed-p5]` whole. Nothing else is read.
- **Exit 0 = PASS:** every precondition below holds, and the composite matches over the region with **mismatch 0**.
- **Exit 1 = FAIL:** the preconditions hold, and the mismatch is > 0.
- **Exit 2 = VOID, a refusal, with its reason printed, if:**
  - a line is malformed, or a grab is not 115,200 bytes;
  - A-STILL fails in either scene;
  - `S_bg`'s prints carry any contact;
  - `S_8copy`'s carry none;
  - any `ovl` is nonzero;
  - any `trk` is not a multiple of 10 (the wire's track is whole degrees, so this means the input was not the bench's);
  - any `up` is not 0;
  - there is no overlapping pair (two contacts' 8-copy footprints share a pixel);
  - there is no edge-clipped contact (a primitive of its marker, at any of the 9 positions, reaches past 0..239).
- The 3 grabs per scene must be byte-equal and `Torn: false`. The window's own step checks that (section 7's
  R-ANCHOR) and hands this tool one grab per scene.

**The host side, read (on `bench/speed-p5`, local until B4' compiles).**
- **PORT-MOVE, `27f1a1452f86`:** the move left test_p5_outline's whole output byte-identical (sha256 `1b4ddcc52dc6`,
  8 ok).
  - The move also exposed a second path: sabotage_p5_outline.py copies a fixed file list into its clean tree, so
    without the new header the copy did not compile (RIG exit 2).
  - That list is fixed. The driver now reads all 5 plants as predicted, its control 8 ok, and its anchors 5/5.
- **The compare core, `e98166743f70`:** rows A-565, A-BE, A-SCENE (control), A-SELF, A-CMP, A-SHIFT, A-REGION and
  A-REGION-IN.
  - The registered A-REGION is split into a pixel at distance exactly 3, which must read 0, and one at exactly 2,
    which must read 1. Without the second half, a region narrower than registered could pass.
  - `Composite` is RenderOld's loop without its `Background()` repaint, named in the header as the second path.
  - A-SELF builds its device frame by a separate route, so the repaint plant reads red.
  - **6 plants, each red in exactly its frozen set:**
    - WHOLE {A-REGION};
    - DIL1 {A-REGION-IN};
    - BLIND {A-CMP, A-SHIFT, A-REGION-IN};
    - REPAINT {A-SELF, A-CMP, A-REGION, A-REGION-IN};
    - 565 {A-565};
    - LE {A-BE}.
  - Control: 8 ok (A-SELF over a region of 616 px; A-SHIFT reads 36).
  - **Stated, not folded in:** the first two driver runs stopped at RIG exit 2, because the WHOLE and BLIND plants left
    a parameter unused under `-Werror`. The control was therefore READ before the plants had all run. The sets were
    frozen in the driver before either ran, and they are unchanged.
- **The scratch self-check, `84a3b1306e2f`:**
  - `include/OutlineScratchCheck.h` holds the pattern, the comparator and the table.
    - The pattern is integer vertices of every primitive kind, so no libm. The anchor checks the headings.
    - The comparison runs over the scratch's own `Row()`, which is what DrawEdge consumes, so it covers the 1-bit
      raster AND the bit reversal.
  - `tools/speed-p5p6/scratch_table.cpp` generates the table.
  - **Red first:**
    - on the placeholder table, red in exactly the predicted {S-SELF, S-TABLE, S-TABLE-FIRST} (82, 83, 83);
    - then the emitted table;
    - then 4 plants, each red in exactly its set: NOCOUNT, HALF, STALE and BYTEORDER.
  - Control: 5 ok. **S-TABLE, the registered plant, reads 1.**

**Next, in order, none of it on a board:**
1. The L/rung pick: a host program over `radarproj::Project` and `fbtable::At` at `kTblT`, up 0, with the device's
   rung → `rLat`/`rLon` arithmetic ported. Its result (the rung, L, and the contact list in screen pixels) is frozen in a
   later section.
2. The device code on `bench/speed-p5`:
   - the ANCHOR kinds and the Override set above;
   - `OnCloudPoll`'s `kTblT` + gs 0;
   - the prints;
   - the scratch check at boot;
   - B4''s scene list.
3. anchor_compare's file mode, red first against synthetic lines.
4. B4' built compile-only, I-4, and the heap bands derived from the three ELFs and frozen.

### 7e. The anchor's rung and level, picked on the host and frozen (2026-11-17 ~01:5xZ), before the device code

**The rule, pre-registered and accepted by the reviewer before it ran:**
- for R = 8, 7, 6, 5, 4 mi in that order, take the first R where some L in 20..60 has ≥ 1 overlapping pair AND ≥ 1
  edge-clipped contact with ≥ 1 on-screen pixel;
- at that R, the smallest such L;
- if no R qualifies, A3 (b), named.

Both definitions are `anchor_core.h`'s `OverlapPairs` and `EdgeClipped`, which the file mode uses too.

**Why an edge clip is reachable at all, as the reviewer asked.** `src/CloudFeed.cpp`'s row parse applies no box
filter, and the radar loop culls only when zoomed in (`ZoomCulled`). So a row beyond the radius box still reaches the
draw loop, and the 240 × 240 backbuffer clips it.
- At the bench's 25 mi rung no row gets near the edge.
- At a small rung, some do. That is what A3 (a) relies on.

**The port** (`tools/speed-p5p6/anchor_pick.cpp`, `bench/speed-p5` `29be96ef520d`):
- `fbtable::At` at `kTblT`;
- the wire's `lat4 / 1e4f`;
- `radLat`/`radLon` from R × 1.609344 / 111.0 and `cos(lat × DEG_TO_RAD)`;
- the view factor exactly 1.0 at the top of the zoom ladder;
- `radarproj::Project` at up 0.

The home and `kTblT` are READ from `include/FrameBench.h`, and the home is not printed. The run's output has sha256
`f66b9ca25527`.

| R | result |
|---|---|
| 8 mi | no L in 20..60 qualifies |
| **7 mi** | **L 20 qualifies:** 20 contacts, 19 with an on-screen pixel, 2 overlapping pairs, 1 edge-clipped |

**FROZEN:**
- the anchor scenes override `radius` to **7** (mi);
- **ANCHOR_8COPY serves level 20**;
- ANCHOR_BG serves an empty table by its KIND, not by a level of 0, because the scene check refuses a level below 1.

**The contacts, as the host predicts them** (table order; screen pixels; all `cat 4`, the table's A3):

| c | x | y | trk ×10 | cat | on screen | edge-clipped |
|---|---|---|---|---|---|---|
| c0 | 102 | 119 | 0 | 4 | 1 | 0 |
| c1 | 209 | 106 | 3510 | 4 | 1 | 0 |
| c2 | 117 | 165 | 2730 | 4 | 1 | 0 |
| c3 | 222 | 181 | 310 | 4 | 1 | 0 |
| c4 | 127 | 46 | 960 | 4 | 1 | 0 |
| c5 | 114 | 92 | 2580 | 4 | 1 | 0 |
| c6 | 221 | 120 | 1800 | 4 | 1 | 0 |
| c7 | 99 | 172 | 1120 | 4 | 1 | 0 |
| c8 | 130 | -9 | 950 | 4 | 0 | 0 |
| c9 | 77 | 47 | 2400 | 4 | 1 | 0 |
| c10 | 116 | 81 | 850 | 4 | 1 | 0 |
| c11 | 23 | 61 | 2110 | 4 | 1 | 0 |
| c12 | 154 | 177 | 2390 | 4 | 1 | 0 |
| c13 | 98 | 119 | 1800 | 4 | 1 | 0 |
| c14 | 100 | 213 | 2820 | 4 | 1 | 0 |
| c15 | 74 | 140 | 1560 | 4 | 1 | 0 |
| c16 | 107 | 242 | 2760 | 4 | 1 | **1** |
| c17 | 197 | 109 | 3520 | 4 | 1 | 0 |
| c18 | 105 | 91 | 620 | 4 | 1 | 0 |
| c19 | 162 | 217 | 660 | 4 | 1 | 0 |

**Overlapping pairs:** (c0, c13) and (c5, c18). **Edge-clipped:** c16, centred 2 px below the bottom edge. c8 is drawn
wholly off screen: it has a line and no pixel.

**The margin, stated: ONE clipped contact and TWO pairs.**
- c16's y comes from its latitude and `radLat` alone, with no `cos`.
- The table's own wire values are checked against the host on the board at boot (TBL-DEV's `sum` at `kTblT`).
- The two pairs are 4 px and 9 px apart.

**What this predicts on the board: recorded, NOT graded (row A-PREDICT).**
- The device's S3 prints carry exactly these 20 `(x, y, trk, cat)`, in the device's own draw order, each with `dot 0`,
  `up 0` and `ovl 000`.
- Its S2 prints carry 0 contacts.
- **If the device's set differs, R-ANCHOR still grades on the device's OWN lines.** anchor_compare re-derives the
  overlap and the clip from them. The difference is recorded and reported, not graded.
- **Outside the set:** fewer than 20 contacts (a filter I did not find), or no clip, or no pair in the device's lines,
  makes R-ANCHOR **VOID** by its own clause. It then comes to the reviewer, and the fallback is A3 (b), named.

**Next:** the device code on `bench/speed-p5`, as 7d lists it, with radius 7 and level 20 above.

### 7f. B4' built and identified; the heap bands derived from the three ELFs and frozen (2026-11-17 ~02:1xZ), before the window

**B4', the device code of 7d/7e** (`bench/speed-p5` `c33529a41f3d`, a clean tree):
- **Build:** compile-only, in a hidden WMI child (`launch.ps1` `d04a1c87f589`), env `blipscope-s3-128-framebench-c1b`,
  flags `-DSPEED_P5_BENCH -DSPEED_A2_BENCH`. rc 0 after 202 s.
- **Versions:** PlatformIO Core 6.1.19, platform 55.03.39, esptoolpy 5.3.0.
- **Warnings:** AircraftManager.cpp's three are the same three B5''s log carries (`addrRowTop`, `live`, a `%02d`
  truncation). None comes from the new code.

| image | branch @ commit | bin | sha256 | `.dram0.data` | `.dram0.bss` | I-4 names |
|---|---|---|---|---|---|---|
| B3' | `bench/speed-base` | 1,966,048 B | `54fc5e28142a` | 36,432 | 44,032 | [frame-bench], [speed-a2] |
| **B4'** | `bench/speed-p5` `c33529a41f3d` | **1,973,552 B** | **`17d9b2df11cc`** | **37,040** | **49,184** | **[frame-bench], [speed-p5], [speed-a2]** |
| B5' | `bench/speed-p6` | 1,968,080 B | `7472e902863c` | 36,432 | 44,040 | [frame-bench], [speed-p6], [speed-a2] |

**I-4, as predicted:** `[speed-a2]` and `[frame-bench]` are in all three; `[speed-p5]` is in B4' only; `[speed-p6]` is in
B5' only.

**The shipping run that must precede any push:** `bench/speed-p5`, env `blipscope-s3-128`, no flags, in a hidden child.
rc 0 after 185 s, with the same three warnings. **`check-no-bench-hooks.sh` PASSES on that shipping image:** 12 markers
checked, anchor present.

**B4' − B3', every internal byte named** (`objdump -t`, by section, never by nm letter):

| symbol | section | Δ | what |
|---|---|---|---|
| `appManager` | `.dram0.bss` | +3,880 | P5's sets, the same +3,880 4y read |
| `TheOutlineScratch()::scratch` + its guard | `.dram0.bss` | +348, +8 | the outline scratch |
| `framebench::S()::s` | `.dram0.bss` | **+912** | the anchor's state: 3 ints and 64 × 14-B records (`static_assert`ed) = 908, plus 4 of alignment |
| `DrawSweepOverWeather()::sweepLut` | `.dram0.data` | +612 | P5 1b's table |

- The symbols sum to +5,148 of `.bss` against the section's +5,152 (4 B of section alignment), and to +612 of `.data`
  against the section's +608.
- **The SECTION deltas are what the heap loses:** −5,760.
- **The runtime block 5a did not count:** the 1-bit scratch's buffer.
  - LovyanGFX's `Panel_Sprite::createSprite` allocates `32 × 4 + 1 = 129` B through `heap_alloc_dma`, so
    `MALLOC_CAP_DMA` (internal), rounded to 132.
  - By the TLSF rule, `roundclass(132 + 12) + 4` = **148 B**.
  - B4' allocates it before the graded window: `BenchScratchCheck` at boot + 90 s, and the C1 scenes' outline anyway.
    B3' never does.
  - Section 6's B4 band missed these 148 B. They were inside its allowance. Section 6 is re-graded by nothing, as
    section 7 ruled.

**gdb** (`-batch -nx`, on these ELFs):
- `sizeof(TrackedAircraft)` = 1,036 in B3' and B4', and 1,040 in B5';
- `sizeof(labelcache::Entry)` = 336 in B5'.

These are 5a's readings, unchanged.

**The bands, by 5a's method and allowances** (J = 2,048 internal, J_ps = 4,096 PSRAM, each on both sides):

| row | expected change | band |
|---|---|---|
| **H-P5'** `free8` and `heap free` (B4' − B3') | **−5,908**: the section deltas −5,760, plus the scratch buffer −148. `TrackedAircraft` is unchanged. | **[−7,956, −3,860]** |
| **H-P5'** `psram_free` (B4' − B3') | **0**: the anchor's state is static, the scratch is internal, and no record grows | **[−4,096, +4,096]** |
| **H-P6'** `free8` and `heap free` (B5' − B3') | **−8 to −1,288**, as 5a: `.dram0.bss` +8, down to −1,288 if the map nodes are internal and each steps one class (1,036 → 1,040) | **[−3,336, +2,040]** |
| **H-P6'** `psram_free` (B5' − B3') | **−14,240 to −15,520**, as 5a: 40 entries × 356 B at C1's level 40, down to −15,520 if the nodes are in PSRAM and step a class | **[−19,616, −10,144]** |

- **Where the graded lines come from:** C1 scenes only, in every image (section 7's rule 4: attach + 120 s, then 10
  lines, inside S0..S1).
- No anchor, clear, label or margin code runs in a C1 scene. The anchor prints only while an anchor scene measures.
- H-NOISE, H-NOISE-PS, H-TLS and H-MARGIN-APART stand as sections 5a and 7 froze them.

**The host side since 7e** (`bench/speed-p5` `2b9a52da2e97`, tools only, so B4''s image is unaffected).
- **anchor_compare's file mode, red first:** `--selftest-file`'s 15 rows. `sabotage_anchor_file.py`'s 8 plants were
  each red in exactly their frozen set on the FIRST run, before the control.
- **The control:** 15 ok, and the file path end to end on emitted files reads exit 0 / 1 / 2 (pass, one changed px, a
  grab 1 byte short).
- **The second-path miss, again:** `sabotage_anchor.py`'s copy list lacked the new `anchor_file.h` and read RIG.
  Fixed, and re-run: 6 plants as predicted, control 8 ok.
- That is the second copy-list miss in this addendum, after `sabotage_p5_outline.py`'s at the port move. Both were
  caught by their driver's RIG exit 2, never by a false verdict.

**What the window still needs:** the reviewer's booking (E2:08). Everything 7, 7d, 7e and this section freeze is now in
place.
