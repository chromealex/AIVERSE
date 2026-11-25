# W1.23g — typed durable adapters

DurableComponentCodecs.h adds addTyped<T>(id,version) using existing ADL Archive io and addTypedMigration<T>(id,version,reader) with reference context. Reads decode before replacing component in staged World. Explicit opt-in: raw runtime Entity fields are forbidden by contract, not automatically detected. Missing component capture returns false. Caller discards staged World on any failure. No native tests run. Next atomic batch restore and removal records. PR #116 Draft; no main merge.
