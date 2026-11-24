# W1.22e — native archive adapter

RegionCheckpointArchive.h reuses engine/core/Archive.h and engine/pack/Sha256.h, serializes ledger copy, rejects trailing archive bytes, computes digest and byte count at publication, verifies SHA256 before decode/replay. Requires existing pack SHA256 implementation linked into consuming target. Checkpoint covers ledger only; caller owns world identity, sequence and save barrier. No native compilation/tests run. Next Windows publisher and session lock, then full ECS persistence. PR #116 Draft; no main merge.
