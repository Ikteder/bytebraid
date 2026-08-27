# Model note

ByteBraid contains no machine-learning model. Near-duplicate discovery is a deterministic algorithm: physical-file identity, bounded content-defined chunking, 64-bit chunk fingerprints, an in-memory or partitioned disk index, and Jaccard set similarity.

The similarity score should be interpreted only as shared chunk evidence. It is not a probability that two files are duplicates and it is not safe grounds for automated deletion.
