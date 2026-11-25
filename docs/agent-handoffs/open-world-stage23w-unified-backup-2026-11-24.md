# W1.23w

UnifiedCheckpointLoader::loadGeneration factors explicit manifest validation. UnifiedBackupRecovery recovers selected UWT1 generation, validates checkpoint digest and all sections, replays complete WAL into private state, rejects incomplete/corrupt tails, and forks into a new UUID via UnifiedWalSession::create. Caller owns source lifetime lock and quiescent barrier. Resource WAL not implemented; resources recovered from checkpoint only. No native tests. Next catalog, retention and session forwarding. PR #116 Draft; no main merge.
