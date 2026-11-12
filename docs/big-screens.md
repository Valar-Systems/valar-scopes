# Big screens: the plan

**Status: a plan, no code.** Written 2026-11-12 by the P4 session. The owner ruled **GO** at 17:26Z: "GO for the build,
after v20's content freeze, as planned." So nothing here starts before **v20's content freeze**, and every step
registers its predictions in this file before its code, as docs/p4-40-bringup.md did.

## 1. The screens

The radar on round touch screens, the big ones seen at arm's length:

| screen | panel | diameter | px per mm | mm per px | how it runs | status |
|---|---|---|---|---|---|---|
| 1.28" Kit S3 | 240 | 32.5 mm | 7.38 | 0.135 | device | shipping; **its output does not change** |
| 2.1" Waveshare (`blipscope-pro-s3-21`) | 480 | **53.48 mm, measured** | 8.98 | 0.111 | device | to be sold (the owner, 17:26Z); bench bring-up first; shipping in skus.yml is its own decision, aimed at v21 |
| P4 4.0" | 720 | 101.52 mm | 7.09 | 0.141 | device | bench (docs/p4-40-bringup.md) |
| 7" | 1080 | 172.8 mm | 6.25 | 0.160 | web build | the owner, 17:06Z |
| 15.7" | 960 | ~399 mm | 2.41 | 0.416 | kiosk app / app.valarscopes.com WebAssembly | venue screen |
| 23.6" | 1280 | ~599 mm | 2.14 | 0.468 | kiosk app / app.valarscopes.com WebAssembly | venue screen |

Sources:
- **1.28":** the nominal diagonal, 32.5 mm.
- **2.1": measured by the owner,** 53.48 mm across the lit screen (2026-11-12, ~19:20Z). It replaces the nominal
  53.3 mm used until then. Every 2.1" figure below is computed from 53.48.
- **P4:** Waveshare's display area for the 4-inch module (bench row B1 measures it).
- **7":** 1080 px at 0.16 mm per pixel (the owner).
- **15.7" and 23.6":** docs/web-kiosk-card.md. They agree with the owner's pixel sizes (0.42 and 0.47 mm) to within 1%.

## 2. The design rules (the owner, 17:26Z)

1. **Text: the smaller of the mm target and today's share of the screen, with the 2-px floor.** The target is a 3.8 mm
   cap height (20 arcminutes at 650 mm). Today's share is the 1.28"'s cap height per pixel of diameter: 7.3 px of
   cap on a 240-px disc. The floor: no text stem under 2 px, anti-aliased, smooth fonts only.

   | screen | 240 | 480 | 720 | 1080 | 960 | 1280 |
   |---|---|---|---|---|---|---|
   | smallest cap | 7.3 px, today | 14.6 px (share) | 21.9 px, 3.09 mm (share) | 23.8 px, 3.8 mm (target) | 12.7 px, 5.28 mm (floor) | 12.7 px, 5.94 mm (floor) |

   The font measures a 0.730 em cap and stems of 0.115 to 0.120 em. The floor is computed on the thinnest stem
   (0.115 em), so it holds for every glyph. The stage-1 pictures sized look (iii) assuming a 7.0-px cap per text
   unit, so their 720 and 1080 text is 4% over the 3.8 mm target. The rule above uses the measured cap.

2. **Lines** follow the same shape: the smaller of 0.5 mm and today's 1 px per 240 px of diameter, with the 2-px floor.
   This applies the text rule to lines (p4's reading). The owner's floor already covers them.
3. **Touch**, p4's proposal as adopted: **tap radius = the smaller of 7 mm and 28/240 of the diameter; swipe = the smaller
   of 10 mm and 40/240 of the diameter.** **The numbers are frozen only after a finger session on the real 7" and
   23.6"**: the owner's hands, scheduled through the orchestrator.
4. **The 1.28" output stays unchanged.** Any change to its look goes to the owner separately. The floor never applies to
   it: its lines are 1 px and its smallest stem 1.2 px today.
5. **The 2.1" gets the 1.28" design at exactly 2x.** At the measured 53.48 mm, every rule above picks today's share,
   so every size doubles:
   - cap 14.6 px (1.63 mm);
   - tap radius 56 px (6.24 mm);
   - swipe 80 px (8.91 mm);
   - lines 2 px (0.22 mm).

   That holds **up to 60 mm**, where 7 mm meets 28/240 of the diameter; 53.48 mm is 10.8% under that. The text and
   line rules hold to 120 mm. The 2x equality is a test (section 4), so a glass over 60 mm is caught by it rather
   than discovered.

## 2a. The 2.1's v21 order (the owner, 2026-11-12, ~19:26Z)

**The 2.1 is a v21 SKU; the big screens follow v20's content freeze.** The owner's order for the 2.1:

1. **Speed and swipes first:** touch in its own fast task, then where the frame time goes, then "the Weatherscope's
   method: static layers drawn once and reused". blipscope-5c, on the 2.1.
2. "Then the 2x layout from the big-screens work, the Connect QR included." That is step 7 below.
3. Then F5's vertical timing and the chirp default (blipscope-5c).

This plan takes 5c's item-1 results for the 2.1 rather than measuring it again (steps 5 and 13), and cites them as
5c's once they exist.

## 3. Steps

Sizes are elapsed days for the step's owner, counted from the day it can start. **F** is v20's content-freeze day.

| # | step | owner | waits on | size |
|---|---|---|---|---|
| 1 | **one build route.** Pictures and goldens come only from models built by `build.sh`: `web-960` and `web-1280` (#668), the existing `s3-128` (240) and `s3-21` (480), and `web-720` and `web-1080`, which do not exist yet. The p4 rig's local overrides retire. | blipscope-5c (tools/web-renderer) | #668 merged | 1 |
| 2 | **the units layer.** Each variant states its glass diameter in mm (`variant::SCREEN_DIAMETER_MM`); one header turns the rules of section 2 into pixels, `constexpr` on every device build. No call site changes yet. Section 4's proof rows are registered and run here first. It also lists every face drawn in fixed 240-px rows (the card; the Connect face's `ConnectLayout.h`, found in the 2.1's bring-up, #676; any other a sweep finds), so steps 6 and 7 cover them all. | p4 | F | 2 |
| 3 | **screen size at run time in the web build.** `SCREEN_SIZE` and everything `Layout.h` derives from it become run-time values in the web build only; device builds stay `constexpr`. One WebAssembly file for every web screen. | blipscope-5c | step 1 | 4 |
| 4 | **text by the rule.** Every `setTextSize` call becomes a text role (label, body, title) sized by rule 1; the smooth font in the web build at every size. | p4 | step 2 | 3 |
| 5 | **lines, icons and touch by the rules.** Stroke width as a primitive (wide lines on the device, `lineWidth` in the web renderer), with the floor; `TAP_RADIUS`, `SWIPE_MIN` and the hold drift from rule 3, provisional until step 10. **The touch measurement has moved to the speed plan** (docs/speed-plan.md): samples per swipe against swipe speed, and the touch task itself. S2 covers the 2.1 (blipscope-5c's) and S4 the other boards and the web build. This step only sizes the thresholds. | p4 (blipscope-5c reads the renderer change) | step 2 | 3 |
| 6 | **the List on the whole disc.** Rows sized in mm across the full disc, so more rows on bigger screens, not fixed 240-px rows. | p4 | steps 4 and 5 | 2 |
| 7 | **the detail card and the Connect face on the whole disc.** The card's fixed 240-px rows (title at y = 136, facts from y = 162) become a flow over the whole disc. The Connect face's QR stops being 4 px per module at row 90 with fixed rows below it (`ConnectLayout.h`): on the 2.1 that QR is 148 px, 31% of the disc, and sits high. It scales as the boot setup QR already does (`SetupQr.h`). | p4 | steps 4 and 5 | 3.5 |
| 8 | **radar labels that do not pile up on the big screens.** Placement and de-collision in mm, fuller labels where they fit. Graded by a host sweep (placed against refused, over every start), per CLAUDE.md's instrument rule. | p4 | steps 4 and 5 | 3 |
| 9 | **golden pictures in CI**, every release: the scripted scene (virtual clock, scripted tap and swipe, the stage-1 rig's method) at **240, 480, 720, 1080, 960 and 1280**, from step 1's models. Each golden asserts its pixel minimums from the display list. The 240 golden is section 4's rendering proof and the 480 golden its 2x test. | blipscope-5c (CI, web renderer); p4 writes the scene script | step 1; steps 4 to 8 for the final images | 3 |
| 10 | **the finger session.** The owner's hands on the real 7" and 23.6", with the step-3 build; tap and swipe frozen from it. Each swipe's sample count is recorded beside the thresholds (the speed plan's S4). | the owner, scheduled through the orchestrator; p4 runs it | steps 3 and 5 | 1 session |
| 11 | **the detailed map** (map-server's read (d), 2026-11-12 18:00Z). One web class serves every big screen: the web build sends `class=web`, and the server applies the web ε (75 m, one line width at 1280) and a 384 KB non-terrain cap. Terrain stays at today's 3 levels, upscaled smoothly on the client, because the source has no finer detail. One stored package per point and radius. The device path is untouched and proven byte-identical. Then the integration: the web shim adds `/basemap`, behind the same auth as `/blips`. | blipscope-cb (map-server) for the package; p4 for the integration | the package: part 2's sea code PR on main, F, **the owner's ruling on (d) section 2** (customer-visible), a production deploy slot. The integration: the package | package: 3; integration: 2. Off the critical path if the package starts by F + 14 |
| 12 | **speed on the real 23.6".** Frame time in the kiosk app at rest, in a dense sky and with a card open; static layers (rings, map) cached if it misses. The speed plan's S8 is this same measurement, and its S5 builds the static layers in the shared drawing code, which the web build inherits. | p4, on the owner's hardware | step 3; section 5 | 1 to measure, 2 if caching is needed |
| 13 | **the 2.1" and the P4 on glass.** The 480 and 720 layouts on the boards, with a smooth font on the device. **Frame cost on each board now belongs to the speed plan** (docs/speed-plan.md): S2 for the 2.1, blipscope-5c's, starting from #676's 9.7 frames/s at 480 px; S7 for the P4, measured, not scaled. **A scan-out reading on each board** (F5 of #676). On the 2.1 the picture is **wrapped vertically**: the frame's top rows (N, "100mi", the ring's top arc) appear at the bottom of the glass. blipscope-5c's comparison of the owner's photo with /diag/fb rules out a mechanical offset. The leading hypotheses:<br>- the bounce-buffer refill out of phase. The driver scans two PSRAM framebuffers through a 20-line bounce buffer (`Panel_ST7701_esplcd.cpp`, `BOUNCE_LINES = 20`), so this wraps by a whole multiple of 20 lines;<br>- the porches (`s3_21.h`'s, `LGFX.h`'s `offset_y`, the ST7701's own): any other count.<br>`s3_21.h`'s pixel-clock comment still says there is no bounce buffer; the driver says otherwise, so that comment is stale. **The reading: a numbered-row test frame photographed on the glass, at boot and again under network load.** The wrap's line count separates the two hypotheses, and whether it moves shows whether it is fixed. **/diag/fb reads the backbuffer, not the buffer being scanned, so a centred /diag/fb proves the drawing only, never the scan-out; the photographed numbered rows are the scan-out reading.** The P4 takes the same numbered-row reading: its DSI scans a 720x720 16-bit framebuffer (~1 MB) from PSRAM (porches in `p4_40.h`). Measured and recorded here; any fix is its own step. | p4; the 2.1"'s bring-up owner | each board's bench bring-up first | 2 each |
| 14 | **the owner judges each golden on glass**, every release. | the owner | step 9 | each release |

**Critical path (p4):** units 2 + text 3 + lines and touch 3 + List 2 + card and Connect 3.5 + labels 3 + map
integration 2 + speed 1 to 3 = **19.5 to 21.5 elapsed days** from F. Steps 1, 3 and 9 run alongside on blipscope-5c's
side, and step 11's package on map-server's.

**The date: F + 22** for everything except step 13, which follows each board's bench bring-up. v20's plan
(docs/v20-plan.md, on main since #673) puts F at **November 21-25**, so **F + 22 is December 13-17**. The date moves one
day for each day F moves. **The first review on glass, with the finger session, is at F + 17**: the radar, the List,
the card and the Connect face by the rules, before the map. That is December 8-12.

How the date moved on 2026-11-12:
- **19:13Z, from F + 21 / F + 16 to F + 23 / F + 18:** the 2.1's findings added one day for the touch measurement and
  half a day for the Connect face.
- **The owner's v21 order (section 2a)** added none.
- **~19:45Z, back to F + 22 / F + 17:** the touch measurement moved to the speed plan (docs/speed-plan.md).

**Together with the speed plan, the date depends on who carries its 1.28" and web work** (its section 5): F + 22 if a
second session does (option b), about F + 30 if p4 carries both (option a).

## 4. How "the 1.28" output stays unchanged" is proven

The claim has three parts, and each has its own instrument. Every row runs with a planted control that must fail
before its pass counts. Anything outside the set below is a stop that comes to the orchestrator.

| claim | instrument | passes when | control |
|---|---|---|---|
| **the values**: every rule at 1.28" gives today's pixel literal | a host test over a table taken from today's source (every text size, stroke, tap and swipe constant) | every row equal | one row moved by 1 px fails |
| **the image**: the shipping 1.28" image is unchanged | **#656's comparator** on `blipscope-s3-128` at the base and at the change, built by the C5 method (`SOURCE_DATE_EPOCH`, one worktree path) | the images differ in no byte outside the five rebuild regions (CLAUDE.md), or, where code moved, #656's comparator decides them equivalent | a planted change to one draw constant is reported |
| **the rendering**: what the 1.28" draws is unchanged | the **240 golden** (step 9): pixel-identical to the base, and its display list (every draw call, in order) hashed frame by frame | every pixel and every hash equal | a planted 1-px move changes them |
| **the 2.1" is the 1.28" at exactly 2x** | a host test: every role at (480 px, 53.48 mm, measured) equals twice its value at (240, 32.5) | every row equal | a diameter of 61 mm fails the tap and swipe rows (55.1 and 78.7 px). At exactly 60 mm they still give 56 and 80 px, so 60 cannot be the control |

**Where #656 stands, and the stop it implies.** The units layer is `constexpr` on devices, so the 1.28" is expected to
fold to the same literals and the byte comparison to decide every step. If a step does change 1.28" code (a function
changes size, say), only a layout-invariant comparator can show equivalence. #656 records that the current one cannot
decide a change that alters a function's size; its next step is in progress. So **a step that cannot keep the 1.28"
image byte-identical does not touch the 1.28" build until #656's comparator can decide it.** That is a registered stop,
not something to work around.

## 5. What is needed from the owner

1. **Done 17:26Z:** the go, the text rule, the layout scope, the touch rule, and the 2.1".
   **Still to rule, through the orchestrator:** map-server's read (d) section 2. It covers one web map class for every
   big screen, terrain kept at today's three levels, and a 384 KB non-terrain cap. It is customer-visible, and step 11's
   package waits on it.
2. **The real 23.6" for the speed test (step 12), and the 7" and 23.6" for the finger session (step 10):**
   - one 2 to 3 hour session with each screen on, after step 3 lands, the kiosk app installed;
   - each screen's make and model, its Android and WebView versions, and the kiosk app's version;
   - a USB debugging path (adb), so a performance trace can come from the WebView. If that is not allowed, the speed
     test falls back to an on-screen frame counter, a weaker measurement;
   - each glass's active diameter measured with a ruler, to confirm 172.8 mm and 599 mm;
   - the 15.7" too, if it is to hand (not blocking).
3. **His judgement on glass for each release's goldens** (step 14).

## 6. Risks, named before they happen

- **Run-time `SCREEN_SIZE` (step 3) is the widest change.** Everything `Layout.h` derives is `constexpr` today. In the
  web build each derived constant becomes a second path, in CLAUDE.md's sense. Its proof is the 240 golden holding in
  the web build too.
- **The map package's size is measured; its drawing cost is not.** Per read (d), the web package is 211-241 KB at a
  40 km disc and 311-509 KB at 162 km; today's device package is at most ~211 KB. Its cost: under $0.20 a month at
  600 screens, inside the included allowances. What one frame costs to draw on the 23.6" waits for step 12, which
  comes before the owner's last review, so a slow frame is found by measurement, not by the owner.
- **Deeper terrain is not in this plan.** The z9 source is ~234 m/px at 40 N, and a 1280-px disc at 40 km wants
  62 m/px. Scaling today's levels adds bytes (up to ~5 MB) and no detail. Finer terrain would need a new ingest,
  which is its own decision.
- **A wrapped picture is a layout risk while it lasts.** The 2.1's frame wraps vertically by ~2 mm (~18 px) (F5; not
  mechanical). Any face that draws to the disc's edge (the ring, edge labels, the card's flow in step 7) shows its top
  rows at the bottom. Steps 6 to 8 keep an edge margin until step 13's numbered-row reading names the cause
  (bounce phase or porches).
- **Scan-out from PSRAM is a risk to measure, not a known one.** A larger device panel scans more PSRAM per frame,
  beside the CPU's own traffic: the 2.1 does it through a 20-line bounce buffer, and the P4's DSI scans ~1 MB a frame.
  Whether that holds up is what step 13's at-boot and under-load readings measure. The web screens draw through the
  browser and are not exposed. This sits beside the frame-speed risk below because both are scan-out against CPU
  traffic.
- **A slow frame is also a touch problem.** The 2.1 runs at 9.7 frames/s at 480 px. If touch is sampled once per
  frame (unmeasured, step 5), every screen that draws slowly also misreads swipes. That is why step 5 measures sample
  counts and step 10 records them.
- **Speed on the Android screen is unmeasured.** The stage-1 rig ran 21,600 frames in about 4 s on a desktop, which says
  nothing about the kiosk's WebView.
- **The 2.1"'s 2x holds for a glass up to 60 mm.** The measured 53.48 mm is 10.8% under that, and the test in section
  4 would catch a panel batch over it.

## 7. Where the stage-1 work lives

The picture rig is kept outside every repository: the harness, a renderer copy with a line-width knob, the job list,
the page generator, both runs' logs and the 36 PNGs. It is not a build route, and step 1 replaces it. The look-(iii)
overrides (a web-only text scale and screen size) were never committed and never ship.
