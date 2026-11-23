# AIVERSE Building Generator — Stage 23I physical aperture motion + textures

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## User requirement and verified source
Stage 23I continues Stage 23H. User specifically added: "Текстуры еще не забудь". Inspected actual engine/world/SpatialWorld.cpp: add/remove are the existing physical collider mutation APIs. Inspected engine/render/Model.h and BuildingStyle.h: Model::images + render::Material support PBR texture indices and shaderGraph texture slots, whereas portable StyleMaterial currently holds numeric material parameters only (no texture asset IDs). Reviewed BuildingApertureOwner, fixed tick, presentation and dynamic nav sources as of feature branch.

## Implemented source
- engine/buildings/BuildingApertureOwner.h:
  - Added committed `physicalPermille` (0..1000) and `moving` state to BuildingAperturePose. Initial open fixtures start at 1000.
  - `beginMotion(socket,targetOpen,domainMask)` opts into moving a single conservative full-width barrier, never concurrent with pending graph changes or another motion.
  - `advanceMotion(spatial,router,stepPermille)` moves its current full-width collider along the major horizontal axis in bounded deterministic Fixed steps; checks the union of old/new AABB with the caller's live actor/vehicle/cargo obstruction probe; commits the collider through existing SpatialWorld. If publishing replacement fails, attempts restoration and faults on rollback failure.
  - Every successfully committed substep updates the pose revision and emits BuildingGraphChange with the swept bounds; graph-owner refusal keeps the precise revision pending for retry.
  - First closing step from a fully opened aperture installs a new collider (previously absent). Final opening step removes the collider. Direct binary setOpen refuses an in-progress motion. closedColliderBounds() now reflects the actually translated AABB for the dynamic nav source.
- engine/buildings/BuildingApertureFixedTick.h:
  - Opt-in `setSteppedMotion(true, stepPermille)`.
  - Accepted commands start movement, then later fixed ticks advance one physical substep, retrying graph ACK before subsequent steps. No complete presentation frame is released while movement/graph publication is incomplete.
  - Legacy instantaneous binary path remains default for compatibility.
- engine/buildings/BuildingAperturePresentation.h:
  - `bindPbrTextures(socket, render::Material, vector<render::Image>)`: validates all referenced base, metallic/roughness, normal, occlusion, emissive, team and ShaderGraph texture image indices, then installs model-local image references and the full PBR material in reusable CPU leaf Model. Must be called before owning renderer GPU upload.
- engine/buildings/BuildingApertureGameBridge.h: exposes `setSteppedMotion` and `bindPbrTextures` to actual native scene owners.
- skills/world/aiverse-building-style/SKILL.md: updated operational instructions including texture ownership and limitations. Existing catalog registration reused.

## Ownership and exact integration
The game must generate runtime static building shell without baked dynamic leaves; attach one BuildingApertureOwner with unique collider IDs and live swept obstruction sensor; register real graph consumers before issuing commands. Authenticate network commands through the existing transport, map clients onto real ECS entities, and enqueue into BuildingApertureFixedTickOwner/BuildingApertureGameBridge. The owner calls fixedTick once per authoritative simulation tick, with stepped motion opted-in; the physics state commits first, then each graph domain receives exact AABB invalidations, then completion is eligible for a read-only presentation frame.

For textures, existing asset importer/catalog and Renderer3D owner must resolve PBR image assets to render::Image and render::Material and bind them to CPU models before GPU upload. The new method does not open files, invent asset GUIDs, bake/paint textures or migrate BuildingStyle JSON. A later stage must add explicit PBR texture references in style assets and a renderer-side resolver contract.

## Important limitations
Stepped physical movement is of ONE conservative full-width AABB per operable socket, not independent double leaves or hinged/swinging panels. Sliding roof uses the same major-axis translation approximation. Visual presentation still interpolates two independent half-leaf meshes after acknowledged fixed-tick frames; it does not yet show every physical substep and its visible geometry does not exactly correspond to the single physical barrier. Gameplay passage remains blocked until the fully-open physical endpoint, despite geometric displacement during opening; game owner must enforce this policy independently from intermediate moving collider placement.

SpatialWorld::add has no global transaction API. The owner attempts rollback; if rollback itself fails the building faults. The graph router still must retry pending acknowledgements. NavMesh remains dirty until the existing owner refreshes its dynamic source and off-mesh links. No real physical actor sweep beyond the required authoritative obstruction callback has been added.

## Provenance
- 7b54f409a418011bb70a4258da173f5f5322b1bd — incremental collider movement.
- 43c8b5d813959a92226ba2d675734f9b0dd356c8 — handle closing from fully open and initialize motion endpoints.
- 2041f08bdc71c9153d2fc418c4645c88af99e1a8 — fixed-tick stepped motion.
- 048bea67748314984ff86c024c6de3fe59115434 — PBR texture/material binding.
- 5f1011f401129d10a865cd821d44dcff11a0e2b9 — game bridge exposure.
- 273e4f644629e34038a660466bee70d53de84966 — skill update.
- This document is written as its own follow-up documentation commit.

## Actual validation
TESTS DEFERRED by user. No Clang/GCC compilation, CMake/CTest, Mac Metal/GL33 texture renders, PHYSIS actor collision integration, GPU resource lifetime, multiplayer tests or live game application wiring has run. Do NOT claim complete production-ready physical doors or texture rendering. Branch diverges from main; PR remains Draft.

## Concrete next step
Stage 23J: replace single-barrier approximation with two collision-owned physical leaves; use correct conservative swept volumes for their individual motion, real actor/vehicle/PHYSIS queries, door-open access interlock, per-substep renderer transforms and revisioned graph recompute. Add style/material asset GUID references for all PBR texture channels and bind via existing resource catalogs/importer, with cross-backend shader/UV consistency. Then wire the game app's scheduler and Renderer3D ModelDraw submissions. Run comprehensive deferred tests only at functional closure, and reconcile main carefully. New agent handoff required after every stage.
