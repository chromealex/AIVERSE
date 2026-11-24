# S6f.3.4b.1 — stable external model versions (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 92f0fa79c698eb06d47d167388544ae5a1901b8e. Implementation 402f57928ce29c26e0c6efdbcf5fcb7b55369b10; fixtures dc5b9529b1527127f6c45ae18a5eb5c9dacc381a.

- Mixed replacement history inspects outgoing and incoming glTF/GLB versions, including recovery payloads, resolving external buffer/image URIs against the logical content path. Only safe, indexed regular files with matching live GUIDs and no overlapping relocation endpoints are admitted. Inspection caching is keyed by physical payload, logical owner and version policy.
- Existing journal v3, digest validation, metadata and GUID checks remain in use. No new MCP action. External-resource moves, directory model topology changes, extension payloads, affected dependency endpoints and mixed URI repairs are still fenced.
- Fixtures cover external glTF replacement Undo/Redo, missing incoming recovery dependency refusal, changed endpoint refusal, restored GUIDs and exact model bytes.

Evidence: GitHub source and fixtures committed; NOT BUILT OR RUN. No CTest, native MCP, frontend, fault-injection or UI evidence. No main merge. This is a bounded first increment, not the full projected graph. Next S6f.3.4b.2: model dependencies in virtual content occupancy across directory/file relocation and replacement transitions, typed URI repair overlays (including streamed GLB), graph edge identity checks and recovery fixtures; then S6f.4 cross-owner durable save, S6h.3 recovery, S6i.3 MCP and final QA.
