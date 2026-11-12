# The Pro 2.1 bench bring-up (registration; nothing is connected, flashed or run)

**Commissioned** by the owner, 2026-11-12 17:26Z, relayed by the reviewer: "The 2.1" Waveshare (blipscope-pro-s3-21,
480x480 round, about 53 mm) will be sold. Add it to the pictures and golden tests. Its rule gives the 1.28" design at
2x. The bench bring-up I approved comes first. Promoting it to shipping in skus.yml is its own decision, aimed at v21."

**What this bring-up is for:** to put a 2.1" on the bench as a known, identified board running today's image, and to
record what it does. It is not the 2x layout. That is the big-screens build, step 13 of `docs/big-screens.md` on
`p4/big-screens-plan`, and it follows this bring-up. Nothing here changes `skus.yml`. The env stays build-only with no
slug, so nothing is published for it.

**What exists today** (main `71156714ba44`):
- `[env:blipscope-pro-s3-21]`: 16 MB QIO flash, 8 MB octal PSRAM, `default_16MB.csv`, the cloud feed (`${cloud.prod}`).
  It is a build-only CI row, green on every run.
- `include/variants/s3_21.h`: an ST7701 480x480 RGB panel; CST820 touch at 0x15; a TCA9554 IO expander driving the
  panel and touch resets and the ST7701's chip select; a QMI8658 IMU; a buzzer.
- `src/board/board_s3_touch21.cpp`: `BoardPreInit()`. Its code last changed on 2026-08-09 (`git log`).
- Its OTA identity row: `blipscope-pro-s3-21 - s3-21`.

## The one thing that is different about this board: it talks through a UART bridge

The env's own comment says serial goes to UART0, through the board's **CH343P** USB bridge, not the S3's own USB. Every
bench board today is found by `scripts/board-port.py`, and the guard there establishes one fact: **the port's USB
serial IS the chip's factory MAC** (Espressif VID 0x303A, read from the chip itself). A bridge's USB serial is the
bridge's own, not the chip's. So pointing the same lookup at a bridge serial would be a second path that does not
establish what the first does. It would identify the cable side of the board, not the ESP32.

**So the identity has two parts, and both run before every write:**
1. **port:** the bridge's USB serial (VID 0x1A86) resolves the port. It is registered in the local board registry
   (`~/.config/blipscope/boards`), never in the repo, like the other four.
2. **chip:** `esptool read_mac` over that port must give the MAC whose last two bytes are the board's label, before
   anything is written. A mismatch refuses the write. Only the label is printed, never the MAC.

That is a code change to `scripts/board-port.py` and `scripts/bench-flash.ps1`, with the board's row added in all three
places together (`BOARDS`, `-Board`, and CLAUDE.md's table), as CLAUDE.md requires. It ships in this PR's code commit
with a selftest: a bridge row whose `read_mac` answers another label is refused.

## The steps, each with its prediction

| step | what | predicted | stop if |
|---|---|---|---|
| **P0** | **the owner connects the board** by its UART USB-C. The YKUSH hub's three ports are full (a Missileer, the Blipscope board E2:08 and the Weatherscope board, his 11-11 setup), so the 2.1 goes on a PC port unless he moves one | it enumerates as a CH343 (VID 0x1A86) | no 0x1A86 port appears |
| **P1** | identity, read-only: `esptool read_mac` over the new port, label taken | answers; label = the MAC's last two bytes; registered locally; the three repo places get the label row | `read_mac` fails twice: report, don't retry |
| **P2** | the image: `blipscope-pro-s3-21` built at main, a FULL write (bootloader, partitions, boot_app0, app) to a fresh board, which has no NVS to lose. **The owner's OK first:** CLAUDE.md's bench rule for a non-Missileer board | `bench-flash.ps1` exit 0; a read-back of the app region matches the built image | a write or read-back error |
| **P3** | the boot, read over the UART. One open; a bridge does not drop on reboot, and opening it may reset the board through DTR/RTS, which is fine for a boot read | `[build] env=blipscope-pro-s3-21 fw=v19` (`BuildIdentity.h:125`), `[board] TCA9554 @0x20 ACK (expander alive)` (`board_s3_touch21.cpp:96`), `[board] QMI8658 WHO_AM_I=0x05 (expect 0x05); LCD_CS held low for ST7701 init` (`:118`), `[disp] tft.init=1 480x480  backbuffer ok` (`main.cpp:409`), and no reboot within 5 minutes | any of the four lines differs, or a reboot loop |
| **P4** | Wi-Fi and location: the owner's phone on the setup portal, or the PC-adapter path once #666's driver exists (his choice, Q-P2); then `POST /location` from the PC | `[WiFi] CONNECTED`; the location reads back from `GET /` | no join in 3 minutes |
| **P5** | the glass over Wi-Fi: `/diag/fb`, three reads, each 480 x 480 x 2 = 460,800 bytes, kept when `Torn:false` and the three agree. Radar, List and a card: today's look at 480, the baseline step 13 changes | three faces captured; the frame is 460,800 bytes | a size other than 460,800 (the capture path isn't the panel's) |
| **P6** | touch: **the owner's taps** (a minute): one tap opens a card, one swipe changes screens, a long press is what the firmware already defines | the tap lands where the finger was (the CST820's coordinates unrotated) | a mirrored or rotated touch: stop, record, no fix in this PR |
| **P7** | **the glass measured** by the owner (calipers): the visible diameter | about 53.3 mm (the 2.1" nominal), and **under 60 mm**, the plan's condition for exactly 2x | 60 mm or more: the 2x rule's host test fails by design, and it goes to the owner |
| **P8** | the Worker side, read-only: the board's first check-in | the board isn't in the manufacturing record, so the activation watch counts it as `not_listed` (watch-only today, so it is not refused) | `/blips` refused: stop. The provisioning record is the owner's, and I never touch the provisioner |

## Escape hatch

- A reading in none of these tables means the bring-up is not doing what this page says. Stop, report, decide.
- The board is NOT made a shipping SKU by anything here. `skus.yml` doesn't change.

## What needs the owner

- **Q-P1:** is a 2.1" board on hand, and where does it plug in? (The hub is full.)
- **Q-P2:** Wi-Fi: his phone, or the PC-adapter path with the bench password file once #666's driver exists?
- **His hands, briefly:** connecting the board (P0), the taps (P6), the calipers (P7).
- **His OK** for P2's flash, and for any later flash of this board.
- The bench windows: P0-P3 need no network; P4-P8 need one window with the board on Wi-Fi.

## Size

- About **1.5 d**: the identity code and its selftest 0.5 d, P1-P5 0.5 d, P6-P8 0.25 d, the record 0.25 d. His minutes
  are extra.
- It doesn't block v20, and it carries nothing from v20.
- CI for this registration (docs only): **32 = 29 / 3 / 0**, or 32 = 30 / 2 / 0 if #675 has merged first.

## Amendment 1: the owner's answers (2026-11-12 18:13Z), and the rows they change (frozen before P0)

**His answers:**
1. "Yes, a 2.1" is on hand. It goes on a plain PC USB port, not the hub. Nothing moves off the hub: E2:08 stays for
   v20's windows, and port 1's photo Missileer is never touched."
2. "Wi-Fi by my phone on the setup portal. That's also our first look at the setup screens on the 2.1"."
3. "Yes to my hands for plugging in, one tap and swipe, and the measurement. I'll measure the diameter of the lit image
   (with a full-screen white or the outer ring showing), not the glass edge."
4. "Yes to the first full flash of today's blipscope-pro-s3-21 image, on these conditions:"
   - before writing, read the target's MAC and confirm by its short label that it's the new board, not E2:08, the
     Weatherscope or the Missileer;
   - the image is identified by sha256, and the write ranges are printed, first;
   - once it's on Wi-Fi, it's labelled a bench board in the fleet reads, so v19's week of readings doesn't count it;
   - its model has no published image, so its update checks fail by design.

**How the first flash meets his conditions.** It uses a one-off guarded script, `pro21-flash.py`, kept in the session's
scratchpad, not the repo. The repo's `board-port.py` / `bench-flash.ps1` bridge support follows in the code commit, as
registered. The script, in order:
- **(i) Port:** exactly one VID 0x1A86 port must be present, or it refuses and prints the count.
- **(ii) Identify only:** `esptool read-mac` over that port, printing the label (the MAC's last two bytes), never the
  MAC. It refuses a label belonging to any registered board (E9:D8, E7:94, 6E:64, E2:08).
- **(iii) Print first:** the factory image's sha256 (64 hex) and size, each component's sha256 and offset, the
  partition table parsed from the built `partitions.bin`, and the write range `0x0 .. 0x0 + size - 1`.
- **(iv) Write, only with `--write --expect-label XX:YY`:** it reads the MAC again **immediately before** writing,
  requires the label to equal the one stated, writes `firmware.factory.bin` at 0x0, and runs `verify-flash` on the
  same range.

The full first write erases the NVS span on purpose: this board has nothing to keep.

**Re-predicted rows:**

| row | predicted | stop if |
|---|---|---|
| **P0'** | the owner plugs it into a PC USB port (not the hub); one new 0x1A86 port appears | not exactly one 0x1A86 port |
| **P1'** | (ii) prints a label that is none of E9:D8, E7:94, 6E:64, E2:08 | `read-mac` fails twice, or a known label |
| **P2'** | (iii) prints first, then (iv) exits 0 with `Verification successful (digest matched).` (esptool 5.3.0) | a write or verify error, or a label change between (ii) and (iv) |
| **P3** | as registered, read with one UART open after a reset through RTS; the full log is kept locally and only the predicted lines are printed | as registered |
| **P4'** | his phone on the `Blipscope-<6 hex>` setup portal is the first look at the 2.1"'s setup screens, his eyes and mine (`/diag/fb` once it's on Wi-Fi). Then `[WiFi] CONNECTED`, and the location set from the PC | no join in 3 minutes |
| **P7'** | the diameter **of the lit image** (full-screen white, or the radar's outer ring), measured by him: about 53.3 mm, and under 60 mm | 60 mm or more: to the owner |
| **P8'** | **the fleet reads label it a bench board.** `scripts/reconcile-fleet.py` gains a bench-board list from a local file, `~/.config/blipscope/bench-devices`. It holds ids only, no keys; the token file is never touched. The ids are excluded from the KV-ONLY reconcile and from A3.1's behind/current counts, and counted on their own line. A small PR, predictions first, with a selftest row and a plant (the exclusion removed must read one unaccounted-for unit), **merged before the day-2 read (11-13)**. The id comes from the board's own config page (`window.BP_DEVID`) and is never printed | the label PR isn't merged by the day-2 read: that read names the board as a bench unit by hand |
| **P8b** | **its update checks:** v19's `version.txt` says 19 and the board runs 19, so today's check finds no update and downloads nothing. When v20 publishes, the check finds 20 and asks for `firmware-s3-21.bin`, which doesn't exist (no slug): it fails, **by design** (the owner) | an update check that downloads anything |

**The Worker's activation watch** will still count it `not_listed`. That count is Worker-side and changing it needs a
deploy, so it is said here rather than changed.
