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

## Amendment 2: the native USB-C port (the owner, 2026-11-12 18:31Z), frozen before P0

**His words:** "the 2.1"'s housing only exposes its native USB-C port. Opening it to reach the UART port waits for
another day, so the bring-up runs over the native USB port." And:
- "Same guards. Read the MAC with esptool's --after no_reset, and write with --before no_reset, so the chip stays in
  download mode between the two reads and the port can't drop."
- "If the chip won't enter download mode through that port, stop. No button presses through the housing; it waits for
  the UART change."
- "This build logs only on UART0 (platformio.ini: no CDC on boot), so there's no serial boot read. Take the boot reason
  from the server's boot report once the board is on Wi-Fi."
- "Everything else as you set out."

**What changes:**
- **The port is Espressif's own** (VID 0x303A). On its native USB-Serial-JTAG the USB serial IS the chip's MAC, the same
  rule as the four registered boards. The CH343 identity design waits for the UART day.
- **The new port is found by elimination.** Of the VID 0x303A ports, the four registered boards' labels (E9:D8, E7:94,
  6E:64, E2:08) are set aside, and exactly one must remain. Any other count is refused.
- **The sequence, each step once:**
  1. `read-mac --before default_reset --after no_reset`: into ROM download mode through the USB-JTAG reset, and it
     stays there. Its label must not be one of the four, and must equal the label of the port's own USB serial when
     that serial is MAC-shaped.
  2. Immediately before the write: `read-mac --before no_reset --after no_reset`, the same label.
  3. `write-flash --before no_reset --after no_reset 0x0 firmware.factory.bin`.
  4. `verify-flash --before no_reset --after hard_reset 0x0 firmware.factory.bin`, which boots the new image.
- **The image and the write range are unchanged:** sha256 `971a8098c53fbe401ada5e5339c4f11c77e64a7f6fed00d5bd76caf577541313`,
  0x000000 .. 0x1E691F. Its app reads PLACED against the earlier main build (75 bytes, all in the five regions).

**THE KEY, found while amending and stated before it bites.** The shipping build's default key is empty
(`CLOUD_FEED_KEY ""`, `src/CloudFeed.h:26-27`). A board's key comes from NVS: `cloud-key` (the config page's
`/enroll-key`), else `cloud-key-fac` (written once by `scripts/provision-device.py`). The full first write erases NVS,
so the flashed 2.1 has NO key. Then:
- **its requests read 401** (`proxy/src/index.ts`, `authenticate` before anything else), so **the radar draws no
  aircraft**, and P6's tap has no contact to open;
- **the server never records its boot reason.** `recordBoot` runs only past authentication
  (`proxy/src/index.ts:398`), which is the "keyless boots are invisible" fact.

So the board needs a key before P6 and before the boot reason can be read. **How it gets one is the owner's call**,
asked now (Q-P5). Either way, the provisioning tool and its records are his; this session never touches them.

**Re-predicted rows (they replace P0', P1', P2' and P3 for this run):**

| row | predicted | stop if |
|---|---|---|
| **P0''** | one new VID 0x303A port; exactly one left after the four registered labels are set aside | not exactly one |
| **P1''** | step 1 enters download mode and prints a label that is none of the four, equal to the port serial's label | "Failed to connect", or no download mode, or a known label: **stop, no button presses** |
| **P2''** | steps 2-4: the same label; `write-flash` exit 0; `Verification successful (digest matched).`; the hard reset boots the image | any error, or a label change |
| **P3''** | no serial read. The boot shows on the glass: the splash, then the setup screen with its QR code (no Wi-Fi saved), seen by the owner. If the glass stays dark after the hard reset (the S3's download-mode trap), he unplugs and replugs it once, and it boots | dark after one replug: stop |
| **P3b** | **the boot reason**, from the server's boot report, read the way #666's driver reads it (`scripts/fba-boot-rows.mjs`). It is readable only after the board has a key. With a key, predicted: `POWERON` if he replugged, else `USB` (the hard reset over USB-Serial-JTAG). With no key: **no row, stated as "keyless, so unread"**, never as a missing boot | a row with any other reason |

## Amendment 3: an incident in the preparation, and P0'' re-frozen (2026-11-12 ~18:40Z)

**What happened.** The first native-USB version of the flash script chose the port by elimination: of the Espressif
(0x303A) ports, set aside the four registered boards' labels, and exactly one must remain. Its control run, with no
2.1" attached, was meant to read a refusal. But this PC had four 0x303A ports and only three registered boards
(the bench Missileer isn't connected). The fourth, COM89, an **unregistered ESP32-C6** with serial label 1E:D0,
passed the elimination. The script ran `read-mac --before default_reset --after no_reset` on it, which reset it into
ROM download mode and left it there. It was reported to the reviewer at once. Its recovery (one hard reset, or its
owner replugging it) waits for its owner's yes, and nothing here has touched it since.

**The shape:** "a board that answers nothing on serial is not a free board" (CLAUDE.md), and a guard whose first step
is a reset is a mutation, even in a control. Elimination by the registered labels established which ports were NOT the
four bench boards. It never established which port IS the 2.1".

**P0'' re-frozen:** the port is the ONE 0x303A port that **appeared** since a snapshot taken before the owner plugs the
board in (`--snapshot`, which opens no port). Any port present in the snapshot is never opened, whoever owns it. Zero
or several new ports: refused, with no port opened. The control now reads that refusal without opening anything:
read 18:4xZ, "new since: 0 ... REFUSED ... no port opened", exit 1.

## Amendment 4: the UART port after all (the owner, 2026-11-12 ~18:47Z), frozen before anything touches COM110

**His words:** "the board is on COM110. something was wrong with the USB connection to the computer, so i removed the
missileer device and placed this on one on the YKUSH board in the missileer place. Im not developing the missileer
anyways so that's ok."

**Read-only at 18:48:11Z** (a port listing that opens nothing): COM110 is VID:PID 1A86:55D3, "USB-Enhanced-SERIAL
CH343", the bridge the env's own comment names. The photo Missileer (E7:94) is not connected. The reviewer's ledger, as
of 18:49Z, provisional until the owner confirms: hub port 1 = the 2.1 (COM110), port 2 = 6E:64, port 3 = E2:08.
**Port 1 is never switched until the owner says otherwise.** COM89 (the ESP32-C6) is out of scope and never opened.

**The identity rule for this port.** A CH343P's USB serial is the bridge's, not the chip's MAC, so the port's serial
proves nothing about the chip (the registration's point). So:
- **(a) the port:** COM110 by the owner's word, AND its VID:PID is 1A86:55D3, AND it is the only 0x1A86 port on the PC.
  Anything else is refused, with no port opened.
- **(b) the chip:** `read-mac --before default_reset --after no_reset`, through the bridge's DTR/RTS auto-reset, into
  download mode, where it stays. Its label must be none of E9:D8, E7:94, 6E:64, E2:08 or 1E:D0. Then, immediately
  before the write, `read-mac --before no_reset --after no_reset` must give the same label.
- **(c) the write**, unchanged: `write-flash --before no_reset --after no_reset 0x0 firmware.factory.bin` (sha256
  `971a8098...1313`, 0x000000 .. 0x1E691F), then `verify-flash --before no_reset --after hard_reset`.

**The serial boot read is back (P3):** one open of COM110 with DTR low (never download mode) and one RTS pulse on EN,
for a boot from reset. 60 s are captured; the full log stays local and is never printed or committed, and only the
predicted lines are printed.

**Rows for this run (they replace P0''-P3'' of amendment 2):**

| row | predicted | stop if |
|---|---|---|
| **U0** | (a) passes: COM110, 1A86:55D3, the only 0x1A86 port | anything else |
| **U1** | (b) enters download mode through the auto-reset; label none of the five, the same on both reads | "Failed to connect" (no button presses), or a known label |
| **U2** | (c) `write-flash` exit 0; `Verification successful (digest matched).`; the hard reset boots the image | any error |
| **U3** | the boot read: `rst:0x1 (POWERON)` (an EN reset on the S3), then `[build] env=blipscope-pro-s3-21 fw=v19`, `[board] TCA9554 @0x20 ACK (expander alive)`, `[board] QMI8658 WHO_AM_I=0x05 (expect 0x05); ...`, `[disp] tft.init=1 480x480  backbuffer ok`, and no reboot within the 60 s | any of them differs, or a reboot |
| **U3b** | the server's boot report, only once the board has a key (Q-P5): `POWERON`. With no key: unread, and said so | another reason |

P4 onward are as in amendments 1 and 2. **Nothing runs** until the reviewer lifts the hold and the owner has answered
Q-P5.
