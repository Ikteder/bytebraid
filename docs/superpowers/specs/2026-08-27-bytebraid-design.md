# ByteBraid design specification

Status: approved for implementation on 2026-08-27.

## Goal

Build a portable, read-only C++20 CLI that helps a user discover exact duplicate files and high-overlap file versions while exposing enough evidence for manual review.

## Required behavior

- Recursively scan regular files under one directory without following symlinks.
- Allow a minimum file size filter.
- Confirm exact duplicates byte for byte after size and hash candidate filtering.
- Find near matches with bounded content-defined chunks and Jaccard overlap.
- Print deterministic human-readable results and optionally write versioned JSON.
- Continue through ordinary permission/read failures and report them as warnings.
- Never mutate scanned content.
- Include deterministic tests for exact, near, unrelated, filtered, empty, and invalid-input cases.

## Acceptance criteria

- Warning-clean C++20 build under `-Wall -Wextra -Wpedantic -Wconversion`.
- All native tests pass.
- A generated demo yields one exact group, two near pairs, and the expected conservative reclaimable bytes.
- JSON parses and reports the same summary as the terminal output.
- README documents algorithms, safety boundary, build/use instructions, and limitations.
