# Dataset note

ByteBraid does not train or evaluate a statistical model and ships no third-party dataset.

The demo corpus is generated locally by `tools/make_demo.py` from seeded pseudorandom bytes. It is deterministic, synthetic, license-free, and deliberately contains two identical files, one 8 KiB revision, one unrelated file, and a small filtered note. Native tests create a smaller synthetic corpus in the operating system's temporary directory and remove it after the test process.

User-selected scan contents remain local. The program does not upload file names, bytes, hashes, or reports.
