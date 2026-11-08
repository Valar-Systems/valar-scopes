# The leaderboard refuses an off-shape model instead of cutting it

**Read this first.** `proxy/src/leaderboard.ts` stores each submission's `X-Blip-Model` with
`.trim().slice(0, 16)`. Weatherscope's W2 (`docs/weatherscope-stage4.md`, S4.1.2) makes the model the device's OTA identity,
scope followed by board, and `weatherscope-s3-128` is 19 characters. Cut to 16 it becomes `weatherscope-s3-`, a value with the
shape of a real one that no device ever sent. That is CLAUDE.md's "a truncated row is a lie": a model is drawn from a known set,
so the honest answer for a value outside that set's shape is no value at all. **Ruled 2026-11-08 ~19:07Z:** a small PR of mine,
predictions first, merged before W7 sends the header.

## The design, frozen

**The shape:** lowercase letters and digits in hyphen-separated runs, at most 32 characters. That is `^[a-z0-9]+(-[a-z0-9]+)*$`.
Every identity the tree can produce fits it. The board slugs are `s3-128`, `s3-146`, `s3-175-amoled` and `s3-21`. The scopes are
`birding-`, `claudescope-`, `eam-`, `fishing-`, `seismic-`, `space-`, `speed-` and `weatherscope-`. The longest combination,
`weatherscope-s3-175-amoled`, is 26 characters.

**What is refused is the value, not the submission.** An off-shape model is stored as `""`, exactly as a device that sends no header
is stored, and the submission is still accepted and scored. A device's standing must not depend on a header its owner cannot see
or change. If the reviewer wants the whole submission refused instead, that is a one-line change to this design before the code.

**Where:** one helper in `leaderboard.ts`, used at the one place the header is read. Nothing else in the Worker reads the model
through the leaderboard.

## Predicted

Each row is a test in `proxy/test/leaderboard.test.ts`, named with its tag:

| row | predicted |
|---|---|
| **LM1** | a submission with `X-Blip-Model: weatherscope-s3-128` stores `weatherscope-s3-128` whole. Today it stores `weatherscope-s3-` |
| **LM2** | `s3-128` is stored as `s3-128`: the radar's model is unchanged |
| **LM3** | each off-shape value is stored as `""`, never as a prefix of itself, and each submission is still answered 200 and stored. The values are 40 lowercase letters, `S3-128`, `s3 128`, `s3-128<b>` and `-s3-128` |
| **LM4** | no header stores `""`, as today |

**Sabotages**, run by `proxy/scripts/sabotage-lbmodel.mjs`. Each is an in-place edit asserted to apply exactly once, the file is
restored from a byte copy and checked by sha256, and the set of red tests must equal the registered set exactly:

| plant | the edit | red, exactly |
|---|---|---|
| **S-LM-slice** | today's `.slice(0, 16)` restored, with no shape check | LM1 (cut to 16), LM3 (the 40-letter value is stored as its first 16) |
| **S-LM-lax** | the shape check accepts anything | LM3 |

A crash or an unparseable run is an instrument failure, never a red.

**Outside the set:** any other row red, a plant that turns no row red, or an anchor that does not match once. I stop and report,
with nothing improvised on the spot.

## CI prediction for the PR

**35 = 32 success / 3 skipped / 0 failed.** The docs-only base is 32 = 29/3/0, plus `workers.yml`'s three jobs, which run because
the PR changes `proxy/**`. That matches #596's finished run. The last job-set change on main is `84409651`, re-derived from the
workflows' history; #618 added steps only.
