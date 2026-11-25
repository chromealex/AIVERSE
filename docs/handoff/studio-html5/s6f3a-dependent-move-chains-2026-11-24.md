# S6f.3.1 — dependent ordinary asset move chains (2026-11-24)

Branch feature/studio-html5. Base cca39ff12ce660d90703bc7f9bbcb27d20be333a. Implementation e474b205c803dfb7e972be3d844a3c2a8180196c; regression scenarios and validation e2bd7e325b41ddbd0008ebea69f66a16fa869446; GUID/material validation bb53c6bcfba85a9d26829e7ec4b4648245f891b5.

## Implementation
- Preflight exact endpoint ordinary asset relocation chains, including A -> B -> C, from retained history and sidecar identities. Validate initial occupancy/digests/GUIDs and simulate intermediate states. Reject nested endpoints and ambiguous repeated recovery states.
- Compose stationary model repair snapshots by exact intermediate bytes, including streamed GLB prefix/tail identities. Compare retained final edits to typed net projected dependency graph before mutation. Retain existing map/rules and material guards.
- Coordinated journal v2 persists binary-safe resource snapshots, streams compact GLB edits through existing publication owner, restores exact bytes and reverses relocation sequence on recovery. Existing journal v1 remains readable.
- Studio agent batch apply uses the chain planner when all asset steps are ordinary moves. Other batches keep conservative existing staging. Existing linked MCP actions use the same owner.
- Regression scenarios for two consecutive moves with stationary glTF and streamed GLB, exact Undo/Redo, interrupted v2 batch rollback and external modification refusal. Skill and MCP docs updated.

## Evidence and limits
GitHub source committed and read back. Tests added, NOT RUN: no C++ compile, CTest, frontend build, actual native/MCP execution, crash injection execution or performance validation. No main merge. Not unrestricted batch support: moved-owner byte repairs, nested endpoint trees, ambiguous cycles, mixed dependent Trash/replacement/metadata and interleaved map/rules changes remain fenced. Preview token fences owner versions, with filesystem preflight at apply. Global cross-owner save atomicity remains open. Next S6f.3.2: ordered resource/relocation phases for moved-owner repairs, then mixed dependencies and S6f.4 durable cross-owner save. Final QA required.
