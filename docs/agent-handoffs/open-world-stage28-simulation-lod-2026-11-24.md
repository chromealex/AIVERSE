# W1.28 — authoritative decision LOD / feature-first continuation

Branch `feature/aiv20261124_open_world_integration`, Draft PR #135. Base b4e7ee696ab842139f3ab7d62b7856b861def307. Owner explicitly requested remaining functionality first, testing afterward.

## Source changes
- 5a443142: in-progress roadmap item.
- eb983fcf, 758d6be7: `engine/world/SimulationLod.h`, reusable validated HOT/WARM/COLD decision policy, stable-ID phase, Chebyshev region distance, critical override, fair slice helper and derived diagnostics. No wall clock, camera, asset residency or RNG dependency.
- 4c7b6705, d6cf1544, f934e8c3: `engine/actor/ActorModule.h/.cpp` read root simulationLod config; reset derived decision counters per pass.
- 02ccd59f, 3b8db4dd: `engine/actor/ActorBots.cpp` conservative existing bot planner cadence. Critical work stays full-rate; retained steering only with no held action/button. No change to motor, physics, combat, damage or ownership step rates. Missing observers, first/life-changed bot state, active interaction, nearby actors, vehicles, loot, indoor traversal or projectile force normal planning. Maximum range guard is conservative.
- 7dce83f8: `engine/game/TrafficPopulation.cpp` reuses shared round-robin arithmetic, existing check window unchanged.
- c8e59997, fab0b2b4: `engine/actor/ActorRegionOwnership.cpp` derived-resource allowlist and reset; `apps/editor/EditorWorld.cpp` reports last-pass simulationLod metrics in world_regions_status.
- 94fdb86c, e0f6a00e: `apps/editor/core/WorldPersistenceOps.cpp` normalized get/validate/set; compare expected settings, delegate existing rules_set owner, refuse occupied world maintenance slot.
- d8059e69, 4404e62c: `apps/studio-native/StudioTools.h`, `StudioSession.cpp` allowlist and expectedProject guard.
- d66b9a2f, cffd9499: typed Studio DTOs and Simulation LOD tab in existing World Persistence panel; explicit save, warning that rule edits affect save content identity.
- 916226dd, 3bfdb234, eced5425: skill, original 16-block roadmap continuation and catalog discovery updated.

## API and compatibility
Opt-in root rules block `simulationLod`: enabled (bool), hotCells/warmCells (ordered 0..4096), warmTicks/coldTicks (ordered 1..120). Default absent disabled. Region distances in existing actor adapter are 32-metre cells; hot every tick, warm/cold at configured stable-ID phase. No new saved authoritative fields. BotMemory and CharacterControl remain canonical; counters are derived. Enabled content changes behavior and therefore content identity. Existing saves may refuse changed rules: no implicit migration or live rule edit. Restore hooks clear diagnostics; unavailable diagnostics do not mean zero work.

## Status and next
This is decision cadence, NOT full cold NPC/vehicle simulation, true absence/rehydration, or a global city AI/traffic LOD. The actor adapter remains conservative and may not reduce work in a dense combat scene. No performance claim. Original OW-02 through OW-16 still require their own implementation/integration work. Keep one area at a time, reuse existing owners and audit external GTA2 generic policies before porting. Next W1.29: implement safe explicit cold-state advancement for opted-in component groups, with deterministic time, preserved stable references and guarded wake/eviction. Then finish missing OW-01 lifecycle and proceed OW-02 tiled navigation streaming. See docs/open-world-framework.md for feature-first queue.

## Verification
Native build, CTest, MCP runtime, Studio typecheck/build, screenshots, deterministic replay, cross-host parity, performance and Mac/KINGSTON gate: NOT RUN (owner deferred). JSON catalog and roadmap parsed during authoring; no acceptance claim. Add regression scenarios for tier classification, multiple observers, phase fairness, critical override, opt-out parity, fixed tick rollback/save parity, stale authoring settings and actual host decision counters before final acceptance. Main untouched; no success devlog or W1 core designation.
