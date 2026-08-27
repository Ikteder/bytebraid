# Dataset note

ByteBraid does not train or evaluate a statistical model and ships no third-party dataset.

The demo corpus is generated locally by `tools/make_demo.py` from seeded pseudorandom bytes. It is deterministic, synthetic, license-free, and deliberately contains two physical identical files, a hard-linked alias, one 8 KiB revision, one unrelated file, and a small filtered note. Native tests create smaller synthetic corpora in the operating system's temporary directory and remove them after the test process.

`tools/benchmark_index.py` also generates a temporary deterministic family-based corpus. The documented 2026-08-27 run used 128 files of 256 KiB each (32 MiB total) with seed `20260827`. It is a systems benchmark fixture, not an evaluation dataset, and is deleted when the script exits.

User-selected scan contents remain local. The program does not upload file names, bytes, hashes, or reports.
