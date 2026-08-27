# ByteBraid 0.2 verification and index benchmark — 2026-08-27

## Environment

| Item | Observed value |
|---|---|
| Operating system | Windows 11 build 26100 |
| Compiler | GCC 16.2.0, portable w64devkit 2.9.1 |
| Python | 3.14.7 |
| C++ mode | C++20, `-O2 -Wall -Wextra -Wpedantic -Wconversion -Werror` |

## Functional verification

| Check | Actual result |
|---|---|
| Native tests | 4/4 groups passed |
| Hard-link accounting | 3 paths became 2 physical files; one 1,024-byte physical copy—not two path aliases—was reclaimable |
| Hard-link-only safety | One physical file with two paths produced one hard-link group, no exact-copy group, and 0 reclaimable bytes |
| Backend equivalence | Memory and disk modes produced identical exact groups, near pairs, reclaimable bytes, and posting-record totals |
| Demo | 6 paths, 5 eligible paths, 4 physical files, 1 hard-link group, 1 exact group, 2 near pairs, 524,288 reclaimable bytes |
| JSON | Schema 2 parsed and all summary assertions passed |
| Temporary cleanup | Zero `bytebraid-index-*` children remained after disk scans |
| Invalid CLI inputs | Unknown backend and non-power-of-two partition count both exited 2 with specific errors |
| Diagnostics | CLI and tests built successfully with warnings treated as errors |

The first public 0.2 matrix run passed GCC and exposed one Clang `-Wsign-conversion` diagnostic in the pre-existing JSON escaping loop. The code now converts each `char` explicitly to `unsigned char`; the warning remains enabled and treated as an error.

The small demo produced 234 posting records. Both backends reported 234 peak resident posting records because the corpus was too small to fill a partition buffer; disk mode is not claimed to reduce memory for small scans.

## Deterministic backend benchmark

Command:

```text
python tools/benchmark_index.py --binary ./bytebraid-improved.exe \
  --files 128 --file-bytes 262144 --repetitions 3
```

Corpus: 128 files × 256 KiB = 32 MiB, arranged as deterministic eight-file version families with seed `20260827`.

| Backend | Median CLI time | Posting records | Peak resident posting records | Exact groups | Near pairs |
|---|---:|---:|---:|---:|---:|
| Memory | 0.0743 s | 6,850 | 6,850 | 16 | 432 |
| Disk, 64 partitions | 0.2836 s | 6,850 | 1,222 | 16 | 432 |

Disk mode reduced the algorithm-reported peak resident posting count to 17.84% of memory mode (82.16% fewer records) while taking about 3.82× the median wall time in this small local run. Exact groups, near pairs, and 4,194,304 reclaimable bytes were identical.

## Interpretation limits

- Peak resident posting records are an internal record count, not bytes, process RSS, or total program memory.
- Per-file metadata, unique chunk sets, and candidate-pair counts are excluded from that metric and remain in memory.
- The benchmark is synthetic, small, cache-sensitive, and run on one machine. The first memory sample was a 1.17 s cold-start outlier; the median is reported, not discarded raw data.
- This result establishes a real posting-memory/time tradeoff, not large-scale production capacity.
