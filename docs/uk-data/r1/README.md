# R1's week of OPERA frames: the running record

The frames live in `bench-logs/uk-opera/` (gitignored). This folder keeps the record that must not be lost.

- **The collector:** `docs/uk-data/instruments/collect_opera.py`, committed at `ba5b557` before its first run.
- **The first run:** 2026-10-28 15:53:07-16:31:25Z, 478 frames held, 0 errors (`runs.jsonl`).
- **The decode control, 2026-10-28** (`decode_check.py`, frame 20261028T0755): 2,221,632 window cells, **0 differ**.
  - Its sabotage, the quality band read for the value band: **1,403,593 differ**. Red.
- **The loop:** every 3 h from 16:31Z until 2026-11-04 16:00Z, as a background job of this session.
  - If the session ends, the loop ends with it. The next session re-arms it.
  - The cache holds at least the previous UTC day, so a gap under 24 h loses no frame.
