"""Put boot_app0.bin at the partition table's REAL otadata offset.

The platform appends boot_app0.bin to FLASH_EXTRA_IMAGES at a hardcoded 0xe000,
which is correct only for the stock tables where otadata happens to live there.
partitions-s3-16mb-bignvs.csv grows nvs to 84 KB (0x9000..0x1E000) and moves
otadata to 0x1E000 -- so the stock behaviour writes boot_app0 INTO THE MIDDLE OF
NVS on every `pio run -t upload`, corrupting two of its pages, while otadata
never gets initialised at all.

Caught 2026-07-31 on the first board flashed with the new table: an ordinary
bench upload silently trashed the device config it had just been given.

This reads the offset from whatever partition CSV the env actually uses, so it is
correct for the stock tables too (where it is a no-op) and stays correct if the
layout changes again.

MUST be registered as `post:`, not `pre:`. The platform appends boot_app0 to
FLASH_EXTRA_IMAGES inside its own builder, which runs after pre: scripts -- a
pre: hook sees an empty list and silently does nothing. post: still runs before
the build actions execute, so it fixes BOTH the upload and the merged
firmware.factory.bin (which otherwise embeds boot_app0 inside nvs too).
"""
Import("env")  # noqa: F821  (injected by SCons)

import os


def otadata_offset(csv_path):
    """Offset of the `otadata` row, or None if the table has no OTA data."""
    if not csv_path or not os.path.isfile(csv_path):
        return None
    with open(csv_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [c.strip() for c in line.split(",")]
            # Name, Type, SubType, Offset, Size[, Flags]
            if len(fields) >= 4 and fields[0] == "otadata":
                return fields[3]
    return None


csv_path = env.subst("$PARTITIONS_TABLE_CSV")  # noqa: F821
want = otadata_offset(csv_path)

def is_boot_app0(value):
    return "boot_app0" in str(value).replace("\\", "/")


if want:
    # (a) FLASH_EXTRA_IMAGES -- consumed lazily by the merged-factory-image action.
    patched, changed = [], False
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):  # noqa: F821
        if is_boot_app0(image) and str(offset).lower() != want.lower():
            print(f"boot_app0: {offset} -> {want} (otadata per {os.path.basename(csv_path)})")
            offset, changed = want, True
        patched.append((offset, image))
    if changed:
        env.Replace(FLASH_EXTRA_IMAGES=patched)  # noqa: F821

    # (b) UPLOADERFLAGS -- and this one is NOT optional. The platform flattens the
    # image list into the esptool argv (`env.Append(UPLOADERFLAGS=[image[0],
    # image[1]])`) while its own builder runs, i.e. BEFORE this script, so fixing
    # (a) alone leaves `pio run -t upload` still writing boot_app0 at the stale
    # offset. Observed exactly that: the build log said 0x1E000 and the upload log
    # said 0xe000 on the same run. Offsets sit immediately before their filename.
    flags = list(env.get("UPLOADERFLAGS", []))  # noqa: F821
    fixed = False
    for i, value in enumerate(flags):
        if is_boot_app0(value) and i > 0 and str(flags[i - 1]).lower() != want.lower():
            print(f"boot_app0 (upload): {flags[i - 1]} -> {want}")
            flags[i - 1], fixed = want, True
    if fixed:
        env.Replace(UPLOADERFLAGS=flags)  # noqa: F821
