# Caverns Stage54u4a — native observer demand and candidate preparation

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Code/test/skill HEAD `c2f010b16c3d8577c616d0b99b7199a553823a2c`. Main unchanged; compare before work reported zero commits behind main. Roadmap 67bf9b0b.

## Source commits
- 29d1736c / 1f48dfb4: `TerraWeaveCavernsPlanetDemand.h` deterministic simulation-anchor observer selection, enter/leave hysteresis, host-supplied pins, global region/volume-slot budget, stable ordering and explicit refusal/pinnedOverflow. Bounded Q16 interior lattice with gradient halo. Auto spacing is coarse and may underresolve narrow features; explicit spacing can be refused. Small regions without an interior slot are refused.
- 4f10faa3 / 92074f18 / 583ef5c3: `TerraWeaveCavernsPlanetRuntimeBuild.h`: capture actual renderer leaves, freeze/index ECS sources, shared CaveWorldStreamJobs sequentially per region, exact rim split with original body vertices preserved, existing owned rim group preparation, native volume payload conversion, sampled solid exterior/influence bounds and exact final boundary certificate. No chart-box clipping or synthetic gameplay caps. Complete body candidate with immutable sources and generation/content fences against stale edits and rollback, including same numeric revision. Worker cancel/poll/drain explicit. No live geometry, GPU, Box3D or SpatialWorld mutation during worker generation.
- 5934c678: release prepared baseline staging after recoverable publish refusal.
- d90b49bf / 65a2240b: extend existing authority and native physics test sources with multi-observer demand, budget/pin priority, hysteresis, retirement demand and candidate equal-revision rollback identity. Runtime build header included by physics test. NOT RUN.
- c2f010b1: skill updated.

## Scope
This is 54u4a, not full automatic gameplay streaming. Demand is a host input/output contract, not a new tick system. The standard spherical GameCore still uses grid/surface movement and no native Box3D planet scene. Current interior coverage is intentionally conservative: may reject a chart because of unsupported renderer leaves, insufficient sampling budget, open procedural boundaries, refused mouth or unrepresentable exact seams. Do not imply these candidates will all publish or that actors can traverse them. No fake collider/nav readiness.

## Evidence
GitHub code/test HEAD confirmed; workflow lookup returned zero runs. No compiler, CTest, real Box3D/SpatialWorld, GPU, performance, frontend or gameplay traversal execution. Test sources registered through previous stages only. Mandatory KINGSTON merged-tree gate outstanding.

## Next Stage54u4b
Wire builder + demand + collision binding into an actual opt-in native gameplay host, using generation/content checks at both staging and publication and real reader exclusion. Define baseline coverage owner lifecycle across changes to demanded leaves; never remove restored baseline while actors can enter it. Keep old complete publication until replacement is ready; gate entering unready pinned regions, cancel on rollback/state restore, use existing authorized region stamp commands for destruction. Establish actual physical actor/vehicle traversal and PHYSIS-aware navigation in 54v, then comprehensive QA. Do not merge to main before native validation.
