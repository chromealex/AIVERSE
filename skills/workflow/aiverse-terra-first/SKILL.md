---
name: aiverse-terra-first
description: Start every AIVERSE game or world-building task with TerraWeave capability selection, then use project-specific implementations only for demonstrated gaps. Applies to new projects and environment changes in existing games.
---

## Mandatory TerraWeave + NEREID ownership

User instruction: construct game environments through TerraWeave + NEREID first.
Terrain/biomes/trees/grass/rocks belong to TerraWeave; all new rivers/lakes/oceans
belong to NEREID. Never substitute a custom height/noise generator or a static PBR
water plane and describe it as the engine feature. Read
`skills/workflow/aiverse-terra-first/SKILL.md` and
`skills/gameplay/aiverse-nereid-water/SKILL.md`. Existing legacy maps retain their
compatibility until an explicit tested migration. For missing host integration,
add the adapter to the shared subsystem; record a real capability gap before any
custom fallback. Do not fake waves, flow, foam or buoyancy in project scripts.
Sky stays with PRISMA, climate with PHYSIS, VFX with SpectraFX; compose owners.


# TerraWeave first

User policy: every AIVERSE game starts by evaluating TerraWeave for its world.
Use existing Terra capabilities before custom generation. This does not require
terrain in a UI-only, indoor or 2D game, nor replacing existing authored content.
Gameplay remains composable ECS; Terra is the environment owner, not a genre framework.

## Choose the actual host

Read `skills/world/aiverse-terraweave/SKILL.md` and the relevant section of
`docs/terrain/TERRAWEAVE.md`. Inspect current code and latest applicable handoff;
source present, native compiled, host connected and visually verified are distinct states.

- Native streamed world: reuse `engine/world/TerraWeaveForestScene.h`,
  `engine/world/TerraWeaveNativeWorldBinding.h` and the opt-in WorldScene manifest.
  Follow `examples/world/terraweave_native/README.md`; configure the existing host.
- Flat editor heightfield: use TerraWeave preview/generate through
  `skills/world/aiverse-terrain/SKILL.md`. Preserve authored edits and Undo.
  Never run flat MapDoc operations on FPS/planet formats.
- Authored models/offline showcase: follow `docs/terrain/native-landscape-authoring.md`;
  use `aiverse_terraweave_landscape`, then the native vegetation/rock exporters.
  Import ordinary assets and keep the real NEREID manifest alongside them.
  An exported asset is not live terrain, ecology, streaming or physics integration.
- For vegetation and rocks read `skills/world/aiverse-terra-vegetation/SKILL.md`.
- Water belongs to NEREID; sky/postprocessing to PRISMA; climate/wind to PHYSIS.
  Compose those owners through existing bridges instead of implementing copies in Terra.

## Before custom code

For each needed environment capability, identify the real API/tool, host hook and
missing behavior. Try configuration, supplied assets and an adapter first. If no
supported implementation exists, record the concrete gap in the project handoff
with the paths inspected, then implement only that gap in the external project.
For a reusable engine gap, extend Terra when engine work is within the task's scope.
Do not stop for a routine additional permission question just to record a gap.

Never write another noise terrain, tree/grass/rock geometry generator or LOD system
in a scene script when Terra already supplies that function. Scene scripts may
choose seeds, profiles, density, composition and export/import native results.
Do not silently replace failed Terra output with a placeholder and call it Terra.
Preserve user-requested custom behavior and existing authored assets.

## Acceptance

Verify the real game host, not only a generator test. Check ground contact, water/road
exclusion, canopy silhouette, seed variety and near/mid/far LOD using scene-only
screenshots. Record exact source revision, commands/results and remaining limitations.
Project-specific scenes/assets stay outside the engine repo. Keep per-stage handoffs.

Runtime library: none. Workflow over existing TerraWeave and companion systems.
