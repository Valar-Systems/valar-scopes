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
