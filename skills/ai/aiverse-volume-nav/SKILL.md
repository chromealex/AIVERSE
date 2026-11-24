---
name: aiverse-volume-nav
description: 3D navigation for flyers, ships and swimmers: occupancy grid from boxes, deterministic 26-neighbour A* with smoothing, flock avoidance and 3D formation slots.
---

# Volumetric 3D navigation

Header: `<aiverselib/bots/VolumeNav.h>`. Library catalog id `volume-nav`, CMake target `AIVERSE::lib_volume_nav`.
Capability area R5 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Build a grid with `buildVolumeGrid(area, cellSize, agentRadius, obstacleBoxes)`; rebuild when obstacles change.
2. `findVolumePath(grid, start, goal, maxExpanded)` returns smoothed waypoints; `partial` when the expansion budget ran out.
3. `flockSteer(config, self, neighbours, goal, obstaclePush)` gives a bounded acceleration for fleets and swarms.
4. `formationSlot3D(leader, yaw, pitch, offset)` rotates formation offsets in 3D for ships and aircraft.

## Limits

Dense grid (up to 4M cells); no hierarchical or streamed volumes yet.
