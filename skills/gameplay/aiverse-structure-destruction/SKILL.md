---
name: aiverse-structure-destruction
description: Destructible structures of box pieces: area damage, fracture into fragments, support graph collapse, authoritative debris and dirty regions for collision/navmesh/visibility.
---

# Destructible structures

Header: `<aiverselib/mechanics/StructureDestruction.h>`. Library catalog id `structure-destruction`, CMake target `AIVERSE::lib_structure_destruction`.
Capability area F3 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Add pieces with `addPiece` (anchored pieces hold the structure; optional fracture grid and threshold).
2. Every tick: `stepStructure(config, state, tick, dt, damage)`. Apply `report.removed` / `report.added` to collision, rebuild navmesh and visibility inside `report.dirty`.
3. Debris bodies are authoritative (gravity, bounce, rest, lifetime): replicate `report.spawned` or the whole state; Box3D may add visual detail only.
4. Use with PHYSIS fracture events (engine/physis/PhysisFractureOutbox.h) and physical-interactions breakables for single objects.
5. Persist with mechanics::save/load (kind 202).

## Limits

Box pieces only (no arbitrary mesh slicing); support is connectivity, not stress analysis; no soft bodies.
