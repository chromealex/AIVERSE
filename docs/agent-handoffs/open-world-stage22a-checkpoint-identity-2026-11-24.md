# W1.22a — checkpoint identity envelope

RegionWalCheckpointIdentity.h defines a portable 68-byte versioned identity containing world/branch UUID bytes, caller-supplied 32-byte checkpoint digest, sequence and ledger revision. Rejects zero identity/digest and malformed size/version. This is a codec only: digest computation, durable manifest publication, binding to WAL file and ActorHost finality integration remain outstanding. No native tests run. Next implement checkpoint manifest publication and enforce identity at session open. PR #116 Draft; no main merge.
