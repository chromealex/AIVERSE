# W1.23t — unified loader

UnifiedCheckpointLoader.h validates active manifest, expected UUID, byte budget, exact unified WAL prefix, checkpoint length and SHA256, then restores all sections into private state and checks sequence/revision. Output unchanged on failure. Caller holds lifetime lock. Does not replay WAL itself; next integrate into UnifiedWalSession without reacquiring lock, then rotation. No native tests. PR #116 Draft.
