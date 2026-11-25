# S6f.3.4b.2a — projected local model dependencies (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 7446f02360bd364eac278c025a41f07d1a30d9b6. Implementation 1d6dc6ad72256e5268b6133c220a0ca2909c525e; fixtures b5221d3aab62389035db9397a5bc46267df2fc31 and c7b31d6d2ec88cf1d853a6fd22e2ad9e6fae6afc.

- Managed glTF/GLB owners are those inside any relocation endpoint. Scan resource slots once per physical payload/extension, resolve at each complete logical step using virtual content GUID occupancy and physical origin mapping. Surviving physical versions preserve target GUID per external slot. Both outgoing and incoming versions are inspected at their live boundaries. Whole-directory and coordinated file moves preserving relative URI resolution are supported.
- Unmanaged model owners (including OBJ/DAE) retain the baseline dependency fence. Unknown extensions, unsafe/missing/unindexed URIs, duplicate GUID/path, nested endpoints and mixed URI rewrites remain fenced. Recovery uses existing journal v3. No new MCP action.
- Fixtures cover external model directory moves with nested settings, combined version swap and paired model/image relocation, missing incoming resource and nested endpoint refusal.

Evidence: GitHub code committed; no build, CTest, frontend, fault injection, native MCP or UI execution performed. No main merge. Next S6f.3.4b.2b: typed resource edit overlay replay, slot identity comparison across rewrites, streamed GLB snapshots, stationary-owner projected dependency checks and recovery fixtures. Then S6f.4, S6h.3, S6i.3 and final QA.
