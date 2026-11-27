# Display particle host and graph integration

## Stage 3 — 6865c0fb288a4e4c35958f10cde28554f254905b

Shared ParticleSystem::renderScene registers deferred output callback and calls render(fog,defer) without changing simulation. ClientFx main/prepared LENS views, Editor scene/game views and optional SpectraFxSceneHost renderer adapter use it. GPU graph admission reports display blend as CPU presentation, preserving mode instead of silently running linear GPU sprites.

Particle batches retain distinct normal and display shader/pipeline pairs. Display graph variants use the existing graph compiler outputs, source textures plus borrowed depth at the next texture slot, graph vertex data/time/world and normalized screen coordinates. Found existing MSL graph substitution targeted obsolete text and appended a duplicate uniform parameter: replaced fragment body explicitly and kept one uniform binding. All newly owned shaders/pipelines retire through the existing retirement path.

Source paths: apps/client/ClientFx.{h,cpp}, GameClient.cpp; apps/editor/Editor.cpp and windows/SpectraFxGpuAssetsWindow.cpp; engine/fx/FxInternal.h, FxRender.cpp, ParticleSystem.{h,cpp}, SpectraFxSceneHost.h, SpectraGraphRuntime.cpp; manifest. git diff --check passed. No builds, tests, shader compilation or GPU captures run.

Still required before acceptance: graph pool/volume/inline batch adapters still call plain render; wire same renderer through them and native hosts (including actor frontend). Preview-only LDR paths need confirm no HDR pipeline. Add graph source/gpu regression (normal/display, with/without graph textures), GPU admission tests, frame-depth lifetime and multi-view replay checks. Current renderer default rejects callback when no post/depth and uses ordinary LDR path. Check callback sizing/texture format and no unnecessary empty callbacks. Compile once at end per user, then fix all real failures and execute gate.
