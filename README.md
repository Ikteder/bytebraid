# ByteBraid

[![CI](https://github.com/Ikteder/bytebraid/actions/workflows/ci.yml/badge.svg)](https://github.com/Ikteder/bytebraid/actions/workflows/ci.yml)

ByteBraid is a read-only C++20 command-line tool that finds both exact duplicate files and likely version families. Exact matches are size-prefiltered, hashed, and then confirmed byte for byte. Near matches use deterministic content-defined chunking and report the Jaccard overlap of unique chunk fingerprints.

It is designed for people auditing backup folders, exports, research artifacts, or media collections before making their own cleanup decisions. ByteBraid never deletes, moves, or modifies scanned files.

## Why another duplicate finder?

Exact hashes miss a common storage problem: `report-final`, `report-final-2`, and `report-final-revised` may share most of their bytes without being identical. ByteBraid surfaces both cases and keeps the evidence visible:

- exact groups include every byte-confirmed path and conservative reclaimable bytes;
- near pairs include the similarity percentage and shared/union chunk counts;
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

The generated corpus contains two exact 512 KiB copies, one 8 KiB revision, one unrelated file, and a small note. With default 8 KiB average chunks and the shown filter, the expected result is one exact group, two high-overlap near pairs, and 512 KiB of conservative exact-copy storage.

## Usage

```text
bytebraid scan <directory> [options]

--min-size <bytes>       Ignore smaller files (default: 1)
--similarity <0..1>      Near-match Jaccard threshold (default: 0.65)
--chunk-min <bytes>      Minimum content chunk (default: 2048)
--chunk-average <bytes>  Power-of-two target chunk (default: 8192)
--chunk-max <bytes>      Maximum content chunk (default: 32768)
--json <path|->          Write JSON; '-' prints JSON only
```

The average chunk size must be a power of two and chunk sizes must satisfy `0 < min <= average <= max`. Larger chunks reduce memory and candidate volume; smaller chunks detect finer-grained reuse but raise overhead and the chance of unhelpful matches.

## How it works

1. Walk regular files without following symlinks; record traversal/read failures as warnings.
2. Fingerprint eligible files in a single streaming pass with a 64 KiB buffer.
3. Group exact candidates by size and 64-bit FNV-1a, then compare candidate bytes before calling them exact.
4. Cut content-defined chunks with a deterministic Gear-style rolling hash, bounded by minimum and maximum sizes.
5. Build an inverted chunk index so only files sharing a fingerprint become near-match candidates.
6. Rank candidates by Jaccard similarity over unique chunk fingerprints.

The whole-file and chunk hashes are candidate filters, not cryptographic proof. Exact status always receives byte-for-byte confirmation. Near matches are leads for human review, not deletion recommendations.

## Current status and limitations

Version 0.1.0 is a tested native CLI with deterministic text and JSON reports. It intentionally has no deletion feature.

- Near-match scoring uses unique chunks, not byte-weighted overlap; repeated content is collapsed.
- The in-memory file metadata, chunk sets, and posting lists can be large on multi-million-file trees.
- FNV-1a and 64-bit chunk fingerprints are fast non-cryptographic filters; adversarial collision resistance is not a goal.
- Files changing during a scan are not snapshotted, so results for actively written trees can be inconsistent.
- Sparse-file allocation, hard-link identity, filesystem compression, and physical block sharing are not reported yet.
- Local verification covers Windows GCC; [GitHub Actions run 33102229975](https://github.com/Ikteder/bytebraid/actions/runs/33102229975) passed on Ubuntu GCC and Clang.

See [the verification record](docs/experiments/verification-2026-08-27.md), [design decision](docs/decisions/0001-read-only-confirmed-matches.md), and [working notes](docs/notes/2026-08-27.md).

## License

MIT
