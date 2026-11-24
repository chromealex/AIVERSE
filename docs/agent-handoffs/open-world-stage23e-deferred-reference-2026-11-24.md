# W1.23e — deferred references

PersistentDeferredReference.h adds an Archive-compatible stable-ID value with lazy resolution against current ledger/index, no cached runtime handle, explicit bind validation and null reset. Unmaterialized targets retain ID; missing/retired/stale distinguished and output handle cleared. No automatic callback/fixup queue or materialization hook. Use explicit durable component codecs next; existing raw Entity fields are not automatically converted. No native tests run. PR #116 Draft; no main merge.
