# AIVERSE mandatory world-ownership rules

Extracted verbatim from the previous root AGENTS.md; original section numbering and repository-root-relative paths remain intentional. Follow cross-references via the root routing table. These rules are binding, not optional guidance.

## Terra-first game creation (user policy, 2026-11-20)

For every new game and environment task, read `skills/workflow/aiverse-terra-first/SKILL.md`.
Use TerraWeave terrain/Arbor/Flora/rocks and existing companion owners first; write
custom project code only for a verified missing capability. Configure or adapt existing
native APIs before introducing a second procedural generator. This is capability routing,
not a mandate to add terrain to UI-only/indoor/2D games or overwrite authored worlds.
Vegetation recipes: `skills/world/aiverse-terra-vegetation/SKILL.md`. Distinguish exported
assets from live native terrain/streaming/ecology/physics and record actual verification.

## Water ownership for all new worlds (user policy, 2026-11-20)

All new water uses NEREID: rivers, lakes, sea/ocean, flow and water interactions.
Read `skills/gameplay/aiverse-nereid-water/SKILL.md`. Do not substitute static PBR
planes or scene-local wave solvers. All terrain and vegetation use TerraWeave first.
Together these are the required environment foundation for games and showcases.
Add missing host adapters through the native shared systems. Preserve existing
legacy gameplay/save compatibility until a deliberate, tested migration; do not
claim source APIs or static exports prove live simulation. PRISMA/PHYSIS/SpectraFX
retain sky/weather/VFX ownership rather than duplicating them in Terra or NEREID.

## City block authoring

For plazas, courtyards, planting beds and street furniture, read
`skills/world/aiverse-city-blocks/SKILL.md`. Resolve curb elevations from the road renderer's
units and support surfaces; keep soil inside planter walls. Project generators and assets
stay in the game's repository. Workflow ownership: city-blocks task; additive catalog routing.

## Rigid-body physics

For Box3D collisions, sensors, queries, joints, motors or detachable assemblies, read
`skills/gameplay/aiverse-physics/SKILL.md` before writing an adapter. Reuse the optional
`AIVERSE::physics` target; distinguish its fixed-manifest JSON wrapper from the direct
C API. Copy transient events before mutations, wake reactivated joints, and validate
real contacts. Physics needs an explicit host and never feeds float state into RTS
Fixed lockstep implicitly. Deterministic vehicle/interaction policies retain their
own authority. A standalone test or a model animation is not an in-client integration.

## Cross-agent ownership: PHYSIS / NEREID / TerraWeave (2026-11-18)

**MANDATORY before weather, terrain, water, foliage, shoreline, hydrology or biome work:** read [`docs/physis-nereid-terraweave-integration.md`](docs/physis-nereid-terraweave-integration.md). Never implement the same capability in two systems. All three engine subsystems and their opt-in adapters are **genre independent**.

| Owner (agent / active feature branch) | Exclusive implementation scope | Shared consumption boundary |
|---|---|---|
| **PHYSIS agent** / `feature/physis-nature-simulation`, draft PR #25 | Atmospheric/climate ECS, rain/snow/wind, surface wetness/snowpack/ice, fire and quake events. `engine/physis/` | Publish read-only `physis::EnvironmentalResponse` / `PresentationCell`; no water body/river/ocean/shoreline solver or terrain meshes. |
| **Water agent (NEREID)** / `feature/nereid-water-core`, draft PR #23 | Generic ECS water bodies, water-surface and flow sampling, river/lake/ocean geometry, buoyancy, optional water mesh and water legacy adapters. `engine/world/nereid/`, `docs/nereid/` | Export NEREID `WaterSample` / `WaterContactComponent` and stable water-body geometry for any terrain/render host; do not own PHYSIS climate or Terra terrain. |
| **Terra agent (TerraWeave)** / `feature/terraweave` | Heightfields, biome/soil and terrain geometry, foliage/scatter, scene/terrain adapters, ground/shore presentation. `engine/world/TerraWeave*.h` | Opt-in adapters consume NEREID water and PHYSIS environment; do not duplicate a mutable water solver or weather simulation in Terra core. |
| **Integration agent** / explicitly assigned at merge time | Coordinates, units, tick order, persistence, optional adapters, editor/host integration and cross-system tests | Independent composition target, **not** dependencies from PHYSIS/NEREID/Terra foundational headers into one another. |
| **Render/VFX agent** / separately assigned | Particles, precipitation visuals, wet/snow material layers, foliage deformation, water material effects | Presentation-only snapshots; never write authoritative PHYSIS/NEREID/Terra ECS state from GPU/render frames. |

**Source of truth:** Terra owns terrain height/biome baseline; PHYSIS owns dynamic atmospheric/weather/surface conditions; NEREID owns persistent water bodies, levels, flow and contacts. PHYSIS has an **experimental standalone `HazardCell::waterDepth` flood model**; for NEREID-managed worlds set `physis::InstallOptions::floodMode = physis::FloodMode::ExternalAuthority` to skip/clear PHYSIS local water while retaining fire and quake. The switch and tests have been authored but are **NOT RUN**; no NEREID integration or cross-module validation has been performed. NEREID water becomes authoritative when enabled. Keep legacy `waterNetwork`/baked maps unchanged until explicit migration and regression testing.

**Future Terra bridge:** read NEREID water masks/surface and PHYSIS wetness/snow/ice/wind to build Terra shoreline, vegetation exclusions and ground overlays. Terra uses signed centimetres, PHYSIS/NEREID `Fixed` metres, X/Z ground and Y vertical: checked deterministic conversion and negative-coordinate floor division are mandatory. PHYSIS `rain` is normalized intensity, **not metres of water**; only the Water owner defines a calibrated precipitation-to-water-volume policy. No mutual include dependencies, double authority, hidden state or genre switch.

**Cross-agent handoff after EVERY stage:** commit own changes only on own branch; document implementation SHA, API, changed files, actual PASS/FAIL/NOT RUN results, integration risks, and next step in a stage handoff. Cross-branch integration waits for a dedicated PR, native builds, tests (`physis_*`, `ecs_nereid_*`, `terraweave*`, legacy water/terrain), rollback, save parity and conflict review. Neither separate PR is automatically merged by this guidance.
