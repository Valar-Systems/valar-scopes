# Missileer: the E-4B table holds the E-4Bs (from #409). Predictions, frozen before the code

**The ruling** (reviewer chat, pasted by Daniel, 2026-10-28): Daniel gave #409 to this session ("You can fix it").
Now, a small PR off main swaps `kE4bHexes` in `src/eam/EamModels.cpp` to `adfeb3`, `adfeb4`, `adfeb5`, `adfeb6`. It
replaces the PLACEHOLDER comment with the citations #409 already carries (serials 73-1676, 73-1677, 74-0787, 75-0125).
Nothing else goes in it. The rest of #409 is rebased after #445 merges.

**Why.** Main's table is `ae0451`-`ae0454`, marked PLACEHOLDER. #409's citations show all four are US Navy C-2A
Greyhounds. `ClassifyAbncp()` names them "E-4B", and `DefaultAbncpWatch()` seeds the OpenSky watch with them.

**It reaches devices without a migration.**
- The `abncp-watch` field is blank by default (`prefs.getString("abncp-watch", "")`), and a blank or unparsable list
  falls back to `DefaultAbncpWatch()` (`src/eam/EamManager.cpp`). So the new table reaches every Missileer whose owner
  never typed a list. A list someone typed is theirs.
- It reaches devices at the next Missileer release, which is Daniel's promote.

| | check | predicted |
|---|---|---|
| **T1** | `scripts/check_e4b_table.py` reads `src/eam/EamModels.cpp`. The `kE4bHexes` initializer must appear exactly once and hold exactly `adfeb3`, `adfeb4`, `adfeb5`, `adfeb6` (in that order, lower case, no duplicates), and none of `ae0451`-`ae0454` | **main's file:** exit 1, which is the control that the check sees the bad table. **The branch:** exit 0 |
| **T1-S** | **the ruled sabotage:** the real file's text with one C-2A address put back (`adfeb6` → `ae0454`), done in memory. The file is never written, and its sha256 is compared before and after | the check reports **FAIL** on it, so the selftest passes. **The real file is unchanged** |
| **T1-R** | the rig: the initializer missing, or present twice | **exit 2**, never a pass |
| **T2** | the artifact: each `missileer-*` ELF (`missileer-s3-146`, `missileer-s3-128`, `missileer-photo`, the only envs that compile `src/eam`), scanned by the same script | **main's ELFs:** `ae0451`-`ae0454` present, the control that the scan can see them. **The branch's ELFs:** each of `adfeb3`-`adfeb6` present and each of `ae0451`-`ae0454` absent. In both, "E-4B" is present, so the scan reads the ELF |
| **T3** | the builds | every `missileer-*` env builds |
| **T4** | CI | 0 failures. T1 and T1-S run in the host-tests job through `test/host/run.sh` |
| **T5** | the scope | the diff touches only `src/eam/EamModels.cpp` (the table and its comment), `scripts/check_e4b_table.py`, one block in `test/host/run.sh`, and this doc |

**Outside the set:** stop and report.

**Not changed, by the ruling:** the config page's hint beside the watch list ("Seeded with the E-4B "Nightwatch" hexes
(verify them)"). Its wording is the rest of #409's business.

## Results (local, before CI)

| | read | verdict |
|---|---|---|
| **T1** | **main's file:** exit 1, "the table holds C-2A address(es) ae0451..ae0454". **The branch:** exit 0, "exactly adfeb3, adfeb4, adfeb5, adfeb6" | **as predicted** |
| **T1-S** | the real file's text with `adfeb6` → `ae0454`, in memory: applied, exit 1. The real file was unchanged (`ba93fa411a92`) | **as predicted** |
| **T1-R** | no table: exit 2. Two tables: exit 2. With six other synthetic cases, all eight are as expected | **as predicted** |
| **T2** | **main's three ELFs:** the four E-4B strings missing, and `ae0451`-`ae0454` present (the control). **The branch's three:** all four E-4B strings present, no C-2A string. "E-4B" was present in all six | **as predicted** |
| **T3** | `missileer-s3-146`, `missileer-s3-128` and `missileer-photo` all build | **as predicted** |
| **T5** | `src/eam/EamModels.cpp` (the table and its comment), `scripts/check_e4b_table.py`, one block in `test/host/run.sh`, and this doc | **as predicted** |
| **host suite, WSL** `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0` | the new block: SELFTEST PASSED and the table ok. The only FAIL line is the known gap, "no xtensa toolchain found" (the board block), which fails the same way on main | — |

T4 (CI) follows on the PR.
