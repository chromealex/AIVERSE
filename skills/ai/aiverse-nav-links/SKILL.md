---
name: aiverse-nav-links
description: Off-mesh navigation links (jump, drop, ladder, climb, door, teleport) planned together with any walking mesh, link traversal over ticks, cover spot generation from boxes.
---

# Off-mesh navigation links and cover generation

Header: `<aiverselib/bots/NavLinks.h>`. Library catalog id `nav-links`, CMake target `AIVERSE::lib_nav_links`.
Capability area F2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Describe links as data (`NavLink`: from, to, kind, cost, bidirectional, traverseTicks, arcHeight, enabled).
2. `planWithLinks(links, start, goal, meshCost)` - `meshCost(a, b)` returns the walking distance on your mesh (bots/NavMesh.h, engine/nav) or nullopt; the result is a list of Walk/Link legs.
3. Drive a link with `beginLink` + `stepLinkTraversal(links, traversal, tick, doorOpen)`; doors make the agent wait until open.
4. `generateCover(boxes, config, standable)` returns cover spots (half/full by box height) around box obstacles; map them to `bots::CoverCandidate` for ShooterBot.
5. Persist LinkTraversal with mechanics::save/load (kind 201).

## Limits

Links are not extracted from geometry automatically; the planner calls meshCost for node pairs (fine for hundreds of links).
