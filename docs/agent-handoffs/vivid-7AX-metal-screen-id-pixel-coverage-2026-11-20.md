# VIVID Stage 7AX — same-frame real Metal Renderer3D colour / actor GPU-ID pixel coverage

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Working branch: `feature/vivid-digital-humans`
Base before this stage: `3b23f0fce1fe20d4246878d89b2b67f166bc0f24`
No write to `main`, Texture RHI, MotionWeave/animation or separate rendering/batching branches; no cross-branch merge.

## Scope and precise result

Stage 7AW showed that one eligible Renderer3D main-colour indexed draw was **encoded**, but no final screen pixels were compared with the VIVID GPU ID pass. Stage 7AX adds an executable native Metal verification source that correlates an actual presented Renderer3D colour pixel with the separate true-GPU-60B-actor-stream ID pixel from the **same render frame and projected viewport location**. This is a **single-pixel coverage oracle**, not yet numeric depth/colour shader parity.

The existing production `aiverse_vivid_renderer_preview` now:
1. Initializes the optional real `SculptGpuIdPass` and requires the Metal actual GPU vertex-stream compute program for the one-actor preview. The 12-actor crowd preview stays unchanged and does not opt into the one-actor sample.
2. During the morph-stage and MotionWeave-motion-stage screenshot frames, requires valid, same-frame **accepted** Renderer3D direct main-colour primitive provenance, the exact borrowed GPU `ModelDraw::vertexOverride` handle and the selected source world matrix. It derives one projected screen pixel at the procedural head centre from the actual Renderer3D `viewProj` and top-left viewport coordinates.
3. Requests the *normal renderer's presented frame* through `gfx::Device::captureNextFrame` and then, after ending the normal colour pass, records the separate GPU ID compute/draw/readback on the same source frame and pixel. The ID pass reads the actor's actual GPU-deformed 60-byte `ModelVertex` position; the previously produced CPU surface is used for source topology metadata, not a second position stream.
4. Correlates the screenshot callback and the ID callback in arbitrary completion order using stage, framebuffer coordinates and GPU request frame serial. It rejects missing/empty readback, stale GPU ID token, absence of `actualOverride`, duplicate request or missing colour data; outstanding GPU ID jobs are serialized. Final success requires the legacy three screenshot differences **and both** actual colour/ID sample outcomes. The GPU ID pass is retired only after real device-idle proof on shutdown.
5. Uses pure `SculptGpuColorPixelOracle.h` to classify *coverage*: selected nonzero ID + colour significantly different from the known clear colour = `Covered`; selected ID + clear colour = `Mismatch`; clear/clear = `ClearBackground`; coloured pixel with ID=0 = `Inconclusive`, because an unrelated occluder may legitimately be drawn. Do not interpret the result as numerical colour or depth equality.

A second, separate native Metal test fixture `vivid_metal_sculpt_id` now tests a **nearer independent sampled alpha-mask zero-ID occluder**, with both alpha-hole and opaque texels. The hole must preserve the selected source ID, while the solid texel must block it via hardware depth. This negative test uses the *actual sampled Metal ID pass* but an occluder whose positions are CPU-staged. It is **not** proof that a separately moving GPU-deformed Renderer3D colour occluder matches the ID pass.

The existing `tools/vivid-validate.sh` checks for both positive stage=1/stage=2 GPU colour/ID coverage lines in the real one-actor renderer preview log. The pure C++ fixture tests all four classification outcomes including inconclusive unrelated-occluder coverage.

## Code and documentation commits

| Commit SHA | Path | Changes |
|---|---|---|
| `83d662f3113cf858dc115e506dbd52d0521bac0e` | `tests/vivid/renderer_preview.cpp` | Adds same-presented-frame screenshot pixel and actual 60B GPU actor ID readback, stage/frame token matching, two positive morph/motion probes, automatic Metal failure, and verified-idle lifetime retirement. |
| `93d18a7cd4ff5c7b772fe84f317066ef853899b3` | same | Ensures callbacks never throw during asynchronous screenshot failure and serializes single outstanding GPU pixel jobs. |
| `ebb7dd3e854ae49f824276601e05351cc485750a` | `engine/character/vivid/SculptGpuColorPixelOracle.h` (new) | Portable, genre-independent, header-only C++20 coverage classification. No RHI/ECS or persistent asset changes. |
| `ddd9ecd134e45e7c293d9f79dbe1ae01c8747803` | `tests/vivid/renderer_preview.cpp` | Reuses the pure coverage oracle rather than hardcoded inline colour arithmetic. |
| `741b7a889129663e46023f653ec48e446f54e3de`, `cd469962997186a4647838749cd55b5e1ed3040a` | `tests/vivid/human_creator.cpp` | UNEXECUTED unit fixtures for Covered, Mismatch, ClearBackground, Inconclusive and invalid thresholds; fixes C++ fixture namespace. |
| `007bb2e9597d0dfb7ae6e17d2fa7dbd3910c306d` | `tests/vivid/metal_sculpt_id.mm` | UNEXECUTED real-Metal ID/depth fixture for sampled, unrelated nearer zero-ID Mask occluder: hole preserves ID, solid blocks. |
| `d1bfba67085f97c908901c0705f6f96b3d2724e0` | `tools/vivid-validate.sh` | Requires both native one-actor colour/ID coverage log lines when GUI acceptance runs. |
| `77d03063e0a4b304a1434c22419c4308ca4039c6` | `docs/vivid.md` | Functional status, intended pixel semantics and open GPU depth parity work. |
| `35fc217c9a5371463d1ac6e576364b74207f04c2` | `tools/vivid-creator/README.md` | GUI acceptance usage, exact unsupported parity cases. |

No modifications to `engine/render/Renderer3D*`, `engine/gfx/*`, `ModelVertex` 60B ABI, HumanDNA JSON, 52 facial channels, MotionWeave animation graph, authoritative ECS state or genre modules during 7AX. The existing Renderer3D accepted source provenance (7AW), VIVID GPU alpha-mask path (7AU), RHI async readback and source-vertex ownership are reused.

## Actual verification, commands, outcomes

**Performed:** Remote GitHub connector file reads/writes and commits. A source-only 17-condition static contract audit inspected the actual updated header, native Metal preview, native Metal ID test, shared `SculptGpuIdPass`, `SculptGpuSubmittedView` and validation runner: GPU actor source handle used, real screen capture, ID frame serial, dual asynchronous completion, wrong-frame rejection, explicit Metal compute, near Mask alpha/depth negative test, async retirement, and runner log assertions. All 17 source inspections matched expected textual contracts. This is **not** compilation or runtime GPU execution.

**NOT RUN:** native CMake configure, C++/ObjC++ compilation, native Metal shader compilation, `vivid_human_creator` CTest, `vivid_metal_sculpt_id` CTest, the GUI `aiverse_vivid_renderer_preview` executable, any real GPU screenshot or readback, texture-alpha/depth pixel results, RHI fence timing, Vulkan/GLES tests, performance/VRAM measurements or full `bash tools/vivid-validate.sh --full`. This tool session has GitHub access but not the user's macOS Metal GPU build host. New fixtures are executable definitions, **not measured PASS results**. Compilation, pipeline readiness and sample thresholds require real Mac confirmation.

Deferred native acceptance from the exact stage HEAD, after all features are ready:
```sh
git fetch origin
git switch feature/vivid-digital-humans
git pull --ff-only
cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-onepass --target \
  aiverse_vivid_human_creator_tests \
  aiverse_vivid_metal_sculpt_id_tests \
  aiverse_vivid_renderer_preview --parallel 4
ctest --test-dir build/vivid-onepass --output-on-failure \
  -R '^(vivid_human_creator|vivid_metal_sculpt_id)$'
bash tools/vivid-validate.sh --full
```
The GUI runner checks screenshot assets and both `GPU colour/ID coverage stage=...` lines. Use the exact commit, full logs and native screenshot/pixel values to debug any failure.

## Exact next task and remaining limitations (Stage 7AY)

1. **Final colour+depth oracle:** Build a renderer-owned main colour + depth target parity test with a *separately GPU-deformed* overlapping alpha-mask occluder, both under the actual Renderer3D draw/LOD/shader and ID paths. The present colour probe only proves a rendered actor's centre pixel visibly differs from clear while its native GPU source produces a nonzero ID. The nearer alpha occluder is currently tested in the ID pass alone, using CPU-staged triangle positions.
2. **Depth API:** `gfx::Device::readTexturePixelAsync` only supports linear RGBA8 colour pixel readback; it cannot read arbitrary depth. For numeric depth equality, introduce a renderer-owned RHI-safe depth encoding/copy or GPU pass using the *same* depth attachment. Never fabricate a CPU depth sample from projection and label it GPU parity.
3. **Hardware QA:** Validate actual Metal screen pixel top-left orientation, callback frame correlation, threshold 36 versus real framebuffer gamma, sample position under MotionWeave rotation, pipeline compile/warmup and the Creator pass-suspend/resume/1px scissor lifecycle. Fix native test or shading failures before claiming parity.
4. **Remaining renderer cases:** indirect/GPU ICB/MDI, dynamically rendered unrelated hair/occluders, ShaderGraph displacement, cornea/Blend/depth-after, selected alternative LOD, final postfx output and per-primitive material lifetime remain unsupported exact GPU ID/colour parity. Keep CPU BVH authoring authoritative on fail-closed paths.
5. **Cross-platform:** Vulkan/GLES3 still have CPU-fed diagnostic ID, not genuine 60B actor GPU source expansion. Implement Vulkan native GPU stream support and GLES3 vertex fetching only with source ownership and backend/fence proof. WebGL2/Windows may keep CPU fallback.
6. **Merge safety:** Shared Renderer3D source submissions from 7AW and overlapping Texture RHI/batching work must be merged semantically on a separate integration worktree. Never overwrite other agent changes or merge straight to main without native acceptance. Continue independent handoff commits after every bounded stage.

Next agent: read `AGENTS.md`, `engine/character/vivid/AGENTS.md`, `docs/vivid.md`, `docs/agent-handoffs/vivid.md` and this file. Record exact implementation SHAs, API/assets, executed tests or NOT RUN, limitations, and next step on each stage.
