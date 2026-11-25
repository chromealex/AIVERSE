# W1.20f — seat cache reuse

Commit `ecc58080d257a867e49fb737cb961f9326eb662d`, PR #116 Draft. ActorRegionOwnership.cpp defers group coverage audit until old/current membership expansion completes and avoids replacing RegionSeatIndex when membership is unchanged. Full canonical traversal and transient map construction still occur; this is not incremental traversal. No native build/tests run. Next implement verified candidate-only path or explicitly retain full scan if writer contract cannot be established; update skill/catalog. No main merge.
