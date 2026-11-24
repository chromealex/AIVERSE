# Stage 145 — portable binary SDF interchange

Owner: SpectraFX continuation agent. Branch: feature/spectrafx-html5-data-interfaces-20261123
Previous HEAD: 5d721458976e1c1b97104b7f3ce109723ebbbb56
Plan: 01bebc401349043042b5cdfa00f99e446564ac95
Implementation: 1827d2cdfdc8506a87b82d0787dc84de74980414

Added engine/fx/SpectraSdfBinary.h, exposed through SpectraFX.h. SDFB version 1, explicit little-endian float32, 32-byte header (magic/version/dimension/origin.xyz/spacing/count), x-fastest samples, trailing CRC32 of header+payload. Exact size and dimension checks before allocation; valid() checks finite bounds and samples. Atomic encode/decode output, native injected-reader load (caller owns bounded IO). 32 cubed = 131108 bytes. CLI tools/spectrafx/sdf_bake.cpp exports .sdfbin or legacy JSON. No GPU ABI change.

apps/editor/core/SpectraGraphOps.cpp extends shared fx_graph_edit with importSdf, sdfAsset content-relative path, expectedProjectPath, Graph v2 CollisionModule. Existing checked dependency guard rejects traversal and symlinks; size checked before allocation; no disk writes. Replaces only staticSdf property with a snapshot, standard compiler diagnostics. Studio SpectraGraphAssetInspector.tsx and spectraStudioAssetIndex.ts expose existing indexed picker for .sdfbin plus import button, same stale graph/project response guard. Asset catalog indexing of .sdfbin needs real host verification. User must create binary with native CLI and place under project content. No new Studio file-write operation. Legacy inline graph format unchanged; snapshot import does not eliminate graph JSON size limit.

Tests expanded in tests/vfx_graph_runtime_test.cpp (exact binary roundtrip, CRC/truncation/version/dimension rejection and atomic output), tests/vfx_graph_ops_test.cpp (import through Workspace, no publish, traversal). Docs updated: owning roadmap, docs/agents/spectrafx.md, skill, apps/studio-web/AGENTS.md.

## Delivery evidence
- Implementation / architecture: source committed, existing volume and compiler owner reused; execution NOT RUN.
- Native API / headless / CLI: source added; independent consumer and execution NOT RUN.
- Skill / documentation: updated; audit NOT RUN.
- MCP / Studio / Inspector: shared edit and indexed picker source; actual bridge/registry/indexing/UI NOT RUN.
- Graph / persistence: existing inline JSON compatible, new binary version and checksum; actual save/load and roundtrip NOT RUN. No authoritative ECS persistence applies.
- Performance / platforms: bounded binary size, no GPU ABI change; profiling and hardware verification NOT RUN.
- Dependencies: header-only codec, no new dependency or target; compile/link NOT RUN.
- Examples / tests: registered fixture sources updated; CTest NOT RUN.
- Publishing: actual screenshot/demo/devlog NOT RUN pending verification.
- Handoff: separate commit.

GitHub expected-head non-force branch update succeeded, implementation diff inspected. No CMake, CTest, TypeScript, CLI, Metal/Vulkan, UI, benchmarks or CI run; owner requests features first. No main sync/merge this stage; merged-tree gate pending.

Next: persistent external SDF asset references through all compiler/preview/runtime/package/replay consumers, including dependency-aware hot reload and bounded resolution. Then GPU atlas/storage reuse, accelerated baker and higher resolution. Snapshot import is not a substitute for persistent references. No feature-ready claim.
