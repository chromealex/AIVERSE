# Agent rules: NEREID water framework summary

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## NEREID universal water framework (2026-11-18)

New opt-in, genre-independent ECS water capability: `AIVERSE::lib_nereid_water`
with `"engine/world/nereid/NereidWaterSystems.h"`. It samples Fixed-point
water-body volumes for native shooter/strategy/vehicle/other hosts; it must not
be reimplemented in FPS or RTS. Existing editor `waterNetwork`, baked tiles,
shore geometry and RTS water paths are still authoritative for legacy games.
**NEREID stages 1–12 are opt-in:** ECS water sampling, polygon/river geometry, buoyancy force output, query index, legacy raster import, surface mesh, Renderer3D flag and reflection fallback, GPU residency, WorldScene manifest, per-body identity and read-only ECS presentation snapshots. GPU effects are presentation-only; no hydraulic solver or native FPS/RTS migration. **Native C++ builds, CTest, GL/Metal and runtime tests remain UNVERIFIED.**
Read `docs/nereid/ARCHITECTURE.md`, mandatory living
`docs/nereid/HANDOFF.md`, and
`skills/gameplay/aiverse-nereid-water/SKILL.md`.
After each NEREID implementation commit, append exact SHA, changes, API,
test status, limits and next step to its handoff as the next separate commit.


