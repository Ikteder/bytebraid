#!/usr/bin/env python3
"""Create a deterministic, license-free ByteBraid demonstration corpus."""

from pathlib import Path
import random
import shutil


def main() -> None:
    root = Path("demo-corpus")
    if root.exists():
        shutil.rmtree(root)
    root.mkdir()

    base = random.Random(20260827).randbytes(512 * 1024)
    (root / "archive-original.bin").write_bytes(base)
    (root / "archive-backup.bin").write_bytes(base)

    revised = bytearray(base)
    revised[256 * 1024 : 264 * 1024] = random.Random(17).randbytes(8 * 1024)
    (root / "archive-revised.bin").write_bytes(revised)
    (root / "unrelated.bin").write_bytes(random.Random(99).randbytes(512 * 1024))
    (root / "readme.txt").write_text("Synthetic ByteBraid demo; no external dataset.\n", encoding="utf-8")
    print(f"created {len(list(root.iterdir()))} files in {root}")


if __name__ == "__main__":
    main()
