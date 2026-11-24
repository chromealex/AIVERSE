# Stage 94 — reviewed relocation publication

Parent `06157fa9b563e108fdce2c82c1b074dd6fd18e90`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

Adds fx_replay_preset_relocate_apply with explicit source/new name and opaque planRevision from preview. Native Workspace recomputes GUID locations, dependency fingerprints and closure, rejects stale/conflicting plans and publishes only a new immutable preset through the shared save helper. UI requires a ready preview and a distinct new name. The original graph, preset and resources are unchanged. Revision is a stale-plan checksum, not an authentication token or cross-process write lock. Tests authored for stale revision, successful publication, duplicate name, preserved source and conflict after preview. No build/test executed. Cross-project portability and manual Asset Picker conflict resolution remain follow-up. Main untouched.
