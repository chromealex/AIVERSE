# S6g.1 — current and saved map dependency discovery (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 33c56b19cb4fc9990411f1e2c2cdb491dc1540e6; roadmap 13d9405a3839b9c73d695fa44fcd7b0fec759ffd; implementation 17da0abb0e330f383526084f113219cb8e003538. Main was one commit ahead of the merge base (site/index.html and site/ru/index.html only); no merge attempted in this scoped Studio stage.

## Implementation
- apps/studio-native/StudioAssetReferences.h: shared bounded saved-map visitor for mutation fence and graph query, canonical project containment, symlink ancestor rejection, deduplicated manifest and discovered maps, history cache exclusion, 512 map / 64 MiB disk scan limits. Exact path candidates from current and saved maps, escaped JSON pointers, existing file GUID resolution and unresolved explicit content paths. Bounded candidate traversal (depth 128, two million values, 32,768 rows). All map candidates are read-only; no write or repair authority.
- apps/studio-native/StudioSession.cpp: existing studio.assetReferences response extended compatibly; rebuild(false) avoids generating GUIDs in graph query. Existing browser and linked MCP move preview use the shared graph; commit retains separate reference revalidation.
- apps/studio-native/tests/StudioAssetTransferTests.cpp: existing native registered suite extended with exact paths, prefix false positive, missing GUID, escaped JSON pointer, duplicate manifest/discovered map, ignored history cache and read-only preview/refusal.
- skills/editor/studio-ui/SKILL.md, docs/mcp-agent-link.md and docs/devlog/systems.json updated. Existing HTML5 move dialog already renders document/owner/field and disables commit for selected read-only references. No new frontend contract or operation.

## Delivery evidence
Implementation/architecture/dependencies: source changes committed and read back; no new dependency or second owner. Native editor API, existing Studio and linked MCP integration: implemented, runtime verification NOT RUN. Skill/docs: updated. Inspector, simulation/headless runtime, persistence schema and graph node authoring: N/A for this read-only editor query; no saved format changed. Example fixture and tests: added to existing suite; NOT RUN. Performance: explicit bounded work, no timing measurements. Platforms: desktop native Studio, Mac and Windows verification NOT RUN. Publishing: roadmap in-progress; screenshot/devlog completion NOT RUN. No claim of full parity or merge readiness.

No local checkout/build on KINGSTON available in this session. No C++ compile, CTest, web build, actual MCP registry audit, native UI interaction or Mac merged-tree quick gate executed. No main merge.

## Next
S6g.2: extend precise dependency discovery to asset-to-asset edges (e.g. glTF external resources), forward/reverse queries and an explicit Assets dependency view; maintain distinction between typed references and conservative candidates. Resolve directory/uncatalogued relative-path preview gaps, add malformed and symlink regression cases, and consider output pagination and deep/large map limits. Do not automatically rewrite unsupported references. Then S6h recovery storage quota and S6i MCP expansion; final QA required. Existing text mention search is not proof of complete dependencies.
