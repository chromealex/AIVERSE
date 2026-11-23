# AIVERSE building generator — Stage 23J: dual physical leaves and PBR GUIDs
Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Implemented code
- engine/buildings/BuildingDualLeafOwner.h: opt-in alternative physical owner for sliding apertures with two distinct existing SpatialWorld collider IDs per opening. attach validates uniqueness/previous ownership, inserts initial leaves with rollback. begin and advance use Fixed 0..1000 travel, authoritative pair of swept AABBs and host-supplied obstruction callback. Failed pair publication tries to restore both previous colliders and faults on failed restoration. Each successful physical step publishes revisioned graph events, and retry handles pending acknowledgements; poses/closedColliderBounds expose authoritative state.
- engine/buildings/BuildingDynamicNavSource.h: generic source snapshot supports legacy or dual owners via the real closed bounds, and buildingDualLeafNavInvalidator marks native NavMesh dirty after committed changes. No separate navmesh.
- libraries/include/aiverselib/systems/BuildingStyle.h: optional material textures GUID object (baseColor, metallicRoughness, normal, occlusion, emissive), strict 32 lowercase hex validation while retaining legacy styles.
- engine/buildings/BuildingApertureTextureAssets.h: map style slots to existing Renderer3D images and material indices.
- apps/studio-native/StudioBuildingTextureResolver.h: StudioAssetCatalog byGuid verification and host-owned safe importer callback; no independent metadata database.
- engine/buildings/BuildingAperturePresentation.h: acceptPhysicalStep permits fully acknowledged intermediate physical movement frames to drive the two visual leaves.
- BuildingRuntime.h and BuildingApertureGameBridge.h: export and expose texture APIs.
- skills/world/aiverse-building-style/SKILL.md: agent contract for ownership, use and constraints.

## Ownership/limitations
Use either BuildingDualLeafOwner OR BuildingApertureOwner, never both for the same sockets. The pre-existing BuildingApertureFixedTickOwner and BuildingApertureGameBridge still drive the legacy owner; the dual owner is an opt-in separate world-owner API and has NOT been registered in any actual application fixed tick. Road, Nav, Spatial and Gameplay graph callbacks still require their authoritative owners. The game must check that an opening is fully clear before actor traversal, regardless of partially translated AABBs. Movement is linear sliding, not articulated hinge/collision geometry. Existing Studio import pipeline must decode GUID-referenced assets; no GPU upload is performed here.

## Provenance
746fea86035a9a076e3c4d98f61cccbb6eeee7fd dual-leaf owner
d1708a2824f8d3c7330512f285f496e8c4360fb9 texture GUID resolver
5c58ed83cf1967725aae3c568dbb35817c79ed91 dynamic nav owners
d9c4bc15e986875d6b92e392c19a96d67292d9a2 attachment accessor
6a3ae4f2d196edddc64da6bdceabec497811c1f9 existing collider ID guard
c0a18fdb756f0e0535e7bb0ef6a77f21ccdbe82a style GUID parsing
64eb8d4115a931f6ab11fe4a9ee333f73380949d Studio resolver
bf5ade773aa69d56e65a050452ffdb9eaf7ad7d4 physical frame visualization
b62f35fffe71174d247874f5541433704a6e30ac skill
This handoff has its own documentation commit.

## Testing and next step
By user instruction tests are deferred. No compile/CTest/PHYSIS integration/game runtime/Metal/Vulkan/GLES texture rendering has been run. Source publication alone is not verified functionality. Next: replace the legacy game bridge with a unified optional physical owner at actual application registration, guarantee actor access interlocks and stable multi-graph retry; run complete end-to-end tests before main merge.