# From today to a released map (a plan, drafted 2026-11-07)

**This is a draft for review. Nothing in it is ruled.** Daniel decided on 2026-11-07 that weather and maps ship together, in the
same Blipscope version, both on by default. So this plan also sets when weather ships.

## The short version

**A released map, and with it weather, is about 2 to 5 weeks away:**

| how the work is run | days from today | rough date |
|---|---|---|
| **Client in a second session,** alongside the server work | **14-22** | **November 21-29** |
| One session does everything, one step after another | 24-36 | December 1-13 |

**I recommend the second session.** The client is the longest stretch and doesn't depend on most of the server work.

**Three things are most likely to move these dates:**
1. **The ocean fix.** Nobody has designed it yet, and it could change the package format the client reads.
2. **Drawing a map under the radar on the 1.28" board.** The frame cost has never been measured.
3. **K1's staging reading and step 0's reading,** which haven't happened yet (open inputs, below).

**Separately, weather's cost matters more than the map's.** With weather on every board, the $2 allowance runs out at 50-84 boards,
and 600 boards cost about $29-46 a month. The map adds about $0.08 at 600 (see "Cost").

## The steps, in order

**Server side.** Most of these go production only at step 6.

| step | what it is | rough estimate | waits on |
|---|---|---|---|
| **1. K1 merged** | Stored map names keyed by a server secret (#596, approved) | today | a quiet moment on main |
| **2. Staging reading** | K1 and step 0 read together, proxy and builder both deployed to staging | under 1 day | blipscope-5c's "#590 read back" |
| **3. Dense-area code** | What a New York scope gets, since today it gets no map | 2-3 days | step 2's numbers; a slot for its own reading |
| **4. Deploy-inputs code** | `deploy.sh` checks only the files a Worker actually ships: 13 rows, 5 plants | 1 day | rulings on Q-DB1 to Q-DB3, not yet given |
| **5. Ocean mask** | Open water past the coastal band draws as land today | 3-6 days | a proposal with predictions; Class A figures before the re-ingest |
| **6. D4, production** | Map switched on in production for the first time | 2 days | steps 1-5; the production resources announced first |
| *alongside* | The guard PR with `--shard`, and the smoke driver's red-prefix refusal | 1 day | not a release gate, but the test rig's port margin is shrinking |

**Server total: 10-14 days.**

**Client side.** No radar code exists yet. Weatherscope has already done part of this job.

| step | what it is | rough estimate | waits on |
|---|---|---|---|
| **A. Fetch** | `GetBytes()` needs `Retry-After` and a limit of about 214 KB (weather's is 32 KB); the retry and spreading rules, F2 and F6 | 2-3 days | shared-code changes go to blipscope-5c through the reviewer |
| **B. Decode and draw** | Adopt `BasemapPackage.h`, already written. Rasterise for the radar: `radar-up` rotation, the road rule S1, nearest-level terrain and the theme table are all new | 4-6 days | the ocean decision, if it changes the format |
| **C. Layering** | Under weather and the sweep, on both the weather and non-weather paths, at a frame rate that still looks right | 2-4 days | a bench board (E2:08 is shared) |
| **D. Setting** | On by default: DO1 to DO4, the config page, and DOC1 and DOC2 | 1-2 days | none |
| **E. Bench** | DOC3, S1's two airports, memory, a soak | 3-4 days | a production `/basemap` for the final pass (step 6) |

**Client total: 12-19 days.** For comparison, #445, the weather layer, took 17 days from opened to merged, alongside other work.

**Release.**

| step | what it is | rough estimate | waits on |
|---|---|---|---|
| **R. The cut** | Fresh-boot acceptance, version bump, tag, prerelease, bench flash, promote | 2-3 days | Daniel's two OKs (below); the count and cost read at the cut |

## How the estimates were made

They come from what comparable work took this week, and they are rough.

| work | ruled or started | merged | elapsed |
|---|---|---|---|
| Step 0 | 11-06 ~15:07Z | 11-06 ~23:07Z | 8 hours |
| S2 | 11-06 ~18:26Z | 11-07 02:36Z | 8 hours |
| K1 | 11-06 ~21:58Z | approved 11-07 ~16:05Z | 18 hours, with two plants off |

- **Each code change** takes about an hour of driver runs, and one machine runs one at a time.
- **Each merge** waits for main's CI, about 15 minutes, and busy mornings add retries.
- **Each staging slot** takes under an hour, plus the wait for one.
- **Days here are elapsed days,** including review. Each step assumes it reads as predicted the first time. An off reading has cost
  about half a day so far.

## Open inputs

| input | status | if it goes badly |
|---|---|---|
| **K1's staging reading** (SK1-SK6) | **not read.** Its slot follows "#590 read back" | about a day's fix before D4 |
| **Step 0's reading** | not read. It says how far over the size cap New York is | a large overshoot makes step 3 a floor decision, not only code |
| **Each build's peak memory** | not measured. The dense plan must state it against the isolate's 128 MB (ruled ~03:23Z). KBWI's build read 50.1 MB | a redesign of how builds hold data |
| **Frame cost of the map** | not measured | a pre-rendered layer, or fewer redraws |
| **The ocean's design** | no data source chosen. Natural Earth and ETOPO1 are already inputs elsewhere, so they are the first candidates | a format change reaches client step B |

**What the ocean fix is likely to cost.** A full store re-ingest was 11,040 puts, about 2.5 hours at today's pacing, plus up to 11,039
repair puts. The account sits at 929,006 Class A of the free 1M, so it fits, and the figures are stated before the run. A new
store id means every stored package rebuilds.

## Cost

**From `docs/wx-cost.md`, with `/wx` polling on every board. It adds about $0.08 at 600 boards for the map:**

| boards | account, a month |
|---|---|
| **17** (active in the last 7 days) | **about $0.79** |
| **50** | $0.95-$2.06 |
| **600** | $29-$46 |

- **The $2 allowance runs out at 50-84 boards, because of weather.** The map barely moves that.
- **Daniel's ruling (~02:36Z) already covers this:** `/wx` is made cheaper before the fleet passes about 50 boards. That fix is
  blipscope-5c's, and it should be on the release path.
- **The factory's 600 units carry v18,** so they reach both layers only by OTA. Boards sold before the release can't add to the
  bill until then, and after it they count at once.
- **Europe's line as built** takes today's fleet to $4.51-$4.74 on its own (`docs/wx-opera-europe.md`). The cheaper parts are in
  progress, and the same $2 covers it.
- **One OTA now brings the map's first fetch and weather's polling together.** The view at ship counts both.

## What this plan needs

| from | what | why now |
|---|---|---|
| **Daniel** | **A second session for the client,** yes or no | it is the biggest lever on the date |
| **Daniel** | The OK for the release (v19 counts as R) and the OK on what a prerelease does | `RELEASING.md` holds both; no prerelease can run without the second |
| **The reviewer** | Rulings on Q-DB1 to Q-DB3 | step 4 can't start without them |
| **The reviewer** | **Whether the ocean proposal comes early.** It was ruled for after the D4 order, but its answer can change the format the client reads | proposing it right after step 2 protects client step B from rework |
| **The reviewer** | Confirm the client's scope is `blipscope-s3-128` | it is the only shipping radar SKU; `blipscope-pro-s3-21` and the AMOLED are build-only |
