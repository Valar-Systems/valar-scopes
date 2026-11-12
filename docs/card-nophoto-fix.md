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

## Amendments to step 3, the cardbench env (written before any capture)

Section 5's keys, as built (`PollCardBench`, `src/AircraftManager.cpp`, under `-DCARD_BENCH` only):

| key | as built |
|---|---|
| `n` | the bench contact `zzcardn`: callsign EXA123, `XAAA -> XBBB`, `Type: XTYP`, "Example Aircraft 100", "Example Airways"; 35,000 ft, 450 kt; 0.5 x the radius at bearing 045. Its metadata is Fetched with no photo path, so the card's own photo step settles it as no photo when the card opens |
| `l` | the same text as `zzcardl`, its metadata held at Fetching: the photo step never runs, and the card stays "Loading photo..." |
| `p` | **M3.** The same EXAMPLE text, with the LIBRARY PHOTO PATH of a live contact the enrich pipeline already resolved with a photo; refused if there is none. The photo is fetched as usual when the card opens. It prints a `P` line that recreates exactly this card |
| `P <0\|1> <path>` | **M3.** That photo card again, from the representative flag and the `/api/v1/blipscope/photo/...` path |
| `o` | a scripted tap at the bench contact's drawn marker (`RadarBlipPosition`, projected as the hit test does), substituted in `HandleTouch` where `SOAK_TEST` substitutes its own: three pressed polls, then the release |
| `d` | the same tap at the card's centre (on a photo card, to its data page) |
| `x` | removes the bench contact |
| `F <url>` | **M4.** The traffic from a local `aircraft.json` (map-client's frozen `fixed_traffic.py`), RAM ONLY: the local source with its details Off, so the fixed contacts are never enriched. No config is written; the URL, which carries a LAN address, is typed over serial and never printed or committed; any reboot undoes it |

**M4, and why** (the reviewer, accepted 2026-11-12, before any capture). FT1 and FT2 run on fixed traffic now that
`fixed_traffic.py` is frozen, and the owner's window terms allow nothing written to NVS, so the switch to it lives
in RAM. Local mode with details Off skips the card's photo step, so the pictures and the photo card's opening happen
in cloud mode first, and `F` comes after; a photo already decoded stays on the open card. **Both window images carry
`F`:** the commits are ordered so the "before" image is main + the hook with `F` (the pins commit, then `F`), and the
"after" image adds only the fix on top.

**M3, and why.** Section 5 had `p` as "a synthetic contact of a type the photo library holds". Two things settle it:
- The photo path holds the library's content key, so the device cannot derive it from a type.
- Picture (c) and FT2 compare a "before" and an "after" session with a flash between them. A live donor is whatever
  is flying then, so the photo, its caption and the data page's text would differ between the sessions for reasons
  that are not the fix.

So `p` takes only the photo path (and its representative flag) from a live contact, every row of text stays the
EXAMPLE values, and the "after" session replays the "before" session's printed `P` line. The two sessions then show
the same photo and the same text.

**What never leaves the board:**
- The contacts' alert and MQTT paths are pre-marked as sent.
- No enrich lookup is made for them: their metadata is already Fetched, or held at Fetching for `l`.
- `ClaimTappedAircraft` returns on a `zzcard` key, under `CARD_BENCH` only, so a tap writes nothing to the logbook.
- `p`'s photo fetch sends only the library URL and the device key (`fetchPhoto`), never the bench key.

**Read before the commit:**
- `check_photo_mode.mjs --role blipscope-s3-128-cardbench` answers `none`.
- `skus.py --validate` is OK: 19 legs, 10 build-only.
- `check_ota_identity.py --emit-table` adds exactly one row, `blipscope-s3-128-cardbench - s3-128`, and
  `--check-table` exits 0 (9 rows).
- The adsbdb gate's `plant_target()` sends this env to the default site, `ConfigMigration.cpp`, which it compiles.
- `check-no-bench-hooks.sh --selftest` exits 0 with `[card-bench]` in `MARKERS`.
- Built locally (21:36-21:42Z): `blipscope-s3-128-cardbench` exits 0 at 1,936,976 B, and
  `check-no-bench-hooks.sh --expect "[card-bench]"` on it exits 0 ("no other bench marker").
- The shipping `blipscope-s3-128`, rebuilt with the hook's code present and compiled out, scans clean (9 markers).
  Against D-ref's "before" image, `cmp5` reads PLACED, exit 0: the hook adds no byte to a shipping image.

**CI for the push that adds the env**, while T-host and T-web are still red: **38 = 31 / 3 / 4**.
- firmware 31 = 26 / 2 / 3: the cardbench leg adds one build job, green with its `--expect` step.
- web-renderer, workers, device-ids and no-traffic-captures: as for the red-first push.

**Off prediction, stopped and ruled: the OTA fleet pins.** A new OTA-compiling env is a row in
`test/fixtures/ota/identities.txt` AND the fleet counts its readers pin. Section 5a predicted only the row. With the
row added, the full host suite (WSL, and CI at `ce6eb4c8`) also failed the OTA identity, OTA install, sabotage
controls and sabotage drivers blocks. The reviewer accepted the fix as proposed: one commit re-deriving those pins
from the 9-row table (R1 27; M1 51/30; M2 90; M3 23/58 (28); O10 18; the model suite 18 checks), each derived first
and then found equal to what the suites printed. `--fleet`, `--fleet-sabotage` and `--check-table`, which CI's
ota-identity job runs, passed throughout. #648 and #659 also add rows: whichever lands second re-derives these on the
merged tree.

## The window on E2:08 (registered before it opens)

One no-hands window, granted by the reviewer under the owner's 04:29Z terms: app only, every image by sha256 first,
the restore's boot log showing Wi-Fi rejoined without the portal, and anything needing hands is a stop. The board is
identified by its pinned serial (E2:08), and reserved with `board-port.py --reserve` for the window.

**The images**, each `firmware.bin` + `partitions.bin` (`28f336fe57a9`, the shipping table), built from a clean tree:

| phase | built from | firmware sha256 | bytes |
|---|---|---|---|
| before | `626ece59b171` (main + the hook, with `F`) | `c1da5b7cfe573f3aa2e4073c49d47ae0c0dd3e42104bfd22af6f806dc73b9d26` | 1,937,424 |
| after | `0f652e899d58` (+ the fix) | `ed4f688c39546364c208e147a387b6ef0bb37d9c1ec2d60cf0dab0bae71b0add` | 1,937,520 |
| restore | the v19 release asset | `f6d73e39163177b0dc25516a104bb02021d2048a94f82a66fc42a90f0a2615be` | 1,931,840 |

Both bench images carry `[card-bench]` and no other bench marker (`--expect`), and the restore image carries none.
Each write is `scripts/app-only-write.py`: ota_0 (app0, 0x020000 .. 0x65FFFF) only, after it reads the board's
partition table and otadata back and refuses if the write would land wrong or not be booted. Nothing is erased, and
NVS, otadata, the bootloader and the partition table are never written.

**The driver** is a session scratch script (`window670.py`, sha256 `611330eba845...`), run one phase at a time. Its
`--dry` mode, which opens nothing, read all three images as identified, the port by its pinned serial, and the write
ranges above. The board's address and the PC's LAN address are never printed. The serial port opens once per
phase, with DTR and RTS set false before the open, and the board is checked over HTTP after every close.

**Each bench phase** (before, then after), in order:
1. Write the image, then wait for `/diag/fb` to answer.
2. Cloud mode, the pictures:
   - **(a)** `n`, `o`, then capture.
   - **(b)** `l`, `o`, then capture.
   - **(c0)** `p` (before; its `P` line is kept locally) or that `P` line (after), then `o`, 12 s for the photo, then
     capture.
3. Fixed traffic (`F`, map-client's `fixed_traffic.py` at sha256 `b530faf4df97a9e586f32f3671b79213b84540ded83b3f694a8c0083d11023a5`, `?home=bend&n=N`):
   - FT2 at n = 20, then at n = 40, on the open photo card.
   - Then `d` to the data page, and capture **(c1)**.
   - Then FT1 at n = 40, then at n = 20, on an `n` card.
   - Before each FT row, wait until two consecutive `[health]` lines count n + 1 tracked (the fixed set and the bench
     contact), up to 150 s.
4. Every capture is `/diag/fb` read three times, kept only when all three are `Torn:false` and agree byte for byte.
5. Every FT sample is the average drawn-frame time over 10 s, from `X-Blipscope-Frame-Seq`, with the tracked count
   from the nearest `[health]` line. Each (row, n) is sampled twice (FT-AA).

**FT1 and FT2 run on the LOCAL path** (the reviewer's note): with fixed traffic, map and weather are off on both
sides. That is equal conditions, and fine for a card-layout change.

**Restore:** write v19, wait 25 s (never open inside the boot window), then one serial read of the boot.

**Predictions, frozen:**

| row | predicted | if not |
|---|---|---|
| (a) | before: no route, type or operator row (the bug). After: caption at y 58, callsign at 36, then route, type, model, operator, Dist, Alt, Spd | the fix is not what T-host/T-web graded: stop |
| (b) | before: no rows. After: (a)'s layout with "Loading photo..." | the same |
| (c0), (c1) | before == after, byte for byte over 115,200 bytes (the two sha256s equal) | the differing pixel boxes are reported, and the fix stops there (section 6) |
| FT-AA | each (row, n)'s two "before" samples within 2 ms of each other | that row reads **"instrument"** and goes to the owner, never re-graded |
| FT1 | after <= before (the fix draws less: no silhouette). Margin: after at most before + 2 ms (each side's mean of two) | beyond the margin: the fix stops, and both numbers go to the owner |
| FT2 | after within 1 ms of before. Margin: + 2 ms | the same |
| tracked | 21 at n = 20 and 41 at n = 40, every sample | an FT row whose counts differ between before and after is not graded: "instrument" |
| restore | app-only-write exit 0, `[WiFi] CONNECTED`, no `[WiFi] ... portal` line, `/diag/fb` answers | stop and report: anything needing hands is the owner's |
| anything else | | the instrument is unreliable for that row: stop and decide, no improvised reading |

**Duration:** about 18 min per bench phase and 3 min for the restore, so about 40 min in all.

### M5: the window's first stop, and its fix (the reviewer's ruling (ii), written before the re-run)

**What stopped it** (22:44:34Z, phase before):
- The write verified and capture (a) was the bench card, showing #670 on glass: no route, type or operator row.
- Capture (b) was NOT the bench card. Both scripted `o` taps landed on the identical point, the bench contact's
  marker, and the hit test cycles to the next stacked candidate when a tap lands within 18 px of the last one
  (`sameSpot -> tapCycleIndex + 1`). So the second `o` opened candidate 1, a live contact (AAL2660) with a photo.
- Its `d` then flipped that card to the data page instead of closing it, `p` and `o` were refused, and the driver
  stopped as designed. Nothing further was written.
- The alternative, the bench contact being undrawn or untappable, is excluded: (a) at the same point opened it.

**The fix, both under `CARD_BENCH` only** (accepted):
1. `o` resets `lastTapX`/`lastTapY` to -1000 before its scripted tap, the same reset a tap on empty radar makes. Every
   scripted tap is then a first tap, and the nearest marker -- the bench contact, at distance 0 -- is the one opened.
2. The hook prints `[card-bench] card open on <key>` when a card opens, naming only a bench key; any other contact is
   "a live contact". **This is CN0's precondition on the bench:** the driver refuses a capture unless the card that
   opened is the expected `zzcard*` key, so a wrong card can never be read as a picture.

**The images change, so both are rebuilt and re-identified** before the re-run. The pushed history is not rewritten
(the reviewer accepted `bfa63636` by sha):
- "after" is the PR branch's head: main + the hook (with `F`) + the fix + this fix.
- "before" is `626ece59` + this fix, a commit pushed on its own branch so it is on the remote: the same content as
  "the hook fix under the fix".
- The shipping `blipscope-s3-128` is rebuilt too, and must still read PLACED against D-ref's "before" image, which
  proves no shipping byte moved.

**The re-run** starts phase before FROM THE START: (a), (b) and (c) are all re-captured on the new before image, then
phase after, then the restore with its boot read, then the release. The first (a) is kept as a record only. If
anything else is off, the restore (f6d73e391631) and the release come before the report, so the board is never left
on a bench image.

### M6: the window's second stop, and the restore's boot-read pattern (written before the next window)

**The re-run** (22:58Z, the M5 images):
- The write verified. CN0 held twice ("card open on zzcardn", then "zzcardl"), so M5's fix works.
- (a) was kept, byte-identical to run 1's: #670 on glass again. (b) was kept: the real bench photo-loading card.
- It stopped at `p`, 37 s after boot: "p: no live contact has a library photo yet; nothing injected". The driver
  asked once, and after a fresh boot no live contact had yet been enriched with a library photo. In run 1 one had, by
  chance.

As ruled, the restore and the release came first: v19 written and verified, E2:08 released at 23:01:11Z.

**The restore's boot read was misread by the driver.** The boot itself met the owner's terms:
- `[build] env=blipscope-s3-128 fw=v19`;
- `[WiFi] CONNECTED`, then "fast join OK";
- no line that opens a portal.

But the driver's pattern, `^\[WiFi\] .*portal`, matched the informational line `[WiFi] credentials saved: portal times
out after 180 s and retries them (...)`, which EVERY boot prints, and the driver reported "portal: SEEN". So the
registered prediction "no `[WiFi] ... portal` line" was mis-stated: that line is on every healthy boot. It is an
instrument defect, the outside-the-set case. It was reported by the log's lines and not re-graded.

**The driver's fixes** (the images, the write ranges and the hook are unchanged):
1. `p` asks every 10 s, for up to 180 s, until a donor exists. None in 180 s is a stop.
2. **The restore's prediction, corrected:** `[WiFi] CONNECTED`, and no line that OPENS a portal. A portal opening is
   one of the firmware's three outcomes, or WiFiManager's AP start:
   - "no network and nobody at the portal";
   - "provisioned via the portal";
   - "portal provisioning FAILED";
   - `*wm:StartAP with SSID` or `*wm:AP IP address`.
3. **The pattern's control** (`--selftest-portal`): this window's real restore log reads 0 portal lines, and each of
   five planted portal lines reads as a portal. PASSED.
4. **(c0) is captured only once the photo is drawn,** not after a fixed 12 s. The slot layout ("Loading photo...")
   is drawn on black, and the full-bleed photo covers the disc. So the driver waits, for up to 30 s, for a frame
   under half black, and a `[photo]` failure line (fetch failed, rejected, decode FAILED) is a stop. Control on real
   frames: run 1's photo page reads 0.0% black; run 2's no-photo and photo-loading slot cards read 93.7% and 97.4%.
5. **(c1) is captured only after the tap logs `hasPhoto=1 page=0 -> flip-to-data`,** so it is the photo card's data
   page.
6. **Read before the window, opening nothing on the board:** the fixed traffic's whole 8-mile ring around the `bend`
   home fits inside the board's scan box, computed locally from its config page with nothing printed but YES. And
   `MAX_AIRCRAFT` (60) holds n = 40.

The driver is a session scratch script, sha256 `03a17062e9fd...`. The images, the write ranges and the hook are
unchanged.

### M7: window B, FT1 and FT2 (written before its code, after the third stop and the split)

**Why FT needs a redesign.** `Update()` returns before the feed merge and the radar enrichment while a card is open
(`if (inDetail) return;`), so traffic can only change with the card CLOSED. The count with a card open is the count
at the moment it opened, frozen.

**The measured quantity, read from the source.** Each sample is the average drawn-frame time over 10 s:
(t1 - t0) / (seq1 - seq0), from `/diag/fb`'s `X-Blipscope-Frame-Seq`.
- `framebuf::FrameDone()`, which advances that sequence, sits at the end of `loop()`, after the flush, at the same
  point as `RecordFrameUs(micros() - frameStartUs)`; `frameStartUs` is the top of `loop()`.
- There is no pacing wait anywhere in the loop path: no `vTaskDelay` or `delay` in `Update()` or the draw. So the
  ~41 ms seen on an open card in run 3 is the card frame's real work, not a floor that would hide a slowdown.

**The hook additions, under `CARD_BENCH` only:**
- **`Z <us>`**, the control: a planted busy-wait (`delayMicroseconds`) at the top of `DrawDetailCard`, 0 .. 20,000 us.
- **Cloud details for a bench photo card.** When a `zzcardp` card opens under fixed traffic (local, details Off),
  `localDetails` becomes Cloud, so the card's own lookup fetches its library photo: the URL and the device key only.
- **Off again on every close.** `ExitDetail()` restores Off. That is every way a card closes, and it runs before the
  same `Update()` can reach `ProcessMetadataLookups`. `PollCardBench` repeats it as a backstop.
- While a card is open, `Update()` returns before the radar enrichment, so a fixed contact is never enriched.

**The procedure, per bench phase** (before, then after), all on fixed traffic, `F` RAM-only:
1. Write the image, then wait for `/diag/fb`.
2. The `P` line: window A's (its library photo path), replayed in both phases. No donor search.
3. **FT1 at 20:**
   - `n`, then `F 20`, then settle with the card CLOSED: two `[health]` lines at 21.
   - `o` (CN0 `zzcardn`), then two samples (FT-AA).
   - **The control:** `Z 5000`, two samples, `Z 0`.
   - Close.
4. **FT1 at 40:** `F 40`, settle at 41, `o`, two samples, close.
5. **FT2 at 40:** the `P` line (it replaces the bench contact; the count stays 41), `o` (CN0 `zzcardp`, the Cloud
   details line), the photo drawn (a frame under half black, up to 30 s), two samples, close.
6. **FT2 at 20:** `F 20`, settle at 21, `o`, the photo drawn, two samples, close.
7. Then the after phase the same way, then the restore with its boot read, then the release.

**Predictions, frozen:**

| row | predicted | if not |
|---|---|---|
| CONTROL | in each phase, the mean with `Z 5000` exceeds the mean without by **5.0 ms +/- 1.0 ms** | the instrument cannot see a slowdown: FT1/FT2 read "instrument", go to the owner, and are not graded |
| tracked | every sample at exactly n + 1, frozen with the card open | that row is not graded: "instrument" |
| FT-AA | each (row, n)'s two samples within 2 ms of each other (FT-AA's margin, as registered) | that row reads "instrument" |
| FT1 | as registered: after <= before. Margin: after at most before + 2 ms, comparing the means of two samples | beyond the margin: the fix stops, and both numbers go to the owner |
| FT2 | as registered: within 1 ms. Margin: + 2 ms | the same |
| restore | `[WiFi] CONNECTED`; no line that opens a portal; `/diag/fb` answers | stop, and report |
| anything else | | stop and decide; restore and release before reporting |

**The images for window B**, each built from a clean tree at its commit (partitions `28f336fe57a9`):

| phase | built from | firmware sha256 | bytes | ota_0 write |
|---|---|---|---|---|
| before | `73aadb8c4102` = `02f1a49fb6e0` + the M7 hook, on `blipscope-5c/card-nophoto-before-img` | `9b03eaf5a5707d5a8b4704bac609ce3d81a3c8371507307af5cdcd9471449645` | 1,938,288 | 0x020000 .. 0x1F936F |
| after | `d3bd09d18143`, the PR head | `dbda143abbaff5fad2b897647ec5a7525d3f99cf150e72e391534b2899d18f31` | 1,938,400 | 0x020000 .. 0x1F93DF |
| restore | the v19 release asset | `f6d73e39163177b0dc25516a104bb02021d2048a94f82a66fc42a90f0a2615be` | 1,931,840 | 0x020000 .. 0x1F7A3F |

- Both bench images carry `[card-bench]` and no other bench marker.
- **No shipping byte moved:** the shipping `blipscope-s3-128` built at `d3bd09d18143`, against the one built at
  `c6eae905c51e`, reads `cmp5` PLACED, exit 0.
- The driver (session scratch, `window670.py`, sha256 `2f652831e2ea...`) runs window B with `--ft`. Its `--dry` read
  all three images and these ranges.

## Results

### Window A, the pictures (E2:08, 2026-11-12 23:16-23:20Z; the reviewer's split after the third stop)

The third stop (run 3) came from the window's design. While a card is open, `Update()` returns before the feed merge
(`if (inDetail) return;`), so switching to fixed traffic with a card up could never take effect. The reviewer split
the window: **A**, the pictures only, in cloud mode; **B**, FT1/FT2, after M7 (not yet registered).

Window A ran on the M5 images with driver `e7ef33a26a55` (03a17062e9fd plus `--pictures-only`). Every write verified;
CN0 held for every card; every capture was kept on its first attempt (three finished reads agreeing).

| picture | before (`afbeb406c067`) | after (`42495c1f5cc3`) | as predicted |
|---|---|---|---|
| (a) no photo | `55308b626da0`: the silhouette, "No photo available", the callsign; no route, type or operator row (#670 on glass) | `67249c67c16a`: the callsign at the top, the caption under it, then route, type, model, operator, Dist, Alt, Spd; the Follow control and the hints intact | yes |
| (b) photo loading | `be4df86e8290`: "Loading photo...", the callsign; no rows | `67913f58caed`: (a)'s layout with "Loading photo..." | yes |
| (c0) photo page | `50cbc2c95094` | `50cbc2c95094` | yes: **byte-identical**, all 115,200 bytes |
| (c1) data page | `a41b82505f6d` | `a41b82505f6d` | yes: **byte-identical** |

The restore wrote v19 (f6d73e391631) and verified it. Its boot read shows `fw=v19`, `[WiFi] CONNECTED` and no line
that opens a portal; `/diag/fb` answered, and E2:08 was released at 23:20:13Z.

The pictures stay local (the session's scratchpad) and go to the owner through the reviewer, before #670 merges.
Runs 1-3's captures are records only. FT1/FT2 follow in window B.

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
