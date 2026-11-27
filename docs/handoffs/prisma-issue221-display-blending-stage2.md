# Display blending geometry replay

## Stage 2 — 7f0bb23bd17038e48b124ccfbb7f8abb49fa2b25

Added FxDisplayShaders.h GLSL/MSL source: encoded colour, distance fog on alpha, normalized original-scene-depth rejection. FxInternal batches include blendSpace; ParticleSystem batch creation propagates it for base texture, sprite frames and trails. render(fog,deferDisplay) keeps a separate run list over the one existing VB upload; renderDisplay replays it after resolve and clears runs. New pipeline/shader resources use existing retirement callbacks at shutdown. No second simulation or geometry expansion.

Paths: engine/fx/FxInternal.h, FxDisplayShaders.h, FxRender.cpp, ParticleSystem.cpp, ParticleSystem.h, manifest. git diff --check passed; builds/tests/GPU NOT RUN. Hosts still use default deferDisplay=false, so feature is not active yet. Shader graph display variants are also pending; the new renderer currently samples only base texture and must not be shipped as graph-complete.

Next: add shared render-with-Renderer3D helper to register callback and defer atomically; wire ClientFx.cpp (two paths), Editor.cpp (two paths), SpectraFxSceneHost and previews/LENS; audit GPU admission (display request cannot be silently linear). Preserve graph-generated colour and extra texture bindings in display path. Check GL/Metal/Vulkan depth orientation, viewport scale, MSAA depth availability and shader output path. Add real GPU brightness/occlusion regression and diagnostics before claiming #221.
