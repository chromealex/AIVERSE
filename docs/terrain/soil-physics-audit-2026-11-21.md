# Soil, erosion, exposed roots and falling trees audit

2026-11-21, feature/aiv87_terrain_pbr, baseline fd67f9e1e.

| Capability | Existing implementation | Current AlpineValley |
| --- | --- | --- |
| Substrate resistance | NereidErosion.h: Sand/Silt/Loam/Clay/Gravel/Rock, critical speeds, bedrock, root reinforcement | Exported PBR assets do not supply live soil components |
| Erosion/deposition | NereidShorelineSystems.h and TerraWeaveNereidErosionBridge.h, integer tick/carry and bounded soil transactions | Not installed |
| Sediment transport | TerraWeave native Integration.h registers NEREID sediment transport before bank commit | Not installed |
| Exposed roots/support | TerraWeaveVegetationSoil.h evaluates sampled terrain loss; RootSupportRegistry emits once-only fall events | Static root geometry only |
| Falling body | TerraWeaveFallenPhysics.h and native PhysicsIntegration.h hand off fall events to Box3D on owner boundary | Static models do not fall |
| Persistence/presentation | TerraWeaveEcsAuthority/Persistence/NativeWorldBinding synchronize saved root/soil state and visual tombstones | Authored document entity identity only |

Real validation executed on KINGSTON: built aiverse_physis_terraweave_fall_bridge_test and aiverse_terraweave_native_micro_physics_compile_test. CTest exact two names PASS2/2,0.80s. First is real PHYSIS/ECS/Terra/Box3D native fall/save/replay fixture. Second is explicitly COMPILE/LINK SMOKE and does not establish erosion runtime behavior. No complete erosion-to-root-to-physics scene/GPU demonstration was run. The erosion equations are bounded gameplay policies, not a continuum geotechnical solver.

The external project native directory contains recipe/export/install scripts and scene-template, no authoritative GameModule source. Do not claim these exported meshes run native terrain simulation. A real playable host must register existing Terra/NEREID components, bank probes/soil paint and rooted plants, consume commit events after ECS barriers, then update native terrain/root presentation and Box3D. Preserve one canonical ECS owner; never independently erode renderer meshes or simulate falls in project visual scripts.

Open visual work from user:1K texture per4m, varied sand, optional grayscale-underlay mode with0..20% upper-layer transparency, native underwater optics. No change to these has been made by this audit.
