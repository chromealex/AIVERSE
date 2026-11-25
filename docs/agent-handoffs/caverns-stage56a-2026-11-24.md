# Caverns Stage56a — shared planetary GI consumer

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Starting HEAD 7a10928d; roadmap b48f8573; implementation 777dc8fa; source fixture `a0cf3dbd8bd9cc4161490bdf31b8e41d28b5c857`.

## Implementation
- `engine/world/TerraWeaveCavernsPlanetGiPublication.h`: stage exact body cut/volume triangles into a private copy of the host's existing GiGeometryScene; share immutable BVH chunks; check original baseline identity independent of BVH ordering, forbid duplicate/foreign IDs, retain original material data, use actual host transform/material/readiness. Publish using existing CPU/GPU target and same scene; refusal leaves CPU scene uncommitted. Retire restores exact original GI chunks and deletes volume slots. Native irreversible refusal follows existing recovery barrier. No new GI algorithm, probe owner, ECS state or genre-specific dependency.
- RuntimeFrame adds optional retirement preflight before native close; existing hooks remain compatible. Managed nav lease publication stays in RuntimeFrame. Native example README, area notes and skill describe setup.
- Existing planet collision test contains source regression with real shared GiGeometryScene and a recording target: refusal, abort, cut commit, unrelated chunk identity, baseline material restoration. It is not a real GPU test.
- Main is one documentation commit ahead (1b77c21b, require squash merge), no source differences from that commit. No main merge or source import performed in this stage; final integration must comply with squash policy. Do not merge before full Mac gate.

## Delivery evidence
Source changes only, no compile/test execution at this stage by user request. Architecture reuses existing native scene and GI owners; native API and documentation added; no new authored fields, graph, inspector, persistence, MCP or Studio mutations (presentation-only adapter over existing schema). Actual native host, GPU backend, headless isolation, performance/memory, visual capture and acceptance NOT RUN. The test fixture is registered via existing CTest but not executed. Live renderer needs its actual baseline GI source and stable projection; this stage does not install a default GameCore host or handle orbital/rebase changes automatically. No claimed complete feature.

## Next
Stage56b: connect the actual shared native host and its planet GI leaf source/ID allocator, coordinate frame and resource lifetime; handle moving-body/rebase transitions and actual GI renderer guard. Then extend the existing motor admission to swim/fly via existing locomotion owners, and integrate surface/subsurface traversal. Keep fixed lockstep isolated from native Box3D; do not change actor genre-specific owners. Continue feature-first; perform unified Mac native/GPU/Studio/MCP acceptance afterward.
