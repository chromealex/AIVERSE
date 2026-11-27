# Display-space additive particles — stage 1

Source SHA: a91527be52fdc112f7c6f273696e5759ca41a109
Branch: codex/issue223-minimap-atlas-20261126

Renderer3D.addDisplayDraw queues an output-pass callback after the existing PRISMA resolve. Original scene depth is borrowed read-only; output size is passed for normalized depth sampling. Registration returns false if there is no active postprocessor/depth. Callbacks clear at begin/reset/shutdown and move out before resolve, preventing accidental replay. No added tone mapping/compositor owner.

FxBlendSpace Linear/Display is authored per emitter and trail, with trail inheritance and explicit override; only additive display blending is accepted. Serialization and trail equivalence preserve the setting. Tests add emitter/trail roundtrip and inheritance cases.

Changed paths: engine/render/Renderer3D.{h,cpp}, engine/fx/FxEffect.{h,cpp}, tests/tests.cpp, docs/prisma.md, docs/spectrafx.md, manifest, roadmap/policy. git diff --check passed; compilation/tests/GPU NOT RUN per final-build request. Data is NOT connected to rendering yet; do not ship or close #221 from this stage.

Next: extend CPU batch key with blendSpace, queue display geometry without drawing it into HDR, implement GLSL/MSL display draw shaders with original depth occlusion and fog, and register renderDisplay via addDisplayDraw from all hosts (client/editor/LENS). Preserve default and LDR behavior; ensure GPU-owned emitters support the same request or route explicitly to CPU at admission, not double simulate. Audit shader-graph particles, output scale/flip and depth lifetime through PRISMA. Add actual GPU pixel/capture regression before final acceptance.

## Stage 2 — 7f0bb23bd17038e48b124ccfbb7f8abb49fa2b25

Added FxDisplayShaders.h GLSL/MSL source: encoded colour, distance fog on alpha, normalized original-scene-depth rejection. FxInternal batches include blendSpace; ParticleSystem batch creation propagates it for base texture, sprite frames and trails. render(fog,deferDisplay) keeps a separate run list over the one existing VB upload; renderDisplay replays it after resolve and clears runs. New pipeline/shader resources use existing retirement callbacks at shutdown. No second simulation or geometry expansion.

Paths: engine/fx/FxInternal.h, FxDisplayShaders.h, FxRender.cpp, ParticleSystem.cpp, ParticleSystem.h, manifest. git diff --check passed; builds/tests/GPU NOT RUN. Hosts still use default deferDisplay=false, so feature is not active yet. Shader graph display variants are also pending; the new renderer currently samples only base texture and must not be shipped as graph-complete.

Next: add shared render-with-Renderer3D helper to register callback and defer atomically; wire ClientFx.cpp (two paths), Editor.cpp (two paths), SpectraFxSceneHost and previews/LENS; audit GPU admission (display request cannot be silently linear). Preserve graph-generated colour and extra texture bindings in display path. Check GL/Metal/Vulkan depth orientation, viewport scale, MSAA depth availability and shader output path. Add real GPU brightness/occlusion regression and diagnostics before claiming #221.

## Stage 3 — 6865c0fb288a4e4c35958f10cde28554f254905b

Shared ParticleSystem::renderScene registers deferred output callback and calls render(fog,defer) without changing simulation. ClientFx main/prepared LENS views, Editor scene/game views and optional SpectraFxSceneHost renderer adapter use it. GPU graph admission reports display blend as CPU presentation, preserving mode instead of silently running linear GPU sprites.

Particle batches retain distinct normal and display shader/pipeline pairs. Display graph variants use the existing graph compiler outputs, source textures plus borrowed depth at the next texture slot, graph vertex data/time/world and normalized screen coordinates. Found existing MSL graph substitution targeted obsolete text and appended a duplicate uniform parameter: replaced fragment body explicitly and kept one uniform binding. All newly owned shaders/pipelines retire through the existing retirement path.

Source paths: apps/client/ClientFx.{h,cpp}, GameClient.cpp; apps/editor/Editor.cpp and windows/SpectraFxGpuAssetsWindow.cpp; engine/fx/FxInternal.h, FxRender.cpp, ParticleSystem.{h,cpp}, SpectraFxSceneHost.h, SpectraGraphRuntime.cpp; manifest. git diff --check passed. No builds, tests, shader compilation or GPU captures run.

Still required before acceptance: graph pool/volume/inline batch adapters still call plain render; wire same renderer through them and native hosts (including actor frontend). Preview-only LDR paths need confirm no HDR pipeline. Add graph source/gpu regression (normal/display, with/without graph textures), GPU admission tests, frame-depth lifetime and multi-view replay checks. Current renderer default rejects callback when no post/depth and uses ordinary LDR path. Check callback sizing/texture format and no unnecessary empty callbacks. Compile once at end per user, then fix all real failures and execute gate.
