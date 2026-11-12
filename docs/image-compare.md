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
