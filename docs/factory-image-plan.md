# The 1.28" factory image: facts, options, and the bench proof

**Status: decided 2026-11-05 (~17:40Z). Bench-proof predictions frozen; nothing flashed yet.**

**Daniel's decisions, all as recommended below:**
1. **Partition table:** today's, unchanged (T1).
2. **Bootloader:** the stock ESP-IDF 5.5.4 one, as in every published factory set (L1).
3. **eFuses:** none burned. **Erase:** ~~the factory erases the whole chip before writing~~ **changed by Daniel on
   2026-11-06:**
   - **no erase for production, the samples excepted** (ruled ~18:34Z, which replaces the ~18:22Z and ~18:29Z
     versions);
   - the 3 pre-production samples get the full erase (ERASE, then START; esptool `--erase-all`), Daniel's call;
   - every other board, test program or not, gets the one file written at `0x0` (START only; esptool without
     `--erase-all`).
   - **Why it is safe, read from the released v18 file and the v18 source** (`672eab75eb6c`):
     - the file covers `0x0`-`0x1F6C00` and rewrites everything v18 reads: the bootloader, the partition table,
       NVS as 0xFF (`0x9000`-`0x1E000`, all 0xFF in the file), otadata selecting app0 (`0x1E000`-`0x20000`,
       initialised), and the app;
     - leftovers can sit only past the image in app0; in app1, which the first OTA rewrites before it is ever
       booted; and in spiffs (`0xCA0000`) and coredump (`0xFF0000`), which v18 never uses. A grep of v18's `src/`
       and `include/` finds no SPIFFS, LittleFS, FFat or core-dump use, and the same grep finds v18's 73 NVS lines,
       so it can see.
     - v18's OTA path is Arduino's HTTPUpdate (`OtaUpdater.cpp:450`), whose update library erases what it writes.
       That part is the reviewer's reading, from the library, not read here.
   - **The rule for later images:** one that starts using the spiffs or coredump partitions treats their contents
     as unknown on first use (format or verify), because production boards are not erased past the file's end.
   - **Proven on a dirty board on 2026-11-06:** see "The bench proof, run" below.
4. **Device keys:** customers activate at setup against a server-side allowlist
   (`docs/device-activation.md`, a separate PR). The factory image is the same either way.
5. **The rollback test is skipped for now.** No prerelease is created. Deferring the "valid" mark
   until an image is healthy (L1's follow-up) comes in a later update, with its own proof.

**Deliverables, due 2026-11-11:** the file and the factory sheet
([factory-sheet-s3-128.md](factory-sheet-s3-128.md), English and Chinese). That is a day before
the factory needs them. Daniel provides the stock unit for the bench proof.
Requested by the reviewer on 2026-11-05 (~15:30Z). This is the plan for one image that a factory
writes to a run of Kit S3 boards (`blipscope-s3-128`), needed by about 2026-11-12. Every 1.28"
scope is to be reachable from that image later, with the chooser and edition switching
arriving by OTA.

**The image only has to get right what OTA can never change.** On this board that is
five things:

1. the **bootloader** at 0x0;
2. the **partition table** at 0x8000;
3. **eFuses**;
4. what the factory writes into **NVS**;
5. the factory app's **own OTA client**: its URL, the version gate and its pinned root
   certificates. That client is the only road to every later image, so it stays fixed until
   the first update succeeds.

Everything else is an app image, and the first OTA replaces it.

Sections 2 and 3 were written as recommendations, before the decisions above. They are kept as
the record of what was weighed.

---

## 1. Facts

Everything here was read from built output, a published release asset or the source. All of
it is read-only: no board was opened, flashed or read.

### F1. The bootloader is ESP-IDF 5.5.4's prebuilt one, and it already supports rollback

- **Where it comes from.** The pinned platform (pioarduino `55.03.39`, `platformio.ini:67`;
  the same at every tag from v6 to v18) installs arduino-esp32 3.3.9 with prebuilt IDF libs
  `5.5.4+sha.735507283d`. The build converts `bootloader_qio_80m.elf` with elf2image, writing
  a DIO / 80 MHz / 16 MB header.
- **It matches the published asset byte for byte.** Regenerating it reproduces
  `bootloader-s3-128.bin` exactly: 19,984 B, sha256
  `b41be55ae9a52aeeb21645c51b86c14027f84c2c91bf67bee6aa0e1b15d18e8b`.
- **Rollback is on.** In the framework's `esp32s3/sdkconfig`:
  - line 422: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`;
  - line 3956: `CONFIG_APP_ROLLBACK_ENABLE=y`.

  A disassembly of the bootloader shows both state changes the feature needs: NEW to
  PENDING_VERIFY, and PENDING_VERIFY to ABORTED.
- **Off in the same file:**
  - anti-rollback (line 423);
  - the factory-reset pin and test-app pin (lines 473-474);
  - secure boot (line 493);
  - flash encryption (lines 494 and 3965).
- **Nothing in this repo customises the bootloader.** There is no `sdkconfig`, no
  `custom_sdkconfig`, no `board_build.bootloader` and no `platform_packages`.

### F2. The app marks itself valid before `setup()` runs

arduino-esp32's `initArduino()` checks whether the running image is in PENDING_VERIFY. If it
is, it calls the weak `verifyOta()` (which returns `true`) and marks the image valid. A weak
`verifyRollbackLater()` (which returns `false`) is what would defer that, and no code here
overrides either function or calls any OTA-validity API.

**So rollback catches only an image that dies before `initArduino()`.** An image that crashes
in `setup()` or `loop()`, cannot join Wi-Fi, or cannot reach the OTA server has already been
marked good, and the bootloader will keep booting it. This is app behaviour, not bootloader
behaviour: any later image can defer its own validation, and an OTA can deliver that (section
2, L1).

### F3. A factory or USB flash leaves the boot record outside rollback tracking

The framework's `boot_app0.bin`, sha256 prefix `f94c5d786a7a`, is the published
`boot_app0-s3-128.bin`.
- Its first otadata entry is `ota_seq = 1` with state `0xFFFFFFFF` (UNDEFINED). That selects
  `app0`.
- Its second entry has a CRC of `0xFFFFFFFF`, so it is invalid.

UNDEFINED is treated as valid and is never rolled back, which is the normal state for a
USB-flashed image. The first OTA writes the other slot as NEW, and rollback tracking begins
there.

### F4. The partition table (`partitions-s3-16mb-bignvs.csv`)

| name | type | offset | size |
|---|---|---|---|
| nvs | data | 0x9000 | 0x15000 (84 KiB) |
| otadata | data | 0x1E000 | 0x2000 |
| app0 (ota_0) | app | 0x20000 | 0x640000 (6,553,600 B) |
| app1 (ota_1) | app | 0x660000 | 0x640000 |
| spiffs | data | 0xCA0000 | 0x350000 (3,473,408 B) |
| coredump | data | 0xFF0000 | 0x10000 |

- **The spiffs partition is unused.** No s3-128 edition mounts or writes it: there is no
  `SPIFFS`, `LittleFS`, `U_SPIFFS` or `esp_partition_write` anywhere in `src/` or `include/`.
- **The coredump partition is written only by the IDF panic handler.** The firmware never
  reads it.
- **The table has one commit,** `c29a803b9fa5` (2026-07-31). Its own comment says it was
  "adopted before the 50-unit pilot burn: whatever table those boards ship with is permanent
  for them."
- **The built table is identical on every release that published one.** `partitions-s3-128.bin`
  has sha256 `28f336fe57a9991a5f39db735295a978e0a6aee5710f5e7107ac44bdd812d29b` on the 09-24
  scratch prerelease and on v15, v16, v17 and v18.
- **Two guards exist around it:**
  - `scripts/fix_boot_app0_offset.py` moves `boot_app0` to the table's real otadata offset,
    0x1E000. The platform hardcodes 0xE000, which is inside the enlarged NVS.
  - `scripts/flash_manifest.py` refuses any region that overlaps NVS.

### F5. Every s3-128 image uses under a third of a slot

| env | image | of a 6,553,600 B slot | source |
|---|---|---|---|
| `blipscope-s3-128` | 1,928,192 B | 29.4% | v18's published `firmware-s3-128.bin` |
| `weatherscope-s3-128` | ~1.88 MB | ~28.6% | a local build of main (not published) |
| `missileer-s3-128` | ~1.74 MB | ~26.5% | a local build of main (not published) |

- **Only `blipscope-s3-128` is published.** It is the only s3-128 row with a slug in
  `skus.yml`. The other two are built in CI but never published.
- **Six editions have no s3-128 build yet:** Orbitscope, Quakescope, Quillscope, Reelscope,
  Claudescope and Speedscope exist only as `-s3-146` envs. Reaching them from this image needs
  an s3-128 env for each, which is OTA-side work and does not change the factory image.
- **The largest s3-128 image leaves ~4.6 MB of each slot free.** Slot size is not a
  constraint on any edition.

### F6. The 50 pilot units

- **Table: certain.** The table above predates the pilot burn and has not changed since (F4).
- **Bootloader: expected, but not proven for every unit.** Every published factory set has the
  same bootloader, `b41be55ae9a5…` (F1), but none was published for v6 to v14.
  - A pilot unit flashed from a published factory set carries those bytes.
  - A unit flashed from a local build before then is expected to carry the same bytes, because
    the platform pin is the same. That is unproven.
- **Release, ship date and flashing method: not recorded on main.**
  - `docs/RELEASE-v16.md:43` says "The 50 units ship on v15".
  - The open PR #384 plans a bench run on v16's factory set, and records no results.
- **Boards flashed before 2026-07-31 keep the stock table** (`default_16MB.csv`: NVS
  0x9000/0x5000, app0 0x10000). Those are bench boards only.
- **The only guard against a wrong table is a serial warning.** `-DBLIPSCOPE_EXPECT_BIG_NVS`
  makes `Logbook::reportCapacity()` print `*** WRONG PARTITION TABLE ***` on a small NVS
  (`src/Logbook.cpp:209-241`). It does nothing else.

### F7. OTA cannot change the table or the bootloader

- **The update path writes only the spare app slot and otadata.** `src/OtaUpdater.cpp:450`
  calls `httpUpdate.update(...)`. In arduino-esp32 3.3.9 that writes the next app partition and
  then sets otadata (`HTTPUpdate.cpp:43-48`; `Updater.cpp:235-236` and `:735`).
- **Nothing in `src/` or `include/` writes flash anywhere else.**
- **WiFiManager's `/update` page writes an app image only.** WiFiManager 2.0.17 registers it on
  the setup portal unconditionally (`WiFiManager.cpp:658-659`), so it is reachable while the
  portal runs.
- **Only a USB flash rewrites 0x0 and 0x8000** (valar-flasher, esptool).

### F8. The factory app's OTA client

- **Version gate.** `OTA_RELEASE_BASE` is
  `https://github.com/Valar-Systems/valar-scopes/releases/latest/download`
  (`src/OtaUpdater.cpp:96`). The device reads `version.txt` there and compares it with
  `FW_VERSION` (18 in v18).
- **Image fetch.** The image comes from `firmware-` + `FW_OTA_PREFIX` + `variant::SLUG` +
  `.bin` (`:106`). For this build that is `firmware-s3-128.bin`.
- **Trust.** TLS is checked against eight pinned roots in `include/OtaCerts.h`: Sectigo E46
  and R46, USERTrust ECC and RSA, ISRG X1 and X2, and DigiCert G2 and G3. Its header gives
  their expiry range as 2035-2046.
- **This is the one piece of app code that the factory fixes in practice.** A unit can only
  replace it by taking an update through it.
- **What changed between v17 and v18:** only the reboot-cap logic (`DeferRebootWithCause`'s
  `scheduled` argument, #377). The download and apply path is unchanged.

### F9. The CI factory set (v18, commit `672eab75eb6c`)

`firmware-s3-128.factory.bin` is 2,059,264 B with sha256
`1e1670abc917737733b15ef8875286f2945e1d9ac16148dbf877c1c6814b20bd`. It is built only in CI
(`RELEASING.md`), by `scripts/flash_manifest.py`, and is published with
`flash-manifest-s3-128.json` and `factory-scan-s3-128.json`.

| region | offset | size | sha256 |
|---|---|---|---|
| bootloader | 0x0 | 19,984 B | `b41be55ae9a5…` |
| partitions | 0x8000 | 3,072 B | `28f336fe57a9…` |
| boot_app0 | 0x1E000 | 8,192 B | `f94c5d786a7a…` |
| app | 0x20000 | 1,928,192 B | `5a5f662bd51b…` |

- **It covers 0x0 to 0x1F6C00 only.** It holds 0xFF across the NVS span (0x9000-0x1E000), so
  writing it at 0x0 also blanks NVS.
- **It leaves 0x1F6C00 to the end of flash alone.** That is the rest of `app0`, all of `app1`,
  spiffs and coredump. On a new board those regions still hold whatever the vendor's demo
  firmware left there.
- **NVS is preserved only in valar-flasher's region mode.** The manifest's `preserve` entry for
  NVS applies to that mode, not to a write of the merged file.

### F10. A factory-flashed unit has no device key

- **The plan of record for assembled units is the bench mint** (`docs/provisioning-mint.md`).
  The bench sends the MAC to `POST /blipscope/provision`, then writes the key into NVS over
  USB.
- **The mint is capped at 120 per UTC day.** The cap is a Worker constant.
- **A keyless unit can instead self-enrol.** The customer solves a Turnstile challenge from the
  config page (`docs/device-enrollment.md`). The firmware shows never-enrolled, enrolled and
  refused as three separate states (`src/ConfigurationWebServer.cpp:2726-2735`).

### F11. What a fresh unit shows on first boot

With no saved Wi-Fi and no earlier join failure, the unit draws the setup screen: a green
`SETUP`, a QR code and the unit's name on a black background (`include/BootScreen.h:111`, drawn
from `include/WiFiManagerHelpers.h:769`). The name is the same one its setup Wi-Fi network uses.
The amber version of the screen (`:763`) appears only after a failed join, so a fresh unit should
never show it. **Corrected 2026-11-05 (ruled ~21:20Z): the firmware version IS on the screen.** A fresh unit
first shows a splash reading "Blipscope V18", then a 3-second Wi-Fi-reset countdown, then the setup screen.
This was seen at the bench (T2). The original line here said the version was not shown, and it was wrong.

On serial the build prints `[build] env=blipscope-s3-128 fw=v18 features=…`
(`include/BuildIdentity.h:125`). On this board, closing a serial capture has left the chip in
ROM download mode three times out of three on the bench, showing a black screen until it is
replugged.

### F12. eFuses

Nothing in the repo burns an eFuse. Secure boot and flash encryption are off (F1). Any eFuse
burn is irreversible.

---

## 2. Options (decided 2026-11-05: T1, L1, no eFuses, a full erase, activation by allowlist)

### The partition table

**T1 — keep `partitions-s3-16mb-bignvs.csv` (recommended).**
- **For it:**
  - It is identical to the 50 pilot units and to every published factory set.
  - CI already builds, launch-gates and scans its factory set.
  - Every s3-128 image fits a slot about three times over (F5).
  - Nothing needs building before 11-12.
- **Against it:**
  - The 3.3 MB spiffs region stays unused for the life of these units. Space that is not
    declared as an app partition today can never become one by OTA.
  - Recovery from an update that breaks Wi-Fi or OTA is by USB only, through valar-flasher.

**T2 — turn the spiffs span into a protected recovery app.** This is a `factory`-subtype app
at 0xCA0000 that can join Wi-Fi and pull a good image over HTTPS.
- **How it would be entered:**
  - The stock bootloader falls back to the `factory` partition when no OTA slot holds a
    bootable image (both invalid or aborted).
  - The main app could also select it on purpose.
- **The factory flash still boots the main app first.** `boot_app0` selects `app0` explicitly
  (`ota_seq = 1`, F3), so a `factory` partition is not booted first. This needs proving on the
  bench.
- **What it catches is narrow today.** The Arduino core marks an image valid before `setup()`
  (F2), so a crash in `setup()` or `loop()` never leaves the slots unbootable. The fallback
  would fire for little until images defer their validation.
- **Cost before 11-12:**
  - a recovery app;
  - a second partition CSV;
  - a factory-set builder for it;
  - a CI row and its launch-gate plant site;
  - a bench proof.

  All of that work is new, so none of it has field history.
- **Two layouts in the field for good.** The 50 pilot units stay on T1. Every OTA image would
  still run on both, since `app0`, `app1` and NVS keep their offsets, but every later plan has
  to remember which units have the recovery partition.
- **The recovery app is fixed for these units' lives.** The main app could rewrite that
  partition, but a power cut during the rewrite leaves no recovery, which is no worse than T1.

**Not proposed: rewriting the table from an app later.** On the ESP32-S3 it needs dangerous
flash writes, and a power cut during the write bricks the unit.

### The bootloader

**L1 — keep the framework's prebuilt bootloader (recommended).**
- It already supports rollback (F1), and it is byte-identical to every published factory set
  and, as far as the record shows, to the pilot units.
- **Making rollback useful is app work, and an OTA can deliver it.** A later image overrides
  `verifyRollbackLater()` to return `true` and marks itself valid only once it has proven
  itself, for example after Wi-Fi joins and one authenticated config fetch succeeds.
- **That protects every update to such an image.** It does not need to be in the factory image.
- **It is proposed as a follow-up, not a blocker.**

**L2 — a custom-built bootloader.** What it could add on this board:
- **Factory-reset and test-app pins.** Not usable: the board's buttons cannot be reached inside
  the enclosure.
- **Anti-rollback and secure boot.** Both need eFuse burns (F12).
- **Flash encryption.** This also changes how valar-flasher and a USB recovery work.
- **Cost:** a bootloader build path with no CI or field history, a week before the factory
  date. Not recommended.

### Smaller decisions the factory flow needs

- **eFuses: burn nothing (recommended).**
  - Burning a USB-JTAG or download-mode disable would remove the USB recovery path.
  - Secure boot and flash encryption are separate projects.
- **Erase the whole chip before writing (recommended).** That clears the vendor demo's bytes
  from `app1`, spiffs and coredump (F9), so every unit leaves the factory identical. The cost
  is the erase time, measured in P1.
- **Device keys.**
  - **(a)** Units pass through the bench after the factory, which mints and writes each key
    (F10). That is 600 USB sessions, and the 120-a-day cap makes it at least five days.
  - **(b)** Units ship keyless and the customer self-enrols during setup.

  Either way the factory image is the same. Only the route after the factory differs.

---

## 3. The factory image (decided: T1 + L1, a full erase, no eFuses)

- **Release: v18** (commit `672eab75eb6c`, Latest since 2026-10-20).
  - Its factory set is CI-built and launch-gated, and it is published with a manifest and a scan.
  - The image only has to boot, join Wi-Fi and take its first update, and a unit takes whatever
    is latest at its first check.
  - **If v19 is published before the factory date, the factory still gets v18.** The image the
    bench proved is the one that ships.
- **One file:** `firmware-s3-128.factory.bin` from the v18 release, sha256
  `1e1670abc917737733b15ef8875286f2945e1d9ac16148dbf877c1c6814b20bd`, written at 0x0. Its
  offsets are in F9.
- **The command,** esptool 5.x (4.x takes the same command with `write_flash` and underscores):

  ```
  esptool --chip esp32s3 --port <port> --before default-reset --after hard-reset \
      write-flash --erase-all --flash-mode keep --flash-freq keep --flash-size keep \
      0x0 firmware-s3-128.factory.bin
  ```

  - `--erase-all` erases the whole chip and then writes, in the same command. That is the
    decided full erase.
  - `keep` leaves the image's own DIO / 80 MHz / 16 MB header untouched. A development
    upload's `--flash-size detect` rewrites it.
  - In Espressif's Flash Download Tool the equivalent is one file at 0x0, *DoNotChgBin* ticked
    and *Erase* ticked.
- **The per-unit pass, which the factory can see:**
  1. **The flash log contains `Hash of data verified.` exactly once.** Its absence is a fail
     even on exit 0: esptool skips the check silently on a ROM that cannot hash
     (`docs/provisioning-mint.md`). There is one file and one region, so a second
     occurrence means a second write that should not exist.
  2. **After the automatic reset, the setup screen appears:** a green `SETUP`, a QR code and the
     unit's name on black (F11). A black screen, an amber screen, a different screen or a reboot
     loop is a fail.
  3. **The unit is unplugged without opening a serial port** (F11).
  4. **The unit's MAC goes on the per-unit list** that comes back with the shipment. The list
     feeds the activation allowlist. esptool prints it as `MAC:`; the device id derives from
     exactly that MAC (`ESP_MAC_WIFI_STA`, `include/DeviceIdentity.h:54`), and P0′ proves it on
     the bench.
- **No new release for a version mark on screen.** The hash check proves the right bytes landed
  and the setup screen proves they boot.
- **The board must have 16 MB of flash, because the table needs it.** P0 checks this. A batch
  with 8 MB would fail at the factory, so it belongs with the per-batch checks in
  `INCOMING-INSPECTION.md`.

---

## 4. The bench proof (predictions frozen 2026-11-05, before any write)

**The unit is the Missileer photo unit, serial ending E794, on COM16.** Daniel released it for
this proof on 2026-11-05 (~17:45Z): it is free to erase and flash, and nothing else uses it.
- **Its port is resolved by serial before every step**, never taken from the COM number.
- **No device id is recorded,** and neither is its MAC beyond the serial suffix already in the
  board registry.
- **One `bench-trace.ps1` capture is held open across each power cycle** and never reopened
  mid-step.
- **The log follows the redaction rules:** no Wi-Fi name, BSSID or LAN address.

**Two notes on the unit:**
- **The board registry still shows a reservation on it** from 2026-10-12 (the photo shoot).
  Daniel's release supersedes it; the registry itself is not edited from here.
- **Its panel size is established by its record, not measured.** USB cannot read the panel. It
  is the unit that ran the 1.28" Missileer photo build (`missileer-photo`, which is
  `missileer-s3-128` plus `PHOTO_MODE`) for the product photos. P2 confirms it: a 1.46" board
  shows a black screen under an s3-128 image.

**P0 is done (read-only, 2026-11-05 ~17:55Z),** and it matched the prediction:
- **Chip:** `ESP32-S3 (QFN56) (revision v0.2)`.
- **Features:** `Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)`.
- **Flash:** manufacturer `ef`, device `4018` (Winbond, 128 Mbit). `Detected flash size: 16MB`,
  quad mode set in eFuse, 3.3 V.
- **USB:** USB-Serial/JTAG mode.
- **MAC:** esptool's `MAC:` line matches the registry serial. It is not printed here.

**Each step below lists its prediction and what any other result means.** "None of the above" is
always a stop: the premise or the instrument is wrong, and the run waits for a ruling. The
phone steps (P3, P4) need Daniel at the bench, because the setup QR is scanned by a phone.

| step | what is done | predicted | outside the prediction |
|---|---|---|---|
| **P0** | `esptool flash-id` (read-only) | ESP32-S3, 8 MB PSRAM, 16 MB flash | **done, as predicted** (above) |
| **P1** | the factory command (section 3), `--erase-all`, with v18's factory set | exit 0; `Hash of data verified.` exactly once; the chip erase 15-120 s; the whole command under 4 min | no hash line or two: **fail** whatever the exit code; a time outside the band is recorded and the band revisited before the factory gets a number |
| **P2** | first boot after the automatic reset | the green setup screen (`SETUP`, QR and name) within 15 s; serial shows `[build] env=blipscope-s3-128 fw=v18` | any other screen, a black screen, a reboot or no banner: **stop** |
| **P3** | Wi-Fi setup by phone from the QR (Daniel) | it joins (`[WiFi] autoConnect() returned true`); the OTA check reads `version.txt` = 18 through the pinned roots and reports up to date, with no download | a TLS or fetch failure: **stop**, because it means the factory app's OTA client (F8) cannot reach the server from a fresh unit |
| **P0′** | the MAC esptool prints, run through the mint's derivation (`deviceIdFromMac`), compared with the id the unit's config page shows. A script prints only the boolean | `true` | `false`: **stop**, because the factory's MAC list would build the wrong allowlist |
| **P4′** | `bash scripts/ota-download-path-diff.sh v17 v18` | exit 0: every item on the download path is the same source | exit 1: P4 proves v17's code only. **Stop and decide** |
| **P4** | the OTA hop: v17's factory set written with the same command, Wi-Fi set up by phone (Daniel), and the unit left to update to the latest release (v18) | it downloads `firmware-s3-128.bin` and reboots into v18, whose banner shows `fw=v18`; otadata selects `app1` | no update, or a failed one: **stop** |

**Not run, by decision:** the bad-update rollback test (P5a and P5b in the first draft). No
prerelease is created. The rollback improvement comes later with its own proof (decision 5).

**P4 is the closest a bench can get to "OTA to the latest release" from today's image.**
- **Why not from v18 itself:** v18 is the latest release, so a unit on v18 has nothing to update
  to.
- **Why the v17 update counts for v18:** P4 runs the update from v17. P4′ is what lets it stand
  for v18's own code: the download path is the same source in both.
- **What else covers v18:** P3 covers v18's own reach to the server.
- **The v18 to v19 hop** is first exercised when v19 is released.

### P4′: read while writing this row (2026-11-05), and to be re-run on the bench day

This check was run while the row was written, so it is a record, not a blind prediction. The
script is committed so it can be re-run against the tags as they are on the bench day.

```
OTA download path, v17 (523cd736526c) -> v18 (672eab75eb6c)
  SAME     MaybeUpdateFirmware()  4b884eef0bc1  (121 lines)
  SAME     FirmwareUrl()  36b6e92c81a1  (4 lines)
  SAME     WaitForClock()  c8b2af9d1da6  (10 lines)
  SAME     NoteOtaAttempt()  ddc1ca30f6c5  (21 lines)
  SAME     NoteOtaFailed()  a94d858da019  (9 lines)
  SAME     drawProgress()  f193ef865397  (20 lines)
  SAME     drawStatus()  1cab93413e75  (7 lines)
  SAME     LogOtaSlot()  6afd84c7fc6d  (23 lines)
  SAME     include/OtaCerts.h  93343da442d7
  SAME     src/HttpRequestManager.h  299afd9e31ca
  SAME     src/HttpRequestManager.cpp  b2964de0a16a
  SAME     the platform pin: https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
  (whole-file, for the record:)
     src/OtaUpdater.cpp | 15 ++++++++-------
     src/OtaUpdater.h   | 16 +++++++++++++---
     2 files changed, 21 insertions(+), 10 deletions(-)
VERDICT: the download path is the same source
```

**What the whole-file diff contains,** for the record and outside the verdict:
- `FW_VERSION` 17 to 18.
- The quiet-hour reboot cap (#377): `DeferUpdateCheckToReboot` and `DeferRebootWithCause` gained
  a `scheduled` argument, and their two callers in `src/main.cpp` pass it. That decides *when* a
  deferred check reboots, not how an update is fetched or applied.

**The check can fail, which makes its result mean something:**

| run | predicted | got |
|---|---|---|
| `v15 v16` (the control) | exit 1, naming `src/HttpRequestManager.cpp` | exit 1, naming `src/HttpRequestManager.cpp` **and** `NoteOtaAttempt()`. I did not name the second one: same verdict, one more item than predicted |
| `nosuchref v18` | exit 2, "cannot see" | exit 2 |

### The bench proof, run 2026-11-05 20:35-20:49Z with Daniel at the bench: all as predicted

**The unit:** the Missileer photo unit (serial …E794) on COM16, its port resolved by serial
before every step. Its registry entry was moved to #545 at 20:34Z, at Daniel's go. The serial
traces are kept outside the repo and are not committed.

| step | got | verdict |
|---|---|---|
| **P0** | ESP32-S3 (QFN56) v0.2, embedded PSRAM 8 MB, flash `ef`/`4018` at 16 MB (read earlier, read-only) | as predicted |
| **P1** | v18's set (sha `1e1670abc917`) with `--erase-all`: exit 0; **`Hash of data verified.` exactly once**; erase **39.1 s**; whole command **60.1 s** | as predicted |
| **P2** | `[build] env=blipscope-s3-128 fw=v18`; `[ota-slot] boot running=app0 ... state=UNDEFINED` (F3 observed); the setup QR drawn **~5 s** after the reset. Daniel saw `SETUP` and the QR on the panel, which confirms the 1.28" panel. He did not report the colour or the name | as predicted |
| **P3** | `[WiFi] autoConnect() returned true`, then one expected first-setup restart, a rejoin, and **`[ota] channel=s3-128 current=18 latest=18`**: no download | as predicted |
| **P0′** | the MAC esptool prints, run through the mint's derivation, is the id on the unit's own config page: **true** (a boolean; no id, MAC or address printed) | as predicted |
| **P4′** | `ota-download-path-diff.sh v17 v18`: every item SAME, exit 0 | as predicted |
| **P4** | v17's set (sha `976ccde6f219`) the same way: exit 0, hash once, erase 37.7 s, 58.6 s. Then Wi-Fi by phone; **`current=17 latest=18`**, `newer firmware available -- updating`, `downloading .../releases/latest/download/firmware-s3-128.bin`, progress 25/50/75/100 in ~47 s; reboot into **`fw=v18`**, `running=app1 @0x660000`; its next check read `current=18 latest=18` | as predicted |

**Also seen, outside the predictions:** after the update, the slot line printed in `setup()` read
`state=VALID`. The new image was marked valid **before `setup()` ran**: F2 observed on hardware,
which is why the rollback improvement belongs to a later image (decision 5).

**Decided at the bench (Daniel, ~20:45Z): the factory image stays v18, option (a).** He asked
whether the factory image should be a neutral "starter" rather than Blipscope. It should not:
all 600 sell as Blipscope at first, and the chooser comes later in a Blipscope update. The
alternatives considered were a new neutral starter image (b) and v18 with neutral naming (c).
Both would have needed a new, unfielded image as the only update path for 600 units, a week
before the factory date.

**Still marked `(bench)` on the sheet, not yet run:** the Flash Download Tool steps (the tool is
not installed on this bench) and the power-button note. This unit was already powered when it
was flashed, so it shows nothing about a cold board.

**After the run:** the unit is back in its app, on v18 in `app1`. The traces were closed by
process id, and none is left running. Closing them left the board in ROM download mode, and an
esptool `read-mac` reset it.

### The Flash Download Tool steps, proven by hand (Daniel's go, 2026-11-05 ~20:55Z): predictions first

**The tool.** `flash_download_tool.zip` from `dl.espressif.com/public/`, linked from Espressif's
ESP32-S3 user guide.
- 25,943,693 bytes. Its MD5 `72713e571a3115c91e89ee0320f6e186` equals the server's ETag, and its
  sha256 is `c1fbf280aa04…`.
- It unzipped safely (every entry checked to stay inside its own directory) to 9 entries.
- The one program is `flash_download_tool_3.9.11.exe`, the latest version in Espressif's release
  notes.
- **It is not code-signed.** Authenticode reports `NotSigned`. Windows may warn, and a factory's
  IT policy may block it, so the sheet says so.

**Daniel runs it by hand**, following the sheet's English steps exactly, with v18's factory file
at 0x0 on COM16.

| | check | predicted | outside the set |
|---|---|---|---|
| **T1** | what the tool shows | ERASE completes; START ends in **FINISH**. Whether its console shows `Hash of data verified.` is **reported, not predicted**, which is the open `(bench)` question | an error, or no FINISH: **stop** |
| **T2** | the panel after an unplug and replug | the setup screen with its QR | anything else: **stop** |
| **T3** | `esptool verify-flash 0x0 firmware-s3-128.factory.bin` (read-only) | the digest matches: the chip's first 2,059,264 bytes are v18's factory file | a mismatch: **stop**, the tool did not write what the sheet says |
| **T4** | the first 4 KiB of `app1` (0x660000), read-only, where P4 left v18 | all 0xFF: ERASE wiped the whole chip | any other byte: **stop**, the sheet's ERASE step does not erase the whole chip |

**Results (2026-11-05 ~21:00-21:10Z): T3 is off as frozen. Stopped; a ruling is needed.**

| | got | verdict |
|---|---|---|
| **T1** | Daniel followed the sheet in **Develop** mode. He had first picked Factory in the opening dialog and switched to Develop, as the sheet says. ERASE ended in FINISH, and START ended in FINISH. `DoNotChgBin` was already ticked and greyed out: 3.9.11 locks it on. The DetectedInfo box shows `STA: <12 hex, no colons>`, which is **the MAC esptool prints** (it ends E794, the board's serial). AP and BT are base+1 and base+2. **The console never prints `Hash of data verified.`** Its write ends `Compressed 2059264 bytes to 1178113...` and `is stub and send flash finish`, with a `No errors detected.` line before it | as predicted (FINISH). The open question is answered: **no** hash line |
| **T2** | after a replug: a splash reading **"Blipscope V18"**, a 3-second Wi-Fi-reset countdown, then the setup screen with its QR | as predicted |
| **T3** | `verify-flash 0x0` over the whole file: **digest mismatch**, exit 2 | **OFF** |
| **T4** | 4 KiB at `app1` (0x660000), where P4 had left v18: **0 non-0xFF bytes** | as predicted: ERASE wiped the chip |

**T3's cause, named by a read-only check region by region:**
- **Match:** the bootloader and partition table (0x0-0x9000), otadata (0x1E000-0x20000) and the
  app (0x20000 onward).
- **Differs:** the NVS span (0x9000-0x1E000) **only**.

The board booted at T2's replug, **before** T3 ran, and the firmware writes NVS as soon as it
runs. So a whole-file compare made after a boot fails, whatever the tool wrote. This is the false
failure `docs/provisioning-mint.md` records from 2026-09-25 ("never a post-boot compare of NVS").
**The alternative, that the tool wrote the wrong bytes, is excluded** for every region whose
content the factory file defines: all of them match. **The miss is in my check, not the tool:**
T3 should have compared the defined regions, or run before any boot.

**F11 was wrong, and is corrected here.** It said the firmware version is not on the screen. A
fresh unit shows a splash with **"Blipscope V18"** before the setup screen. I missed it when
reading the source.

**Proposed for a ruling. The sheet is not edited until then:**
1. **The Flash Download Tool's pass sign** becomes FINISH plus the screen. Its console has no
   hash line, so `Hash of data verified.` stays the esptool pass sign only.
2. **Both tools gain the screen sequence** as a pass sign: the splash reading "Blipscope V18",
   the 3-second countdown, then `SETUP` with the QR. The version on the splash names the image.
3. **The Download Tool steps** say `DoNotChgBin` is already ticked and locked in 3.9.11, and that
   the STA value appears as 12 characters without colons (the import accepts both forms).
4. **A note** that the tool is not code-signed.
5. **T3 re-graded as a region check:** A, C and D match. That is this run's evidence that the
   tool wrote exactly what the sheet says.

**Ruled ~21:20Z.**
- T1, T2 and T4 are accepted as predicted.
- **T3 stays recorded as OFF.** It is re-graded as the region check: the bootloader and partition
  table, otadata and the app all match. The miss is kept: it was in the check, not the tool.
- All five sheet edits are accepted, and F11 is corrected in place. The sheet is final apart from
  the factory's answer on which tool it uses.
- The import tool must accept a MAC list with or without colons, with a test (on #548).
- COM16 stays reserved until the sheet merges, then is released.

## Sheet edits of 2026-11-06 (Daniel's; ruled ~18:22Z): predictions, frozen before the edits

**Superseded by the ruling of ~18:34Z (below), with this section kept as it was frozen.** E3's line about boards written to before was dropped (~18:29Z, then ~18:34Z): production is START only for every board. The bench item E6 named, the write without the erase, is proven by B1 below.

The factory reads the sheet from main, so this is a docs-only PR that goes up today. Before the edits:

| | check | predicted |
|---|---|---|
| **E1** | the diff | exactly two files, `docs/factory-sheet-s3-128.md` and this one |
| **E2** | the sheet's file table | byte-identical: the same file name, version (v18), size (2,059,264 bytes), SHA-256 (`1e1670abc917...`), address (`0x0`) and source link |
| **E3** | section 2, in English and in Chinese alike | the full erase only for the **3 pre-production samples** (Flash Download Tool ERASE, then START; esptool with `--erase-all`). **Production boards (new chips, which arrive blank): no erase**, write the one file at `0x0` (START only; esptool without `--erase-all`). One line: a board that had anything written to it before (a test program, for example) gets the full erase first, like the samples |
| **E4** | the Flash Download Tool steps, in both languages | BAUD **1152000**; SPI SPEED **80MHz**, SPI MODE **QIO**, FLASH SIZE **16MB**; **DoNotChgBin** ticked; **"Do not burn any eFuses."** |
| **E5** | nothing else in the sheet | the pass signs, the serial warning and the MAC list unchanged |
| **E6** | this doc | decision 3 records Daniel's change of 2026-11-06. With DoNotChgBin ticked, the tool writes the file unchanged, so the board boots with the file's own header (DIO, 80 MHz, 16 MB), as on our bench |
| **E7** | before the push | `check_device_ids.py`, `check_wifi.py` and `skus.py --check-readers` pass (no workflow file is named) |
| **E8** | CI on the PR, read only with nothing queued, twice | **31: 28 success, 3 skipped** (`promote`, `version`, `ship-alert`), 0 failed. Baseline: #568's PR run, a docs-only PR that started after main's last job-set change, `84409651` (#552); main's last workflow change is `259e8688`, a step |

**Outside the set:** stop and report.

**The edits, as made (E6's record):**
- **Decision 3** above records Daniel's change of 2026-11-06.
- **DoNotChgBin and the board's flash settings.** With DoNotChgBin ticked, the Flash Download Tool writes the file
  unchanged, so the tool's SPI SPEED, SPI MODE and FLASH SIZE govern the download only. The board boots with the
  file's own header, as on our bench.
  - That header, read from the v18 factory file itself (sha256 `1e1670abc917`, 2,059,264 bytes): magic `0xE9`,
    **DIO**, **16 MB**, **80 MHz**.
- **One thing the sheet now says that our bench did not run.** The sheet's opening line says every step was run on
  our bench on 2026-11-05. Two of today's steps were not:
  - the production write without the erase: P1 ran with `--erase-all`;
  - the tool with BAUD 1152000 and these SPI settings.
  The line is unchanged here, by E5. It is reported for a ruling: a bench run of the no-erase write, or a wording
  change.

## The ruling of ~18:34Z: no erase for production, proven first on a dirty board (replaces ~18:22Z and ~18:29Z)

**The factory's answer (EC Buying, 2026-11-06):** Espressif's Flash Download Tool (version not stated); the one merged
file at `0x0`; no erase, since new chips arrive blank and the erase is slow; 1152000 baud.

**The bench proof, frozen before it runs.** On COM16, the `missileer-photo` board (`...E794`), used for the 11-05
proof, which this ruling names "if it is free". Its registry reads unreserved, and the registry is not touched. Never
the Bend feeder, E2:08, Weatherscope's board or the Remote's. esptool from PlatformIO's own penv, as in P1.

**Made dirty first:**
- **D1, another image in app1:** v17's app (its factory file from `0x20000`) written at `0x660000`. Otadata is not
  touched, so the board still boots app0.
- **D2, a made-up Wi-Fi network in NVS:** an NVS image built by ESP-IDF's `nvs_partition_gen.py`, namespace
  `nvs.net80211`, `sta.ssid` = `MadeUpNet-01` and a made-up password, written at `0x9000`. Never Daniel's real network.

| | check | predicted |
|---|---|---|
| **B0, the control** | read back before the write | app1's first 4 KiB equal v17's app's first 4 KiB, **and** the NVS read-back holds `MadeUpNet-01`. Either missing: **stop**, since the proof would mean nothing |
| **B1, the write** | `esptool ... write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0 firmware-s3-128.factory.bin`, with **no `--erase-all`** | exit 0; `Hash of data verified.` **exactly once**; the time recorded, predicted 10-40 s (P1's 60.1 s less its 39.1 s erase is about 21 s) |
| **B2, what boots** | serial, after a `read_mac` reboot | `[ota-slot] ... running=app0 @0x020000 ... fw=18` |
| **B3, NVS blank** | NVS read back after that boot | **no** `MadeUpNet-01` bytes: the made-up network is gone, so the board shows SETUP. By the screen too, if Daniel is there |
| **B4, app1 left alone** | app1's first 4 KiB, read only | **still v17's**: the file ends at `0x1F6C00`, below app1 at `0x660000` |

The Flash Download Tool's START without ERASE is the same write, so it is not repeated by hand. **Outside the set:**
stop and report. Only after B0-B4 do the sheet and this doc change.

### The bench proof, run (2026-11-06, 18:40-18:42Z, COM16): all as predicted

| | read | verdict |
|---|---|---|
| **D1, D2** | v17's app written at `0x660000` (exit 0, 20.2 s); the made-up network's NVS written at `0x9000` (exit 0) | done |
| **B0** | app1's first 4 KiB are v17's: **True**; NVS holds `MadeUpNet-01`: **True** | **as predicted**: the board was dirty |
| **B1** | no `--erase-all`: exit 0; `Hash of data verified.` **exactly once**; **20.8 s**. esptool printed "Flash will be erased from 0x00000000 to 0x001f6fff": it erases only the range it writes, never the whole chip | **as predicted** |
| **B2** | `[build] env=blipscope-s3-128 fw=v18`; `[ota-slot] boot running=app0 @0x020000 state=UNDEFINED next=app1 fw=18`; and `[WiFi] no saved credentials: portal stays up indefinitely for first setup` | **as predicted**. Serial shows the SETUP state outright |
| **B3** | NVS after that boot holds `MadeUpNet-01`: **False**. The 3,058 bytes that are not 0xFF are the first boot's own writes | **as predicted**: the made-up network is gone |
| **B4** | app1's first 4 KiB are still v17's: **True** | **as predicted**: the file ends below app1 |

The Flash Download Tool's START without ERASE is the same write, so it was not repeated by hand. The board was left
booted into its app (`read_mac`).


**The sheet, as edited after B0-B4 (the ~18:34Z list):**
- samples: ERASE, then START (esptool `--erase-all`); production, every other board: START only (esptool without);
- the tool's settings: BAUD 1152000 (no matter over USB); SPI SPEED 80MHz, SPI MODE QIO, FLASH SIZE 16MB;
  DoNotChgBin must stay ticked (unticked, the tool rewrites the header's flash settings); no eFuses;
- the time per board: production about 21 s (B1's 20.8 s), the samples about 1 min;
- the opening line now names the factory's tool and both bench days;
- the file, size, SHA-256 and version: unchanged.
