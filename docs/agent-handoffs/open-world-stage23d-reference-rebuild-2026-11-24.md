# W1.23d — stable reference rebuild

PersistentEntityReferences.h adds atomic rebuild from RegionStableIdentity and live ledger records, rejects duplicates and invalid tags, permits unmaterialized live records. classify distinguishes null/materialized/unmaterialized/missing/retired/stale and checks current tag and handle. Rebuild must be invoked by host at quiescent restore/materialization barriers; not automatically hooked. Deferred fixup queue and durable component codecs remain next. No native tests run. PR #116 Draft; no main merge.
