# E6 — long EAMs on Missileer: the check (2026-10-11)

ROADMAP E6 asked, before any screen redesign: does a `/eam/latest` message of 31,
75, 290 or 500 characters (500 = valar-eam-feed `MAX_COPY`) parse, stay out of
every fixed buffer, draw without overrunning, and what does it cost the heap?
**This is a check, not a redesign: nothing the device shows was changed.**

| instrument | what it can see | what it cannot |
|---|---|---|
| `test/host/test_eam_long_message.cpp` | the shipping `EamFeedClient` + `EamModels`, the real arduino-esp32 `String`, ArduinoJson 7.4.3; every byte they ask for | the HTTP client, the socket streams, lwIP, the real allocator, other tasks |
| `test/host/test_eam_ticker_draw.cpp` | the real LovyanGFX sprite drawing the ticker's lines at every scroll offset, under ASan/UBSan on Linux | the panel itself: colour, legibility, what it looks like on glass |
| the bench step below | the real heap on the board, the real HTTP path, and the glass | anything but the one board and the four fixtures |

The fixtures are `test/fixtures/eam-long/latest-{031,075,290,500}.json`, written by
`scripts/e6-fixture-server.py --generate` in the backend's `/eam/latest` shape
(20 station copies; message 0 is the long one). The host tests parse those bytes;
the bench board is served the same bytes.

## Findings on the host

**Parse: every length is held whole.** 31, 75, 290 and 500 characters, each as a
station copy (one group, no spaces), as a spaced transcript (6-character groups),
and with `groups: []` (the firmware's `SplitGroups` fallback): the stored `text`
is byte-identical to the input, `groups` are the input's groups, `char_count` and
`group_count` are the served ones. There is no truncation by design: `text`/`groups`
are Arduino `String`s and the `JsonDocument` is elastic (ArduinoJson 7, strings up
to 65,535 characters). Controls: a copy cut to 255 characters **is** reported; a
body cut mid-string does not parse and leaves the store empty.

**The fields the backend is holding back parse too.** valar-eam-feed keeps
`announced_length` (and position notes) off `/eam/latest` "until the firmware's
JSON parsing has been read", on the worry that a fixed-size document could
overflow. It cannot: the document is elastic. A 500-character message also
carrying `machine_text`, `origin_text` (500 each), `announced_length` and twelve
`station_notes` parses, the text intact, for +2.0 KB at the worker's peak and
nothing retained (the firmware ignores fields it does not know). **Whether to
serve them is the backend's ruling; the parsing is no longer the reason not to.**

**Fixed buffers: none is reachable by message text.** Every `char x[N]` and every
`snprintf/strcpy/memcpy`-family call in `src/eam/` and `include/NtfyAlerter.h`
(14 `char` arrays -- 13 scratch buffers and one const literal -- and 12 bounded `snprintf`s) is fed by numbers, the tempo level, or a
literal. The text goes: `ParseMsg` → `String` → three `std::vector<Msg>` copies
in `EamFeedClient` → `DrawTicker` (`drawString`) and the ntfy alert body (a
`String` POSTed as-is). The id, not the text, reaches the logbook, the drill and
USB open. The host test **asserts** this inventory: a new fixed buffer in
`src/eam/` fails it until someone writes down what feeds it.

**Draw path: in bounds, and what it shows today.** The ticker's lines were drawn
by the real LovyanGFX into a guard-banded 16-bpp sprite at 240 and 412 px, at
every scroll offset (up to 581 frames for a 500-character spaced message): no
byte outside the sprite changed, every frame drew, every group was drawn — under
ASan/UBSan on Linux as well. **What it shows:** a station copy is ONE group,
drawn centred on one line, and nothing scrolls sideways, so only the middle of a
long one is on the glass:

| copy | 1.28" (240 px): chars inside the disc | 1.46" (412 px) |
|---|---|---|
| 31 | all 31 | all 31 |
| 75 | 33, starting at char 21 | 59, starting at char 8 |
| 290 | 34, starting at char 128 | 60, starting at char 115 |
| 500 | 34, starting at char 233 | 60, starting at char 220 |

So on the 1.28" even a 75-character message loses its first 21 characters — the
preamble, which is the message's name. That is today's behaviour, recorded, not
changed: it is the screen-redesign question E6 already names (page it, or "first
30 +N"). Spaced copies (6-character groups) scroll vertically and every group is
shown. One library bound worth knowing: LovyanGFX accumulates a line's pen in an
`int16_t`, so a single line wraps past 32,767 px — 5,461 characters at size 1,
1,820 at size 3. `MAX_COPY` is inside it at every size the EAM screens use.

**Heap: a 500-character message costs about 2–3 KB more at the poll's peak and
under 1 KB more per copy kept** (32-bit host build, the ESP32's pointer size;
KB of internal device heap, converted from the bytes requested by a rule derived
from the IDF heap source — 16 B of poisoning + TLSF header per block, 4 B
alignment — not measured):

| one poll, 20 messages | worker peak | handed to loop | loop peak | kept |
|---|---|---|---|---|
| all 30-char | 20.2 | 7.5 | 18.9 | 5.4 |
| one 500-char station copy | 22.5 | 8.4 | 21.7 | 6.3 |
| one 500-char spaced copy (72 groups) | 26.5 | 8.9 | 23.4 | 6.9 |
| all 20 at 500 (the most `limit=20` allows) | 60.3 | 26.2 | 75.1 | 24.1 |

Every block in those rows is ≤ 4 KB, so all of it is internal heap (the S3 sends
only blocks over 4,096 B to PSRAM). The worst row is theoretical — twenty long
EAMs in the latest twenty — but it is 75 KB of internal heap at the loop's peak,
where a TLS handshake needs one 16.7 KB block. The loop's peak is three copies of
the payload's messages alive at once (`res.messages`, `incoming`, `merged`) plus
the old store. Reported, not changed.

The 64-bit CI build prints the same tables with its own (different) sizes; no
check depends on pointer size.

## What the host could not prove

The real HTTP path (HTTPClient, `ChunkedSocketStream`, lwIP) with a longer body;
the device allocator's real per-block cost; other tasks allocating at the same
time; and what the ticker looks like on the glass. The socket streams never look
inside the JSON, so a long `text` is to them a longer body (6.5 KB for the
500-character fixture against `MAX_JSON_BODY` = 256 KB) — a premise stated, not
tested. The bench step measures all four on the board.

## The bench step — about 10 minutes

**Board:** the registered Missileer bench board (USB serial `90706931E9D8`),
resolved by serial by `scripts/bench-flash.ps1` — never a COM number.
**Env:** `missileer-s3-128-e6bench` = the shipping `missileer-s3-128` plus
`-DMISSILEER_E6_BENCH` (`src/eam/E6Bench.h`). In RAM only, it points the feed at
the fixture server **over** NVS `eam-base-url` (a build default would not reach a
saved key), blanks the ntfy topic (no alert reaches a phone), never writes the
`eam-log` logbook (fixture ids never become "seen" EAMs), and polls at the Normal
cadence. The fixture server 404s everything but `/eam/latest`, including the game
config, so no drill can be offered; this env has no `GAME_API_BASE`, so no vote
is sent. NVS is not touched, so putting the normal image back restores the board
exactly.

1. **Start the fixture server** (PowerShell, repo root):
   `python scripts/e6-fixture-server.py`
   It prints this PC's LAN address and the line to paste next. Allow Python
   through the Windows firewall on private networks if asked. Check it from a
   second window: `curl.exe http://<that ip>:8765/healthz` must answer the 404
   JSON (that path does not advance the rotation).
2. **Build and flash the bench image** (second PowerShell, repo root):
   ```powershell
   $env:E6_FIXTURE_BASE = 'http://<that ip>:8765'
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 `
       -PioEnv missileer-s3-128-e6bench -Board missileer -Label e6bench
   ```
   The recorder it attaches writes `c:\Github\Blipscope\bench-logs\e6bench-<stamp>.log`
   (bench-capture.ps1's ledger folder).
3. **Let it run 5 minutes.** The rotation is **per request**, not per time: each
   `GET /eam/latest` gets the next file (031, 075, 290, 500, 031, ...), and the
   server prints one line per request (`#N GET /eam/latest -> latest-500.json ...`).
   The board asks once right after it initialises and then every 30 s, so the
   first 500 arrives ~90 s after the first request and ten requests (5 min) give
   every length at least twice. Anything else that requests `/eam/latest` (a
   browser, a second board) advances the rotation too. When a new long message
   arrives the ticker jumps to it with NEW (not on the very first poll after
   boot): **look at the glass once** when 500 is up — one line across the middle,
   both ends cut by the bezel, is what the host test predicts. Do not save the
   config page during the run: that is a real NVS write, and it re-runs
   Initialise (the bench overrides re-apply, but the run is no longer clean).
4. **Collect:** `Select-String -Path c:\Github\Blipscope\bench-logs\e6bench-*.log -Pattern 'e6-bench'`
   and keep the output. A reboot shows as `t=` (milliseconds since boot) going
   BACKWARDS between two lines; the `[build] env=` banner is printed before
   TinyUSB is up on this board, so the recorder may never see it and its count
   proves nothing.
5. **Put the normal image back, then stop the server:**
   ```powershell
   Remove-Item Env:E6_FIXTURE_BASE
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 `
       -PioEnv missileer-s3-128 -Board missileer -Label missileer-bench
   ```
   then Ctrl+C the fixture server. The restored board's `[eam] init; backend=` line
   must name the production feed (its NVS value), and no `[e6-bench]` line may
   appear in the new ledger.

### What each result will mean — written before any board has run it

Read the `phase=` lines per poll. `Δx = int_free(pre) − int_free(x)`, in KB; take
the median of each length's samples. The host's prediction for the same rotation
is printed by `test/host/run.sh` ("PREDICTED [e6-bench] int_free drop"):
Δdoc ≈ 20.1 / 20.4 / 21.4 / 22.5 for 31 / 75 / 290 / 500, **500 − 31 ≈ +2.3**,
and Δkept ≈ 0 in steady state. The device adds the HTTP client's own transients
to Δdoc, so compare the **500 − 31 difference**, not the absolute.

| outcome | reading |
|---|---|
| Δdoc(500) − Δdoc(31) between +1 and +5 KB; `tls16k=1` on every line; `int_free` at `kept` steady from cycle to cycle at the same length; one boot | the host model holds. A 500-character EAM costs ~2–3 KB transient and < 1 KB kept. E6's heap question is closed. |
| the difference above +8 KB (over 3× the prediction) | the host model misses a copy, or the HTTP path buffers the body. Find the phase that grows (`doc` vs `merge`) before trusting the host table. |
| `tls16k=0` on any line | at that phase a long message left no 16.7 KB block for a TLS handshake. A finding: note the phase and length. |
| `int_free` at `kept` falling cycle over cycle at the same length | a per-poll leak on the device that the host ledger does not see. A finding. |
| `t=` going backwards, or no `[e6-bench] parsed` line at all | a reboot (a crash), or the feed never reached the fixture server. Check the server's console first (firewall), then the ledger around the reboot. |
| none of the above — e.g. two samples of one length differing by more than the 500 − 31 difference itself | the instrument is too noisy at this scale. Stop and keep the raw lines; do not improvise a reading. |

**Building it.** `E6_FIXTURE_BASE` is read by `platformio.ini` (`${sysenv...}`),
so changing it changes PlatformIO's project checksum and the next `pio run`
cleans every env's build directory: expect a full rebuild after setting it and
again after removing it. Unset, the image's feed points at
`http://e6-fixture-base-not-set.invalid` and every poll prints
`[e6-bench] /eam/latest FAILED`.

**With `/diag/fb` on top** (`-DMISSILEER_DIAG_FB`, the `-fbdiag` envs): the two
flags touch different files and compose. Take the framebuffer capture AFTER
collecting the heap lines, or note its time: serving `/diag/fb` copies the frame
into PSRAM and streams it over the async web server, which moves `int_free` and
`psram_free` for any `[e6-bench]` line taken during it.

`int_largest` is printed because it was asked for, but it is a max across heap
regions and latches onto reserves nothing allocates from (`src/HeapHealth.h`);
read `int_free` and `tls16k`. `int_min` is the low-water mark since boot: if it
drops on a `doc` or `merge` line, that poll set a new low.

## Bench run, 2026-10-11, on the photo unit

Run by the overnight session on `missileer-photo` (USB serial `90706931E794`), not
`missileer` (E9D8, which was not connected). The image was `missileer-s3-128-e6bench` +
`-DMISSILEER_DIAG_FB`, built from branch `bench/e6-hw-run-2026-10-11` @ 3112cfa. That is this
PR's firmware (67a5f0d; nothing under `src/` or `platformio.ini` changed after it) merged
with #398 for `/diag/fb`. Flashed through `bench-flash.ps1 -Board missileer-photo
-DanielApproved -NoCapture`, which wrote bootloader, partition table, otadata and app0, and
not NVS. Serial via `pio device monitor --rts 0 --dtr 1`. Fixture server on 192.168.86.45:8765.
16 `/eam/latest` requests (21:08–21:15 PDT); the monitor attached after request 2, so 14
polls are in the ledger. Every one printed `parsed ok=1`.

Read against the table above, which was written before the run:

| length | Δdoc (pre − doc) per poll, B | Δmerge, B | `int_free` at kept, B |
|---|---|---|---|
| 31 | 16,020 · 16,012 · 16,004 · 15,992 | 18,572 · 18,616 · 18,576 · 18,624 | 127,596 · 125,500 · 127,076 · 127,084 |
| 75 | 16,140 · 16,132 · 16,140 | 18,780 · 18,804 · 18,812 | 127,336 · 125,512 · 127,040 |
| 290 | 16,896 · 16,820 · 16,832 · 16,832 | 18,544 · 19,740 · 19,720 · 19,732 | 129,248 · 127,064 · 125,504 · 127,080 |
| 500 | 17,476 · 17,464 · 17,548 · 17,488 | 20,564 · 20,584 · 20,624 · 20,648 | 127,812 · 127,080 · 125,516 · 127,084 |

- **Δdoc(500) − Δdoc(31) = +1.47 KB** (medians 17,482 and 16,008 B), inside the +1 to
  +5 KB row. At the merge peak the difference is +2.0 KB. `tls16k=1` on every line, and
  `t=` only increases (one boot). Lowest `int_min` was 86,252 B. **Row 1: the host model
  holds.**
- **Noise check.** The Δdoc samples of one length agree within 84 B, far below the
  1.47 KB difference, so the instrument resolves it.
- **Kept is not falling.** The four consecutive `kept` readings from 21:11:45 to 21:13:15
  (one per length) are about 1.6 KB lower, then return to about 127.1 KB. A burst of six
  `/diag/fb` requests ran from 21:11:22 to 21:11:47. lwIP holds a closed connection's PCB in
  TIME_WAIT for about 2 minutes, which spans that window exactly. That explanation is
  inferred, not proven; the other candidate is an unrelated transient on the loop. Neither
  is a per-poll trend.
- **Glass** (captures via `/diag/fb`): the ticker shows the header (`11175 kHz - 16h ago -
  N ch`) and one centred line. 31 fits; 75, 290 and 500 are cut at both ends of the disc,
  as the host test predicts. One capture right after a fetch returned a frame with only the
  screen dots and `torn=false`. The next ten captures of the same message all show it.
  `torn` only reports a frame completed DURING the copy, not one half-drawn when the copy
  began. Read a single `/diag/fb` frame with that in mind.
- Afterwards the unit was flashed with `missileer-photo` (the photo-mode run); it was never
  restored to `missileer-s3-128` in between.
