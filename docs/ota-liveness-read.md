# Were the behind-but-"ok" devices awake at their 03:00? (registration; predictions before the code)

**Ruled** by the orchestrator (2026-11-13 17:44Z), after v19's step 8 day 2 read OUTSIDE THE SET on P4. 5 devices
behind version.txt have "ok" as their last OTA result, but they did not take v19 overnight. Three causes are open:
- **(a)** off, or making no request, at their local 03:00 (the quiet-hour update check);
- **(b)** the check was refused: the quiet policy's 20 h cap after another reboot, or an unsynced clock;
- **(c)** it checked, the update failed, and v18 does not report the failure.

This line can **exclude (a)** per device. It cannot separate (b) from (c).

## The change

One new line in the A3.1 block of `scripts/reconcile-fleet.py`, under "by last OTA result":

    of the K behind with last result ok: present every hour of the last 24 h X, with gaps Y, no request Z (AE sample interval max S)

- **Window:** the 24 whole UTC hours before the run. Any 24 consecutive whole UTC hours hold exactly one local 03:00
  for every zone, half-hour and 45-minute offsets included. So presence in all 24 means the device was present at the
  last 03:00 it faced. **No zone is inferred, ever**: a zone would come from a location.
  - **For day 3** (~16:30Z on 11-14) the window is the night of 11-13 to 11-14, the second night since promote. A
    device present all night and still behind with "ok" is (b) or (c), with (a) excluded.
- **Presence:** at least one per-request Analytics Engine point in that hour whose `blob5` is the device.
  - Points are selected positively, by route (`index1 LIKE '/%'`, the only shape `routeTemplate` returns). Boot,
    OTA, usage, error and gap points are not presence.
  - The device ids stay inside the script. Only counts print.
- **The words** (condition 1): "present every hour", "with gaps", "no request". **Never "offline."**
  - A device on OpenSky or a local feed makes no `/v1/blips` request and can leave whole hours empty while it is on.
  - So a gap is evidence of no request, not of power-off. It leaves (a) open.
  - Only a device present in all 24 hours has (a) excluded.
- **Sampling** (condition 2). The query asks for presence, not volume.
  - **Analytics Engine's adaptive sampling** can only drop points. So it can turn presence into a gap (the
    conservative direction, which leaves (a) open), and can never invent presence. The line prints the largest
    `_sample_interval` it saw: S = 1 means Analytics Engine sampled nothing in the window. That is measured, not
    assumed. S > 1 adds a warning that a gap may be sampling, not silence.
  - **The Worker's own 1:10 sampling** applies to `/v1/blips` HIT and STALE points only. At a 10-20 s poll, a live
    device still leaves about 18-36 sampled points in an all-hit hour.
- **Unreadable:** if Analytics Engine is unreachable or refuses, the line reads "liveness unread: <reason>", with a
  warning. It is never counted as gaps.
- **Shape** (condition 3): the line contains no 16-hex token. This is asserted, the way `usage::Format`'s payload is.

## Predictions

**Selftest** (`scripts/reconcile-fleet.selftest.py`). Fixtures only, EXAMPLE ids, no network:

| row | fixture | predicted |
|---|---|---|
| **LV1** | behind with "ok": A (all 24 h), B (20 of 24 h), C (no point). D is behind with "no ota point" and present every hour. E is not behind. S = 1 | exactly `    of the 3 behind with last result ok: present every hour of the last 24 h 1, with gaps 1, no request 1 (AE sample interval max 1)`, no warning. D and E are not counted |
| **LV2** | LV1's line | no 16-hex token |
| **LV2-ctl** | LV1's line | matches the full shape, with its three counts and S, so LV2 is not passing on an empty or truncated line |
| **LV-S1** (plant, in memory) | the line's format gains the ids | LV2 red; LV2-ctl as predicted |
| **LV3** | Analytics Engine unreadable | `liveness unread: <reason>` and one warning; no counts printed |
| **LV4** | no behind device has "ok" | `of the 0 behind with last result ok: nothing to read`; no query made |
| **LV5** | as LV1, with S = 10 | `(AE sample interval max 10)` and one warning naming sampling |
| every existing row | unchanged | as before; `SELFTEST PASSED` |

**Rehearsed red first:** against main's script, every LV row reads BLIND, because the reader does not exist yet.

**Live, once, by hand:** the full read, with stdout piped through the same filter as step 8 day 2 (ids never on disk
or screen). The values X, Y and Z belong to day 3's predictions. This PR predicts structure only:
- the line appears;
- K equals the same run's "ok" bucket;
- X + Y + Z = K;
- the line holds no 16-hex token;
- S is printed.

The query's syntax is first exercised in this run. A refusal there reads "liveness unread" with Analytics Engine's
own reason, and is reported as is.

**CI: 36 checks, 34 success and 2 skipped, as #705** (the same kinds of file: this script, its selftest, docs). Any
other count gets the differing job named before anything is judged.

**Outside the set:** any other reading means stop and report.

## Amendment 1 (18:15:26Z, 2026-11-13), registered before the re-run

**The OFF that caused it.** After the code, the existing row S-R1body read BLIND: "its anchor was found 2 times". The
registration said every existing row would read as before. `read_liveness_rows` had copied `read_ota_points`'
HTTPError line verbatim, so S-R1body's anchor was no longer unique. The guard refused rather than planting into the
wrong function, which is CLAUDE.md's editor-anchor row working.

**The fix** (ruled 18:17Z): `read_liveness_rows`' error gets its own wording,
`"Analytics Engine refused the liveness query (%d): %s"`. S-R1body's anchor is unique again, and it tests
`read_ota_points` as registered. S-R1body is not widened.

**Two new rows,** because the copied path is a second path (CLAUDE.md: whatever S-R1body establishes for the old reader is
unproven for the new one):

| row | what | predicted |
|---|---|---|
| **LV6** | `read_liveness_rows` against a stub `urlopen` that raises HTTP 422 with a body | rows None; the reason carries "422" and the body |
| **S-R1body-lv** (plant, in memory) | `read_liveness_rows`' error line made to drop the body, anchored on the new wording | red exactly `['LV6']`; BLIND if the anchor is not unique |

**The re-run predicts:**
- 0 rows off;
- S-R1body red `['R1h']`, as before;
- S-R1body-lv red `['LV6']`;
- LV as predicted with LV6 green;
- LV-S1 as before.
