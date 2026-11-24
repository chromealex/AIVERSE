# S6g.5 — COLLADA and paged dependency coverage (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 5870551b98faf1e18ee443af5affb31d702bddfe; roadmap 45ec879a829cf11ea7fae4f908b4e9641f13449f; implementation f1d6412f405165d956e296f1a19cffac3bd457d0.

## Implementation
- Shared existing engine/render/Xml.h BOM conversion, reused by ModelDae.cpp and Studio. COLLADA library_images/image/init_from or ref child produces conservative external image candidates, including unused image declarations; surface init_from IDs are not treated as paths. Existing model move validation now checks DAE edges. No external XML resources loaded.
- StudioAssetReferences adds scan coverage counters (glTF, GLB, OBJ, DAE, opaque GLB, other known unsupported model formats, symlinks, unsupported URIs, missing libraries, embedded resources). Coverage explicitly not complete. Existing strict malformed/budget failure behavior preserved.
- studioDependencyPage produces bounded rows (max 100), direction totals, unresolved GUID occurrences, depth and coverage. SHA-256 token hashes selected known edges and scan counters independent of page/direction; stale token refuses mixed snapshots. Graph computed again per page; no persistent index/cache or filesystem transaction snapshot.
- StudioSession adds project-bound native studio.assetDependencyPage; existing assetReferences array contract retained. Linked studio_asset_dependencies now uses native page owner, preserves previous direct mode without depth; direction with depth and optional expectedToken.
- HTML5 Assets displays native pages, direction totals, coverage diagnostics and graph refresh on stale/error; existing request guard retained. Mock refuses native-only page request.
- Regression fixture for declared DAE images, percent paths, missing GUIDs, unsupported model diagnostics, page bounds and stale token refusal. Existing target reused. Docs/skill and roadmap updated.

## Evidence and remaining
GitHub implementation committed and read back. C++ compile, CTest, TS typecheck/build, actual MCP transport, UI and performance NOT RUN. No main merge. Coverage is conservative, not a proof of complete runtime dependency enumeration. DAE file prefix handling follows supported local subset, remote/absolute/malformed paths may remain unresolved; no external model extensions, arbitrary scripts, FBX/legacy formats, full effect usage analysis or URI source rewriting. Tokens fence changes in known selected graph and counters, not unrelated bytes or concurrent noncooperating writes. Traversal still bounded to 16 hops and scan budgets. Next S6g.6 transactional path repair (start with typed glTF URI edits), followed by dependent history chains, durable cross-owner save and recovery/MCP extensions. Final QA required.
