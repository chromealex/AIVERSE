# W1.23r — resource section

UnifiedResourceSection.h adds selected native resource capture and restore adapter using PersistentEcsRegistry, stable sorted schema IDs, versions, existing migration and staged validators. Registry must outlive adapter; resources must use durable-safe codecs without raw runtime handles. Explicit selection, not automatic policy forwarding. No resource WAL or native tests yet. Next unified checkpoint publisher and identity binding. PR #116 Draft; no main merge.
