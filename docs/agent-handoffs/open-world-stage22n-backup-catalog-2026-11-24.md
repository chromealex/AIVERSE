# W1.22n — backup catalog

RegionBackupCatalog.h enumerates canonical regular WAL generation files, newest first, flags active generation, reads bound manifest or optionally performs full SHA256/archive/WAL validation with per-checkpoint byte budget. Explicit validated/metadataValid flags distinguish metadata listing from recovery success. PersistentWorldSession::backups exposes under held lock. Checkpoint-only orphans not listed; full validation can be expensive. No native tests run. Next update skill/catalog, then universal ECS persistence. PR #116 Draft; no main merge.
