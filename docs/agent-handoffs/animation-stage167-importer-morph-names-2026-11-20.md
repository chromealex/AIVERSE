# AIVERSE MotionWeave — Stage 167: importer-backed morph semantic metadata

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Starting HEAD: `a6070b53444bebe836de20a0b4b6ffb4e7a9e5e6` (Stage 166)
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — keep DRAFT, not merged.

## Actual source changes

1. `engine/render/Model.h` — commit `6e88f97bb35961a6f84589f9306ccb2d49917e29`: universal `MeshDef::morphTargetNames` stores **importer-provided ordered** morph semantic names. Empty means unavailable, NOT generated from indices. The data follows the mesh/model CPU asset through staging and must be invalidated/replaced alongside its Model asset generation.
2. `engine/render/ModelGltf.cpp` — commit `18bd126ed92e59c92fc256b06b196f203ca88337`: read ecosystem-authored glTF mesh `extras.targetNames`, preserve order, require an array of unique nonempty strings, reject more than `kMaxMorphTargets`, and require each primitive's morph target count to equal the named-channel count. Missing names remain unavailable. The importer intentionally **does not invent target names** for core glTF `targets` entries.
3. `engine/view/ModelPoseRetarget.h` — commit `b6db2c3aa7b2b29b61bb247a50ec1dff85dab26e`: `importedModelMorphChannelNames(model)` derives per-node channel names from the referenced imported `MeshDef`; `compileImportedModelMorphRetarget(source,visual,map,error)` routes them into Stage 166's strict transactional `compileNamedModelMorphRetarget` and the same generation-bound compiled plan. No dependency on glTF JSON or FBX format enters the universal View API.

## Public authoring API

```cpp
using namespace aiverse::view;
ModelPoseRetarget authored=makeNamedModelPoseRetarget(source,visual);
std::string error;
if(!compileImportedModelMorphRetarget(source,visual,authored,&error)) {
    // Show authoring error; keep the previous published mapping and graph.
} else {
    auto plan=compileModelPoseRetarget(
        source,visual,authored,sourceEpoch,visualEpoch);
    // Publish immutable generation-bound plan if plan != nullptr.
}
```

This supports two separately imported glTF models whose mesh `extras.targetNames` are both present and valid (including reordered named channels). A missing name table, incomplete names for morph-bearing source/target nodes, duplicate/empty names or mismatched primitive counts rejects instead of making false channel-index assumptions. The source/visual Model and GPU owner lifetime contract is unchanged.

## Important exclusions and safety

- **FBX:** The inspected `engine/render/ModelFbx.cpp` currently imports meshes, skins and animation clips but does not populate morph delta channels. Stage 167 does NOT claim FBX blendshape/morph import support, and it does not manufacture metadata from ufbx shape names without actual deformable-mesh channel import.
- **glTF embedded mesh LODs:** `ModelGltf.cpp::loadMeshes` explicitly rejects morph-bearing built-in `extras.rtsLods` source and target meshes. Stage 167 does NOT lift this restriction or provide native engine mesh-LOD morph deformation. The generic retarget API targets independently imported source/visual Models.
- `extras.targetNames` is ecosystem metadata, not guaranteed by the core glTF specification. It must be authored by the exporter.
- Existing CPU morph buffer upload/release, static batching, native Graph sampling, parent/child lifetime, backend GPU and resource fencing are unchanged.
- The new MeshDef member can affect aggregate source compatibility and should be checked during the final native build; no runtime ABI compatibility guarantee is claimed.
- No synchronous token-safe provider shutdown, advanced twist IK/rig retarget, scene-loader integration or crowd perf result is claimed.

## Verification

**ACTUAL:** GitHub-connected source audit of `Model.h`, `ModelGltf.cpp`, `ModelFbx.cpp`, `ModelPoseRetarget.h`, `Json.h` and Stage 166 handoff; SHA-guarded GitHub commits for the three source files.

**NOT RUN:** Mac-native C++ compiler/link/CMake/CTest, real glTF imports, malformed `extras.targetNames` tests, FBX morph, GPU render/Metal/Vulkan/GLES, ASan/TSan/UBSan, perf/VRAM/fences. Source implementation cannot be described as tested or production-ready.

## Final acceptance cases

- glTF mesh with `extras.targetNames=["BlinkLeft","Smile"]` and visual mesh reordered `["Smile","BlinkLeft"]` yields explicit remap; output weights match semantics.
- glTF with morph targets but no metadata retains unavailable names; API fails closed when morph channel mapping is needed.
- Duplicate/empty/non-string target names, malformed per-primitive counts and too many channels are rejected by the loader.
- A mesh shared across multiple visual nodes supplies the same ordered semantic table; each node still uses its own validated source-to-target bone mapping.
- Model asset hot reload produces new metadata and a new compiled plan bound to source/visual publication epochs.
- Native compilation across all platform backends, presentation/skeleton visuals, crowd allocation/perf checks.

## Readiness

| Subsystem | Source status | Remaining |
|---|---|---|
| Universal ECS MotionWeave/AnimationGraph | Incremental source integrated | Full native regression |
| Independent animated child sources | Stage 161–164 source | Native multi-instance/skin and reload |
| Generic rig retarget safety | Stage 165 source | Complex rigs and perf |
| Named morph semantic plan | Stage 166 source | Native mapping/crowd regression |
| glTF extras.targetNames metadata import | Stage 167 source | Real GLB/GLTF fixtures |
| Universal Model → LOD authoring morph names | Stage 167 source | Editor/asset authoring UX |
| FBX morph delta+semantic import | NOT IMPLEMENTED | Actual ufbx blendshape support |
| glTF built-in morph LOD chains | NOT IMPLEMENTED | GPU/mesh deformation support |
| SceneLoadCoordinator adapter | NOT INTEGRATED | Peer branch review |
| Full GPU-token provider retirement | NOT IMPLEMENTED | Shared texture/cache/rollback/fence proof |
| Complete CMake/CTest/Metal/Vulkan/GLES/sanitizers | NOT RUN | Final user-requested Mac campaign |

## Next Stage 168

Prioritize real FBX blendshape/morph delta import (or explain/guard unsupported cases), including correct shape ordering and animation integration, without inventing semantics; alternatively close built-in glTF morph LOD support with proven GPU skinning. Then integrate authoring UX, scene lifecycle and full GPU resource retirement before coordinated native tests. Add a separate handoff and updated complete readiness table after each stage.
