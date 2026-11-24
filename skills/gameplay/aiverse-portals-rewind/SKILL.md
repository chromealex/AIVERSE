---
name: aiverse-portals-rewind
description: Linked portals with position/velocity/facing transfer and recursive view transforms, plus gameplay time rewind over a bounded snapshot history with an energy budget.
---

# Portals and time rewind

Header: `<aiverselib/mechanics/PortalsRewind.h>`. Library catalog id `portals-rewind`, CMake target `AIVERSE::lib_portals_rewind`.
Capability area Q4 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Portals: define pairs (`Portal`: center, normal, up, half size, link). Each tick call `crossPortals(portals, cooldowns, tick, previous, bodies)` after movement and apply the crossings.
2. Rendering recursion: apply `portalTransformPoint/Direction` to the camera once per depth level.
3. Rewind: `recordRewind(config, history, tick, snapshotBytes)` every tick (use mechanics::save output or a world snapshot); `rewindTo(history, tick)` returns the snapshot to restore and spends energy.
4. This is gameplay rewind; engine/actor rewind history stays lag compensation.
5. Persist PortalCooldowns (kind 203); RewindHistory (kind 204) only for small histories (save limit 1 MB).

## Limits

No portal-in-portal physics for bodies larger than the opening; rewind of single objects (local time bubbles) is left to the owner.
