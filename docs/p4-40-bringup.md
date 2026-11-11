# Blipscope on the ESP32-P4 4" (p4-40): bring-up before the board

**Read this first.** This page brings the Blipscope radar up on Waveshare's ESP32-P4-WIFI6-Touch-LCD-XC in its 4-inch
version (Waveshare also sells it as the "4C"), before the board is on the bench. The brief came from the reviewer chat on
2026-11-11 at about 17:29Z: do everything that does not need the board, then stop.

- **Board:** 720 x 720 round IPS panel on MIPI-DSI (JD9365), GT911 touch, and Wi-Fi through an ESP32-C6 over ESP-Hosted.
- **Status:** bench-only. Nothing here ships, nothing is published, and nothing is called working until its bench row (section 7)
  passes. Bench time is Daniel's.
- **Order:** the design and every prediction were frozen in one commit, before any code existed. Each later result is graded against
  that commit, and a reading outside a row's set stops that row.
- **Out of scope:** the 3.4-inch 800 x 800 version of the same board, any customer SKU or slug, any CI release row, and audio.
- **Not used:** nothing comes from Alex-developer/ADSB-Radar. It was not opened, and its data paths (airplanes.live, PlaneSpotters)
  are not referenced anywhere.

## 1. What the board is, and where each fact comes from

No fact below has been checked on hardware yet. The last column names the bench row that will check it.

| fact | value | source | checked by |
|---|---|---|---|
| SoC | ESP32-P4NRW32: dual-core RISC-V, 32 MB flash, 32 MB PSRAM in package | Waveshare wiki, 4C page | B0, B1 |
| silicon revision | unknown. Waveshare quotes "up to 360 MHz", which is the pre-v3 figure (v3.x runs 400 MHz) | wiki | **B0** |
| panel | 4 in round IPS, 720 x 720, JD9365 on 2-lane MIPI-DSI | wiki; BSP `BSP_LCD_TYPE_720_720_4_INCH` | B2 |
| DSI link | 2 lanes at 1500 Mbit/s; D-PHY powered from LDO channel 3 at 2500 mV | BSP `display.h` | B2 |
| DPI timing | 80 MHz pixel clock; h porch 20/20/40 (back/pulse/front), v porch 12/4/24 | BSP `bsp_display_new_with_handles` | B2 |
| panel reset | GPIO 27, active low | BSP `BSP_LCD_RST` | B2 |
| backlight | GPIO 26, LEDC PWM **inverted** (`output_invert = 1`), 5 kHz in the BSP | BSP `bsp_display_brightness_init` | B2, B3 |
| colour order | RGB, so MADCTL = 0x00 | BSP `BSP_LCD_COLOR_SPACE` = RGB; the driver writes MADCTL 0 for RGB | B2 |
| touch | GT911 at 0x5D, or 0x14 as a fallback; **INT and RST not connected** | BSP `BSP_LCD_TOUCH_INT/RST = NC`, `bsp_touch_new` | B4 |
| I2C | SDA 7, SCL 8, port 1, 400 kHz (ES8311 codec on the same bus) | BSP | B4 |
| C6 link | SDIO: CLK 18, CMD 19, D0-D3 14-17, C6 reset 54 | the Arduino core's `esp32p4` variant and its sdkconfig. **Waveshare does not document these for the 4C** | **W1** |
| ESP-Hosted host | 2.12.8 | `esp_hosted_host_fw_ver.h` in core 3.3.9's libraries | W5 |
| ESP-Hosted on the C6 | unknown until read | | **W5** |

**Pixel density, which drives the whole layout section.** If the 4 in figure is the visible diameter, the 4C has 720 px over
101.6 mm, about 180 ppi. The 1.28 in Kit S3 has 240 px over 32.5 mm, about 188 ppi. Within 5 %, **one pixel is the same physical
size on both boards.** Bench row B1 measures the visible diameter with a ruler, because "4 inch" could mean something else.

### Sources taken, and their licences

| what | where | used for | licence |
|---|---|---|---|
| Waveshare BSP `waveshare/esp32_p4_wifi6_touch_lcd_xc` **v2.0.0** | component registry; zip sha256 `cc8709610583c2821eb0ef1f9e27ad06817cf85ab7a2e03655f59523c51af575`; upstream commit `395d9adf0ac0` | the 720 x 720 init table, DSI timing, pins | Apache-2.0 |
| Espressif `esp_lcd_jd9365` **v2.0.2**, the driver the BSP's `*` dependency resolved to on the BSP's date | component registry; zip sha256 `ab5975120d8c10d5de422bfe6eb0056af7a762ce7e57d67c8529987e858dd72a` | the four commands the driver sends before the vendor table, and its reset timing | Apache-2.0, (c) 2024 Espressif Systems |
| LovyanGFX 1.2.21 | already pinned in `[common]` | `Bus_DSI`, `Panel_DSI`, `Touch_GT911` | FreeBSD |
| arduino-esp32 3.3.9 via pioarduino 55.03.39 | already the platform | ESP-Hosted, `esp_wifi_remote`, the P4 board JSONs | LGPL-2.1+ |

Both Apache files ship the same unmodified Apache-2.0 text, byte for byte. One copy goes beside the code that uses them, as
`include/Panel_JD9365-LICENSE-Apache-2.0.txt`. What changed from the originals is stated in the header of `include/Panel_JD9365.hpp`,
which is what Apache-2.0 section 4(b) asks for:
- the table is re-encoded from `jd9365_lcd_init_cmd_t` into LovyanGFX's `{len, cmd, params...}` byte stream;
- the 800 x 800 branch is dropped;
- the driver's four lead-in commands are prepended to the table;
- the driver's ID read is not carried over.

## 2. A finding that needed no board: every P4 would share one identity

`DeviceIdentity::Name()` and `DeviceIdentity::LeaderboardId()` (`include/DeviceIdentity.h`) both read the MAC with
`esp_read_mac(mac, ESP_MAC_WIFI_STA)`, into a buffer initialised to zero, and both ignore the return code. The Name is the
mDNS name, the DHCP name and the setup-AP SSID. The LeaderboardId is the `X-Blip-Device` header that `CloudFeed` sends.

ESP-IDF 5.5's `esp_read_mac` answers only the types listed in `s_mac_table`, and the type byte leads each 8-byte entry. Here is
that table, read from the libraries this platform links:

| library | types in `s_mac_table` |
|---|---|
| `esp32s3/dio_opi/libesp_hw_support.a` (**the control**) | 00 WIFI_STA, 01 WIFI_SOFTAP, 02 BT, 03 ETH, 05 BASE, 06 EFUSE_FACTORY, 07 EFUSE_CUSTOM |
| `esp32p4_es/lib/libesp_hw_support.a` | 03 ETH, 05 BASE, 06 EFUSE_FACTORY, 07 EFUSE_CUSTOM |
| `esp32p4/lib/libesp_hw_support.a` | 03 ETH, 05 BASE, 06 EFUSE_FACTORY, 07 EFUSE_CUSTOM |

The sdkconfig agrees: on the P4, only `CONFIG_ESP_MAC_ADDR_UNIVERSE_ETH=y` is set. So on a P4 the call fails with "mac type is
incorrect (not found)", the buffer stays all zero, and:
- **every P4 board gets the same name:** the product name and a suffix of six zeros (the suffix is the MAC's last three bytes);
- **every P4 board sends the same `X-Blip-Device`**, which is the first 8 bytes of SHA-256(six zero bytes || salt).

**The alternative, and why the data excludes it.** The alternative is that `esp_wifi_remote` or ESP-Hosted adds a WIFI_STA entry
at run time. Neither can: `s_mac_table` is a static array in `.data`, and its contents are fixed at compile time. Two things still
cannot be read off the library:
- whether a later `esp_iface_mac_addr_set` call could fill the entry (it cannot add a type the table does not list);
- what the C6 itself reports.

Row W2 reads the SSID on the board, which settles it.

**This needs a ruling (Q-ID), and it is not fixed here.** `DeviceIdentity.h` is shared, and a device's identity is tied to OTA
(#552). The candidates are:
- **(a)** on the P4 only, read `ESP_MAC_BASE` (the P4's own efuse MAC);
- **(b)** use the C6's STA MAC through `esp_wifi_get_mac`, which is only available after Wi-Fi starts, so `Name()`'s "works before
  Wi-Fi" property is lost;
- **(c)** something else.

Option (a) can be written under `#if CONFIG_IDF_TARGET_ESP32P4` so that no S3 image changes.

**Until the ruling, no P4 can reach a Worker, by construction.** Both P4 envs point `CLOUD_FEED_BASE` at `https://192.0.2.1`. That
is TEST-NET-1, the same unroutable address `[cloud] netfault` uses. The cloud feed is still compiled in, so the code that ships is
the code that is built. The OTA version check goes to GitHub and sends no device header, so it can still serve as the TLS row (W3).
Pointing the envs at staging is a one-line change, made after Q-ID.

## 3. The design

**New files** (nothing reads them except the P4 envs and the web model):
- `include/variants/p4_40.h`
  - the pins above, `BLIPSCOPE_PANEL_JD9365` and `BLIPSCOPE_TOUCH_GT911`;
  - `SCREEN_SIZE = 720`, `BANDED_RENDER = false`, `HAS_AUDIO = false` (the ES8311 has no driver here), `HAS_IMU = false` (the board
    has none), and `TOUCH_WATCHDOG = false` (the supervisor exists for the CST816, not the GT911);
  - `SLUG "p4-40"`, which names no release asset and no CI row.
- `include/Panel_JD9365.hpp`: an `lgfx::Panel_DSI` subclass whose `getInitParams(0)` holds the lead-in plus the 720 x 720 vendor
  table, and whose `getInitDelay` honours the table's two delays:
  - the lead-in is `E0 00`, `36 00`, `3A 55`, `80 01`;
  - the delays are 120 ms after `11` (sleep out) and 20 ms after `29` (display on).

  To honour both delays, the table is split into four lists: everything before `11`, `11` (120 ms), `29` (20 ms), and `35`
  (0 ms). LovyanGFX applies a delay only after a whole list. After the lists, LovyanGFX's own `init_panel` writes MADCTL and COLMOD again. With
  `rgb_order = true` those writes are `36 00` and `3A 55`, the same values the lead-in sent.
- `include/Panel_JD9365-LICENSE-Apache-2.0.txt`: the licence text, verbatim.
- `src/board/board_p4_40.cpp`, wrapped whole in `#if defined(BLIPSCOPE_VARIANT_P4_40)`, so every other env compiles it to an empty
  object. It holds `variant::BoardPreInit()`, which does two things.
  - It prints `[p4] chip=... rev=vX.Y cpu=...MHz flash=...B psram=...B` (B0 and B1 read it).
  - It registers a Wi-Fi event handler that prints, once, at the first STA or AP start:
    `[hosted] host=2.12.8 slave=X.Y.Z target=... -> equal|slave older|slave newer`.
  - So `main.cpp` is not touched at all.

**Shared files changed (each needs blipscope-5c's read before any merge):**
- `include/variants/Variant.h`: one `#elif` for `BLIPSCOPE_VARIANT_P4_40`.
- `include/LGFX.h`: a `BLIPSCOPE_PANEL_JD9365` block (`Bus_DSI` + `Panel_JD9365`) and a `BLIPSCOPE_TOUCH_GT911` block. Both are
  additions inside `#if` branches no S3 variant takes.
- `platformio.ini`: two new envs. No common section changes.
  - `[env:blipscope-p4-40]`: `board = esp32-p4`, the pre-rev-3 libraries at 360 MHz, which matches Waveshare's figure.
  - `[env:blipscope-p4-40-r3]`: extends the first with `board = esp32-p4_r3`, the rev-3.1+ libraries at 400 MHz.
  - **Two envs because an image for one silicon revision does not boot on the other:** the v3 libraries carry
    `CONFIG_ESP32P4_REV_MIN_FULL=301`, and the ES libraries `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`. Row B0 picks one, and both are
    compiled so that bench day does not start with a build.
  - Both use `flash_size = 16MB` and `default_16MB.csv` (two 6,553,600 B app slots), as `blipscope-pro-s3-21` does. A 16 MB header
    on a 32 MB part is valid and leaves the upper half unused. Reaching past 16 MB needs 32-bit flash addressing, which the
    prebuilt libraries were not configured for.
  - Serial goes through the board's USB-UART port, so no `ARDUINO_USB_*` flags.
  - Both add `-DCORE_DEBUG_LEVEL=1`, so the core's `log_e` lines are printed. A failed ESP-Hosted bring-up is otherwise silent at
    level 0.
- `tools/web-renderer/build.sh`: one `case` line, `p4-40) VARIANT=BLIPSCOPE_VARIANT_P4_40`, for section 5. No release asset
  changes; the workflow builds only `s3-128` and `s3-21`.

**Values that differ from the BSP, on purpose:**
- **Backlight PWM at 20 kHz, not 5 kHz.** This follows `[common]`'s rule that a dimmed backlight must not whine. Row B3 checks that
  the backlight driver dims at 20 kHz, and falling back to the BSP's 5 kHz is a pre-registered outcome.
- **One DPI framebuffer, not the BSP's default of three.** LovyanGFX's `Panel_DSI` creates one, and it writes back the CPU cache
  itself (`Panel_FrameBufferBase`, `esp_cache_msync`). So `board::DisplayFlush` stays the inline no-op, unlike the RGB panel on the
  2.1. A single buffer can tear, which row B5 records.

**What is deliberately not done:**
- no change to `DeviceIdentity.h` (Q-ID);
- no `skus.yml` row (Q-CI);
- no OTA source for a P4 image (Q-OTA);
- no C6 firmware update (Q-C6).

## 4. Compile rows (graded on this workstation)

| row | prediction | outside the set |
|---|---|---|
| **C1** builds | `pio run -e blipscope-p4-40` and `-e blipscope-p4-40-r3` both end `[SUCCESS]`. Fixes inside the new files do not count against this row. **Any edit to an existing `src/` or `include/` file needed to get there is listed in section 8, by file and reason, and counted.** | a failure inside the toolchain or platform (not our code): stop and report, with no workaround in shared files |
| **C2** image size | each `firmware.bin` is 1,450,000 to 2,510,000 B, which is 0.75x to 1.3x the s3-128's 1,931,983 B measured today at `8f1a878f`. Expected near the low middle: the S3's native Wi-Fi blobs (`net80211`, `pp`, `phy`) are replaced by the smaller ESP-Hosted host and `esp_wifi_remote`. | under 1.45 MB: something large did not link (the radar, or the Wi-Fi stack). Over 2.51 MB: something unexpected linked (both Wi-Fi stacks, or LVGL). Either way, find out which before anything else |
| **C3** partitions fit | the image is under half of the 6,553,600 B app slot. PlatformIO's RAM line (static DRAM against the 327,680 B board figure) is under 50 % | either over: stop |
| **C4** the artifact holds what the ini says | the ELF contains `blipscope-p4-40`, `p4-40` and `192.0.2.1`. It does **not** contain `scopes.valarsystems.com` or `scopes-staging.valarsystems.com` via the feed base: checked as in `[env:blipscope-s3-128-netfault]`, with that env's image as the anchor that the grep can see a host at all. It contains the symbols `Panel_JD9365` and `Touch_GT911`. `esptool image_info` gives the r3 image a minimum chip revision of v3.1, and the other image a minimum below v1.0 and a maximum below v3.0 | any mismatch: the ini did not take; stop |
| **C5** no existing image changes | for every env in `skus.yml` that CI builds (ten), the image before the change (`8f1a878f`) and after differ **only** in the five rebuild regions: `app_elf_sha256` (32 B), the trailing SHA-256 (32 B), the checksum byte, and WiFiManager's two `__TIME__` strings (up to 5 B each). Same size, zero payload bytes. Graded by `tools/weatherscope/s1/stampdiff.py`, whose UNEXPLAINED bucket must be empty. | any unexplained byte, or a size change: that env's image changed; stop before anything is pushed |

Why C5 is expected to hold: the new TU compiles to nothing outside its `#if`, and both header edits sit inside branches no S3
variant takes. Neither header expands `__LINE__`, so their shifted line numbers reach no other TU's bytes. **If C5 fails, that
reasoning was wrong, and the byte offset says where.**

## 5. The 720 px layout, before the board (web renderer)

`tools/web-renderer/build.sh p4-40` compiles the radar's own translation units, unmodified, with `variant::SCREEN_SIZE = 720`. The
frames are rendered in a browser from `site/scene.js`, the synthetic scene, so no request leaves the page. The page is a scratch file
next to `dist/` that calls `mount()` the way `site/app.js` does. It is not committed and not published.

**The rule that predicts most of it.** The radar draws geometry two ways:
- **scaled:** ring radii, the globe, Follow's faces, the dots row and the splash are written as "the 240 px figure times
  `SCREEN_SIZE/240`", which is 3.0 at 720;
- **absolute pixels:** text is almost entirely `Font0` (6 x 8 px) at `setTextSize(1)`, and the touch thresholds are
  `SWIPE_MIN = 40` and `TAP_RADIUS = 28`.

At about 180 ppi against about 188, **anything in absolute pixels is the same physical size it is on the 1.28 in board, and anything
scaled is about 3x larger.**

| row | prediction | outside the set |
|---|---|---|
| **L1** the model is the 720 one | `_web_screen_size()` returns 720 and the canvas reports a 720 x 720 panel | anything else: the variant did not take |
| **L2** text stays small | radar labels, the list rows and the stats rows render at 8 px cap height, as at 240. **On a 720 px face they look a third the size relative to the disc.** That is the main layout finding expected, not a defect of this bring-up. Nothing is clipped: `ChordWidthPx` only gets wider | any text clipped by the disc edge at 720 that is not clipped at 240 |
| **L3** scaled geometry is right | rings, the globe and the splash fill the disc in the same proportions as at 240 and 480 | a scaled element that does not scale, for example a ring still at its 240 px radius |
| **L4** touch targets | swipe (40 px, 5.6 mm) and tap radius (28 px, 3.9 mm) are the same physical size as on the 1.28 in board, so they are fine. Any on-screen control drawn with scaled geometry is larger than on the 1.28 in, also fine. Nothing is predicted too small to hit | a visible control smaller than 28 px across |
| **L5** faces | Radar, List, Stats, a detail card and Connect each render and switch on swipe, as at 240 (the carousel's own markers: "AIRCRAFT", "STATS", "http://") | a face that fails to draw, or draws at 240 px in the corner |

L2 and L4 are recorded as findings for a ruling (Q-TEXT: whether a 720 px product scales its text), not changed here.

## 6. Bench rows, frozen now, run only when the board is here

The pre-registered outcomes are the only readings with a meaning. Anything else is "the instrument or the premise is wrong: stop
and decide". Every row reads the board's own output: serial at 115200 through the USB-UART port, or the screen.

**Before any flash:**

| row | how | prediction | then |
|---|---|---|---|
| **B0** silicon revision | `esptool --chip esp32p4 chip_id`, which writes nothing; or the ROM banner | **v1.x** (the 360 MHz figure) | v1.x: flash `blipscope-p4-40`. v3.x: flash `-r3`. Anything else, or unreadable: stop |

**After a flash:**

| row | how | prediction | then |
|---|---|---|---|
| **B1** boot facts | the banner `[build] env=` and the `[p4]` line; a ruler across the lit disc | env matches what was flashed; `psram` about 32 MB; `flash` header 16 MB; cpu 360 (or 400 on r3); visible diameter 99 to 103 mm | a wrong env: the wrong image is on the board. PSRAM under 8 MB: stop |
| **B2** panel lights, right colours | `[disp] tft.init=1 720x720`; the boot splash, then the radar | backlight on, image stable, **green radar on black, no colour swap**; the Locked EAM palette is not involved | **red/blue swapped:** flip `rgb_order` (one line), re-run. **Black with backlight on:** compare the DSI timing to the BSP's; no other change. **Rolling or shifted image:** the DPI clock; stop and read |
| **B3** orientation and backlight | the splash's text and the radar's north | text reads upright and left to right; north is at the top. Brightness steps visibly at 20 kHz with no audible whine | mirrored or rotated: `offset_rotation` (one value, recorded). **No dimming at 20 kHz:** use the BSP's 5 kHz, a pre-registered fallback, and re-run B3 |
| **B4** touch maps | the existing `[touch] release start=(x,y) ... -> TAP/SWIPE` line | (a) a tap at the disc's centre logs `start` within 40 px of (360,360). (b) Taps just inside the disc edge at top, right, bottom and left log y < 80, x > 640, y > 640 and x < 80, with the other axis within 80 px of 360. (c) A rightward swipe logs dx > 0 with abs(dx) > abs(dy), and a downward one dy > 0. (d) The GT911 answers at 0x5D or 0x14; LovyanGFX tries both, and which one is recorded | (b) or (c) inverted on one axis: mirror flag. Axes exchanged: swap. Coordinates scaled (taps reach only part of the disc): the GT911's configured resolution is not 720, so read registers 0x8048-0x804B and record them. No touch at all: stop |
| **B5** frame time, empty sky | the `[health] frame avg=... p95=...` line on the Radar face. **n = 0 by construction:** the feed points at TEST-NET until Q-ID, so this is a lower bound. The loaded-sky reading (n of 20 to 40) is **B5b**, run after Q-ID | **p95 33 to 66 ms**. The P4 pushes 9x the pixels of the s3-128 (1,036,800 B a frame) and also feeds the DPI scan-out at about 132 Hz (80 MHz / (800 x 760)), roughly 136 MB/s of PSRAM reads, competing with drawing. A tear line in the sweep is possible (one buffer) and is recorded, not failed | **under 33:** better than predicted; record. **Over 66 but under the 85 ms ceiling:** a performance item. **Over 85:** BUDGET BROKEN by the firmware's own ceiling. A model-line breach (`> model + 10`) **without** a ceiling breach means "the s3-128 model does not fit this board", not a fault. Stale stripes or a frame that never updates: the cache writeback did not happen; stop |

**Wi-Fi through the C6:**

| row | how | prediction | then |
|---|---|---|---|
| **W1** hosted link | the `[hosted]` line and any `log_e` at boot | the line appears, `target=esp32c6`. No `esp_hosted_init failed` or `connect_to_slave failed` | no `[hosted]` line plus a hosted `log_e`: the SDIO pins or the C6 reset differ from 14-19/54. Read Waveshare's schematic before changing anything |
| **W2** setup portal | no saved credentials, so the portal starts. A phone joins it and opens the page | the AP SSID ends in **six zeros** (section 2: the product name, a hyphen, then the all-zero suffix), and the portal page loads | a real suffix: section 2 is wrong. Stop and re-read the MAC path before any cloud row |
| **W3** TLS fetch | credentials saved through the portal; the firmware's own OTA version check to GitHub | STA joins and gets an IP; `version.txt` is read over TLS (HTTP 200); no `[health] ALLOC FAILED`. The feed's requests to 192.0.2.1 fail at the transport, by design. **So the reachability ladder (`NetWatchPolicy.h`) walks as it did in Run 4:** after at least 10 failures over at least 5 min, Reconnect, then RadioReset (which here goes through ESP-Hosted to the C6), then one Reboot, capped at one per 24 h. Each logs its own cause line. These are expected under this base, not faults, and any frame-time reading taken across one is marked | TLS error: record its text. `CanHandshake` refusing: record it. A reboot with no ladder or firmware cause line: the reboot is not the firmware's (the capture-rig entry in CLAUDE.md); stop and check the rig |
| **W4** OTA | needs a P4 image to install (Q-OTA) | not runnable until Q-OTA. Then: a download into the other app slot, a reboot into it, `[build] env=` unchanged, and the OTA identity record reading board `p4-40` | |
| **W5** ESP-Hosted versions | the `[hosted]` line | host 2.12.8. The slave version is recorded as read. Pre-registered verdicts: **equal**; **slave older**, which is Q-C6, the C6 is not flashed without a ruling; **slave newer**, recorded | a slave version of 0.0.0: the RPC failed; stop |

**Run only after Q-ID is ruled and the base points at staging:** a fetch to the Worker (`/v1/blips` answered), and the enrich path.

## 7. Questions for rulings

- **Q-ID** (section 2): the P4's identity source. It blocks every cloud row.
- **Q-CI:** whether `blipscope-p4-40` gets a `build-only` row in `skus.yml`, so the tree cannot rot (the 1.75 in lesson), or stays
  local until the board passes. A row also needs a `plant_target()` case in the adsbdb launch gate, or that gate reads UNTRUSTWORTHY.
- **Q-OTA:** how to give a P4 an image to install for W4. Either a pinned pre-release like `ota-preflight-v6`, which belongs to
  blipscope-5c's release process, or something else.
- **Q-C6:** whether the C6's ESP-Hosted firmware may be updated if W5 reads "slave older". `hostedHasUpdate()` would fetch
  `espressif.github.io/arduino-esp32/hosted/esp32c6-v2.12.8.bin`.
- **Q-TEXT** (section 5): whether a 720 px face scales its text.

## 8. Results

Filled in as each row is graded, against the predictions above as committed.
