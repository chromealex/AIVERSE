# AIVERSE MotionWeave / Universal View — Stage 158 handoff

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — kept OPEN / DRAFT / unmerged
Previous: [Stage 157](animation-stage157-modelview-atomic-model-file-reimport-2026-11-20.md)

## User requirements, scope and decision

All MotionWeave animation graph/ECS, view, retargeting and shared attachment APIs remain **entirely genre-independent**. Per presented animation source there is precisely ONE graph/pose writer; any host-computed pose is submitted as `HostPrecomputed`. Every completed implementation stage needs a **separate handoff** and a **user-visible readiness table**. User asked to complete functionality first and run ONE comprehensive native Mac compiler/CTest/GPU/fault/sanitizer suite later.

Stage 157 identified a crucial incomplete GPU resource feature: shutting down a `ModelAssetProvider` still requires a proven full-GPU-idle via BOTH its ModelResourceCache and the speculative image rollback pool. During Stage 158 source audit, `ModelResourceCache::shutdown` was confirmed to call `Device::waitForResourceIdle()`, `ModelImageRollbackPool::drain` calls `waitForResourceIdle()`, and shared texture lifetime is also owned by `ModelTextureCache`. **It would be unsafe and inaccurate to claim that polling a retirement token and then calling unchanged `shutdown()` implements nonblocking full-provider retirement.** True token-backed provider shutdown needs an explicit safely proven API covering resident models, rolled-back GPU textures, shared texture leases, pending cache retirement batches, worker join, CPU ModelReadLease holders and Device generation. No such shortcut was added and **true token retirement remains NOT IMPLEMENTED**.

Stage 158 therefore closes another concrete cross-host missing capability: **one genre-neutral CPU-only attachment layout document with fully validated affine transforms**, shared unchanged between AIVERSE Studio, ModelView and future scene/prefab authoring. This prevents incompatible duplicate JSON parsers and ensures authored socket local+child-space matrices reach the actual compiled runtime attachment. This is a genuine source feature, not a claim of a complete scene/prefab ECS importer.

## Exact Stage 158 code and documentation commits

1. `fa6d5667ed468d784ec9eb083ee696c5296df05d` — new `engine/view/AttachmentLayout.h`:
   - `view::AttachmentLayoutItem` stores `model`, `socket`, `enabled`, complete `render::mat4 local` and `render::mat4 childWorld`. `view::AttachmentLayout` stores optional `parent` and an ordered array of children.
   - `AttachmentLayoutCodec::read(Json,AttachmentLayout&)` is transactional: invalid objects/entries/matrices fail BEFORE changing existing `out`. Missing `local`/`childWorld` mean identity for backwards compatibility with Stage 154/155 files. Each supplied matrix must be exactly 16 numeric components (column-major), finite and within an explicit bound, with affine bottom row `[0,0,0,1]`. Non-affine/projective inputs or invalid shape reject the document.
   - `AttachmentLayoutCodec::write(AttachmentLayout)` serializes ordered child arrays and both full affine matrices after validation, preventing opaque GPU handles, raw runtime Model pointers or gameplay data from appearing in project documents. This is CPU-only authoring, no Graph clock, ECS simulation, GPU/Model ownership or renderer dependency beyond the `render::mat4` type.
2. `f016d19ae484149327eeaa1b77a3fc90fc23a470` — `apps/modelview/main.cpp`:
   - Extend its native `AttachmentSpec` to carry the same full `local` and `childWorld` matrices.
   - Switch previously duplicated handwritten attachment JSON load/save to shared `AttachmentLayoutCodec`, preserving any authored transform across roundtrips, while still accepting legacy minimal JSON.
   - Pass stored `local`/ `childWorld` to actual `ImportedAttachmentAuthoring::ChildRequest` for compiled socket composition and expose `Socket offset` X/Y/Z editing in the Attachments ImGui window.
3. `da19d85e3bb4ed96e6072b43e38d3c1429992783` — `apps/editor/windows/AttachmentAuthoringWindow.cpp`:
   - Extend Studio `AuthoredChild` with both affine matrices. Use the SAME `AttachmentLayoutCodec` for project-relative Load/Save JSON, and apply stored matrices to the actual compiled child request. Studio's existing project path sandbox and GPU resource owner constraints remain.
   - `Socket offset` X/Y/Z changes are editor-authored layout state, not an extra animated transform/graph owner.
4. `ff17371063470c5788e592f0e83124d52e3a0851` — Studio window advanced transform editor: edit the complete 3×3 basis of `local` and `childWorld`, plus the child-local translation. Supports authored rotations, nonuniform scale and shear without coercing to a lossy TRS representation; matrices are validated on serialization.
5. `2dc8d06db75defba789c6b5f2b67b27ad0f660d1` — `engine/view/ImportedAttachmentAuthoring.h`: new generic `ImportedAttachmentAuthoring::childFromLayout(layoutItem, publishedChildSnapshot)` converts a validated document entry into an actual native imported child request, applying both matrices, visibility and named socket, while retaining **only** the caller-supplied published child metadata (NOT CPU/GPU Model lifetime).
6. `29ab06b68a340db0b9e3329e9f3f06c743b6ee84` — ModelView uses shared `childFromLayout()` rather than duplicating transform field assignment.
7. `e900fa8274af656aea3fd60e44610e4393947184` — Studio uses shared `childFromLayout()` as well, eliminating differing runtime conversion semantics across authoring hosts.
8. `ecd222d35e14bca9602a8d5e28e50fbc4a474ed3` — `apps/modelview/main.cpp`: when launched with only `--attachments-file file.json`, ModelView parses its `parent` and opens the real source Model without requiring a separate parent CLI argument. Errors in malformed/missing JSON fail before rendering; actual complete attachment layout is parsed once during ModelView init.
9. `de2df586d92cb564f0a7acc2ef2fe313ce6cf39e` — [docs/attachment-layout.md](../attachment-layout.md): public matrix schema, JSON example, backward compatibility, ModelView command, ownership, explicit remaining scene/prefab runtime gap and verification limitations.

## Native API and layout example

```cpp
#include "engine/view/AttachmentLayout.h"
#include "engine/view/ImportedAttachmentAuthoring.h"

aiverse::view::AttachmentLayout document;
aiverse::view::AttachmentLayoutCodec::read(Json::parse(json), document);
Json serialized = aiverse::view::AttachmentLayoutCodec::write(document);

// The host has already resolved and published an actual imported child
// with a matching asset path and a valid owner ModelReadLease contract:
auto childRequest =
    aiverse::view::ImportedAttachmentAuthoring::childFromLayout(
        document.attachments.at(0), childAppearanceSnapshot);
```

The affine matrices use `render::mat4::m[col*4+row]`, in **column-major** order. The exact existing composition rule is:

```cpp
childDraw.world = parentDraw.world * socketModelSpace *
                  authored.local * childDraw.world;
```

Here `authored.childWorld` is stored in the child draw's own `world` field before composition. The name-based socket is resolved against the **source base skeleton**, not the currently chosen visual LOD. Missing matrix fields are identity. A serialized document NEVER contains a raw Model pointer, compiled bone index, ECS Entity handle, GPU texture/buffer handle, animation graph clock or gameplay behavior.

### Example JSON / Studio

```json
{
  "parent": "models/hero.glb",
  "attachments": [
    {
      "model": "models/lantern.glb",
      "socket": "Hand.R",
      "enabled": true,
      "local": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0.1,-0.03,0.05,1],
      "childWorld": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    }
  ]
}
```

```sh
aiverse_modelview --attachments-file assets/hero.attachments.json
```

Studio's Attachments tab and ModelView use this same document. Studio supports editing a socket position and full local/child basis vectors; ModelView exposes the simple offset and can read/write the full JSON.

## Safety and exactly what's NOT done

- Read rejects invalid matrices/entries before mutating output. Write also validates every matrix. A missing local transform is identity; a corrupted local transform is NOT silently reset.
- Relative paths and symlink traversal validation remain the responsibility of the calling Studio project host; the generic CPU codec does not embed filesystem access or a genre-specific scene path policy.
- Scene and prefab integrations can NOW store/reload these source-neutral attachment descriptions. **However, existing scene/prefab ECS serialization has NOT been changed to automatically spawn, save, resolve, destroy, or animate attachment entities.** Do not mark full native scene/prefab runtime integration complete.
- Animated child with its own Graph still needs an independent source ID and a separate single-writer animation owner; the imported *static* child attachment helper continues rejecting children with graphs.
- Actual GPU token-driven full ModelAssetProvider release remains unfinished, as audited above. No false claims of nonblocking image/texture/resource-cache shutdown. Studio/ModelView's retained provider generations still have full-device-idle fallback and possible peak VRAM cost.
- No gameplay or genre module, network protocol, authoritative simulation logic, model importer or GPU backend was modified in this stage.

## Actually checked vs NOT RUN

**ACTUAL:** connected GitHub readback of `engine/view/AttachmentLayout.h`, `ImportedAttachmentAuthoring.h`, updated ModelView and Studio window source, `ModelResourceCache::shutdown`, `ModelAssetProvider::shutdown`, `ModelImageRollbackPool` GPU retirement, `ModelTextureCache` lifetime and Json/Math3D interfaces. Confirmed the new shared codec and runtime mapping APIs exist in all intended files, that Studio/ModelView both call `AttachmentLayoutCodec` and `childFromLayout`, and that ModelView can derive parent path from saved JSON. Exact source SHAs are listed above; no main or other-agent branches modified.

**NOT RUN:** native C++/CMake configure or compile/link, fresh passing CI for current head, CTest, interactive Studio or ModelView, actual save/load roundtrip, negative malformed-matrix tests, Metal/Vulkan/GLES draw/target retirement, GPU token failure injection, ASan/TSan/UBSan, crowd/memory/VRAM profiling, complete `bash tools/animation/validate-final.sh`. This is **committed source, not confirmed runtime correctness**.

## Required readiness matrix at Stage 158

| Subsystem | Source status | Still required |
|---|---|---|
| MotionWeave genre-neutral ECS and AnimationGraph | Implemented source | Native graph/worker/pose tests |
| Named graph hot reload / portable phases | Implemented source | Epoch / failed reimport regression |
| Source ID / Model publication epoch | Implemented source | End-to-end reuse/runtime validation |
| Whole-model Animation LOD | Core source implemented | Complex rigs and performance |
| Retargeting / morph target mapping | Partial | Advanced importer and Studio authoring |
| Compiled socket placement / real static child Models | Implemented source | GPU/visual and lease lifetime tests |
| Shared JSON attachment layout with full affine transforms | **Stage 158 source implemented** | Native compile, roundtrip and malformed input tests |
| Common runtime binding of authored transforms | **Stage 158 source implemented** | Real GPU scene parity |
| ModelView file hot reload and attachment UI | Source implemented | Disk failure/visual tests |
| Studio 3D attachments preview and affine UI | Source implemented | Native GUI, model/pixel tests |
| Actual scene/prefab ECS instance serialization | NOT IMPLEMENTED | Spawn/despawn/resolve/restore integration |
| Genuine GPU-token ModelAssetProvider retirement | NOT IMPLEMENTED | Complete shared texture/cache/rollback safe API |
| Metal/Vulkan/GLES GPU | NOT VERIFIED | Backend visual and lifetime tests |
| Full CMake/CTest/ASan/TSan/benchmarks | NOT RUN | One final coordinated Mac pass |

## Next Stage 159

1. Continue true genre-neutral scene/prefab runtime attachment restoration: resolve compiled child sources with source publication epochs, write real ECS entity association and model lifetime rules without adding game/genre components to animation core. Keep animated children as distinct identified sources, not double graph writers.
2. Design **genuine token-complete ModelAssetProvider retirement**, accounting for ModelResourceCache resident GPU models, shared ModelTextureCache, speculative ModelImageRollbackPool, pending GPU-fenced retirements and owner-thread ModelReadLease; if safe API cannot yet be implemented, do not bypass existing full idle checks or claim zero-stall.
3. Continue skeleton retarget/morph and LOD/editor authoring parity, then after all features close do the user's ONE full native Mac validation pass with compiler/CTest/backends/sanitizers/VRAM/perf. Fix failures before requesting PR #22 merge to main.
4. After each next stage: independent handoff with exact SHAs/test evidence, append to AGENTS.md/chronological journal and show user a progress table.
