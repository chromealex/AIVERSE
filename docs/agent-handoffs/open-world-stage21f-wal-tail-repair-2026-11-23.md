# W1.21f — incomplete tail repair

Commit `4ed04e35`, PR #116 Draft. RegionWalWriter::open(path,previous,repairIncompleteTail=false) optionally truncates only an incomplete trailing frame to reader validBytes under exclusive flock, then fsyncs. CRC corruption, sequence gaps and other scan failures remain fail-closed. Apple append uses F_FULLFSYNC when defined and fails closed if unsupported by filesystem. Repair sync remains fsync. Windows backend and transaction replay not implemented. No native tests run. Next Windows backend and typed transaction payload/replay. No main merge.
