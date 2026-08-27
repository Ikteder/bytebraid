# Decision 0002: physical identity and a partitioned disk index

Date: 2026-08-27
Status: accepted

## Context

Counting paths as copies overstates storage when multiple directory entries are hard links to one physical file. The original inverted index also retained every chunk posting in memory, limiting larger audits.

## Decision

ByteBraid will identify physical files with volume serial plus file index on Windows and device plus inode on POSIX. It will reuse fingerprints for aliases, report hard-link groups separately, and count distinct physical identities for reclaimable-byte estimates.

An optional disk backend will hash-partition fixed-width chunk postings into a uniquely named temporary directory. It will reduce one partition at a time and remove the temporary child through RAII. The memory backend remains the default for speed and compatibility.

The report will expose posting-record counts rather than estimating bytes or process RSS. Peak resident posting records include write buffers and the largest loaded partition.

## Consequences

- Hard-linked aliases no longer inflate reclaimable-byte totals or duplicate near-match candidates.
- Disk mode uses temporary storage and is expected to be slower.
- Partitioning bounds the posting-list portion of memory relative to partition count, but not file metadata, chunk sets, or candidate-pair counts.
- Identity-unavailable paths remain visible in physical totals, but they do not add speculative reclaimable copies. Exact groups mark identity completeness explicitly.
