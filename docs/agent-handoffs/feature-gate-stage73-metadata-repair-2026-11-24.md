# Stage 73 — missing sidecar recovery primitive

Parent 9620b618, PR #183. Persist original generated sidecar JSON alongside SHA-256/GUID. Internal restoreMissingMetadata checks committed material equality, fixed asset path/channel, original GUID/schema/importer, byte integrity and disk-sidecar uniqueness; exclusive hard-link creation never replaces existing metadata. Caller must ensure exclusive project access and invoke before rebuilding catalog. Not exposed as RPC yet; project-wide writer exclusion, quarantine, rollback and tests pending. No power-loss durability claim. Main untouched.
