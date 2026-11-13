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
| `pq4d-oldrows.mts` | bfb4b0526f78 | the values for the code PR's rewritten MB / MN / MW13 rows, from pq4d on the pinned synthetic fixtures |
| `oldrows-pq4d.json` | c4d9bad3b26c | its output, as frozen in the registration |
| `cp-run.mts` | 2eaa14c28661 | one build by the code PR's builder over a local store (SEA-PKG's harness) |
| `cp-seapkg.py` | e3bd13ca7f16 | SEA-PKG: the 16 packages against run 10's sha256s |

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
