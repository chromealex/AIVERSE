# W1.21e — transaction codec and replay

RegionWalTransaction.h (`483fd37b`) encodes version/base revision/count and 29-byte mutation records, verifies contiguous revisions and exact payload length, replays through exclusive undo transaction and rejects invalid transitions/tombstone resurrection. Frame-level atomicity, not whole-file atomicity. Ledger capture bounded to frame budget; temporary since vector may still allocate, and exceptions require abandoning transaction. No native tests run. Next durable commit wrapper, host integration and checkpoint recovery. PR #116 Draft; no main merge.
