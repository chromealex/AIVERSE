# W1.22l — backup recovery

RegionCheckpointLoad.h factors explicit generation loading; RegionCheckpointBackup.h reads selected generation's bound WAL manifest and validates generation, expected world, SHA256, archive and WAL replay into private ledger. Output unchanged on rejected recovery. Source files and active manifest untouched. Caller holds source world session lock. No backup enumeration or fork publication yet; next expose locked session backup API and create recovered world in separate directory with new UUID. Native tests not run. PR #116 Draft; no main merge.
