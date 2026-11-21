# AIVERSE LENS Stage 47A.1 — resident GroundFX material kit, no late draw texture allocations

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: feature/lens-motionweave-integration (main untouched)
Parent: 4f013e405da016e07ccff55b123f97b93ee97b3c
Prior handoff: docs/agent-handoffs/lens-motionweave-integration-stage47a-groundfx-owner-transaction-2026-11-20.md

## Source changes

- RtsGroundFx owns two procedural default material TextureRefs (cobble 64x64, dirt 32x32) as part of its staged GPU kit. initUnpublished allocates both after the white fallback texture, and fails the whole candidate kit if either GPU allocation fails.
- init swaps the fully prepared TextureCache GPU owner, then copies refs from the prepared kit. shutdown clears references and lets TextureCache shutdown own and release their GPU texture handles after host idle proof.
- drawPrepared no longer executes TextureCache::addGenerated or makes procedural noise; it captures stable TextureRefs owned by published GroundFX GPU kit. This avoids late texture creation after GroundFX/Roads/Shores/Fog owner publication.
- tests/tests.cpp: adjusted the staged kit idle-rejection cleanup count to include the 2 new generated textures, added a no-texture-allocation-in-groundfx-preparation-or-draw regression fixture.

## Checks

GitHub source patch based on the exact parent HEAD and refetched after publication; mechanical target checks. CMake, compiler, tests, GPU parity and backend builds are NOT RUN. Test assertions must be verified in the consolidated native run.

## Known limits

- GroundFX pre-frame CPU state clone is still O(map) and should use copy-on-write/dirty journal optimization.
- Terrain/Detail is NOT YET part of the same external OwnerPrepareCommit; Terrain::draw can still upload dynamic visibility/LOD textures after publication.
- No shader backend parity claim is made. Metal/Vulkan/GLES verification remains pending.

## Next

Stage 47B: separate Terrain full-map and incremental Detail prepared resources from TerrainRenderer::build, expose a no-throw publish and wire into relevant map-change transaction; remove dynamic texture/LOD RHI mutation from primary draw where practical. Add late Fog owner failure regression preserving Terrain/GroundFX and secondary-camera publication, then continue MotionWeave/sprites/lighting/overlays and the one consolidated native test pass.
