# ByteBraid

[![CI](https://github.com/Ikteder/bytebraid/actions/workflows/ci.yml/badge.svg)](https://github.com/Ikteder/bytebraid/actions/workflows/ci.yml)

ByteBraid is a read-only C++20 command-line tool that finds exact duplicate files, hard-linked aliases, and likely version families. Exact matches are size-prefiltered, hashed, and then confirmed byte for byte. Near matches use deterministic content-defined chunking and report the Jaccard overlap of unique chunk fingerprints.

It is designed for people auditing backup folders, exports, research artifacts, or media collections before making their own cleanup decisions. ByteBraid never deletes, moves, or modifies scanned files.

## Why another duplicate finder?

Exact hashes miss a common storage problem: `report-final`, `report-final-2`, and `report-final-revised` may share most of their bytes without being identical. ByteBraid surfaces both cases and keeps the evidence visible:

- exact groups include every byte-confirmed path and conservative reclaimable bytes;
- hard-link groups distinguish multiple names from actual duplicate storage and prevent overcounting;
- near pairs include the similarity percentage and shared/union chunk counts;
- a partitioned disk index trades speed and temporary I/O for fewer resident posting records on larger scans;
- JSON output makes results scriptable without turning the tool into a cleanup engine;
- symlinks are not followed, permission failures become warnings, and small files can be filtered.

## Build

Requirements: a C++20 compiler. CMake 3.20+ is optional.

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Or build directly with GCC/Clang:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Iinclude \
  src/main.cpp src/analyzer.cpp -o bytebraid
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Iinclude \
  tests/test_analyzer.cpp src/analyzer.cpp -o bytebraid_tests
./bytebraid_tests
```

## Try the deterministic demo

```bash
python tools/make_demo.py
./bytebraid scan demo-corpus --min-size 1024 --similarity 0.70 --json demo-report.json
```

The generated corpus contains two physical exact 512 KiB copies, a hard-linked alias of the original, one 8 KiB revision, one unrelated file, and a small note. The expected result is one hard-link group, one exact group with two physical copies, two high-overlap near pairs, and 512 KiB of conservative reclaimable storage.

## Usage

```text
bytebraid scan <directory> [options]

--min-size <bytes>       Ignore smaller files (default: 1)
--similarity <0..1>      Near-match Jaccard threshold (default: 0.65)
--chunk-min <bytes>      Minimum content chunk (default: 2048)
--chunk-average <bytes>  Power-of-two target chunk (default: 8192)
--chunk-max <bytes>      Maximum content chunk (default: 32768)
--index-backend <type>   Posting index: memory or disk (default: memory)
--index-partitions <N>   Disk hash partitions, power of two (default: 64)
--temp-dir <path>        Parent directory for temporary disk indexes
--json <path|->          Write JSON; '-' prints JSON only
```

The average chunk size and index partition count must be powers of two. Chunk sizes must satisfy `0 < min <= average <= max`; partition count must be between 2 and 4096. Larger chunks reduce memory and candidate volume; smaller chunks detect finer-grained reuse but raise overhead and the chance of unhelpful matches.

Disk mode creates a uniquely named child beneath the selected temporary parent, removes it automatically on success or failure, and never writes inside scanned files. It still keeps per-file chunk sets and candidate-pair counts in memory.

## Measured index tradeoff

The reproducible benchmark helper creates a temporary synthetic version-family tree and checks that both backends return identical evidence:

```bash
python tools/benchmark_index.py --binary ./build/bytebraid \
  --files 128 --file-bytes 262144 --repetitions 3
```

On the documented Windows run, 64-way disk partitioning reduced peak resident posting records from 6,850 to 1,222 (17.84% of memory mode) while median CLI time increased from 0.0743 s to 0.2836 s. This internal record count is not process RSS or total memory; see the [benchmark record](docs/experiments/improvement-verification-2026-08-27.md) before interpreting it.

## How it works

1. Walk regular files without following symlinks; record traversal/read failures as warnings.
2. Read volume/file identity on Windows or device/inode identity on POSIX, and reuse fingerprints for aliases of the same physical file.
3. Fingerprint eligible physical files in a single streaming pass with a 64 KiB buffer.
4. Group exact candidates by size and 64-bit FNV-1a, then compare candidate bytes before calling them exact.
5. Count distinct physical identities—not path names—when estimating reclaimable bytes.
6. Cut content-defined chunks with a deterministic Gear-style rolling hash, bounded by minimum and maximum sizes.
7. Build an in-memory posting index or spill postings into hash-partitioned temporary files.
8. Rank candidates by Jaccard similarity over unique chunk fingerprints.

The whole-file and chunk hashes are candidate filters, not cryptographic proof. Exact status always receives byte-for-byte confirmation. Near matches are leads for human review, not deletion recommendations.

## Current status and limitations

Version 0.2.0 is a tested native CLI with deterministic text and JSON schema 2 reports. It intentionally has no deletion feature.

- Near-match scoring uses unique chunks, not byte-weighted overlap; repeated content is collapsed.
- Disk mode reduces resident posting records, but file metadata, per-file chunk sets, and candidate-pair counts remain in memory.
- A pathological corpus where most files share most chunks can still create a very large candidate-pair map.
- FNV-1a and 64-bit chunk fingerprints are fast non-cryptographic filters; adversarial collision resistance is not a goal.
- Files changing during a scan are not snapshotted, so results for actively written trees can be inconsistent.
- Physical identity lookup can fail on unsupported or restricted filesystems. Those paths remain visible but add no speculative reclaimable copies; sparse allocation, filesystem compression, reflinks, and physical block sharing are not reported.
- Local verification covers Windows GCC; [GitHub Actions run 33111325555](https://github.com/Ikteder/bytebraid/actions/runs/33111325555) passed the 0.2 matrix on Ubuntu GCC and Clang, including both index backends and JSON assertions.

The best next improvement is a spillable candidate-pair accumulator with measured process RSS on a much larger synthetic tree. Disk-backed postings cannot bound memory when a highly repetitive corpus produces a very large shared-pair map.

See the [0.2 verification and benchmark record](docs/experiments/improvement-verification-2026-08-27.md), [disk-index decision](docs/decisions/0002-physical-identity-and-partitioned-index.md), original [verification record](docs/experiments/verification-2026-08-27.md), and [working notes](docs/notes/2026-08-27.md).

## License

MIT
