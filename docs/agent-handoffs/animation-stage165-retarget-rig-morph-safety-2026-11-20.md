# AIVERSE MotionWeave — Stage 165: generic rig retarget and morph safety

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Starting source: `fffcd5ba697f0486c967aae7a40bc6b24b58aeff` (Stage 164)
Source commit: `42e32528d520a6ab8d4e3ac9a20eeee6b32168d3`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — keep draft, unmerged.

## Actual change

File: `libraries/include/aiverselib/animation/GenericRetarget.h` (genre-neutral, not FPS/RTS-specific).

- `GenericRetargetMap::compile` captures copied per-node name, parent index and bind T/R/S for both source and target rigs. It rejects nonfinite mapped translation ratios.
- New read-only `matches(source,target)` checks those signatures exactly. A hot-reimported rig with equal node count but changed node order, name, parent or bind transform now fails safely rather than using stale mapped indices. The map remains independent of a live Model pointer and can compare copies.
- `apply` now prepares a scratch bind-pose output and commits it to the caller's Pose only after full mapped-pose validation. Failed size/identity/NaN/degenerate source scale or incompatible morph input leaves prior `dstPose` untouched.
- `copyScale` rejects degenerate source bind axes instead of silently substituting 1.0.
- Optional `copyMorph` refuses unsafe mismatched mesh-local morph channel counts. The old implicit copy-by-index can be used only when channel counts match; arbitrary remapping remains the explicit `engine/view/ModelPoseRetarget.h` policy.
- Rejects nonfinite animated input position, rotation, scale and transferred morph weights; checks final T/R/S for finite output.
- No changes to AnimationGraph clocks, Graph owner, parent animation, physics, networking, editor GPU ownership, authoritative worlds or other genre modules.

## Architecture and API

```cpp
auto map=aiverse::lib::animation::GenericRetargetMap::compile(source,target,aliases);
if(map.matches(source,target)) {
    // Throws std::invalid_argument for stale rigs or invalid channels.
    // On exception the previous target pose is unchanged.
    map.apply(source,target,sourcePose,targetPose,
              {.scaleTranslation=true,.copyScale=false,.copyMorph=false});
}
```

This is strict indexed-skeleton compatibility, **not** a complete retarget engine for mismatched humanoid proportions, twist constraints or semantic morph names. Mesh-local channel count equality is necessary, not sufficient, to guarantee same semantic expression. For unrelated morph layouts do not enable `copyMorph`; use authored explicit maps and validation.

## Actual verification and limitations

Performed: GitHub source read of current `AnimationLod.h`, `ModelPoseRetarget.h`, `GenericRetarget.h` and current animation branch commit; SHA-guarded GitHub source write and source review. This stage is SOURCE IMPLEMENTATION ONLY.

**NOT RUN:** native Mac CMake/C++ compiler/link/CTest, automated tests, actual retargeted imported rigs, Metal/Vulkan/GLES, ASan/UBSan/TSan, performance, GPU resource lifetime. The new generic retarget signature checks and scratch pose copy can add CPU/alloc overhead; evaluate crowd profiling at acceptance and replace with compiled epoch/pinned signatures if performance requires it. Do not declare release-ready or tested.

## Follow-up verification cases

1. Same rig/valid pose matches and produces the prior expected delta.
2. Same number of nodes with a reordered or reparented bone is rejected.
3. Same pointer/array layout with changed bind T/R/S (hot reimport) is rejected.
4. Nonfinite pose or degenerate bind scale causes failure with byte-for-byte unchanged destination pose.
5. `copyMorph=true` with mismatched channel counts rejects without partial output; equal-count path copies bounded channels.
6. Complex rigs and morph mesh semantics require artist-approved explicit retarget mapping; verify thousand-instance allocation/performance.

## Readiness matrix

| Subsystem | State | Still needed |
|---|---|---|
| Generic MotionWeave ECS / Graph | Source implemented incrementally | full native regression |
| Independent child Graph Studio + ModelView | Source implemented Stage 161–164 | compile, actual pixels, no double writer |
| Graph restore telemetry + input continuity | Stage 164 source | actual native tests |
| GenericRetarget stale-rig guard | Stage 165 source | native compile and malformed-rig tests |
| GenericRetarget morph safety | Stage 165 source | real imported morph semantic parity |
| Complex rig/twist/IK and Animation LOD | Partial | production rigs, crowd benchmarks |
| SceneLoadCoordinator adapter | Not implemented | cross-branch reviewed integration |
| Fully tokenized GPU Model provider retirement | Not implemented | texture/cache/rollback + GPU fence proof |
| Final CMake/CTest/Metal/Vulkan/GLES/sanitizers | NOT RUN | one coordinated final Mac test campaign |

Next Stage 166: continue compiled LOD/morph retarget robustness (especially explicit semantic morph mapping and high-crowd behavior) and evaluate actual scene integration contracts without merging peer branches. Keep stage-level handoff and user-visible progress matrix. 
