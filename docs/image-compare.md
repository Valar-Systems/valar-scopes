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
