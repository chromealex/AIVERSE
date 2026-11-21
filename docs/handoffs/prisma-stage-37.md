# PRISMA Stage 37 — universal GPU-fence retirement for shader/pipeline/buffer and ShaderGraph/VFX hot reload

**Date:** 2026-11-19.
**Working branch:** `feature/prisma-postfx`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26.
**Previous handoff:** `docs/handoffs/prisma-stage-36.md`.
**Main:** unchanged; no merges from other agents' feature branches.

## Work completed / exact source commits

1. `35407e97fb54dd61382f10a516f888976f557989` — **new** `engine/render/GpuResourceRetirement.h`: genre-independent graphics-thread lifetime owner for `gfx::PipelineHandle`, `ShaderHandle`, `BufferHandle`, `TextureHandle`, `ComputeHandle` and complete owner bundles. Uses `Device::gpuSubmissionFence` and `gpuCompletedFence`, quarantines token=0, releases pipelines before shaders, and never treats CPU frame index as completion. Provides counts and optional known bytes.
2. `466f13ac07ee304b5b18e59fbf92e8f662e82b73` — `apps/editor/Editor.h`: Editor-owned generic retirement manager and public getter. This owner outlives editor windows, including ShaderGraph/FX previews.
3. `678d3879f201f8ba5f635bc780f7c255052834c8` — `apps/editor/Editor.cpp`: collect generic GPU resources every frame, integrate into known-pending memory pressure and drain only after a verified GPU idle when necessary or during owner teardown.
4. `ff69a952eba6a7a4fe6aa115a41d46e36ce63880` — `apps/editor/windows/ShaderGraphPreview.h`: expose active GPU device and `shutdownAfterGpuCompletion()`. The latter releases the complete preview at the known safe point, avoiding secondary retirement of its own color/depth and taking care of Portrait/PRISMA/FX/Cache/shaders/buffers together.
5. `28d30bae4913abe687ad4a5f183445c201fc9812` — `apps/editor/windows/ShaderGraphWindow.cpp`: change preview ownership from an inline object to a shared owner bundle, retire the previous bundle on asset/project/domain changes or GPU-relevant graph edits, and create a fresh preview separately. Deferred bundle survives ShaderGraphWindow closing and destruction. Includes a GPU-only graph signature.
6. `c2e4d53d1160df4bb1a8de73e8f7fc955abdc7a9` — `engine/fx/ParticleSystem.h`: optional `setGpuResourceRetirement(render::GpuResourceRetirement*)` hook for editor FX hot reload; no runtime/ECS dependency.
7. `673cec7e9661e665c52e063630eaadc6e730de54` — `engine/fx/ParticleSystem.cpp`: old graph variant shaders/pipelines in `reloadAll` are fence-retired if a GPU owner is supplied. Shutdown also uses deferred release for particle main shader variants, reactive pipelines, vertex/index buffers and built-in atlas. The old immediate path remains for clients with no owner.
8. `59d1df19ead3b3f2f375dcafdb4ca8deea143324` — `engine/render/TextureCache.h`: optional GPU retirement owner hook for editor-owned reimportable texture caches.
9. `2bca613207703ab19af1f56ea2e39d5a32ba6c6b` — `engine/render/TextureCache.cpp`: deduplicate actual owned texture handles (including atlased pages) and fence-retire each once on hot reload/shutdown, preserving the nonopted-in legacy owner path.
10. `16ec77b6f92b8fff81c2cf84dec5d8460237ea10` — `apps/editor/Editor.cpp`: opt the editor main `gameFx_` ParticleSystem and its `fxTextures_` cache into this generic GPU owner, before FX initialize/reload.
11. `3b17b07e58c94bff753b3ffaf44416a126584c85` — `apps/editor/windows/FxWindow.cpp`: opt FX preview ParticleSystem and TextureCache into the same Editor-owned retirement service.
12. `8ae32335178198665082777769fd2fbb6371abc8` — `apps/editor/windows/StatsWindow.cpp`: show waiting pipeline/shader/buffer/texture/bundle resource counts and fenced/unfenced entries alongside existing PRISMA memory diagnostics.
13. `c03f30f3d5df9427aef2caff910b1c953818223e` — `apps/editor/windows/ShaderGraphPreview.h`: `ShaderGraphPreview::resourceKey(graph)` excludes the editor-only node `position` fields so dragging nodes does not repeatedly recreate GPU pipelines/materials.
14. `e01364040b7969ac02bb2d123d7a629581c75945` — `apps/editor/windows/ShaderGraphWindow.cpp`: reuse the same GPU-relevant signature when choosing whether to retire the entire preview bundle.
15. `a939e7561ecc88a22dd0337a5c51559b2916563d` — `apps/editor/Editor.cpp`: more than eight pending whole-preview bundles triggers an attempt at a **real GPU-idle pre-render safe point**, independent of unknown bundle byte counts. A bundle count is pressure, never proof of completion.
16. `f66a4855480e4772c04b2cbe2aa533ca16edfd0d` — `docs/prisma.md`: Stage 37 architecture, API, owner scopes, GPU dependency order and actual unrun validation status.
17. **This handoff** — `docs/handoffs/prisma-stage-37.md`, committed separately after source/docs.

## Runtime contract and ownership

`render::GpuResourceRetirement` is a reusable graphics-owner-thread queue. Its typed `retire(device,handle)` overloads detach owned RHI handles from their callers; `retireOwned(device,cleanup)` captures an entire resource-owning bundle for a callback that is invoked only when its device's GPU fence completes. `collect(device)` must run on the owning graphics thread, polling `gpuCompletedFence` without a GPU wait. `drainAfterGpuIdle(device)` is valid only after explicit backend proof of graphics-queue idle and may release entries with a zero submission token. All resource ordering is pipeline -> compute -> buffer -> texture -> shader -> owner bundles, while complete bundles manage their own internal dependency ordering.

**No borrowed handles** are to be transferred to the queue. TextureCache removes duplicate atlas-page/cached references. ShaderGraphPreview captures all of its owned resources together instead of individually retiring the same texture twice. Its `shutdownAfterGpuCompletion()` suppresses nested color/depth re-retirement so the complete bundle can release immediately after its fence.

The ShaderGraph window's UI does not own resources across a GPU hot reload: previous `ShaderGraphPreview` stays referenced by the Editor's pending bundle callback, while the visible window instantiates a new preview. Graph node positions are purely visual and excluded from GPU-key invalidation; the actual material/shader/particle graph changes still retire old bundles and rebuild new ones. Closing a window similarly transfers GPU lifetime ownership to the central Editor manager.

Editor core `gameFx_`, `fxTextures_` and FX Preview now use opt-in retirement hooks; native/runtime game clients without the owner retain previous shutdown ownership. There are no RTS/FPS specific code paths in `GpuResourceRetirement`. Gameplay ECS, authoritative tick state, postfx JSON, MotionBlur experimental flags, water and particle rendering semantics are unchanged.

## Actual verification

**Performed:** connected-GitHub fetches/source review of shader hot reload paths, ShaderGraphWindow/Preview, ParticleSystem/FX editor, TextureCache, Editor lifecycle, and graphics Device API; implemented and committed changes; re-fetched eleven affected source files to verify new calls/declarations. Corrected UI-only graph node-position invalidation to avoid per-drag-frame GPU bundle replacement.

**NOT RUN** under user's continuing features-before-tests direction: CMake native build, C++/Objective-C++ compile, CTest, Metal/GL/Vulkan GPU runtime, preview material/particle hot reload smoke, FX reload playback, framebuffer parity, GPU fence signal or memory profiling, CI. Do NOT claim compilation success or GPU safety verified on hardware.

## Known limitations and residual safety requirements

- This is an **opt-in ownership mechanism**. Native modules outside Editor's opted-in FX and ShaderGraph Preview retain their existing manual owner policies. The queue itself does not intercept arbitrary RHI `destroyShader`/`destroyBuffer` calls.
- `std::function` bundle cleanup captures a shared owner of the complete previous preview. It must not capture ShaderGraphWindow/Editor raw pointers; current preview bundle cleanup uses its own state and GPU Device. The service and GPU Device must outlive queued cleanups.
- A backend with zero/unavailable GPU fence cannot destroy uncommitted resources safely in a normal frame. Pending work can accumulate until a real GPU-idle drain; the preview bundle count threshold triggers **attempted** synchronization, not guaranteed memory release. WebGL still lacks a blocking submitted-work idle implementation.
- GPU bundle `knownBytes` is optional and does not include all nested owned resources. The 128 MiB total pending estimate remains a lower bound; the >8 archived preview threshold handles this blind spot.
- ShaderGraph preview changes now allocate a fresh bundled preview rather than reusing potentially GPU-busy shader/pipeline assets. This can temporarily increase VRAM during rapid true shader parameter editing, but prevents early use-after-free.
- Shader Graph Preview's UI resources are scoped to the Editor graphics owner; use this model for other preview windows before adding cross-device sharing. Do not compare GPU completion tokens from independent devices.
- The generic service intentionally does not perform GPU waits in `retire`, `collect` or callbacks; pressure waiting stays in `Editor::frame` before new render submissions.
- The earlier renderer/Prisma target retirements still use their own texture-only retirement queues; unification into the generic queue can be staged later without risking duplicate frees.
- No C++/Metal compiler or GPU has verified the source yet. Potential pointer/lifetime issues at backend/device teardown remain bounded by the documented host-required GPU-idle contract.

## Next feature Stage 38 (tests still deferred until user permits)

1. Apply the common lifetime service to remaining live editor preview owners (`RtsPortrait`, FX mesh caches, SpriteBatch paths and any ShaderGraph renderer bypassing ShaderGraphWindow), respecting source buffer/texture ownership and avoiding duplicate frees.
2. Add a per-device persistent resource registry and tracked size/format metadata for buffers/pipelines where supported; improve pending VRAM diagnostics without GPU readback or significant overhead.
3. Improve device-lost, shutdown and GL/WebGL0-fence behavior with explicit queue abandonment and owner-state detachment policies, so pending bundle callbacks cannot dereference a destroyed gfx::Device.
4. Verify backend GPU timeline on actual Metal, GL33/GLES3/WebGL2 and Vulkan; real fence-completion ordering must be tested before declaring production safety.
5. When the user finally permits tests, run targeted builds and smoke: ShaderGraph node drag, rapid undo/redo, domain change, FX import/reload, Scene/Game simultaneous render, editor shutdown with pending screenshot, VRAM accumulation/resolution, and CI. Record actual PASS/FAIL.
6. After any further feature stage commit `docs/handoffs/prisma-stage-38.md` detailing files, exact SHAs, API, actual tests and next concrete step. Never merge PR #26 into main without explicit request.
