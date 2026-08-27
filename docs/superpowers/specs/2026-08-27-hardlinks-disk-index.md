# ByteBraid 0.2 Improvement Specification

Date: 2026-08-27
Status: Approved by the user's explicit request to implement the recorded next improvements.

## Goals

1. Detect multiple scanned paths that name the same physical file.
2. Prevent hard-linked aliases from inflating conservative reclaimable-byte estimates.
3. Add a disk-backed alternative to the in-memory inverted chunk-posting index.
4. Preserve identical exact/near-match results across index backends.
5. Measure backend behavior on a deterministic synthetic tree without claiming process-memory numbers that were not measured.

## Hard-link design

- Read a stable physical identity for regular files: volume serial plus file index on Windows, or device plus inode on POSIX.
- Record the filesystem-reported link count when available.
- Reuse an already computed fingerprint for another path with the same physical identity and size.
- Emit `hard_link_groups` separately in human and JSON reports.
- Keep all logical paths visible in an exact-content group, but count distinct physical identities when computing `reclaimable_exact_bytes`.
- Do not emit an exact-copy group when every matching path refers to one physical file; the hard-link group is the correct evidence in that case.
- Fall back conservatively to treating a path as a distinct physical file if identity lookup fails.

## Disk index design

- Add `--index-backend memory|disk`, defaulting to `memory` for compatibility.
- Add `--index-partitions N`, requiring a power of two between 2 and 4096; default 64.
- Add `--temp-dir PATH` to choose the parent for the temporary index.
- In disk mode, write fixed-width `(chunk fingerprint, file index)` records into hash partitions.
- Close writers, then load, sort, and reduce one partition at a time into shared-file-pair counts.
- Remove the run-specific temporary directory through RAII on success or failure.
- Report total posting records, used partitions, and peak resident posting records across write buffers or one loaded partition.

## Compatibility and schema

- Bump the CLI version to 0.2.0 and JSON schema to 2.
- Preserve existing option defaults and exact/near-match semantics for trees without hard links.
- Add fields rather than renaming existing summary fields.
- Keep the tool read-only with respect to the scanned tree. Disk mode writes only beneath a separate temporary run directory and removes it afterward.

## Verification

- Existing exact/near fixture remains unchanged under the memory backend.
- Memory and disk backends must produce equivalent match evidence.
- A hard-link fixture must prove that two aliases plus one physical copy produce one hard-link group, two physical files, and only one file-size unit of reclaimable storage.
- A hard-link-only fixture must not be reported as reclaimable exact copies.
- Invalid backend/partition settings must fail.
- Strict warning-enabled builds, native tests, deterministic demo assertions, and JSON parsing must pass.
- A documented synthetic benchmark will compare elapsed time and peak resident posting records, explicitly distinguishing record count from process RSS or bytes.

## Non-goals

- Deleting or relinking files.
- Persistent/reusable indexes across scans.
- Bounding candidate-pair memory in pathological all-files-share-all-chunks corpora.
- Snapshotting actively changing files.
- Sparse extents, compression, deduplication, or physical block-sharing analysis.
