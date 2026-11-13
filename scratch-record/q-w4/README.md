# Q-W4's scratch record: the prototype pq4d's generator, and the scripts that produced the code PR's prototype-derived values

Scratch tooling, kept so the values the code PR (docs/basemap-dense-code.md, part 2's Q-W4) asserts can be regenerated. None of it is
shipped code, and nothing here reads a credential, a board or a production service.

## What is here (sha256, first 12)

| file | sha256 | what it is |
|---|---|---|
| `mk-pq4d.mjs` | 27d3a9f5b4c0 | generates the prototype `pq4d` from the pinned commit 525cf7d1725d (copies its `proxy/src/basemap`, `proxy/scripts/basemap` and the synthetic-store test helper by `git show`, then adds the Q-W4 knobs) |
| `mk-pq4d-sea.part.mjs` | 6f9850543279 | the generator's sea part, kept identical to its section of `mk-pq4d.mjs` |
| `pq4d-sea.part.ts` | ab03ff2600d2 | the prototype's `sea.ts` |
| `pq4d-frame-lock.part.ts` | b367cb15ca8e | the prototype's edge-locked simplify and KB4 (B') repair, spliced into its `frame.ts` |
| `dnw/src/basemap/frame.ts` | e1ab75e3cae9 | the dense-water prototype's `frame.ts`, the generator's fourth argument (tp's source) |
| `pq4d-oldrows.mts` | ff6351759648 | the values for the code PR's rewritten MB / MN / MW13 rows, from pq4d on the pinned synthetic fixtures |
| `oldrows-pq4d.json` | a5393eea0e57 | its output, as frozen in the registration |
| `pq4d-seaci.mts` | 7a192acf82cb | the SEA-CI tests' values (and, in plant mode, the sabotages' predicted red sets) from pq4d on the tests' own fixtures |
| `seaci/seaci-pq4d.json` | cbf66229187f | its clean output; `seaci/seaci-plant-*.json` its seven plant outputs |
| `pq4d-xchk.mts` | 00129dbe7053 | xisland's repair line and xisland-tall's stop, from pq4d |
| `cp-run.mts` | 2eaa14c28661 | one build by the code PR's builder over a local store (SEA-PKG's harness) |
| `cp-seapkg.py` | e3bd13ca7f16 | SEA-PKG: the 16 packages against run 10's sha256s |
| `seafig/seapkg-0653Z.json` | f73cfc9b84e6 | SEA-PKG's output at 82113178 (06:53:58-06:54:05Z): 16 of 16 byte-identical |
| `seafig/sf-run.mts` | 51c8bf0fca58 | SEA-FIG's build child: one build by the code PR's builder, with every piece's fate, ringIdx, PROVENANCE's dropped.water |
| `seafig/sf-grade.run1.py` | 152e123d4cf4 | SEA-FIG's grader as first run (07:02:53Z): run 10's definitions (dr-p2c.mts:57), the consistency check, run 10's own pieces |
| `seafig/sf-grade.py` | 2bbabb5d5be9 | the same grader with its control's two plant hooks (`SF_PLANT=drawn-sc` / `order`); reversing them gives 152e123d4cf4 |
| `seafig/sf-grade.out` | 724a193379c4 | SEA-FIG's reading, 413 checks, 0 off (both unplanted runs) |
| `seafig/sf-out.json` | 28f8343a47b8 | the 16 builds' results the grader read |
| `seafig/control-pred.txt` | 9117f6b99e4d | the control's predictions, written before the replay |
| `seafig/control-drawn-sc.out`, `control-order.out` | 0870aeb5f944, 63e2066669ea | the control's readings: 5 off and 1 off, as predicted |
| `seafig/u1-ksea.py` | 30bdaa902b56 | scope ruling u1's input hashes and the KSEA guard on the code PR's package |
| `seafig/u1-ksea.out` | 3fc4621fda73 | its reading: 01ddfab4ae28 both sides; KSEA 0.886081 PASS |
| `cm/mk-cm.mjs` | 751201f6d83f | the two trees from their commits: the code PR's byte-identical, main's with mk-hooks' seven lines (hooked build.ts f60ae55f45d9, #639's) |
| `cm/dr-cm.mts` | 3a98e065bd05 | the memory child: dr-p2m.mts with the tree by name, the child-held plant, and R-KMSP's nocap8 mode |
| `cm/cm-run.py` | 8a9400a4af7b | the driver, groups rkmsp / rd2 / seamem / cm / gate; `cm-run.272c0a2b7bf8.py` is the registered version whose gate matched itself |
| `cm/gate-control-pred.txt` | a597def69d5d | the gate fix's control, predicted before it ran |
| `cm/rkmsp.out`, `rd2.out`, `seamem.out`, `cm.out` | 7526f6d595bd, 803226cfa9ea, 4b9817bfc216, 6f571098aad2 | the four groups' readings, all AS PREDICTED |
| `cm/seamem-builds.r4.json`, `cm-builds.r4.json` | 421312abd1fb, f4e7ce06c75e | the builds behind seamem and cm, floats rounded to 4 places. The full-precision files the doc cites (770f80633dbe, 373bc70b3512) stay in scratch: their 16-digit float tails trip the device-id hook |
| `cm/rv-meas.log` | fc43b98c419e | #639's measurement run, CM-0's references (the only record of its cell phase) |
| `cm/dr-q4c3.mts`, `pk-cp.mts`, `pk-grade.py` | b02b5a66d240, c9ac9a5c5a4f, 3d5f5c8a62f2 | the 5.6 package rows: QW4-C's child for pq4b's references, the code PR's twin, the grader |

## Regenerating

From a directory holding a checkout of this repository as `wt-q4` (any commit that has 525cf7d1725d) and these files:

```
node mk-pq4d.mjs wt-q4 525cf7d1725da76f7647b8d64949d9dbaf0897ec pq4d dnw/src/basemap/frame.ts
```

**The control that this record is complete:** that command, run from this record into a fresh directory, reproduced all 18 of the
measured pq4d's files byte for byte (2026-11-13). Then, with `tsx` resolvable (run from a `proxy/` that has `node_modules`, with the
paths adjusted):

```
Q4_LADDER=b Q4_D2=1 Q4_TP=1 Q4_PROV=1 Q4_SEA=1 node --import tsx pq4d-oldrows.mts > oldrows-pq4d.json
```

`pq4d-oldrows.mts` refuses to run under any other settings. `cp-run.mts` imports the code PR's builder as `./wt-code/proxy/src/basemap`
and reads a store directory given on its command line; `cp-seapkg.py` drives it over the 16 points.
