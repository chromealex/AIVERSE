# W1.23c — stable references

PersistentEntityReferences.h adds bidirectional stable ID/runtime handle mapping with liveness and generation checks, null encoding, conflict rejection, explicit ioPersistentEntity Archive helper. Derived map must be rebuilt after restore; missing references fail closed. No automatic remapping of arbitrary component fields, no deferred cross-region references or disk integration yet. Next build map from RegionStableIdentity and add explicit durable component codecs. Native tests not run. PR #116 Draft; no main merge.
