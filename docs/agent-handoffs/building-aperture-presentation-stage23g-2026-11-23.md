# AIVERSE Building Generator — Stage 23G: movable apertures render handoff

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Objective and current code examined

Continues Stage 23F authoritative ECS fixed tick and its verified-by-source-only collision / multi-graph events. Reviewed current branch headers BuildingApertureFixedTick.h, BuildingApertureOwner.h, BuildingLiftPresentation.h, RuntimeBuildingModel.h and BuildingRuntime.h. Reuse the exact existing Renderer3D CPU Model conversion conventions instead of introducing an independent model backend. Lift visualization remains owned by BuildingLiftPresentation, not this adapter.

## Changes

- NEW `engine/buildings/BuildingAperturePresentation.h`
  - `BuildingApertureVisualAsset`: one reusable CPU `render::Model` for a half-leaf per stable socket and the axis/travel dimensions.
  - `buildingApertureLeafModel`: builds a simple half-panel from the authoritative closed collider AABB and current `BuildingStyle` plate/shell material using existing Mesher and `runtimeBuildingModel`, marks node runtime-mutable and dedicated geometry so the existing renderer may apply instance transforms.
  - `BuildingAperturePresentation::initialize(poses,style)`: validates socket identities and bounds, generates assets only once and stages them before publication.
  - `accept(BuildingApertureFixedTickFrame)`: only takes complete, monotonic committed server-tick frames with matching socket geometry/kinds; aborts mismatched frame without mutating the currently accepted visual state.
  - `interpolate(renderDtMs,animationMs)`: computes two separate instance positions for each door/gate/roof from its currently accepted physics-authoritative open/closed target; interpolation changes only visual positions, never collision, authority or graph links.
  - `model(socketId)`: read-only CPU render Model for existing render-owner GPU upload/leases; `clear()` for lifetime teardown after renderer releases GPU resources.
- `engine/buildings/BuildingRuntime.h` includes the new presentation adapter.
- `skills/world/aiverse-building-style/SKILL.md` records Stage 23G model ownership, tick/render separation and limitations.

## Integration contract for next agent

1. On authoritative building spawn call `RuntimeBuildingGenerator::generateRecipe`, which intentionally excludes moving DoorLeaf/RoofPanel and dynamic elevator platform from STATIC generated CPU render mesh + physics.
2. Attach `BuildingApertureOwner` with unique game collider IDs and obstruction probe; initialize `BuildingAperturePresentation` using authoritative owner poses and the same style.
3. Existing renderer owner uploads `presentation.model(socketId)` CPU assets to its Renderer3D device once and keeps two distinct instance/node handles per socket. CPU Model is not a live GPU resource.
4. Authenticated game transport maps client -> real existing ECS actor. Stage 23F fixed-tick owner validates access/range and orders physics collision commit before graph notifications. Existing graph owners receive changes through Stage 23A/23C/23D/23E adapters.
5. On **complete** fixed tick, pass `frame` to `presentation.accept`. Render owner calls `interpolate` on its separate render phase and applies only returned instance positions and revision. Visual opening fractions are never authoritative for collision, navmesh route access, network commands or safety.
6. On streaming unload, reconcile passengers, pending graph publications and scene lifetime before GPU retire. Renderer releases resources via its current fences, then presentation clear is allowed.

## Actual status and explicit caveats

Code is committed but NOT integrated into a real example game's scheduler or actual Renderer3D instance submission path. No network packet endpoint, asset registration, GPU upload or final motor trajectory is claimed. This first visual layer uses simple two-panel boxes along the widest horizontal axis of their *world* AABB; it does not implement hinged doors, curved gate meshes, physical swept leaf collision or timed lock/unlock of a passage after finishing an animation. BuildingApertureOwner remains an instantaneous binary physical state machine. Rendering is independent and cannot grant gameplay permission.

Per user's direction, tests remain deferred. No C++ build, CTest, native Mac GPU/Metal/GL screenshot, PHYSIS simulation, real game tick, multiplayer replay, Studio TS or render resource lifetime run has been executed. Do not claim production readiness based on GitHub source publication.

## Source provenance

- `deaff3cd8336e10ba81faf68f086ca27a4688a7d` — new movable aperture CPU Model and interpolation.
- `47f54052981821289b2d771de02745586df34159` — runtime umbrella.
- `fcb1840a5ce32f4e56f0edf6bd817627de09e737` — agent skill.
- This document is its own handoff commit.

## Next concrete stage

Stage 23H: wire game application authoritative fixed tick, network actor identity and actual Renderer3D instance transform publishing to these APIs; add true physically animated movable-leaf motion/collision interlocks instead of binary instantaneous physical gates, and ensure all registered graph owner state transitions synchronize at the same revision. Inspect existing renderer ownership and relevant game modules before edits. Then run the deferred full native compiler, CTest and Mac GPU/PHYSIS tests, and reconcile Draft PR with concurrent main changes without overwriting peers. Write a new handoff after each stage.
