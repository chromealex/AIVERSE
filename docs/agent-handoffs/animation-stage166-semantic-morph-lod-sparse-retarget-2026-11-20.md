# AIVERSE MotionWeave — Stage 166: semantic morph mapping and sparse compiled LOD retarget

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Starting head: `51b4af73e0b3b702f41c7f38115024b8d047ceff`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — remains DRAFT/unmerged.

## User requirement

Genre-neutral MotionWeave animation and visual LOD. Finish feature work before the agreed one-pass native Mac acceptance. After every stage write a separate handoff, including precise changed files, API, actual tests/results, limits and next step. Do NOT merge other agents' scene branches without review.

## Source delivered

1. `07da45c6d10501d1d8e43d05fcd47f4129302120` — `engine/view/ModelPoseRetarget.h`: new public `ModelMorphChannelNames` and `compileNamedModelMorphRetarget(source,visual,sourceNames,visualNames,map,error)`. Asset importers/authoring supply *explicit semantic names* per Model node and mesh-local morph channel, not automatic index assumptions. The helper translates each target named channel into its source channel index through the already-authored visual-to-source node mapping; unmatched named channels remain unmapped (`-1`, target bind weight). Bad/duplicate/empty names, invalid mapped source index, missing/incomplete per-node channel descriptions or counts exceeding `kMaxMorphTargets` fail before mutating the previous mapping. Both source and target counts are checked against actual imported mesh primitive morph counts. The produced compact integer plan reuses the existing `validModelPoseRetarget` and `compileModelPoseRetarget` validation path.
2. `57609a9bcd394bda9dc28277d038b5ead09018b8` — `engine/view/ModelPoseRetarget.h`: compiled `NodeStep` now stores a dense array of ONLY the active `(targetChannel,sourceChannel)` morph transfers plus an active count. The `retargetCompiledModelPose` hot path checks/transfers only those mappings, rather than scanning all `kMaxMorphTargets` for each node/instance. Existing compiled plan publication/generation checks, source owner, bind fallback, GPU Model ownership and actual transform output remain intact. This is a source-level operation reduction, **not measured runtime speedup**.

No gameplay, physics, networking, FPS/RTS or editor GUI layer dependencies were added.

## API and semantics

```cpp
using namespace aiverse::view;
ModelPoseRetarget map=makeNamedModelPoseRetarget(source,visual);
ModelMorphChannelNames sourceNames(source.nodes.size());
ModelMorphChannelNames visualNames(visual.nodes.size());
// The asset importer assigns ordered mesh-local semantic names to the
// corresponding Model node indices. Example:
// sourceNames[sourceFaceNode]={"BlinkLeft","Smile"};
// visualNames[visualFaceNode]={"Smile","BlinkLeft"};
std::string error;
if(!compileNamedModelMorphRetarget(
       source,visual,sourceNames,visualNames,map,&error)) {
    // Keep the previously published map; show the authoring error.
}
auto compiled=compileModelPoseRetarget(
    source,visual,map,sourcePublication,visualPublication);
// The caller retains both CPU Models and pins graphics resources until draw.
```

Important: this is an explicit **authoring helper**; it does not claim the glTF/FBX importer automatically populates semantic morph names, and does not prove the same-named expressions have equivalent geometry/deformation. An importer must explicitly supply complete, correctly ordered per-node names. The mapping is local to each visual node; targets without a matching source semantic are left at target bind weight. Reimport requires recompilation with new immutable source/target publication epochs. The output from the helper is transactional; existing `map` survives failure.

## Actual verification

ACTUAL: GitHub-connected source audit of current `AnimationLod.h`, `ModelPoseRetarget.h`, `GenericRetarget.h`, the Stage 165 handoff; SHA-guarded source commits and readback of added APIs.

**NOT RUN:** native C++ compile/link (including -Werror), CMake/CTest, semantic morph mapping regression, real imported glTF/FBX models, Metal/Vulkan/GLES pixels, sanitizers, 1k/10k/100k crowd benchmark, GPU lifetime/error injection. Do NOT label Stage 166 tested or performance-verified. The additional array per compiled node increases compiled plan memory while reducing per-actor iteration over missing morph slots; tradeoff needs measurement.

## Validation targets for final one-pass Mac suite

- Source names `[Blink,Smile]` and visual names `[Smile,Blink]` compile remap `[1,0]` and correctly transfer weights.
- Missing target semantic preserves target bind morph weight; duplicate names, invalid mapping, incomplete source/visual descriptions, excessive morph counts fail without replacing an existing map.
- Compiled sparse morph parity with the old full-channel loop for randomized masks of `-1` and valid channel indices; confirm generation mismatch rejects stale plans.
- Native C++ compile under strict warnings, full CTest, Metal/Vulkan/GLES rendering and long-running resource retirement tests.
- Benchmark sparse retarget on 1k/10k/100k entities, with and without morphs; record CPU median/p95, allocation counts, RSS, VRAM and rendered parity.
- Verify compatible multi-LOD morph authoring with externally supplied channel-name metadata from the actual importer (not assumed implemented here).

## Readiness matrix

| Area | Source status | Remaining |
|---|---|---|
| Genre-neutral ECS MotionWeave/Graph | Integrated incrementally | native regression |
| Independent animated Studio/ModelView children | Stage 161–164 source | native compile, GPU/skin parity |
| Playback/parameter continuity | Stage 163–164 source | actual reload/fault tests |
| Generic skeleton retarget safety | Stage 165 source | complex rigs/crowd cost |
| Semantic morph name-to-index mapping | Stage 166 source | importer metadata integration and visual parity |
| Sparse compiled visual LOD morph transfers | Stage 166 source | CPU perf and correctness benchmark |
| Complex twist/IK/morph retarget | Partial | production assets and authoring UX |
| SceneLoadCoordinator adapter | Not integrated | peer-branch review |
| Full GPU provider token retirement | Not implemented | shared cache, texture, rollback, fences |
| Native CMake/CTest/Metal/Vulkan/GLES/sanitizers | NOT RUN | coordinated final Mac acceptance |

## Next Stage 167

Integrate **real importer-owned morph channel names** into the generic authoring metadata (preserving importer index order), propagate validated names through LOD authoring and fail safely on duplicate/ambiguous source names. Avoid inventing mesh channel names. Then finish remaining scene lifecycle and GPU retirement work before one full test campaign. Create another standalone handoff after the stage.
