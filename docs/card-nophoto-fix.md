# The no-photo detail card on the 240 px Kit S3 (#670): the fix plan (registration; nothing is run)

**The ruling** (the owner, 2026-11-12 17:26Z, relayed by the reviewer): a shipping bug, for v20, "with a test that
fails on today's code first. Confirm it on a real board." And from the same ruling: "The 1.28" output stays unchanged.
Any change to its look comes to me separately." So before the fix merges, the owner sees before-and-after pictures
(section 6). **This page is committed before any query, test, build or flash.**

## 1. The bug, as read from the source (#670)

On a 240 px panel, a card with no decoded photo shows only the callsign:

- `useSlot = !hasPhoto` (`src/AircraftManager.cpp:11594`, main `71156714ba44`);
- the slot layout starts the flowed rows at y = 162 (`:11699-11712`);
- with the Follow control, `hardBottomY = SCREEN_SIZE - 74` = 166 (`:11750-11755`);
- `line()` drops a row with `y + fontHeight() > 166` (`:11756-11761`). 162 + 8 = 170, so every row drops.

`OutcomeForSwipe` (`include/FollowRouting.h:180-184`) offers Follow whenever the device has a location, so this is
every no-photo card, and every card for the moment its photo loads. It has shipped in v9 through v19 (`de8a2b55`,
2026-09-01). The 480 px Pro 2.1 is not affected.

## 2. How often: the no-photo share (Q1)

Approved by the reviewer at 17:33Z as one aggregate: counts only, no ids, no per-device or per-aircraft rows.

**The instrument:** `proxy/scripts/nophoto-share.ts`, committed here, unrun. It makes two Analytics Engine queries on
production's `blipscope_proxy`, each returning one row with one number. Neither selects a blob that holds a hex, a
type or a device id.

| | query |
|---|---|
| no photo | `SUM(_sample_interval)` where `index1 = 'enrich_gap'` and `blob2` is `'type'` or `'photo'`, 7 days |
| all | `SUM(_sample_interval * double4)` where `index1` is `'/api/v1/blipscope/enrich'` or `'/v1/enrich'`, 7 days |

**One definitional choice, said plainly, because the approval named "photo misses":** a `type` gap counts as no photo
too. Photos are keyed by type, and `recordEnrichGap` writes only the root gap, so a typeless lookup never writes a
`photo` gap even though its card has no photo. `name` gaps are left out, since a type without a friendly name says
nothing about the photo. That makes the share a slight undercount. A per-hex photo on a typeless airframe would be an
overcount, and those are few.

**The output, exactly** (from the script's own `console.log` lines):

```
no-photo share, blipscope_proxy, the 7 days to <YYYY-MM-DDTHH:MM>Z
  enrich lookups with no photo (root gap type or photo): <N1>
  enrich lookups, all:                                   <N2>
  share: <r> %
```

| reading | means |
|---|---|
| `share:` printed, exit 0 | about r % of enrich lookups leave a card without a photo. It's a lookup share, not a card-open share: cards are opened on the aircraft people find interesting, which nothing here measures |
| `INSTRUMENT:`, exit 2 | the query or the token failed. Its whole error is printed. No share is stated |
| `BLIND:`, exit 2 | a zero on either side, or N1 > N2: a query that cannot see what it counts |
| anything else | the instrument is not doing what this page says. Stop and decide |

**My expectation, written so it can be wrong:** 2-12 %.

## 3. The tests, red on today's code first

**T-web (`tools/web-renderer/card_nophoto.mjs`, new).** The web build fetches no photos, so every card in it is a
no-photo card: the case itself, end to end, through the firmware's own drawing. Against a built model:

1. Run the radar on the synthetic scene (`site/scene.js`), with enrich answered, until the contacts are drawn.
2. Find the drawn text op of a scene contact whose row has a route, a type and an operator (`SKW3307`: CRJ9, Bombardier
   CRJ900, SkyWest Airlines, SLC -> SEA). Tap at that text op's own x, y. The radar's hit test accepts its label box,
   so this goes through the real tap path, with no shortcut into the card.
3. After 60 frames, read the frame's text.

| row | asserts |
|---|---|
| **CN0** (precondition) | the card is open: the frame draws `Loading photo...` or `No photo available` (the slot layout's own captions). If not, **INSTRUMENT, exit 2**: a card that did not open is never read as rows dropped |
| **CN1** | the route row `SLC -> SEA` is drawn (`routelabel::CardLine`) |
| **CN2** | `Type: CRJ9` is drawn |
| **CN3** | `SkyWest Airlines` is drawn |

Predicted on today's code: **s3-128: CN0 ok, CN1-CN3 FAIL, exit 1. s3-21: all ok, exit 0**. That is the built-in
control, so the check is shown able both to pass and to fail before any fix exists. If #668 has merged by then, the
720/960/1080/1280 legs run it too: all ok. The check joins `web-renderer.yml` for every model.

**T-host (`test/host/test_card_layout.cpp`, new).** It covers the layout arithmetic. Today the numbers are literals in
`DrawDetailCard`, so step 1 moves them, unchanged, into `include/CardLayout.h` (`cardlayout::HardBottomY(screen,
followCtl)`, `SlotFlowStartY(screen, followCtl)`, `RowsThatFit(y0, bottom, fontH, lineH)`), and the card calls them.

| row | asserts | today |
|---|---|---|
| **CL1** | 240 px, Follow offered, slot layout: at least 3 rows fit (route, type, operator) | **FAIL**: 0 fit |
| **CL2** (control) | 480 px, Follow offered: at least 3 fit | ok |
| **CL3** (control) | 240 px, no Follow: at least 3 fit | ok |

**D-ref, for step 1's move:** `blipscope-s3-128` and `blipscope-pro-s3-21` are rebuilt before and after it, in one
worktree path, and compared by the five-region comparator. Both must read PLACED. The move is not allowed to change a
pixel, and this is how that is shown.

## 4. The fix (the design; written after the tests are red)

Only the slot layout changes, and only when it cannot hold three rows: `RowsThatFit(162, hardBottomY, ...) < 3`. On
240 px with Follow offered, that is exactly the broken case. The 480 px panels, and 240 px with no Follow, keep
today's layout to the pixel.

In that case:
- the silhouette is not drawn;
- the title moves to y 36, as on the data page;
- the slot's caption (`Loading photo...` / `No photo available`) moves to y 58, where the data page puts
  `representative photo`;
- the flow starts at y 70.

From 70 to 166 there is room for 7 rows: route, type, model, operator, and the first telemetry rows. The Follow
reservation is untouched. Identity outranks the silhouette, which is the comment's own promise ("with full telemetry
beneath it") kept.

After the fix: CL1 ok, CN1-CN3 ok on s3-128, and every control still ok.

## 5. Showing a no-photo card on glass, with no hands

The shipping image cannot open a card without a tap, and live traffic shows a no-photo card only by chance. The radar
serves `/diag/fb` in every build (`src/ConfigurationWebServer.cpp:431`), which gives the glass's pixels over Wi-Fi.
The one missing piece is opening a chosen card.

**A bench-only env, `blipscope-s3-128-cardbench`** (`-DCARD_BENCH`, never shipped). Its serial keys:

| key | does |
|---|---|
| `n` | injects a synthetic contact with a route, type and operator (EXAMPLE values, `XAAA -> XBBB`), marked enriched, its photo settled as none: the no-photo card |
| `l` | the same, with the photo never resolving: the photo-loading card |
| `p` | a synthetic contact of a type the photo library holds, fetched as usual: the card WITH a photo |
| `o` | taps the injected contact at its drawn position, **through `HandleTouch`**, the touch poll's own path. That is the second-path rule: the tap's whole path runs, with nothing re-implemented to open a card |
| `d` | taps the open card, to its data page |

- The `[card-bench]` marker joins `scripts/check-no-bench-hooks.sh`, so CI fails if it ever appears in a shipping image.
- Each capture is `/diag/fb` read three times, kept only when `Torn:false` and the three agree.
- The board is E2:08, in a window from the reviewer after map-client's ends (~19:30Z). Afterwards it gets the v19
  release asset back (`f6d73e391631`), app-only, read back with `esptool verify-flash`.

### 5a. The cardbench env's consequences, named before any code (the reviewer, 17:55Z)

A new env is not only a new `[env:*]`. Each consequence below lands in the commit that adds the env:

| what | consequence | predicted |
|---|---|---|
| `skus.yml` | a row `{ env: blipscope-s3-128-cardbench, status: build-only }`, with no slug. CI compiles it on every run, so the hook cannot rot, and it publishes nothing and cannot withhold `version.txt`. `--validate` refuses it if it ever gets a slug | `skus.py --validate` exit 0 |
| the adsbdb launch gate | no new `plant_target()` case. The env compiles the radar, `ConfigMigration.cpp` included, so the default site (`scripts/check-adsbdb-out.sh`, the `*)` arm) holds its control, as for `-wxlook` and `-joindiag` | the leg's gate control reads FOUND, never UNTRUSTWORTHY |
| the OTA identity table | `test/fixtures/ota/identities.txt` gains its row through `check_ota_identity.py --emit-table`, which writes the file; CI's `--check-table` holds it equal | exactly one new row, `blipscope-s3-128-cardbench - s3-128` (env, scope `-` for none, board), like `-joindiag`; `--check-table` exit 0. **No identity ruling is needed, and here is why, stated so it can be checked:** no new board name (the board field is `s3-128`, from `s3_128.h`'s `SLUG`, as every s3-128 env has); no new scope (`-`, with no `FW_OTA_PREFIX`); the record's format, the 64-byte record at 0x120 (`include/OtaIdentity.h`), unchanged. The env also keeps s3-128's partition table, flash size and memory type, which the table's one-board rule requires. **If any of these turns out false at code time, it goes to the owner before the code** (the reviewer, 17:57Z) |
| the bench-hook scan | `[card-bench]` joins `MARKERS` in `scripts/check-no-bench-hooks.sh`. That scan runs on slug-ful legs only, so it never sees this image. A positive-control step, like `-wxlook`'s, runs `--expect "[card-bench]"` on the cardbench image, so the marker's absence from every slug-ful image means something | the expect step passes on the cardbench leg; every slug-ful image still scans clean |
| photo mode | `check_photo_mode.mjs --role` answers `none` for it | `none` |
| **the CI job set** | the firmware matrix gains one leg: **firmware 31** on a PR. With #675 merged: 31 = 29 / 2 / 0. Without it: 31 = 28 / 3 / 0. The PR that adds the env re-predicts its whole tally from these | as stated, re-read at that commit |

## 6. The pictures for the owner, before the fix merges

Each picture is a `/diag/fb` capture of the cardbench image, before (main + the bench hook) and after (the fix + the
bench hook):

| | card | before | after |
|---|---|---|---|
| **(a)** | no photo (`n`, `o`) | the callsign only. This is the on-glass confirmation of #670 | callsign, caption, route, type, model, operator, telemetry |
| **(b)** | photo loading (`l`, `o`) | the callsign only | the same layout as (a), with `Loading photo...` |
| **(c)** | WITH a photo (`p`, `o`; then `d` for its data page) | the full-bleed page and the data page | **pixel-identical to before**, both pages, byte for byte over the 115,200-byte frame |

(c) is a test as well as a picture: the fix may touch only the slot layout. A (c) that differs is reported with the
differing pixel boxes, and the fix stops there. The pictures go to the owner through the reviewer, and the fix merges
only after his word on (a) and (b).

## 7. Size and order

| step | what | size |
|---|---|---|
| 0 | this registration | 0.25 d |
| 1 | Q1, run once, results here | 0.1 d |
| 2 | `CardLayout.h` (the move, D-ref), T-host and T-web, both read red on today's code, in CI | 1 d |
| 3 | the cardbench env, and the before-captures on E2:08 in a window (a, b, c) | 0.75 d |
| 4 | the fix: the tests green, the after-captures, the pictures to the owner | 0.5 d |
| | **total** | **about 2.6 d** (up from the 2 d first sent; the glass method is the difference) |

**CI for this registration** (`docs/` and `proxy/scripts/`; `proxy/**` is in workers' paths): firmware 30 (27 success
/ 3 skipped), workers 3, device-ids 1, no-traffic-captures 1: **35 = 32 / 3 / 0**. workers'
`npm run typecheck:scripts` (`tsconfig.scripts.json`) covers the new script. Read locally before the push: exit 0, and
`--listFiles` names `nophoto-share.ts`, so the check opened the file.

## Amendments to step 2 (the reviewer's ruling (A), 2026-11-12, after the first D-ref; written before the re-run)

The first D-ref read OUTSIDE on both envs (Results, below). The move had sent `line()`'s test through
`cardlayout::RowFits(y, hardBottomY, backbuffer.fontHeight())`. That changes the evaluation order (`y` is loaded before
the `fontHeight()` call) and the `[&]` closure's capture order, which follows first mention (`y`, `hardBottomY`,
`backbuffer` instead of `y`, `backbuffer`, `hardBottomY`). Neither changes behaviour, but D-ref proves the move with
bytes, and placing the bytes after seeing them would be a re-grade (the reviewer: "Not (B)").

**M1. `line()` keeps its own test until the fix.**
- At step 2, `line()` keeps `y + backbuffer.fontHeight() > hardBottomY`, byte for byte.
- `RowFits` stays in `CardLayout.h`, for `RowsThatFit` and T-host. CL4 pins it to `line()`'s rule by numbers
  (158 + 8 = 166 draws; 159 + 8 = 167 drops).
- The card calls `RowFits` from step 4, the fix, which changes bytes anyway and is graded end to end by T-web.

**M2. The slot flow starts at `SLOT_FLOW_Y` until the fix.** `SlotFlowStartY(SCREEN_SIZE, followCtl)` needs
`followCtl` before the slot is drawn, and computing it there reorders code the same way. At step 2 the card uses
`cardlayout::SLOT_FLOW_Y`, the value `SlotFlowStartY` returns. The fix calls `SlotFlowStartY`, since it needs
`followCtl` there anyway.

**D-ref, re-run on the amended move.** The same two envs, the same worktree path, compared against the same "before"
images. **Predicted: PLACED on both, `cmp5` exit 0 on both.** Any byte outside the five regions is a stop again.

**The red-first push** (accepted by the reviewer; that head shows red and is not for merging): **37 = 30 / 3 / 4**.
- firmware 30 = 25 / 2 / 3: both host-tests legs fail on CL1, and the `host-tests` aggregate with them; `version` and
  `promote` skip.
- web-renderer 2 = 0 / 1 / 1: the build fails at the T-web step on s3-128, after s3-21 has also run; publish skips.
- workers 3 / 0 / 0; device-ids 1; no-traffic-captures 1.

## Results

### D-ref, the first run (off prediction; stopped, ruled (A) above)

Both envs rebuilt in one worktree path, before (`a7eb4be1342a`) and after the first version of the move. Image sizes
unchanged (s3-128 1,932,544 B; pro-s3-21 1,927,456 B). 91 bytes differ in each: 75 in the five rebuild regions
(32 + 1 + 32 + 5 + 5) and **16 outside**, so `cmp5` exits 1 on both. The same 16 bytes appear in both envs. Mapped
through the image's segment table to the "after" ELF's symbols (s3-128):

| bytes | where | what |
|---|---|---|
| 15, at 0x0c5823..0x0c5833 | `DrawDetailCard`'s `line` lambda, `operator()` | the load of `y` moved ahead of the `fontHeight()` call; the `l32r` literal index shifts by one; the bound's compare re-encoded |
| 2, at 0x0cd9b9 and 0x0cd9c5 | `DrawDetailCard` | two stores into the closure object swap slots 0x74 and 0x78 |

The alternatives, and why the data excludes them:
- **A stale or different build:** every other byte is identical, and both places are in the function the edit touched.
- **Rebuild nondeterminism:** CLAUDE.md's v9 accounting places a same-source rebuild entirely in the five regions,
  and these bytes sit at the edited expression.

### D-ref, re-run on the amended move (M1, M2): PLACED on both, as predicted

Rebuilt 21:11-21:12Z in the same worktree path, against the same "before" images:

| env | before | after (amended) | differing | `cmp5` |
|---|---|---|---|---|
| `blipscope-s3-128` | `5f623ce7ac1a` | `b3f4a66cb9f4` | 75 B: 32 + 1 + 32 + 5 + 5 | PLACED, exit 0 |
| `blipscope-pro-s3-21` | `ea2c5aab02f7` | `f4de46ce3f36` | 75 B: 32 + 1 + 32 + 5 + 5 | PLACED, exit 0 |

(sha256 prefixes of the app images; the sizes are unchanged.)

**The rebuild landed.** The time strings equal the first "after" build's, because WiFiManager's objects were reused, so
an identical result could have been a stale image. Against the first "after" image, the amended one differs in exactly
the 16 bytes, from the same first offsets (0x0c5823; 0x0c4503), plus the hash regions. The move changes no byte of
either image outside the build stamps.

### T-host and T-web on today's code (red first, as predicted)

- **T-host**, in WSL Ubuntu-24.04 (`g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`), with `run.sh`'s flags:
  - CL0 ok.
  - CL1 FAIL: "flow from y 162 to 166 holds 0 row(s), needs 3".
  - CL2 ok (19 rows at 480 px); CL3 ok (5 rows, 240 px with no Follow); CL4 and CL5 ok.
  - Exit 1. It was read through `wsl --exec`, with a control (a known `exit 1` read back as 1).
- **T-web**, on web builds of `a7eb4be1342a` (before any move):
  - The scene clock was +179 s, chosen from the scene's own `/blips`: SKW3307 20.2 km from the centre and 7.7 km
    from the nearest other aircraft.
  - **s3-128:** CN0 ok (the card for SKW3307 is open, with its title and "No photo available"); CN1, CN2 and CN3
    FAIL; exit 1. The frame draws 5 strings.
  - **s3-21:** all ok; exit 0. The frame draws 13 strings.

### Q1, the no-photo share (run once, as frozen; the reviewer's OK on the numerator, 17:55Z)

Run 2026-11-12 17:55:40Z from `proxy/`, the script byte-equal to its commit (`fc92dae1`), exit 0. stdout:

```
no-photo share, blipscope_proxy, the 7 days to 2026-11-12T17:55Z
  enrich lookups with no photo (root gap type or photo): 10538
  enrich lookups, all:                                   162103
  share: 6.5 %
```

stderr was token-file.ts's own note that Windows can't check file permissions, and nothing else.

**As predicted** (2-12 %). About one enrich lookup in 15 leaves its card without a photo. On a 240 px Kit S3 with a
location set, each such card shows only its callsign for as long as it's open, and every other card does so for the
moment its photo loads. That is a lookup share, not a card-open share, as section 2 says.

## The frame-time row (the owner's standing rule, 2026-11-12)

In his words: "every change to drawing code reports its frame time on the bench, before and after. A slowdown beyond
the margin frozen in its registration stops it." The card fix changes drawing code, so it carries this row.

| row | what | predicted | frozen margin |
|---|---|---|---|
| **FT1** | the 240-px Kit S3 (E2:08, in a window), the detail card open on a no-photo contact (the cardbench `n`, `o`), frame time from `/diag/fb`'s sequence counter over 10 s, read before (main + the bench hook) and after (the fix + the bench hook) | **after at most before.** The fix draws less: no silhouette on the compacted card | **after at most before + 2 ms.** Beyond that, the fix stops and goes to the owner with both numbers |
| **FT2** | the same on a card WITH a photo (`p`, `o`) | **unchanged within 1 ms** (the photo path isn't touched; (c) also requires pixel-identical) | after at most before + 2 ms |

Both rows are read in the same window as the pictures (section 6), so no flash is added.

**What FT1/FT2 measure, exactly** (the reviewer, 19:39Z):
- the **average** frame time over 10 s;
- of **drawn** frames: `/diag/fb`'s sequence counter counts backbuffer frames, not presented ones, as established on
  the 2.1 today;
- not a p95, and not of presented frames.

For a change that draws less, an average against a +2 ms margin is enough. Later drawing registrations use map-client's
frame-time procedure once it is accepted.

**The same traffic, and an A/A control first** (the cloud reviewer, relayed 20:01Z; amended before any reading):
- FT1 and FT2 compare "before" and "after" at the **same aircraft count**, which is recorded per sample. The firmware's
  fit is about 0.8 ms of p95 per aircraft, so a few planes of live drift can swing a 2 ms margin either way. Once
  map-client's `fixed_traffic.py` is frozen by sha, both rows run on its 20/40 set, cited by that sha.
- **FT-AA, first:** "before" is read **twice**. If the two reads differ by more than the row's margin (2 ms), the row
  reads **"instrument"** and goes to the owner. Nothing is re-graded.
