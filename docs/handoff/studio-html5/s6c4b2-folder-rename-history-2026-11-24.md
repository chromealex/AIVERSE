# S6c.4b.2 — folder, large-file and rename history (2026-11-24)

Branch `feature/studio-html5`, base `ea6d4fac`. Streaming identity/folder implementation `4db672b1c336d8fb430ddf6755fec4e7e88a1f1e`; rename/UI `3bb680b0f35926a1d4de18d2a20549e334ae1f42`.

- Reuse engine pack SHA-256 with a 64 KiB streaming buffer. Regular asset bytes are no longer retained; directory digest frames sorted relative paths, entry kinds, sizes and file bytes (including nested .meta and empty directories). Refuse unsupported/symlink entries and cap tree at 20k entries. Exact root sidecar and material snapshots still consume existing 16 MiB history payload budget. Replay validates directory kind, root GUID and entire tree digest before/after using existing journal. Shared pack link added to editor core.
- Native Rename is the same transfer preview and decisions, single validated filename, same extension/importer for files, no direct overwrite. Web Assets context menu exposes Rename and review form; reports non-retained transfers. History UI describes file/folder/rename scope.
- Current map, merged rules, manifest-listed maps and saved JSON maps in maps/ now checked for exact path strings/descendant references before move/replay; refuse unsupported reference instead of modifying it. Bound saved-map checks to 512 files / 64 MiB. Materials remain repaired by the existing owner and exact recorded material document replay.
- Added test source for 17 MiB payload, nested folder/empty directory, digest budget, changed tree conflict, GUID round trip, folder rename and extension refusal, saved map descendant guard.

Limits: hashing is synchronous and reads file/tree payload several times; performance and external-writer races unverified. Not a complete dependency resolver for scripts, dynamic/relative/encoded paths or binary documents. Saved-map validation is conservative and may refuse if a manifest map is missing or budget exceeded. Case-only rename on case-insensitive filesystems not supported. Existing POSIX move journal limits remain. Import/replace/delete remain history barriers. No full native build, tests, UI typecheck, screenshots or Mac/KINGSTON gate run; no main merge.

Next S6c.4c: recoverable storage and typed plans for import/create/replace/delete (including sidecars and references), then S6c.5 durability and S6d agent integration.
