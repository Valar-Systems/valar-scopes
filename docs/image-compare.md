# A layout-invariant image comparison for the fix-only skip (#656): the registration, before any code (2026-11-12)

**What this is.** `RELEASING.md`'s fix-only exception skips the fresh-boot test (step 4) only when two halves pass: the
source diff, and the built images compared function by function. The artifact half built for #651
(`docs/ota-head-patience.md`, section 6) can't decide a fix that changes any function's size, which is nearly every fix
(#656). This file picks the replacement's approach, freezes its rules, and registers its controls with their readings,
before a line of it exists. No code and no build has run for it.

**What it does not change.** The source half (section 6's `git diff --stat` and function-level source check, with its K1)
is untouched. #657 (`ota-download-path-diff.sh` doesn't read `include/OtaInstall.h`) is separate. The owner's rule is
untouched: anything out of scope, or an instrument that can't decide, means step 4 runs with the owner.

## 1. The approach: compare the objects before the link, then account for the link from each image's own map

**Picked: relocation-based, on the objects.** Every compiled unit (one function or one data item: the build uses
`-ffunction-sections -fdata-sections`) is compared in its object file, before the link, with every relocated field
replaced by the thing the relocation names. Then the link is accounted for from each image's own linker map: every
difference in the final images' function (name, size) table must be explained by a named link mechanism, or the
instrument can't decide.

**Why this one.** In an object file nothing has an address yet. A relocation names a symbol, or a section of the same
object plus an offset into it, and nothing another translation unit does can move either. A layout shift therefore
can't reach the comparison at all, rather than being absorbed by a normalisation that has to guess. Three facts about
this build make the object side complete:
- **No LTO** (`-fno-lto` in the framework's flags; `platformio.ini` adds no `-flto`). Inlining, IPA clones and GCC's
  identical-code folding all happen inside one translation unit, so each one shows up as a changed unit *in a changed
  object*, where it is attributable.
- **GNU ld 2.43.1 has no identical-code folding** (no `--icf` in its `--help`). Nothing folds across objects at the link.
- **What the link can still do to an unchanged object is a short, closed list:** keep or drop a section
  (`--gc-sections`), pick one copy of a COMDAT group, and relax a long call (the build compiles with `-mlongcalls` and
  links without `--no-relax`). Section 4 gives each one a check.

**Rejected: map-based on the final images.** Naming every address as (output section, nearest preceding symbol,
offset) works for named targets and fails for anonymous ones. String literals and constants live in mergeable sections
that the linker merges across objects, with no symbol inside them. A string added anywhere earlier in a merged pool
moves every later string's offset from the nearest symbol: the same failure as `cosf`'s `l32r` literal in #656, in a
form the map can't fix. Relaxation also changes the final bytes of functions whose source didn't change.

**Rejected: `--emit-relocs` on the final link.** In a linked image, a relocation against an anonymous target is
rewritten relative to the *output* section, so it shifts with the layout as before. It also changes the link of the
image under test.

## 2. The build protocol

For each pair: **one worktree, both builds back to back, each from clean.**
1. **Inputs, recorded before the first build and again after the second, and equal:** the host; `pio --version`; the
   platform, toolchain and framework package versions; every resolved library version; `platformio.ini`'s sha256. If
   the two commits' `platformio.ini` differ, the pair is out of scope by definition and nothing is compared.
2. **`SOURCE_DATE_EPOCH` fixed for the whole sequence.** GCC then expands `__DATE__` and `__TIME__` to that time, so
   WiFiManager's two stamped strings (CLAUDE.md's five regions) can't differ between two builds. **Landing check T0:**
   both strings in each image read the fixed stamp. If they don't, stop: no allowance is improvised.
3. **Each build:** delete `.pio/build/<env>`, `pio run -e <env>`, copy out every object, archive and the ELF.
4. **The map:** the link command from that build (`pio run -v`) is run a second time on the same objects with
   `-Wl,-Map=` added, to a second ELF. **Check M0:** the two ELFs are byte-identical (`cmp` exit 0). If not, the map
   isn't this image's map, and nothing is compared.
5. Switch the same worktree to the second commit (`git switch --detach`), and repeat 3 and 4.

Prebuilt framework archives aren't rebuilt. Each archive in either link is hashed, and an archive whose hash differs
between the two links is compared member by member like any object.

## 3. The comparison, exactly

**A unit's identity** is (input file, name of the symbol at offset 0 of its section). Archive members are
`archive(member)`. A section name alone isn't used: `IRAM_ATTR` sections are numbered by a per-file counter
(`.iram1.N`) that renumbers when a function is added above. Where a name is reused inside one object (IPA clones
`.constprop.N` can renumber), the unit reads as gone plus new, which over-reports and never hides. A section with no
symbol at offset 0 is identified by its section name, and if that name occurs twice in one object its units are
UNDECIDED. Mergeable string and constant sections aren't units: they're compared through every reference to them.

**Compared:** every allocated section except `.eh_frame`, `.xt.prop*`, `.xt.lit*`, `.xtensa.info`, `.comment`,
`.note*`, `.group` and `.debug*`. The unwind and property tables are derived from code that is compared anyway, and
debug sections carry line numbers that move when a header gains a line.

**Two units are the same** when their sizes are equal, their bytes are equal outside relocated fields, and their
relocation lists are equal after each target is named as follows:
| the relocation's target | named as |
|---|---|
| a global or weak symbol | its name |
| a local function or object symbol | its name, in this input file |
| the unit's own section | `self+offset` (a branch inside the function) |
| a mergeable string section | the string's bytes, from the offset to the NUL |
| a mergeable constant section | the constant's bytes |
| a literal (`.literal.*`) | the literal's own target, named by this table; a literal with no relocation is its 4-byte value |
| a per-item section (`.rodata.X`, `.data.X`, `.bss.X`, `.text.X`) | that unit's identity, plus the offset |
| anything else | **UNNAMED**: the unit can't be decided |

**Relocated fields:** a 4-byte word for `R_XTENSA_32`; the whole instruction for `R_XTENSA_SLOT0_OP`, its length read
from the opcode's low nibble (8 to 13 means 2 bytes, 0 to 7 means 3). **Refusals, each making its unit UNDECIDED,
never "same":** a nibble of 14 or 15 under a relocation; any relocation type other than `NONE`, `32`, `SLOT0_OP`,
`ASM_EXPAND` and `ASM_SIMPLIFY` in a compared section; an UNNAMED target.

## 4. The image layer: every (name, size) difference gets exactly one mechanism

The final images' function tables are still compared, as #651's layer 1 did, but each function is identified through
the map by the input section it came from, never by its name: local names repeat across objects.

| mechanism | what the instrument shows | what it means |
|---|---|---|
| **CHANGED** | its input unit differs (or exists on one side only) | goes to the verdict |
| **GC** | its input unit is the same, but kept in one image only | every unit referring to it in that image must be CHANGED. If so it takes their verdict; if not, UNDECIDED |
| **PICK** | a COMDAT group's kept copy comes from a different input | the two kept copies compared as units: same means no change, different means CHANGED |
| **RELAX** | its input unit is the same and kept in both, its final size differs, and it has at least one long-call site (`R_XTENSA_ASM_EXPAND`) | layout, not a change. The size delta is printed, not graded. With no long-call site, UNDECIDED |
| anything else | | UNDECIDED |

The premise this rests on, said plainly: two identical input units whose targets all name the same things become the
same code after the link. That is a claim about the linker, the same binary on both sides. It isn't tested here.

## 5. The verdict

**IN SCOPE** when every CHANGED unit, in both shipping images (`blipscope-s3-128` and `blipscope-pro-s3-21`):
- matches the fix's allowed list, frozen in its registration as demangled patterns before the diff is read; **or**
- has local binding (a static function, an IPA clone, local data), sits in a changed object, and every unit referring
  to it, in either image, is itself in scope (taken to a fixed point).

**OUT OF SCOPE**, with every unit that keeps it out named, when a CHANGED unit has global or weak binding and isn't on
the list. The reason is plain: such code can be reached from outside the scope. This instrument can show that compiled
code differs, never that the difference is harmless. That includes a COMDAT copy kept from the changed object, and
every IPA side effect that lands in a global function outside the list.

**UNDECIDED** when any unit or mechanism reads UNDECIDED. The fresh-boot test then runs with the owner.

## 6. The controls, frozen with their readings

The order is M0 and T0 on every build, then K0, K3, K2, K4 and POS. **Any reading outside its row's predicted set
stops the run. It means the instrument can't decide, and its next step waits for a ruling.** A stop blocks only what
depends on it.

| id | the pair | predicted | also printed |
|---|---|---|---|
| **M0** | every build: its ELF against its `-Map` relink | byte-identical | |
| **T0** | every build: WiFiManager's two stamped strings | both carry the fixed `SOURCE_DATE_EPOCH` stamp | |
| **K0, null** | C (`3a322758eadc`) twice, `blipscope-s3-128`, back to back | ELFs byte-identical; 0 changed units; 0 image-layer differences; reads NO CHANGE | |
| **K3** | window 1's A and S, `blipscope-s3-128-otatest` at C. S is A with the v19 record's S plant at `include/OtaIdentity.h:99`, the parse moved one byte (`head + kRecordOffset` + 1); the exact edit is read from that record before running | exactly one changed unit, `MaybeUpdateFirmware()` in `OtaUpdater.cpp`'s object; every other input the same; 0 image-layer differences | |
| **K2** | C, and C with `src/main.cpp:324`'s `.timeout_ms = 10000` set to 10001 (#656's plant) | exactly one changed unit: `setup()`, or a data unit that only `setup()` refers to; reads OUT OF SCOPE, naming `setup()`; 0 image-layer size differences | |
| **K4, layout** | C, and C with `{ volatile uint32_t k = 0x5A5A5A5Au; (void)k; }` added after `src/main.cpp:328` | landing: one or more functions' addresses move between the images; exactly one changed unit, `setup()`, larger; every other image-layer difference is RELAX, PICK or GC, with 0 UNDECIDED; reads OUT OF SCOPE, naming `setup()` alone | the number of moved functions; RELAX deltas |
| **POS** | C and C′ (`7b943ae0b5d1`, the v19 tag: #651's squash), on both shipping envs | one object differs, `OtaUpdater.cpp`'s, in both. Its changed units are all on `docs/ota-head-patience.md` section 6's list: `MaybeUpdateFirmware()`, `ota::ReadSome<…>` instances, `StreamSource` members, `UpdateFlash::writeRest`. 0 other units, 0 UNDECIDED; every other image-layer difference is RELAX, PICK or GC; reads **IN SCOPE** on both | the image-layer differences and their mechanisms, set beside #651's ten |

**The plants are edits to a scratch tree, asserted before building and restored by bytes.** None is committed. K4's
landing check exists because a plant that moves nothing would pass K4 for the wrong reason.

**Why K0 and K4 were added to the issue's three.** K0 is the null: it shows the instrument can read "nothing" on a real
build, so a later empty reading means something. K4 is the only control that moves the layout under an
out-of-scope change. #651's comparator read 1,250 differing functions on a shift like that.

**Which commit POS names.** #656 names the candidate `ed0d660b9d53`, which isn't on the remote. C′ (`7b943ae0b5d1`) is
what section 6 defines as the candidate ("the squash of this fix into `release/v19`"), and it is what shipped. The
ten (name, size) differences #651 read are set beside POS's reading as context. They were read from builds in two
worktrees, so they aren't graded.

## 7. Size, and the order

| step | what | size |
|---|---|---|
| 1 | this registration | this commit |
| 2 | the code: one script, about 600 lines (pyelftools 0.33, which the host already has); a selftest on committed fixture objects, one pair per mechanism and per refusal, each built by the toolchain's own compiler from sources and commands committed beside it; one step in the host-tests job; a PR with its CI predicted | about 4 h, plus CI |
| 3 | the runs: 9 clean builds (`blipscope-s3-128`: C twice, C′, K2, K4; `-otatest`: A, S; `blipscope-pro-s3-21`: C, C′), read in this file's section 6 order; the record against this table | about 2 h (each build timed; the first one sets the estimate) |
| 4 | the wording of the fix-only rule in `RELEASING.md`, naming this instrument and what UNDECIDED means. The rule is the owner's, so its text goes to the owner | about 30 min to draft |

About one working day in all. The builds need no board, bench window or deploy slot. They use disk (about 3 GB of
copied build output, my own rebuildables, deleted after the record).

## 8. Amendment 1, before any code (2026-11-12, 16:47Z)

The reviewer accepted the approach at 16:44:50Z with two notes. blipscope-5c described its ELFs at about 16:46Z, and
map-client named its pairs at 16:46:17Z. This amendment folds all three in. No code exists yet.

**(a) RELAX has to be counted.** This replaces section 4's RELAX row, whose "the size delta is printed, not graded"
is withdrawn. A function is RELAX only when every one of the following holds; otherwise it is UNDECIDED:
1. its input unit is the same on both sides, as section 3 compares it;
2. its input has n ≥ 1 long-call sites (`R_XTENSA_ASM_EXPAND` on a `callx`) and no `R_XTENSA_ASM_SIMPLIFY`;
3. **in each image, the final copy is walked against the input, instruction by instruction:**
   - lengths come from the opcode nibble, and a nibble of 14 or 15 makes the unit UNDECIDED;
   - an instruction with no relocation must match exactly;
   - one with a relocation must match in length and opcode nibble;
   - the only other steps allowed are an `l32r` that is deleted, and a long-call `callx` that becomes a `call`;
   - both walks must end exactly at the last byte;
   - the walk counts r, the number of relaxed sites. The deleted `l32r`s must number exactly r, with 0 ≤ r ≤ n;
4. **the size delta is exactly 3 × (r on one side − r on the other).** Each relaxed site drops one 3-byte `l32r`;
5. **every site relaxed on one side and not the other is located and its reach printed, from that image's own
   addresses:**
   - where it became a `call`, the target lies within `call` range (an 18-bit word offset: −2^19 to 2^19 − 4
     bytes);
   - where it stayed long, the literal's target lies outside that range.

So a RELAX reading lists n, r on each side, and each flipped site with its two reaches. K4's predicted reading becomes:
its RELAX functions listed with those counts, and 0 UNDECIDED. The same applies to POS's other differences.

**Alignment fill is not modelled.** If the linker pads to keep an aligned target inside a function (a `loop` body),
the walk fails and the function reads UNDECIDED. That is the conservative direction. The prediction is still
0 UNDECIDED in K4 and POS, and a reading off it stops the run for a ruling.

**(b) POS: C′ stands in for #651's candidate.**
- The candidate #656 names is `ed0d660b9d53`. blipscope-5c confirms it is reachable as `refs/pull/651/head`, and that
  its `src/` and `include/` equal C′'s, with 0 files differing.
- `git diff C C′ | git patch-id --stable` reads `114cd48aa115`, the reviewer's value.
- POS therefore compares C and C′, both rebuilt in one tree, as section 6 says.

**(c) blipscope-5c's three ELFs: context only, never graded.** They are `blipscope-s3-128` ELFs, built on this machine
with platform-espressif32 55.03.39:

| name | sha256 | built from |
|---|---|---|
| C-s3-128.elf | `c63a1c83230c9cc984925f06377fa4443cdd90519aee10654024fa56d6261b66` | C, in worktree `wt-m630`. 5c says the commit is inferred, not proven at build time |
| CAND-s3-128.elf | `c76b38c984356d5bdfa9758d93436c6d145b3973a9b66f35d3e0ba0ee3ead7b1` | `ed0d660b9d53`, in worktree `wt-fix` |
| CPRIME-s3-128.elf | `b56c6718b6c6fbeec4ac8740f4c507fce7139c6d369dc8c348d97bf1e258b10f` | C′, in `wt-fix`, from a clean env build |

- **What this explains about #651's reading:** its C and its candidate were built in worktrees whose paths differ in
  length (`wt-m630`, `wt-fix`). Any string carrying an absolute source path is then one byte longer in one image. That
  is a layout shift with no code change, and #656's "checked" paragraph (library versions, `platformio.ini`) didn't
  exclude it. This is named here, not decided. Every pair in this file is built in one tree, so this cause can't
  arise in it.
- **The context row, printed after POS:**
  - for my rebuilt C′ against CPRIME, the count of image-layer (name, size) differences;
  - whether the two ELFs are byte-identical. The prediction is not identical, because the tree path differs.

**(d) A pair may differ in build inputs only on lines its registration names.** This replaces section 2's step 1
"out of scope by definition" with a rule that keeps that as its default:
- the comparison records every input, and every difference must be named, by file, section and key, in the pair's
  registration before it is read;
- **the fix-only skip names none**, so any input difference there still makes the pair out of scope, as before;
- map-client's P-FLIP would name exactly one: `platformio.ini` `[weather] layer`, from empty to
  `-DWX_LAYER_SHIPS`.

**(e) Map-client's pairs are registered in amendment 2, before they run.**
- P-REDO is Bw against Cw, both `blipscope-s3-128-wxlook`, with no input difference.
- P-FLIP is the v20 candidate with the switch off and on, on `blipscope-s3-128`, wanted about 20-26 Nov.
- Which of the two runs is the reviewer's ruling. Each one's expected set is frozen in map-client's record first and
  copied here with that record's sha. Both run only after K0, K3, K2 and K4 have read as predicted.

## 9. Amendment 1a, from the code's selftest, before any build is compared (2026-11-12, 17:13Z)

Nothing below has been read on a real build. Every item comes from the instrument's own fixtures, and all of them are
in the code commit that follows this amendment: `scripts/image-compare.py`, `test/host/image_compare_selftest.py`,
`test/fixtures/image-compare/`.

**(a) Relaxation also narrows, so the walk accepts an instruction's own other form.**
- **What the fixture showed:** the `relax` fixture is one long call that crosses `call8`'s limit when its callee
  moves 128 KiB. ld deleted the `l32r` and turned `callx8` into `call8`. It also narrowed `or a10, a2, a2` (3 bytes)
  to `mov.n a10, a2` (2 bytes), so the size delta was 4, not 3.
- **Why the rule changes:** under amendment 1 (a), every relaxation that narrows reads UNDECIDED, so POS could never
  pass.
- **The rule now:** the walk also accepts a step where the final copy holds the input instruction's own narrow or
  wide form, decoded field by field to the same operation and the same operands. The forms are `add`, `addi`,
  `mov` (`or` with s = t), `movi`, `l32i`, `s32i`, `beqz`, `bnez`, `ret`, `retw` and `nop`.
- **What is counted:** narrowings and widenings, per side, printed. The size must reconcile exactly:
  final = input − 3r − narrowed + widened.
- **What still reads UNDECIDED:** a width change to anything else.
- **RELAX still needs** a relaxed-site difference with a crossing reach. A narrowing alone never earns it.

**(b) The rest of the rules as the code implements them, registered so the runs are read against them:**
- **A relocated instruction compares with only its operand cleared, per format:** `l32r`, `call`/`j`, the BRI12
  branches, BRI8 and `loop`, RRI8, and `beqz.n`/`bnez.n`. The opcode and the registers stay compared. Any other
  format under a relocation is UNDECIDED. This is stricter than section 3's "the whole instruction".
- **Every input is re-hashed against `inputs.json`, on both sides,** before its record is believed.
- **A store is refused if its `record.json` says M0 did not hold.**
- **M0's mechanism:** the relink runs through `PLATFORMIO_BUILD_FLAGS=-Wl,-Map=…` in the same tree. M0 requires every
  object and archive to hash the same across the relink, and the ELF to be byte-identical (`scripts/image-compare-build.ps1`).
  It holds the same property section 2 named, without recovering a link command from a tempfile.
- **GC is taken to a fixed point.** A function kept in one image only passes when every kept unit naming it is
  CHANGED, or is itself a GC function that passes. So a chain counts only when it is rooted in a CHANGED unit. This
  is what "takes their verdict" in section 4 needs: a library function pulled in by a newly kept library function
  (map-client's `fmin` → `__fpclassifyd`) would otherwise read UNDECIDED.
- **A label, for reading only: `CHANGED (load/store offsets only: k)`.** It applies when a CHANGED function has the
  same size, the same named relocations and the same instruction boundaries, and every differing instruction is the
  same load or store with the same registers, differing only in its offset immediate.
  - The forms counted: `l8ui`, `l16ui`, `l16si`, `l32i`, `s8i`, `s16i`, `s32i`, `lsi`/`ssi`/`lsiu`/`ssiu`, and
    `l32i.n`/`s32i.n`.
  - `addi`, `addmi` and `movi` are left out on purpose: they carry constants as often as offsets.
  - The verdict is unchanged: a labelled unit is still CHANGED and still scope-checked.

**(c) The selftest, and the proof that it can fail.**
- **19 cases, each graded on its whole output and its exit code:**
  - the fixtures `null`, `body` (with and without an allow list), `grow`, `gc` (a chain, through an archive), `local` (with and
    without), `relax`, `pick`, `strpool` and `offset`;
  - six refusals: R1 an unknown relocation type, R2 a stale input, R3 a size change with no long-call site, R4 a
    narrowing to a different operation, R5 a flip whose reach never crossed, R6 a register change under a relocation;
  - T0 in both directions.
- **It runs in the host tests** (`test/host/run.sh`, one new block). A missing pyelftools or a crash is the rig,
  exit 2.
- **A scratch sabotage run** (sha256 `c39a68943d8dc574d37021a0576b6234800f19adfae781fd04634bf6668f3093`, not
  committed) plants 11 defects in a copy of the comparator. Each one's red set was predicted before it ran, and all
  11 read as predicted. Two readings stopped first and are recorded:
  - **P1** (strings named by offset) first read no red. GCC had emitted the fixture's new string *after* t1's, so
    nothing had moved. The fixture was fixed so that t1's in-place addends go 0, 6 → 5, 11, checked by dumping the
    bytes. P1 then read as predicted.
  - **P9** (`addi` counted as an offset) read no red. `local`'s constant is a `movi`, not an `addi`. It was
    re-registered as P9b (`movi`) and read as predicted.
- **Directions no fixture reaches yet:** the `addi` exclusion, and GC's refusing direction (a newly kept function
  named by an unchanged unit).
- **The repository's guards read the fixtures; they don't skip them.**
  - The fixture objects and archives first failed `check_wifi.py` and `check_macs.py` as "not a known binary type:
    cannot be checked". That is the guards refusing what they can't read, as they should.
  - `check_wifi.py`'s `BINARY_EXT` now carries `o` and `a`. `check_binary_strings.py` imports that one definition, so
    it reads every printable string in them under the same patterns: 88 objects and 12 archives, 0 findings.
  - `check_wifi.py --selftest`, `check_wifi_sabotage.py --code-only`, `check_binary_strings.py --selftest` and
    `--sabotage`, `check_macs.py`, `check_device_ids.py` and `check_no_traffic_captures.py` all pass.
  - **`check_device_ids.py` skipped every binary**, and `check_binary_strings.py` doesn't read the device-id shape. So
    once `check_wifi.py` excused objects and archives, an id compiled into one would have reached no guard. It now
    reads their printable strings under the same pattern and allowlist.
  - **The control: a reading that relaxes a guard needs one (the reviewer, 17:27:43Z).** A copy of a fixture object
    and of the fixture archive each had a private-LAN-address string and a device-id-shaped string appended. Both
    strings were assembled at run time and written into no committed file. The run was in scratch, never pushed:

    | guard | predicted (17:29:40Z) | read |
    |---|---|---|
    | `check_wifi.py`, `check_macs.py` | exit 0: excused binaries, nothing MAC-shaped | exit 0, exit 0 |
    | `check_binary_strings.py` | exit 1: a LAN finding in exactly the two plants | exit 1: `lan` in exactly the two plants |
    | `check_device_ids.py` | exit 1: the id in exactly the two plants | **first read exit 0, and it was no reading.** The repository's pre-commit hook refused the scratch commit over the id, so the plants never reached the HEAD this guard scans. Re-read in a fresh repository holding only the guard, its two lists and the plants (no hook bypassed): exit 1, the id in exactly the two plants. **main's guard on the same commit: exit 0**, which is the gap this closes |

    On the real fixtures every guard reads clean.

## 10. Amendment 2: map-client's P-REDO, before it runs (2026-11-12, 17:13Z)

The reviewer ruled both pairs in (16:51:30Z). P-REDO's expected set is frozen in map-client's record, and
copied here: `docs/blipscope-map-client-steps.md` at `0e0c394b68e02eee623d8b74190f73b3f225b8b9` on
`feat/map-client-step-c-blend`, section "#656's first customer pair: the expected changes, frozen before anything is
read". P-FLIP's set comes later, from the v20 candidate, as amendment 3.

- **The pair:** Bw (tree `eb84bef82d41`) against Cw (tree `0b1d19f08e26`), both `blipscope-s3-128-wxlook`. They
  name no input lines: `platformio.ini` is identical.
- **The object layer:**
  - only `AircraftManager.cpp`'s and `main.cpp`'s objects differ;
  - CHANGED by source in `AircraftManager`: `Initialise`, `MapConfigure`, `ConsumeBasemap`, `RebuildWeatherLayer`,
    `DrawWeatherUnder`, `ConsumeFetchResult`;
  - NEW: `AircraftManager::DrawMapBase`, `MapAirportBoxes`, `MapAlloc`, `MapFree` (the lambda included);
    `mapcompose::*`, `maplayer::*`, `mapplaces::*`, `bmpk::*`; `wxraster::Text` and `Put`; `glcd::font`;
    `wxlayer::OffDisc`; `wxs2::To565`;
  - in `main.cpp`: `_GLOBAL__sub_I_tft`;
  - **map-client's prediction:** every other CHANGED unit in those two objects reads `load/store offsets only`
    (four members were inserted at `AircraftManager.h:725`, ahead of about 99). `RecordFrameUs` is not CHANGED.
- **The image layer:** `fmin`, `fmax` and `__fpclassifyd` are GC, and nothing else enters or leaves. Every other
  difference is RELAX. UNDECIDED reads 0.
- **The escape hatch, map-client's:** anything outside this set means the instrument or its model is unreliable at
  this scale. Stop and decide.
- **What the label cannot see, said before the run so it can't be argued after:**
  - a member moving past `l32i.n`'s reach (an offset of 60 or more) changes the instruction's width;
  - an offset past 1020 needs a literal or an `addmi`;
  - an `addi` that takes a member's address is a constant to this rule.
  
  Each of these reads plain CHANGED, not `offsets only`. Map-client may amend its prediction for them before the
  run.
- **It runs after K0, K3, K2 and K4 have read as predicted.** It needs two builds of `blipscope-s3-128-wxlook` at Bw
  and Cw, one tree, from clean.

## 11. Amendment 1b: M0 takes the build's own map (the reviewer's ruling, 17:36:57Z)

**The stop that led here.** Step 3 stopped at M0 on its first build: K0's C on `blipscope-s3-128`, at 17:33:19Z. Nothing was
compared.
- The relink step's `PLATFORMIO_BUILD_FLAGS=-Wl,-Map=…` recompiled all 259 files and wrote no map at the store's path. The
  driver refused, as registered: "M0: the relink wrote no map".
- **The cause, read from the platform:** the framework already maps every link.
  `framework-arduinoespressif32-libs/esp32s3/pioarduino-build.py:209` passes `-Wl,-Map="${BUILD_DIR}/${PROGNAME}.map"`, and
  ld honours that later flag.

**M0, as ruled:**
- the store takes the build's **own** `firmware.map`, written by the same link as the ELF. No relink, no
  `PLATFORMIO_BUILD_FLAGS`;
- **the tie, on content:** `image-compare.py m0` must find every allocated, non-empty ELF section among the map's
  output-section lines, with the same name, address and size;
- the map's mtime must be no earlier than the build's start;
- either one false: the driver stops (exit 3), and the comparator refuses the store. The comparator also re-checks the
  tie on every store it reads.

**The red control, shown before any K row, as the reviewer asked. Predicted at 17:38Z, before the run:**

| map | predicted | read (17:38:36Z) |
|---|---|---|
| the real build's own map, against its ELF | 14 of 14 sections tied, exit 0 | **as predicted** |
| a copy of it with `.flash.text`'s size edited by 4 bytes (`0x139b60` → `0x139b64`), the edit asserted | 13 of 14 tied, `.flash.text` named, exit 1 | **as predicted** |
| **K2's map against C's ELF**, once K2 is built and before K2 is read | exit 1, naming every section whose size or address the plant moved | to be read |

## 12. Amendment 1c: T0′ (the reviewer's ruling, 17:44:43Z)

**T0 is OFF on the record.** On K0's first build, `t0` found 2 stamp fields in WiFiManager's object, both the fixed stamp,
and exited 2 ("fewer than the two date+time strings it exists to check"). Its model was wrong, not the build.
- **What it had assumed:** that WiFiManager carries two `__DATE__ " " __TIME__` strings, from CLAUDE.md's description of
  the five rebuild regions.
- **What this build has:** one such string (`WiFiManager.cpp:2312`), and a date-only stamp from the Arduino core,
  which is built in the tree (`libFrameworkArduino.a`).
- **What the read showed** (it printed dates only):
  - every tree-built stamp is the fixed one;
  - the build day's date is in no input and not in the ELF;
  - the other dates are in precompiled framework archives, whose hashes are equal in every build.

**T0′, as ruled:**
- every date-shaped (`Mmm dd yyyy`) string in an input BUILT IN THE TREE (an `inputs.json` entry with a copy) equals the
  fixed date, and every `HH:MM:SS` beside one equals `00:00:00`;
- at least one fixed stamp is found: that is its control;
- precompiled package archives are outside it, because their hashes are equal across builds;
- K0's byte-identical ELFs stay the decisive check: no unfixed `__TIME__` survives two builds minutes apart.

**Its red control, before it is trusted. Predicted at 17:45:19Z, before the run:**

| store | predicted | read (17:45:36Z) |
|---|---|---|
| K0's first C store, as built | 3 fixed stamps in 2 inputs (WiFiManager's object 1, the core archive 2), 0 not, exit 0 | **as predicted** |
| a copy of it with the build day's date patched into WiFiManager's object (same length, its sha256 re-recorded) | 2 fixed in 1 input, 1 not, WiFiManager's object named, exit 1 | **as predicted** |

The selftest's two T0 cases move to T0′'s output, and still pass both ways. The scratch sabotage run still reads all
11 plants as predicted.

**A finding for a later docs fix, not in this PR.** CLAUDE.md's rebuild-regions table names WiFiManager's `aboutdate`
and `Software Info:` strings as two `__DATE__ " " __TIME__` stamps. In this WiFiManager there is one, and the second
build stamp in the image is the Arduino core's date-only string.

## 13. K2 OFF, amendment 1d, and M0's different-build control (the reviewer's ruling, 17:55:34Z)

**K2 read OFF.** With the instrument at `232511551c28`, against K0's second C build:

    inputs: 362 loaded, 1 differ (src/main.cpp.o)
    functions whose address moved: 0
    CHANGED data local A+B sec:.rodata [src/main.cpp.o] -- OUT
    verdict: OUT OF SCOPE: sec:.rodata      exit 1

- **What matched:** one changed unit, OUT, and 0 size differences.
- **What didn't:** the unit is `main.o`'s **unsplit** `.rodata` (140 B, no symbol). It holds the `wdtConfig` constant
  that `setup()` copies. `setup()`'s code is unchanged.
  - Four functions name that section: `joinfail::AdviceFor`, `LGFX_Sprite::LGFX_Sprite`,
    `WiFiManagerHelpers::BootTouchToForget` and `setup()`. So it is not "a data unit that only setup() refers to".
  - The miss is mine: I predicted a literal or a per-item section, and it was a section shared by four functions.
- **The verdict was right,** and in the safe direction.

**Amendment 1d, as ruled. No change to the verdict rule.**
- Every changed data unit prints its referrers: the kept units of its own object, on either side. The verdict names
  the unit with them.
- An unsplit section is attributed to ALL its referrers, never to a guessed one.
- **The fixture `rodata`:** `f1` and `f2` keep their struct constants in one unsplit `.rodata`. B changes `f1`'s, and
  neither function's code changes. Its three cases:
  - no allow list: OUT, named by `f1, f2`;
  - allow `f1`: still OUT, because `f2` is not on the list;
  - allow `f1, f2`: IN.
- **Red first:** today's code (`232511551c28`, unchanged) read exactly those three cases red, and nothing else (17:58:21Z).
  With 1d, all 22 pass.
- **The sabotage run:** P12 (referrers not printed) turns exactly the three red. P7's set was re-derived when the
  fixture landed: its first run read OFF on `rodata allow=f1,f2`, the very rule P7 breaks. All 12 plants now read as
  predicted.

**K2 re-read under 1d (K2's first re-read).** Predicted at 17:59:34Z, written to a file before the run, with the four
names demangled first:

    CHANGED data local A+B sec:.rodata [.pio/build/blipscope-s3-128/src/main.cpp.o] -- OUT (named by:
      WiFiManagerHelpers::BootTouchToForget(LGFX&, lgfx::v1::LGFX_Sprite&), joinfail::AdviceFor(joinfail::Cause),
      lgfx::v1::LGFX_Sprite::LGFX_Sprite(lgfx::v1::LovyanGFX*), setup())
    verdict: OUT OF SCOPE: sec:.rodata (named by: the same four)      exit 1

**Read (18:00:41Z): byte-identical to the prediction once line endings are stripped.** Windows Python writes CRLF and
the prediction was LF; 5 CRs, nothing else.

**M0's different-build red control.**
- Amendment 1b's row expected K2's map to fail. That was corrected before it was read: a same-size plant moves no
  section, so a section-table tie can't fail on it. K2's map tied 14 of 14, as corrected. That row is no longer a
  red control.
- **K4's map against C's ELF became the control.** It was predicted to fail with `.flash.text` among the untied
  sections, and it **read exit 1, 13 of 14, `.flash.text` alone (18:01:01Z)**. K4's own pair ties 14 of 14.
- M0 has now been seen refusing a different build. This was read before any K4 row.

## 14. K4 OFF, and amendment 1e: a function's exception table is part of the function (the reviewer's ruling, 18:03:33Z)

**K4 read OFF.** With the instrument at `e15f9fb0afee`, against K0's second C build:
- **What matched:** 6,557 functions moved; `setup()` CHANGED; 0 UNDECIDED, 0 RELAX, 0 PICK, 0 GC; OUT.
- **What didn't:** a second unit, `sec:.gcc_except_table._Z5setupv`. That is `setup()`'s own C++ exception table, whose
  call-site offsets moved when `setup()` grew. It read "named by: no kept unit", because only `.eh_frame` names it, and
  section 3 excludes `.eh_frame`.
- The miss is mine: section 3 excluded `.eh_frame` as derived from code, and left out its partner.

**Why it isn't simply excluded too.** A table holds its catch types, by typeinfo relocation. A catch-type change can
leave the code byte-identical and change only the table. Excluding it would hide that.

**Amendment 1e, as ruled.**
- `.gcc_except_table.<suffix>` is part of the function whose section is `.text.<suffix>` in the same object. Its
  masked bytes and named relocations are folded into that function's signature.
- An unpaired table stays a data unit.
- `offsets_only` walks only the code, and needs the folded table equal.
- **1d's referrer lookup is widened:** a global or weak unit is named by its symbol, as well as a local one by its
  place. 1d's text said "its referrers", and the `ehtype` typeinfo units showed the narrower lookup missing them.

**The fixtures, red first on today's code (`e15f9fb0afee`, 18:05:35Z):**
- `eh`: `handle` grows ahead of its call site. Today's code read `handle` CHANGED plus its table as a second unit.
- `ehtype`: only the catch type changes (`EA` → `EB`). The probe showed the code and the table's bytes identical, and
  only the table's relocation target (`_ZTI2EA` → `_ZTI2EB`) different. Today's code read the table as a data unit and
  `handle` as SAME.
- **Under 1e:** `eh` reads `handle` alone; `ehtype` reads `handle` CHANGED, with its typeinfo units named by `handle`
  and their `_ZTS` names named by their `_ZTI`. 24 of 24 cases pass.
- **The sabotage run:** P13 (the table not folded) turns exactly `eh` and `ehtype` red. P12's set was re-derived:
  its first run with `ehtype` read OFF on that case, which prints referrers. That is the same miss as P7's, a set not
  re-derived when a case was added. All 13 plants now read as predicted.

## 15. Every plant declares every case; K3 (the reviewer's rulings, 18:10:27Z and 18:14:25Z)

**The repeated miss, removed mechanically.** P7's set, then P12's, wasn't re-derived when a case was added. That rule
had to be remembered, and it failed the second time.
- The selftest now publishes `case_labels()`, every case it grades. It asserts that it graded exactly those.
- The driver, committed as `test/host/sabotage_image_compare.py`, makes **every plant declare every case, as red or
  unaffected**, with both halves written out.
- It **refuses to run** (exit 2) while any case is missing from any plant, sits in both halves of one, or is not a case
  the selftest grades. Adding a case fails the driver until every plant has declared it.
- **The refusal, shown once (18:12:27Z):** in a copy, the selftest gained a case and every plant declared it except
  P10. The driver printed `P10 the offsets label never given: does not declare ['dummy']` and exited 2, as predicted.
- **Under run.sh's driver conventions:**
  - `--anchors`: 13 of 13 present exactly once;
  - `--control`: the unplanted selftest's line, derived from `case_labels()` rather than typed as a count. No
    pyelftools reads SKIPPED, never passed;
  - the full run: 13 plants × 24 cases, ALL AS DECLARED;
  - `lint_driver_counts.py`: 0 flagged.

**K3: A against S, `blipscope-s3-128-otatest` at C, instrument `033ac5ad`.**
- S's planted header matched blipscope-5c's LF sha256 exactly (`6418c9541333…` from C's `3c1cb9849c42`).
- The reading:

      inputs: 362 loaded, 1 differ (src/OtaUpdater.cpp.o)
      functions whose address moved: 0
      CHANGED func global A+B MaybeUpdateFirmware(LGFX&, lgfx::v1::LGFX_Sprite&, HttpRequestManager&) -- OUT

- **The registered row holds:** exactly one changed unit, in OtaUpdater.cpp's object, with 0 image-layer differences.
  That is the answer #651's comparator gave for this pair, now from the objects.
- **The filed exact text was OFF by one token, recorded as K3's first off reading, by text only.** I wrote
  `MaybeUpdateFirmware()` without demangling the symbol first. No re-read: the same builds would reproduce the output
  and prove nothing new.
- **The fix, applied to POS:** every name in an exact prediction, and every allow pattern, is demangled from the
  object's own symbol table and checked against a real symbol before it is filed.

## 16. Amendment 1f: the assembler's fill (the reviewer's ruling, 18:29:31Z)

**Prompted by POS's reading, said first:** this amendment exists because of the control it will now be graded on.
- POS read OFF on both envs. Besides `MaybeUpdateFirmware` and the new `ota::ReadSome<StreamSource>`, five
  OtaUpdater.cpp functions #651 never touched read CHANGED: `ResetReasonName`, `StampRebootCause`,
  `TakeOtaMemReport`, `DeferRebootWithCause` and `ota::Token`.
- objdump's decode, with the registers kept, showed them the same program instruction for instruction. They differ
  only by a 2-byte alignment fill the assembler inserted or dropped (the branch and literal offsets move with it), and
  by same-operation width swaps: `addi.n a10, a1, 8` ↔ `addi a10, a1, 8`, and `or a12, a6, a6` ↔ `mov.n a12, a6`.
- **Why the assembler chose differently** inside an unchanged function is **NOT established**. That stays a named
  alternative, not a finding.
- The reviewer's reason for (a): #656's v20 job is map-client's accounting, and that noise would clutter it.

**The rule.**
- A CHANGED function reads **FILL**, never silently same: it is printed with its fill bytes on each side, its width
  swaps by kind, and its instruction count, and it is not counted as changed. It reads FILL when its two copies are
  the same instructions, pair by pair:
  - bytes equal;
  - or, under a relocation, equal with only the operand cleared;
  - or a same-operation width swap, decoded field by field by 1a's `canon()`.
- Every relocation must name the same target, and a branch inside the function is compared by **instruction index**,
  not offset.
- A folded exception table (1e) is compared with its call-site offsets mapped to instruction indices, and must
  otherwise be equal.
- **Fill is ONLY what the object's own property tables (`.xt.prop*`) mark unreachable.**
  - Instruction regions are those with the instruction bit; fill regions those with the unreachable bit.
  - **Found while building this, not assumed:** code the exception table's offsets are computed from carries data and
    no-transform bits too (`0x106` code, `0x10c` fill), so the data bit is tolerated in either.
  - Literal or unknown combinations, a gap, an overlap, or no table at all: **UNDECIDED, never FILL, never guessed.**
- **In the image layer**, each side's final copy is walked against its own input on the linked image's own property
  table. The firmware keeps `.xt.prop`; the fixture linker script now keeps it as Xtensa's default script does. A
  relaxed-site flip must still cross the call limit.

**The fixtures, independent of POS:**

| case | what it is | 1f's reading | the pre-1f code (HEAD), red first |
|---|---|---|---|
| `fill` | the same source, B assembled `-Wa,--no-target-align` | FILL, 1/0 fill bytes, 34 instructions, NO CHANGE | **red** (CHANGED) |
| `fillchg` | that, plus one real change (`y << 4` → `<< 5`) | CHANGED | CHANGED: a guard, red under P15/P17's family of plants |
| `swap` | `mov.n a12, a6` against the wide `or a12, a6, a6` (assembly) | FILL, `mov 2->3 x1` | **red** (CHANGED) |
| `swapop` | B's wide instruction is `add a12, a6, a6` | CHANGED | CHANGED: a guard, red under P14 |
| R8 | B's literal names `pick` where it named `ext` | CHANGED | CHANGED: a guard, red under P15 |
| R9 | B's `.xt.prop` renamed away | UNDECIDED (`no property table covers .text.pick`) | **red** (CHANGED) |

- 30 of 30 pass.
- **The driver refused, by itself,** when the six cases were added: "13 plant declaration(s) do not cover the selftest's
  30 cases". Each plant then declared them.
- **Four plants added:**
  - P14: the operation not compared on a width swap → `swapop`;
  - P15: relocation names not compared in the fill walk → `R8`;
  - P16: a missing table read as one instruction region → `R9`;
  - P17: the counts not printed → `fill` and `swap`.
- **P15's set was re-derived after its first run read OFF.** I had declared `body` too, guessing its 10000 → 10001
  was a literal. The disassembly shows `movi.n 16` → `17` plus `addmi 0x2700`: immediates. That is my codegen guess
  failing again, after P9.
- All 17 plants read as declared.

**(c), as ruled:**
- `SPIClass::end` stays UNDECIDED. RELAX's reach rule is not loosened, and ld's relaxation research is not sized.
- **So for a #651-like fix on `blipscope-s3-128`, the skip still would not fire, and step 4 runs with the owner.**
  That is the conservative direction.
- The other seven of #651's eight unexplained functions don't appear in one-tree builds. That is consistent with the
  two-worktree cause, which stays a named alternative.

## 17. The regression, POS's re-read, and POS closed (the reviewer's ruling, 18:47:55Z)

**The regression under 1f.** Each row's accepted reading was copied to a file before its re-read. K0 (accepted
17:46:38Z), K2, K3 and K4 all re-read **identical**, with exits 0, 1, 1 and 1. 1f moved nothing it shouldn't.

**POS re-read under 1f.** The checker was filed at 18:44:35Z (sha `86b6c699cc3a`). It predicted IN on
`blipscope-pro-s3-21` and, on `blipscope-s3-128`, UNDECIDED from `SPIClass::end` alone.
- **As predicted, on both envs:** the five out-of-scope functions read FILL, with their counts:

  | function | fill bytes A/B | width swaps |
  |---|---|---|
  | `ResetReasonName` | 2/5 | none |
  | `StampRebootCause` | 2/1 | `addi 2->3 x1` |
  | `TakeOtaMemReport` | 9/9 | none |
  | `DeferRebootWithCause` | 7/5 | `mov 2->3 x1, mov 3->2 x2, s32i 3->2 x1` |
  | `ota::Token` | 7/5 | none |

  `MaybeUpdateFirmware` reads CHANGED and `ota::ReadSome<StreamSource>` NEW, both in scope.
- **OFF, POS's second off reading:** the two FILL functions whose size changed (`ResetReasonName` 152 → 155,
  `DeferRebootWithCause` 336 → 332) read UNDECIDED at the image layer on both envs: "the image's property table ends
  at +0". So `blipscope-s3-128` reads UNDECIDED (3), and `blipscope-pro-s3-21` UNDECIDED (2), where IN was predicted.
- **The cause, read from the link's own map:** `OtaUpdater.cpp.o`'s `.xt.prop` (0xea0 B) is under "Discarded input
  sections". So are 3,445 property-table inputs in all. The image keeps 5,613 entries, and none covers these
  functions: the entries run from 0x420664c7 to 0x42067bac across `ResetReasonName` at 0x42066624.
- **The walk refused rather than guess. That is condition 2 working.**

**POS CLOSES under its row's second branch:** "IN SCOPE, or the comparator must name exactly which functions keep it
out, and why".
- Every out-of-scope function is named: five FILL with counts; two UNDECIDED with a cause read from the map;
  `SPIClass::end` UNDECIDED, as ruled.
- Against the filed exact predictions, POS read OFF twice, and both readings are on the record.

**No further amendment (ruled (B)).**
- Inferring the final copy's fill length by matching the next instruction was offered as (A). It isn't taken: it would
  be new inference logic, prompted by the same control a third time, and graded on that control again. That is a
  positive control the instrument keeps being adjusted toward.
- If fix-only skips should reach IN later, (A) is the named route, as its own registered piece of work.

**The cost, said plainly.** For a #651-like fix, the fresh-boot skip does NOT fire on either shipping env, so step 4
runs with the owner. #656's v20 job is map-client's accounting (P-REDO, P-FLIP). There, a named UNDECIDED line with its
cause is a usable result.

**Two findings for the record, neither assumed:**
- the firmware link discards most objects' Xtensa property tables (3,445 here), so the image's own `.xt.prop` covers
  only part of the code;
- in this build, WiFiManager carries one stamped string, and the second build stamp is the Arduino core's date-only one
  (section 12).

## 18. Amendment 3: P-REDO's expected set, as map-client froze it, before it is read (2026-11-12, 18:54Z)

**Copied from map-client's record,** `docs/blipscope-map-client-steps.md` on `feat/map-client-step-c-blend`, three pieces.
Each was frozen before anything was read:
- the set, at `0e0c394b68e0`;
- its amendment 1, at `239afa64`;
- its amendment 2, at `b2bc9671a129`.

**The pair, as built:**
- **Bw** = `e3c2508e` + `git merge --no-ff cb9d6ac2`; **Cw** = `e3c2508e` + `git merge --no-ff c4ff317a`. Both are merged
  locally in the run tree.
- Each `HEAD^{tree}` was checked against the registered tree **before** it was built: `eb84bef82d41` and `0b1d19f08e26`,
  both equal.
- Env `blipscope-s3-128-wxlook`, built from clean in one tree, with M0 and T0′ as for the controls.
- Read with `--allow` (the patterns below), `--cxxfilt`, and **`--reloc-names`**.

**`--reloc-names` (map-client's amendment 1, its definition confirmed in its amendment 2):**
- After each plain CHANGED function, it prints `relocations: unchanged` or `relocations: +[…] -[…]`.
- The names are the multiset of the function's relocation targets, its folded exception table included:
  - a branch inside the function is dropped;
  - `lit(sym:foo)` counts as `sym:foo`;
  - a literal holding a plain constant is dropped;
  - every other name is kept, strings included.
- It is off by default, so no accepted reading moves. Two flag-on cases were added:
  - `local relocs`: a constant change, `relocations: unchanged`;
  - `gc relocs`: a gained call, `+['sym:h+0', 'sym:h+0']`.
- The driver refused by itself until every plant declared both cases. P18 (the print omitted) was added. 18 plants ×
  32 cases read as declared.

**The expected set, by object:**
- **Every object but `AircraftManager.cpp.o` and `main.cpp.o` is identical,** `SoakHarness.cpp.o` included.
- **In `AircraftManager.cpp.o`:**
  - CHANGED by source: `AircraftManager::` `Initialise`, `MapConfigure`, `ConsumeBasemap`, `RebuildWeatherLayer`,
    `DrawWeatherUnder` and `ConsumeFetchResult`.
  - NEW: `AircraftManager::` `DrawMapBase`, `MapAirportBoxes` (its lambda included), `MapAlloc` and `MapFree`, and the
    header-only code they reach: `mapcompose::*` (`LayerSink`'s vtable included), `maplayer::*`, `mapplaces::*`,
    `bmpk::*`, `wxraster::Text`/`Put`, `glcd::font`, `wxlayer::OffDisc` and `wxs2::To565`.
  - `RecordFrameUs` is NOT changed: its hunk is under `FOLLOW_BENCH`.
- **In `main.cpp.o`:** `_GLOBAL__sub_I_tft` CHANGED.
- **Any other unit in those two objects with no source change** reads one of:
  - `CHANGED (load/store offsets only: k)`;
  - plain CHANGED with `relocations: unchanged`;
  - **FILL** (its amendment 2, 1a).

  A no-source plain CHANGED unit printing `relocations: +[…] -[…]` is **reported by name as the TU-wide inlining
  alternative**.
- **At the link:**
  - GC exactly `fmin`, `fmax` and `__fpclassifyd`. Nothing else enters or leaves; another library member is named,
    with its caller, and reported.
  - Every other difference is RELAX.
- **UNDECIDED (its amendment 2, 1b):** only `UNDECIDED <name> (FILL): size a -> b: the image's property table ends at
  +0`, for a FILL unit in the two objects. Each one is named, counted, and reported as UNACCOUNTED: a known blind spot
  of the instrument, never a pass for that unit. **Any other UNDECIDED is "stop and decide".**
- **A FILL unit in any other object** is a difference in an object predicted identical. It is reported by name, OFF.
- **The escape hatch, its own:** anything else means the instrument, or the model, is unreliable at this scale. Stop
  and decide.

**The checker** is filed with its sha256 before the read. It sorts every line of the reading into exactly one of:
expected, the counted classes (offsets-only, relocations unchanged, FILL, RELAX, UNACCOUNTED), REPORTED by name, or
STOP. Its outcome is the worst class present.

## 19. P-REDO, as read (2026-11-12, 18:57Z): STOP, under map-client's own rules

**Read** with instrument `5c56eda044ea`, the filed checker (sha `a99238d1a809`, 18:55:54Z; its three controls read
first: clean, STOP and REPORTED, each for the right reason), the allow file (`506cbcfce9b0`), `--cxxfilt` and
`--reloc-names`.
- **Builds:** both stores were built from clean in one tree, porcelain clean, M0 14 of 14, T0′ 3 fixed in 2 inputs.
  The merge trees were checked equal to the registered `eb84bef82d41` and `0b1d19f08e26` before building.
- **The reading:** 292 lines, exit 3; 8,090 functions moved. The checker sorted every line:

| class | count | what |
|---|---|---|
| expected | 54 | the two differing objects; the six changed by source; `_GLOBAL__sub_I_tft`; the NEW map code; GC exactly `fmin`, `fmax`, `__fpclassifyd` |
| relocations unchanged | 50 | no-source CHANGED functions in the two objects whose named targets are unchanged |
| FILL | 64 | no-source functions in the two objects that are the same instructions apart from the assembler's fill |
| offsets-only | 0 | none: the offset shifts surfaced as "relocations unchanged" instead |
| RELAX | 0 | none succeeded |
| UNACCOUNTED | 22 | size-changed FILL functions in `AircraftManager.cpp.o` whose final copy no image table covers: map-client's 1b blind spot |
| **REPORTED** | 3 | see below |
| **STOP** | 38 | see below |

**REPORTED, each by name:**
- `HandleTouch`: `+unit:sec:.rodata+436 -unit:sec:.rodata+392`. That is the object's **unsplit** `.rodata` moving
  under it, because new constants were added. A unit of an unsplit section is named by its offset, so this reads as a
  relocation change. **It is a naming artifact, not an inlining signal.**
- `UpdateBrightness`: `-sym:HWCDCSerial`. A reference to `Serial` is lost: a real codegen change, the inlining
  alternative.
- `DrawSweepOverWeather`: `+sym:wxs2::To565 x2`. It now calls the out-of-line `To565` that Cw instantiates: the
  inlining alternative.

**STOP, each by cause:**
- **`RecordFrameUs` CHANGED, with its relocations unchanged.** That is the offset-shift form: it reads members
  declared after `AircraftManager.h:725`. The set expected it unchanged because its own hunk is compiled out, and
  didn't allow for the shift. Map-client's rule: stop.
- **Three data units the set doesn't name:**
  - `AircraftManager.cpp.o`'s unsplit `.rodata`, named by many;
  - `main.cpp.o`'s `appManager`, named by `_GLOBAL__sub_I_tft`, `loop()` and `setup()`;
  - `main.cpp.o`'s `.gcc_except_table._GLOBAL__sub_I_tft`, unpaired. **A gap in 1e:** a static initializer lives in
    `.text.startup.<name>`, so pairing on `.text.<suffix>` misses it.
- **`not linked: CHANGED EnrichDeferredForSubmit`**: a changed function linked in neither image. The checker had no
  class for the line.
- **33 UNDECIDED in functions whose objects are identical,** at the image layer. RELAX's earning walk refused them:

  | count | cause |
  |---|---|
  | 19 | a flipped long call whose reach never crossed the limit |
  | 7 | an unrelocated instruction differs: the linker re-aligned |
  | 5 | a long-call site neither relaxed nor kept |
  | 1 | a relocated format the masks don't know |
  | 1 | a width change to a different operation |

  **The finding:** with a layout shift of about 14.5 KB, the linker's relaxation does more than 1a models (a deleted
  `l32r`, `callx` → `call`, narrowing). The walk refuses rather than guess, which is the conservative direction. In K4
  (+setup only) and POS (#651's patch) it met this once.

**The outcome is STOP: map-client's escape hatch, "the instrument, or the model, is unreliable at this scale". Nothing
is explained away, and the next step is the reviewer's and map-client's to decide.**

## 20. Amendments 1g and 1h′, and the regression under them (the reviewer's rulings, 18:59:34Z, 19:00:57Z, 19:02:32Z)

Both come from P-REDO's reading, and each was built with **independent fixtures, red first** against the committed
comparator (HEAD before them).

**1g: a static initializer's exception table is part of it.**
- 1e paired `.gcc_except_table.<s>` only with `.text.<s>`. A static initializer lives in `.text.startup.<s>`, so P-REDO
  read main.o's `_GLOBAL__sub_I_tft` table as a separate data unit.
- Now the table pairs with `.text.<s>` or `.text.startup.<s>`, and only when exactly one of them exists.
- **The probe came first:** a static initializer with an inlined try region compiles to
  `.text.startup._GLOBAL__sub_I_g_val` (39 B → 51 B) beside its table, whose bytes differ (`…0606 14…` → `…0812 0620…`).
- **`inittry`:** the old code read the initializer and its table, two lines; under 1g, the initializer alone.

**1h′: a target in an unsplit section is named by content.** It replaces the reviewer's 1h, which named by covering
symbol.
- **The reason:** real unsplit `.rodata` carries no symbols. AircraftManager.o's holds 500 B over 14 referenced
  offsets, main.o's 140 B over 7, and none has a symbol inside. The reviewer recorded its wording as its own miss.
- **The rule:**
  - (a) a covering symbol where one exists;
  - (b) otherwise the **content span**, from the target to the next distinct offset referenced anywhere in the object
    (or the section's end), with the relocations inside named;
  - **a span whose content repeats elsewhere in its section reads UNDECIDED**: content can't tell twins apart.

**1h′'s fixtures, in assembly (GCC merges identical initializers and orders constants itself):**

| case | what | old code | 1h′ |
|---|---|---|---|
| `rodshift` | f0's constant inserted ahead of f1's | f1 CHANGED (offset) | f1 absent: moved, not changed. f0 NEW. `sec:.rodata` named by f0, f1 |
| `rodown` | f1's own constant changed | f1 not reported | f1 CHANGED |
| `rodswap` | two byte-identical constants swap owners | f1, f2 CHANGED | **UNDECIDED (2)**, exit 3, exactly as filed |

- **`rodshift`'s first reading was off by one line:** `UNDECIDED entry: … a long-call site that is neither relaxed
  nor kept`, under both codes. That was my fixture's root, which **called** an undefined weak `f0`, a call the linker
  rewrites. The root now only takes the weak symbols' addresses, and the re-read is as predicted.
- **Declared before the run:** 1h′ moves the 1d `rodata` fixture, whose f1 now reads CHANGED (its constant carries its
  bytes). New expectations were filed for all three of its cases, and they read as filed.

**The plants:**
- 21 plants × 36 cases, ALL AS DECLARED. The driver refused by itself when the four cases were added.
- **New:** P19 (pair with `.text.` only) → `inittry`. P20 (unsplit spans named by offset again) → the three `rodata`
  cases, `rodshift`, `rodown`, `rodswap`. P21 (the twin refusal off) → `rodswap`.
- **Re-derived before their run:** P12 (+`rodshift`, `rodown`), P13 (+`inittry`), and P15 (+`rodown`, plus the three
  `rodata` cases moved from unaffected to red: their f1 now differs only in a relocation name, which P15 stops
  comparing).

**The regression, each prediction filed before its re-read (19:07:16Z), instrument `5756d185e02d`:**
- **K0, K3 and K4: unchanged, as predicted.**
- **K2 changed, as declared before the run:** `setup()` now reads CHANGED beside `sec:.rodata` (named by the same
  four). Its constant's bytes changed, so the function that reads it changed. That recovers K2's original row ("setup()
  …") for the right reason. It reads exactly the filed text.

## 21. Amendment 1i: LINK-ONLY (the owner's ruling, relayed 19:12:19Z), registered before any code (19:28Z)

**The owner's words, as relayed:** "A function whose compiled code is identical, differing only in where the linker
placed it, counts as accounted for, on two conditions: 'Identical' means identical after every address is replaced by
the symbol it points to, not just masked. The read-only data it reads is compared too. A control first: a planted
change that only swaps one call target, at the same size, must read as a real change."

### The class

A function reaches 1i only through the image layer's RELAX branch: a (name, size) difference whose unit the object
layer read SAME (or whose two PICK copies compare equal), and a walk that refused. It reads **LINK-ONLY** when both hold:

- **(a) object code equal.** Its object-layer signature is computed on BOTH sides and is equal: the size, the bytes
  with each relocated field masked, and every relocation's named target with its addend. This holds even when the input
  file's bytes are identical; nothing is taken from the file hash. A signature that refuses fails (a).
  - **Checked in the code (`_section_sig`): no field is merely masked.** Only R_32 (its 4 bytes) and SLOT0_OP (the
    operand bits) are masked, and both are then named by section 3's table. ASM_EXPAND and ASM_SIMPLIFY are named and
    not masked. R_NONE is neither (it modifies nothing). A target the table cannot name refuses the unit. The same holds
    inside unsplit spans (1h′: R_32 masked and named, any other type refuses) and in folded exception tables (1e).
  - **One limit, stated:** a local symbol is named by its name, and nothing refuses a local name that repeats in its
    object. GCC suffixes function-local statics (`counter.0`, `counter.1`), so C does not produce one, but no check
    stands behind that. A refusal can be added if the reviewer wants it; it is not part of 1i.
- **(b) the read-only data it reads is equal.** Take the function's named targets, then follow them:
  - **Content names** (`str:`, `cst:`, `lit:0x…`, `span:`): their bytes are in the signature, so (a) compared them.
  - **`lit(X)`:** the literal is in the signature; X is followed.
  - **`unit:`, `local:`, `sym:`:** the input section that holds the target, on each side. `unit:` and `local:` are in
    the function's own object. `sym:` goes through each image's symbol table and map.
    - **Read-only data** (allocated, neither writable nor executable): that section's whole signature must be equal on
      the two sides, and its own targets are followed the same way. A symbol in an unsplit section is compared as the
      whole section, so a change anywhere in it fails (b). That is conservative.
    - **Executable:** not followed. Code is accounted by its own units.
    - **Writable:** not followed. The ruling names read-only data; a changed initializer is reported as its own CHANGED
      unit, as now.
    - **Outside every section of the image** (ROM, absolute, an undefined weak at 0): the same address on both sides,
      not followed.
    - **Anything else** (no map entry, no unit, a name that repeats): fails (b).

**Printed, by name and counted:**
- One line per function, after the RELAX lines and before `functions whose address moved`:
  `LINK-ONLY <name> [<input>] -- size <a> -> <b>; the walk: <its refusal>; object code equal; read-only data equal: <n> section(s)`.
  The walk's refusal stays visible.
- The count is on the verdict: `verdict: <as now>; LINK-ONLY: <n>`, only when n > 0. With none, nothing new is printed.
- LINK-ONLY is accounted. It never adds to UNDECIDED, and it is never SAME.

**When (a) or (b) fails,** the UNDECIDED line is as now plus `; not LINK-ONLY: <why>`, one of:
- `its object code cannot be read: <refusal>`
- `it reads read-only data that differs: <section> [<input>], …`
- `a read-only datum it reads cannot be compared: <why>`

### Scope

- **Only the RELAX branch's refusals.** A FILL unit's image walk (the `(FILL)` UNDECIDED, P-REDO's 22) is **not
  reached**: its two copies are FILL-equivalent, not equal, so (a) fails by definition, and its lines are unchanged. If
  the ruling meant those too, it needs its own wording.
- Not reached either: no input section in the map, a locate failure, a repeated name with different sizes, GC.
- **What the real walk refusals are** (P-REDO's 33, probed read-only before writing this):
  - 19 "flipped, but its reach did not cross": ld decides on distances before relaxation.
  - Probed 3 of the 7 "an unrelocated instruction differs": each stops on zero fill (`00 00 00` in the input where the
    image has the next instruction). That is the linker re-laying alignment fill.
  - 5 "neither relaxed nor kept", 1 width, 1 mask format: not probed.

### What 1i trusts: the blind spot, stated

**1i trusts the linker for object-identical functions. It does not check the image's code against its input.**
Anything that makes an image differ from the link of its recorded inputs reads LINK-ONLY. The selftest's R3, R4 and
R5 are exactly that: each is made by patching the image.
- R3 changes a symbol size.
- R4 changes a register (`mov.n a10` → `a11`).
- R5 points a call at 0x40070000, where nothing is.

Under 1i all three read LINK-ONLY, verdict NO CHANGE, exit 0. What keeps such an image out is how a store is built
(one tree from clean, M0's tie, every input's sha256), not this comparator.

**Proposed, not part of 1i unless ruled: (c) the call sites the walk reached.** Each resolved site's target (a `call8`
target, or a literal's word) must be the image's address of the symbol its input names; otherwise not LINK-ONLY.
- **It would close R5.** The walk collects every site before the reach check, so the 19 real flips are fully covered.
- **It would not close R4** (a register), nor any site past an early stop.
- **Predicted under (c):** R5 reads `UNDECIDED lo_caller: size 12 -> 16: site 0 flipped, but its reach did not cross
  the call limit; not LINK-ONLY: site 0 calls 0x40070000; its input names hi_callee, at 0x40090000`, verdict
  UNDECIDED (1), exit 3. Everything else as below.

### The controls, built first

1. **The owner's, `callswap`.** Two assembly variants: `_call8 fa` and `_call8 fb`. The underscore stops the assembler
   turning it into a long call, so the swap is one instruction of the same length. `fa` and `fb` are both kept (their
   addresses are taken).
   - **`callswap`**, as built: CHANGED.
   - **`L1 the call swap, in front of LINK-ONLY`**: B's image with `caller`'s size +2 (R3's method), so that the
     function would stand in front of LINK-ONLY if its identity were masked only. Reads CHANGED, never LINK-ONLY.
   - **Red:** P22 (a call's target masked, not named). `callswap` then reads NO CHANGE, silently. L1 reads
     `LINK-ONLY caller`, which is exactly the failure the owner named.
2. **The second, `rodread`.**
   - **`rr.c`** holds `tbl` (global), `ktbl` (global, never changed), `ltbl` (static) and `rl` (reads `ltbl`).
     **`rd.c`** holds `rd` (reads `tbl`) and `rk` (reads `ktbl`).
   - **B** changes `tbl`'s and `ltbl`'s last word. The sizes are the same.
   - **`rodread`**, as built: `tbl` and `ltbl` read CHANGED.
   - **`L2 read-only data, in front of LINK-ONLY`**: B's image with `rd`, `rk` and `rl` each +2. `rd` and `rl` read
     UNDECIDED, not LINK-ONLY (their data differs). `rk` reads LINK-ONLY (its one section is equal). This exercises
     all three paths: `sym:` across objects, the local `unit:`/`local:`, and a pass.
   - **Red:** P23 ((b) skipped).
3. **`R10 unreadable object code is never LINK-ONLY`.** `null`, with `relax.c.o`'s first SLOT0_OP relocation set to
   type 17 in BOTH stores (so the inputs stay identical), and B's image `lo_caller` +2.
   - Checked first: in `null`, `lo_caller`'s long call stays long, nothing narrows, and both walks complete.
   - **Red:** P24 ((a)'s refusal read as equal).

### Every new or moved case, its whole output, filed now

Sizes come from the stores' symbol tables, as R3's already do. ext is 7, lo_caller is 12/16 in `relax` and 16 in
`null`. callswap's and rodread's sizes are read once their stores are built.

| case | exit | output |
|---|---|---|
| `callswap` | 1 | `inputs: 3 loaded, 1 differ` / `  differs: src/cs.S.o` / `functions whose address moved: 0` / `CHANGED func global A+B caller [src/cs.S.o] -- OUT` / `verdict: OUT OF SCOPE: caller` |
| `L1` | 1 | exactly `callswap`'s |
| `rodread` | 1 | `inputs: 3 loaded, 1 differ` / `  differs: src/rr.c.o` / `functions whose address moved: 0` / `CHANGED data local A+B ltbl [src/rr.c.o] -- OUT (named by: rl)` / `CHANGED data global A+B tbl [src/rr.c.o] -- OUT (named by: no kept unit)` / `verdict: OUT OF SCOPE: ltbl (named by: rl), tbl (named by: no kept unit)` |
| `L2` | 3 | `inputs: 3 loaded, 1 differ` / `  differs: src/rr.c.o` / `LINK-ONLY rk [src/rd.c.o] -- size K -> K+2; the walk: its size differs and it has no long-call site; object code equal; read-only data equal: 1 section(s)` / `functions whose address moved: 0` / the two CHANGED lines of `rodread` / `UNDECIDED rd: size D -> D+2: its size differs and it has no long-call site; not LINK-ONLY: it reads read-only data that differs: tbl [src/rr.c.o]` / `UNDECIDED rl: size L -> L+2: its size differs and it has no long-call site; not LINK-ONLY: it reads read-only data that differs: ltbl [src/rr.c.o]` / `verdict: UNDECIDED (2); LINK-ONLY: 1` |
| `R3` (moved) | 0 | `inputs: 7 loaded, 0 differ` / `LINK-ONLY ext [src/ext.c.o] -- size 7 -> 9; the walk: its size differs and it has no long-call site; object code equal; read-only data equal: 0 section(s)` / `functions whose address moved: 0` / `verdict: NO CHANGE; LINK-ONLY: 1` |
| `R4` (moved) | 0 | `inputs: 7 loaded, 0 differ` / `LINK-ONLY lo_caller [src/relax.c.o] -- size 12 -> 16; the walk: an instruction changed width, not to its own narrow form, at input +3; object code equal; read-only data equal: 0 section(s)` / `functions whose address moved: 7` / `verdict: NO CHANGE; LINK-ONLY: 1` |
| `R5` (moved) | 0 | as R4, with the walk's `site 0 flipped, but its reach did not cross the call limit` |
| `R10` | 3 | `inputs: 7 loaded, 0 differ` / `functions whose address moved: 0` / `UNDECIDED lo_caller: size 16 -> 18: the walk ends 2 bytes before the function does; not LINK-ONLY: its object code cannot be read: relocation type 17 in a compared section` / `verdict: UNDECIDED (1)` |

- The other 33 cases: unchanged, word for word. None has a walk refusal, and with none, 1i prints nothing.
- **Red first:** the new selftest (41 cases) against the committed comparator is predicted red on exactly R3, R4, R5,
  L2 and R10, and green on `callswap`, L1, `rodread` and the other 33. `callswap`, L1 and `rodread` are controls: they
  pass on correct code, and their red is P22's and P23's.

### The plants, each row filed in the driver before its run

| plant | the five new cases it turns red |
|---|---|
| P5 (type 17 allowed) | R10 (the signature computes, so (a) holds) |
| P12 (a data unit's referrers not printed) | `rodread`, L2 |
| P15 (the fill walk's relocation names not compared) | `callswap`, L1: with those names off, `caller`'s two copies are the same instructions, so it reads FILL |
| **new P22** (a call's target masked, not named) | `callswap`, L1 |
| **new P23** ((b) skipped) | L2 |
| **new P24** ((a)'s refusal read as equal) | R10 |
| every other plant | none |

- **R3, R4, R5: every plant's declaration is unchanged.** P2 turns R4 to RELAX. P3 turns R5 to RELAX. P6 turns
  `relax`, R4 and R5 to LINK-ONLY with the walk's reason `width`. Each of those is still red.
- **24 plants × 41 cases.** Any plant off its row stops for a ruling.

### The regression, filed

- **K0, K2, K3 and K4: unchanged, byte for byte** (the 19:07Z outputs). None carries a walk refusal, so 1i adds
  nothing to them.
- POS and P-REDO are not re-read. map-client registers a fresh pair after 1i.

### The escape hatch

Any reading outside this section is a stop. It is reported, and nothing is fixed before the ruling.

## 22. Amendment 1i′: (c) and the local-name refusal (the reviewer's rulings, 19:33Z), registered before any code

1i (section 21) was accepted with three rulings:
- **Q1: no extension.** The FILL units stay outside LINK-ONLY and stay reported. If they stop P-REDO or P-FLIP, that
  is a finding for the owner.
- **Q2: 1i + (c).**
- **The local-name refusal: ruled in.**

Everything else in section 21 stands, except where this section replaces an output.

### (c): every call site the walk reached is named in the image

- **What is checked.** For a LINK-ONLY candidate that passed (a) and (b), each side's walk is run again to collect
  its call sites, as far as it gets.
  - The run uses `need_sites` off, so a function with no long-call site is still walked.
  - **A call site** is a long-call site (relaxed or kept), or a direct call instruction (`call0`-`call12`) under a
    relocation.
  - Each site's target in the image (the `call`'s target, or the kept long call's literal word) must equal the image
    address of what the input's relocation at that site names. That is the long-call marker's symbol, or the direct
    call's SLOT0_OP symbol, plus its addend.
  - The address comes from the image itself: a global's value from its symbol table, or a section or local symbol's
    input section placed by the map.
  - The relocations are read from the object directly, not from the signature.
- **A mismatch:** `; not LINK-ONLY: <A|B>'s site <k> calls 0x<actual>; its input names <name>, at 0x<expected>`.
  Several mismatches are joined with `; `.
- **A target that cannot be placed:** `; not LINK-ONLY: <A|B>'s site <k>'s target cannot be placed: <why>`.
- **Order:** (a), then (b), then (c). The first that fails is printed.
- **The stated limit, printed on every LINK-ONLY line.** Non-call bytes, and sites past where a walk stopped, are not
  checked. The line now ends: `; call sites checked A/B: <x>/<y>; non-call bytes trusted to the link (store built from
  M0, inputs hashed)`. R4 (a register) is that limit, and its own line shows A's walk checked no site.
- **Not covered, stated:** RELAX's own sites (a walk that succeeds) are not run through (c). That is unchanged from
  before 1i and not ruled.

### The local-name refusal

- A target named by a local symbol (not a section or file symbol) whose name another local of its object shares
  refuses: `a local name that repeats in its object: <name>`.
- It applies wherever a local's name names a target: section 3's `local:` and 1h′'s covering symbol, through one
  check.
- **Checked before writing this, read-only:** no fixture object carries a relocation against a named local (the
  assembler turns them into section symbols). The K rows' differing inputs carry 10 (main.o), none naming a repeated
  local. No named local repeats in any of those objects or in any fixture object. So no existing reading can move.

### R11, the refusal's synthetic red case

**`R11 a local name that repeats refuses`:** the `local` fixture, with the same three byte patches in BOTH stores'
`f.c.o`, each asserted before the read:
- the literal's R_32 is pointed from `.text.helper`'s section symbol to `helper`'s own symbol (the same target, now
  named by a local name);
- the FILE symbol becomes a local function symbol (type byte);
- its name becomes `helper` (st_name).

### Outputs this amendment replaces or adds, whole, filed now

| case | exit | output |
|---|---|---|
| `R3` | 0 | as section 21, with the LINK-ONLY line ending `read-only data equal: 0 section(s); call sites checked A/B: 0/0; non-call bytes trusted to the link (store built from M0, inputs hashed)` |
| `R4` | 0 | as section 21, with the LINK-ONLY line ending `read-only data equal: 0 section(s); call sites checked A/B: 0/1; non-call bytes trusted to the link (store built from M0, inputs hashed)`. A's walk stops at input +3, before the site. |
| `R5` | 3 | `inputs: 7 loaded, 0 differ` / `functions whose address moved: 7` / `UNDECIDED lo_caller: size 12 -> 16: site 0 flipped, but its reach did not cross the call limit; not LINK-ONLY: B's site 0 calls 0x40070000; its input names hi_callee, at 0x40090000` / `verdict: UNDECIDED (1)` |
| `L2` | 3 | as section 21, with `rk`'s line ending `read-only data equal: 1 section(s); call sites checked A/B: 0/0; non-call bytes trusted to the link (store built from M0, inputs hashed)` |
| `R11` | 3 | `inputs: 7 loaded, 1 differ` / `  differs: src/f.c.o` / `functions whose address moved: 0` / `CHANGED func local A+B helper [src/f.c.o] -- OUT` / `UNDECIDED f [src/f.c.o]: a local name that repeats in its object: helper` / `verdict: UNDECIDED (1)` |

- `callswap`, L1, `rodread`, R10 and the other 33: as section 21.
- **42 cases.**
- **Red first,** against e25f0c7c's comparator: red on exactly R3, R4, R5, L2, R10 and R11. Green on `callswap`,
  L1, `rodread` and the other 33.
- **R5 under 1i without (c)** (the reviewer's red-first): R5 reads LINK-ONLY, NO CHANGE. That is P25's reading
  below.

### The plants: 26 × 42, rows filed in the driver before its run

| plant | the six new cases it turns red |
|---|---|
| P5 (type 17 allowed) | R10 |
| P9b (MOVI counted as an offset) | R11: `helper` reads offsets-only, as in `local` |
| P12 (a data unit's referrers not printed) | `rodread`, L2 |
| P15 (the fill walk's relocation names not compared) | `callswap`, L1 |
| P22 (a call's target masked, not named) | `callswap`, L1. L1 then reads `LINK-ONLY caller … call sites checked A/B: 1/1`: (c) holds, since each side calls what its own input names |
| P23 ((b) skipped) | L2 |
| P24 ((a)'s refusal read as equal) | R10 |
| **new P25** ((c) skipped: a mismatch never refuses) | R5, which reads LINK-ONLY, NO CHANGE |
| **new P26** (a repeated local name not refused) | R11: `f` reads SAME, so the UNDECIDED line is gone |
| every other plant | none |

- **R3, R4 and R5's rows are unchanged:**
  - P2 turns R4 to RELAX.
  - P3 turns R5 to RELAX.
  - P6 turns `relax` and R4 to LINK-ONLY with the walk's reason `width`. It turns R5 to UNDECIDED with reason
    `width` and (c)'s mismatch.
- Any plant off its row stops for a ruling.

### The regression

K0, K2, K3 and K4 are unchanged byte for byte. No walk refusal, and no repeated local in their differing inputs.
