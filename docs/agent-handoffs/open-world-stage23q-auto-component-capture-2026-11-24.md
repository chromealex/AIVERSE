# W1.23q

DurableComponentCodecs adds presence predicates (automatic for typed adapters, explicit for custom), deterministic captureAll by live stable ID and schema, budget checks and staged output. UnifiedComponentSection::adapter connects capture and paged restore to checkpoint framework; codecs must outlive adapter. Only materialized entities captured; cold storage not integrated. Full scan and memory staging remain. No native tests. Next resource adapter and publisher. PR #116 Draft.
