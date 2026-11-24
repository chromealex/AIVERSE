# W1.21e — finalized host boundary

RegionWalFinality.h introduces host-owned non-snapshot session with recovered sequence and revision checks, finalized commit and fail-closed state. Caller must prove checkpoint identity, recovered ledger content and tick finality; matching counters alone are not proof. Do not append speculative rollback ticks. Not wired to ActorHost yet; inspect authoritative finality owner before integration. WAL currently covers ledger ownership only, not full ECS persistence. No native tests run. PR #116 Draft; no main merge.
