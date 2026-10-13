# Missileer photo mode

Ruled by Daniel on 2026-10-12. Built in the PR that adds `env:missileer-photo`.

## Purpose

Product photos of the EAM monitor. A photo needs a frame that holds still on one
screen: the shipping firmware rotates screens, dims at night, and jumps to the ticker
when an EAM arrives, and any of those ruins a long exposure.

## The env

`env:missileer-photo` is `env:missileer-s3-128` plus `-DPHOTO_MODE`, and nothing else:

```ini
[env:missileer-photo]
extends = env:missileer-s3-128
build_flags =
    ${env:missileer-s3-128.build_flags}
    -DPHOTO_MODE
```

So the firmware in a product photo is the firmware that ships. With `PHOTO_MODE` off,
every shipping code path is unchanged: `missileer-s3-128` and `blipscope-s3-128` built
from `main` and from the photo branch differ only in their build stamps.

## Behaviour under `PHOTO_MODE`

- **No auto-rotation.** No 8 s dwell and no 30 s hold. A horizontal swipe still
  changes screen, and the screen stays until the next swipe. The board boots to the
  Clock.
- **No auto-dim.** The panel holds the configured brightness day and night. It's set
  with the config page's **Brightness** slider (Display section), which every EAM
  build already has.
- **A fresh EAM changes nothing on the glass.** There's no jump to the ticker, no
  `NEW` pulse, and no drill offer: the offer is a banner over the current screen, and
  the drill is game content, not the feed. The feed, logbook, stats, ON WATCH count
  and ntfy alerts all keep updating underneath.
- **The Ticker steps through the newest 10 messages from the live feed.** Swipe up
  for older, down for newer. The pick is pinned by message id, so an arrival never
  swaps the text, and it's drawn exactly as the ticker draws the newest message. A
  position toast (`3 of 10`) clears itself after 1.2 s. Nothing is filtered: a copy
  with `?` in it is shown as it is, and the photographer picks. The messages come
  from `/eam/latest`, the same list the logbook notes, because the logbook itself
  stores only message ids.
- **The ICBM screen is unchanged.** It appears only for a real launch from the feed
  and is otherwise hidden.
- **No OTA.** The boot update check and the quiet-hour update reboot are both off,
  so the photo firmware is never replaced or rebooted mid-shoot. Flashing back to
  `missileer-s3-128` restores OTA.
- **`/diag/fb.html`** serves the real screen as a PNG (**Save image**), for
  screenshots.
- The board prints `[photo-mode]` at boot.

## Real data only

Nothing invented reaches the screen: no fixtures, no demo data, no injected or made-up
EAMs, launches or stats. A marketing photo must show what the device actually shows.

- No bench, fixture or demo macro is compiled in. `scripts/check_photo_mode.mjs`
  derives that list from the sources' `#if` lines and refuses any of them in the
  photo env.
- The Clock's two `kHeritageSample` lines are left out of this build, because their
  own source calls them `SAMPLE DATA`. The shipping Clock still carries them, and
  that is tracked as a Missileer launch blocker in ROADMAP.md.

## Flash it, and go back

Only through `scripts/bench-flash.ps1`. It finds the port from the board's USB serial
and prints that serial before it writes anything (CLAUDE.md, Bench boards). The photo
unit is `-Board missileer-photo` (USB serial `90706931E794`). NVS is not erased, so
Wi-Fi and settings carry over in both directions.

Photo mode:

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 -PioEnv missileer-photo -Board missileer-photo -DanielApproved -Label photo -NoCapture
```

Back to the normal firmware, which also brings OTA back:

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 -PioEnv missileer-s3-128 -Board missileer-photo -DanielApproved -Label shipping -NoCapture
```

## Never shipped

`missileer-photo` is never a shipping env and never in an OTA manifest. CI enforces
this; it isn't left to this page:

- **`skus.yml`.** The env is listed `build-only` with no slug. CI compiles it on every
  run, and no release asset or OTA channel can carry it.
- **`scripts/check_photo_mode.mjs`.** Runs with `--selftest` before any build. It fails
  CI if any of these happens:
  - the photo env stops being exactly its parent plus `PHOTO_MODE`;
  - `PHOTO_MODE` appears in any other section;
  - any env extends or interpolates the photo env;
  - a bench macro reaches the photo env;
  - the `skus.yml` row gains a slug or shipping status;
  - a workflow names the env or the flag.
- **`scripts/check-no-bench-hooks.sh`.** Every shipping image is scanned for the
  `[photo-mode]` marker. The photo image is *required* to carry it, which proves the
  marker survives a real build, so its absence from a shipping image means something.
