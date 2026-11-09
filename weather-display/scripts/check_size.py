#!/usr/bin/env python3
"""Flash-size budget check for CI.

For each PlatformIO build directory given, compares firmware.bin with the
size of the first app partition read from the partitions.bin that the build
produced, so the budget follows the partition table automatically.

    python scripts/check_size.py .pio/build/crowpanel_7 .pio/build/feather_s3_reverse_tft

Exit status 1 when any image uses more than FAIL_PCT of its slot; a warning
(GitHub Actions annotation) above WARN_PCT. A Markdown table is appended to
$GITHUB_STEP_SUMMARY when that variable is set.
"""
import os
import struct
import sys

WARN_PCT = float(os.environ.get("SIZE_WARN_PCT", 90))
FAIL_PCT = float(os.environ.get("SIZE_FAIL_PCT", 98))

ENTRY = struct.Struct("<HBBII16sI")  # magic, type, subtype, offset, size, label, flags
MAGIC = 0x50AA
TYPE_APP = 0x00


def app_slot_size(partitions_bin):
    """Size in bytes of the first app partition in an ESP-IDF partition table."""
    with open(partitions_bin, "rb") as f:
        data = f.read()
    for pos in range(0, len(data) - ENTRY.size + 1, ENTRY.size):
        magic, ptype, _sub, _off, size, _label, _flags = ENTRY.unpack_from(data, pos)
        if magic != MAGIC:
            break
        if ptype == TYPE_APP:
            return size
    raise ValueError(f"no app partition in {partitions_bin}")


def main(dirs):
    rows, failed = [], False
    for d in dirs:
        env = os.path.basename(os.path.normpath(d))
        image = os.path.getsize(os.path.join(d, "firmware.bin"))
        slot = app_slot_size(os.path.join(d, "partitions.bin"))
        pct = 100.0 * image / slot
        status = "ok"
        if pct > FAIL_PCT:
            status, failed = "FAIL", True
            print(f"::error title=Flash budget::{env} uses {pct:.1f}% of its app slot (limit {FAIL_PCT:.0f}%)")
        elif pct > WARN_PCT:
            status = "warn"
            print(f"::warning title=Flash budget::{env} uses {pct:.1f}% of its app slot")
        rows.append((env, image, slot, pct, status))
        print(f"{env:28} {image:>9,} / {slot:>9,} bytes  {pct:5.1f}%  {status}")

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("### Firmware size\n\n| Env | Image | App slot | Used | |\n|---|---:|---:|---:|---|\n")
            for env, image, slot, pct, status in rows:
                f.write(f"| `{env}` | {image:,} | {slot:,} | {pct:.1f}% | {status} |\n")
            f.write(f"\nWarn above {WARN_PCT:.0f}%, fail above {FAIL_PCT:.0f}%.\n")
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1:]))
