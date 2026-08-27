# Decision 0001: stay read-only and confirm exact matches

Date: 2026-08-27
Status: accepted

## Context

A storage-analysis tool can cause irreversible loss if it promotes heuristic similarity into automated deletion. A fast non-cryptographic hash can also collide, even if accidental collisions are unlikely.

## Decision

ByteBraid will not delete, move, or hard-link files. Whole-file size and FNV-1a narrow exact candidates, but every reported exact group is partitioned with byte-for-byte comparison. Content-defined chunk similarity is labeled as a near-match lead and never contributes to the reclaimable-byte total.

## Consequences

Exact analysis may read duplicate candidates twice and the user must perform cleanup separately. In exchange, the tool has a narrow safety boundary, conservative storage figures, and no collision-based deletion claim.
