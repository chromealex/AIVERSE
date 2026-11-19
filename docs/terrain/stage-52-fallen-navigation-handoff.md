# TerraWeave Stage 52 — fallen-tree NavMesh obstacle snapshots

Date: 2026-11-19. Branch `feature/terraweave`, Draft PR #24.

## Source commit and files

- `5b47d8735f2cd39f20ded857b3fdb0e828c266d6`: NEW `engine/world/TerraWeaveFallenNavigation.h`.

API `captureFallenNavigation(FallenPlantPhysics&,const physics::Scene&,clearanceCm=70)` reads server-owned Box3D body positions and quaternions AFTER an authoritative physics step, quantizes to integer-centimetre conservative X/Z footprint rectangles and produces an immutable `shared_ptr<const FallenNavigationSnapshot>`. Each tree/shrub is keyed by stable `AnchoredPlantId`. No grass entities and no per-branch NavMesh polygon allocations. Rectangles follow the X/Z AABB of the body capsule's rotated local Y axis, plus caller-defined clearance, not the exact timber silhouette.

`FallenNavigationSnapshot::build` validates at most 8192 obstacles, bounded area and spatial buckets (6400cm), <=128 obstacles per bucket and <=1024 buckets per object. `blockedAtCm(x,z)` has a bucket lookup, not a full scan of all fallen trees. `changedFrom(previous)` returns previous AND current footprints when a body moves, falls asleep, despawns, or respawns, so a formerly blocked path is restored correctly. `fallenNavigationBlocker(snapshot,EarthworksNavConfig,otherBlockedCallback)` composes this immutable fall obstruction snapshot with existing authored water/building blockers through `EarthworksNavSource::Blocked`. `markFallenNavigationDirty(navMesh,source,current,previous)` marks the needed existing `nav::Mesh` columns/halo; host calls `refreshEarthworksNav`/ordinary `nav::Mesh::refresh` after publishing the new snapshot.

## Ownership sequence

1. Authoritative NEREID bank/terrain excavation tick yields `PlantFallEvent`.
2. Opt-in server `FallenPlantPhysics::spawn` creates a dynamic body; it updates poses in its own `physics::Scene::step()`, not on the render or nav worker thread.
3. At a post-Box3D step barrier, capture an immutable `FallenNavigationSnapshot`; compare to previous, mark dirty nav tiles.
4. Rebuild `EarthworksNavSource` with a blocked callback capturing the NEW immutable obstacle snapshot, then call existing navmesh `refresh`. Native clients replicate server authority; floating Box3D motion is NOT deterministic between platforms.
5. Remove/spawn/replant trees explicitly in gameplay, persist stable IDs, and update collision/save state in the host.

No duplicate navigation solver, water system, GPU shader or entity-per-blade grass simulation is introduced. Stationary sleeping fallen bodies naturally stop changing their NavMesh footprint.

## Limitations and verification

This is an **opt-in native** adapter; no AIVERSE FPS/RTS client automatically supplies or consumes fallen obstacle snapshots. Conservative blocked rectangles may overblock passage beside thin branches; navmesh is single ground span per column by default, not a full volumetric navigation mesh. Box3D bodies remain approximate capsules. A host must keep previous and current snapshots alive across `refresh` and must never call `physics::Scene` from a navmesh worker.

Actual QA: Github source was committed. Clang/GCC/MSVC, CMake, CTest, RTS/FPS startup, Box3D simulation, NavMesh correctness, Metal/GL/Vulkan visuals and performance are **NOT RUN**. Previous 9/9 TerraWeave CPU and 1/1 GPU timing router pass are for earlier revisions. Keep PR #24 Draft.

**Next:** Run a first native integration compile (including optional Box3D + navmesh target), fix any errors, add deterministic smoke fixtures for tunnel/eroded riverbank/grass cover/tree collapse/old-vs-new nav obstacles and validate with Metal screenshots. The user asked features first, tests later.
