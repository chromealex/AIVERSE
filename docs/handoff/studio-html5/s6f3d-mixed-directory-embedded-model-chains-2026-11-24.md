# S6f.3.4a — mixed directory and embedded model chains (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 652e19f3466bc03da466e9d4b392e87074a87cb0; roadmap fdacbd1abe48469790090b189821f7074be542b9; implementation bc76cbd6253e91d0452b09b22754906edb6cd95d; nested settings and version fixtures 8cae8832ec8dfb9b18d0ab434f57e6d445f21533.

## Implementation
- Mixed history planner supports non-overlapping directory roots, whole-tree directional digest checks and nested GUID/importer validation across live and recovery paths. Nested content sidecar settings follow physical directory identity through exact-endpoint chains.
- Mixed model version transitions support glTF/GLB with embedded resources only. Scan both current and recovery payloads; refuse unknown extensions, external URI dependencies, malformed resource entries and unsupported directory payload types. This is dependency inspection, not full model geometry validation. Existing known incoming-reference guards remain.
- Reuses coordinated journal v3, virtual overlays, recovery and quota logic. Existing agent batch dispatcher automatically selects the expanded mixed planner; no new MCP action. Added scenarios for folder move/delete/restore, nested metadata Undo/Redo, embedded glTF version exchange and external-resource refusal. Existing test target reused. Skill and MCP docs updated.

## Evidence and remaining
GitHub source committed and read back. Tests added, NOT RUN; no C++ build, CTest, frontend build, native MCP execution, UI, fault-injection execution or performance evidence. No main merge. Mixed supported directory payloads are textures/audio/BIN and self-contained glTF/GLB; arbitrary files, external-resource model versions, mixed URI repair, nested relocation endpoints, nested Trash sidecar edits, ambiguous repeated states and interleaved map/rules dependencies remain limited. Existing ordinary model repair planner still supports its previous scope. Next S6f.3.4b: projected graph for external model version transitions and mixed URI repair; then S6f.4 cross-owner durable save, S6h.3 recovery, S6i.3 MCP and final QA.
