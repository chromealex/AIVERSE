# TerraWeave Caverns — Stage 15 handoff (2026-11-23)

## Branch / commits
Repository: `chromealex/AIVERSE`. Working branch: `feature/terraweave-caverns-core-20261123`, not merged into main.
- `adac3b84d219b4126428bbb0710101e374bb506c`: `engine/world/TerraWeaveCavernsLodPolicy.h`
- `b5e8991fa812489d8af850ff204bc8dff95fe8e0`: `tests/terraweave_caverns_lod_policy_test.cpp`
- `109c1e3189f2d2ebdef947e5da4a432133621394`: `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This dated handoff follows these commits.

## Source/API
- `resolveCaveFaceLod(CaveFaceSpec)` validates two adjacent cave meshes for any of X/Y/Z axes and both direction/argument orders, including negative signed chunk coordinates. Equal physical span required.
- Extracts integer-centimetre edge sets on their common world face. If identical returns `CaveFaceDecision::ExactMatch`.
- If coarse is on the negative-coordinate side, fine is positive, and stage-14 `stitchCaveCollinearBoundary` can partition each coarse contour edge exactly, returns `Subdivided` and a modified visual coarse mesh. Otherwise returns `RegenerateAtCommonLod` with a common step/resolution, keeping original input meshes untouched.
- `CaveFaceLease` with `caveFaceLeaseCurrent` offers revision guard for host-owned seam certification.
- New fixture `tests/terraweave_caverns_lod_policy_test.cpp` covers all three axes, both argument orders, negative chunk keys, divergent contour, conservative reverse-direction fallback and stale revision check.
- Skill updated with use, ownership, fallback policy, and limitations.

## Ownership
- STRATA/GeometryScene own visual selection, residency, and GPU cluster hierarchy. These are presentation-only meshes. Box3D, SpatialWorld, and nav::Mesh continue to consume independent authoritative geometry.
- The policy does not write into live RHI/STRATA; the host must regenerate and publish a matched pair and ensure both revisions are current BEFORE permitting draws.
- A returned `ExactMatch` certifies integer-centimetre boundary edges only; normal/UV/material, nonmanifold topology, and shader parity remain unverified.

## Actually executed tests
- GitHub connector successfully created code/test/skill commits.
- **No local compiler, CTest, sanitizer, real generated p cave mesh seam audit, Metal/OpenGL/Vulkan capture, GeometryScene, Box3D or gameplay test executed in Stage 15.** New tests are NOT RUN, not PASS.
- In full checkout, compile test with C++20 and run it, followed by prior stage regression targets and a generated neighbor grid audit.

## Known limitations and next steps
1. `resolveCaveFaceLod` does **not** provide a universal Transvoxel transition. Unsupported complex contours must fall back to common density sample spacing and actual regenerated mesh pair. The stage-14 helper only supports collinear partitions in one coarse/fine orientation.
2. The current per-face policy is not yet wired to stage-12 shard re-generation or stage-11 GeometryScene. Add an owner-coordinated face-certification cache keyed by both source revisions, publish matching visual chunks, invalidate STRATA leases on updates.
3. Stage-14 mixed LOD triangle generation needs real generated-scene topological/normal validation and cross-backend rendering QA. Avoid claims of watertightness and arbitrary mixed LOD.
4. Complete no-blocking GPU-fenced per-shard residency and cross-subsystem publication/rollback; current immediate Box3D/SpatialWorld publishers are not a global atomic transaction.
5. Register and run Caverns CMake/CTest, then add editor brushes, save/replay and geological detail while keeping stage-specific handoffs and the agent skill current.
