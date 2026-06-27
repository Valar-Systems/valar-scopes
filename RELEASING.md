# Releasing Blipscope firmware

Blipscope ships several hardware SKUs from one codebase (see `include/variants/`). All SKUs
are versioned and released **together** from a single commit, and each device self-updates to
**its own** binary over OTA. This doc is the release checklist.

## How OTA works

- A device checks `releases/latest/download/version.txt` — a single integer, the latest
  firmware version, shared by all SKUs.
- If that integer is greater than the device's compiled `FW_VERSION`, the device downloads
  **`firmware-<slug>.bin`**, where `<slug>` is its own `variant::SLUG` (e.g. `c3-128`). A C3
  never downloads an S3 image, and vice-versa.
- The flow lives in [src/OtaUpdater.cpp](src/OtaUpdater.cpp); `FW_VERSION` is in
  [src/OtaUpdater.h](src/OtaUpdater.h).

## Cutting a release

1. **Bump the version:** edit `FW_VERSION` in `src/OtaUpdater.h` (one number, all SKUs).
2. Commit + merge to `main`.
3. **Create a GitHub Release** with a tag (e.g. `v5`). Publishing it triggers
   [.github/workflows/firmware.yml](.github/workflows/firmware.yml), which:
   - builds every SKU in the matrix,
   - attaches each as `firmware-<slug>.bin`,
   - attaches a `version.txt` containing `FW_VERSION`,
   - and re-publishes a legacy `firmware.bin` (= the C3 build) for devices that shipped
     before per-SKU naming.
4. Devices pick up the update on their next daily check (or reboot).

> Don't hand-upload assets — the workflow names them so they match what devices request.

## Adding a new SKU to releases

A SKU's slug must be identical in **three** places (keep them in sync):

1. `variant::SLUG` in its `include/variants/<sku>.h`
2. its `[env:*]` in `platformio.ini`
3. its `{ env, slug }` row in the `matrix.include` of `.github/workflows/firmware.yml`

Add all three, and the next release automatically builds and publishes that SKU's binary.

## Legacy migration note

Firmware that shipped before per-SKU naming fetches a plain `firmware.bin`. The workflow keeps
publishing `firmware.bin` as the C3 build so those devices can still update onto the first
variant-aware build; after that they use `firmware-c3-128.bin`. Once the fleet has moved on,
the legacy alias can be dropped from the workflow.
