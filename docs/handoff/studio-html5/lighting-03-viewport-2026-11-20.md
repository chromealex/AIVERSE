# Lighting Stage 27 — live Studio Metal preview

2026-11-20, integration/lighting-main-stage1-20261120.
Code 5bcc70c846e4d66910f7b16248937387d9d94712.
DynamicGiHost/LumaHost moved to engine/render; bench aliases preserve callers.
RenderLightingRuntime composes the same ownership/fence policy for native hosts.
Studio consumes renderLighting, rebuilds immutable PBR preview geometry on document
revision/project changes, binds GI, composes Luma before PRISMA HDR, and reports
actual backend/convergence/fallback. GPU resource retirement precedes release;
frame scope closes recording on exceptions. Material preview clears GI overlay.
RTS/FPS document proxy geometry remains a PREVIEW (terrain/proxies, not full game
assets/animation). Direct capacity fallback is explicit, forced GPU GI rejects.
Authored source adapter optional limit avoids truncating Luma to legacy1024;
standard LightSystem capacity unchanged. No ECS authority or save ABI changes.

All builds, temp, captures and logs under KINGSTON lighting-completion-20261120.
- Debug aiverse_studio, aiverse_renderbench/gl, aiverse_studio_metal_lighting_tests
  and authored-light test build PASS. Initial pointer API and test entrypoint
  compile/link errors fixed (Model*, aiverseMain).
- ctest '^lighting_(native_host|luma_host|animated_host)': 6/6 PASS Metal/GL.
- ctest '^studio_metal_lighting$': PASS. Actual CAMetalLayer/StudioMetalScene
  CPU GI, GPU GI, GI off with Luma, removal of sources and disabling. Captures
  cpu/gpu/off/unlit.ppm in build/apps/studio-native/metal-lighting-captures.
  Source removal changed3699pixels, total RGB delta46014; pipelines31 failures0.
  gpu.png converted/viewed: terrain grid and light gizmo visible, no blank frame.
- ctest '^studio_(render_lighting|authored_lights)$': 2/2 PASS, including1500sources.
- npm typecheck and external bundle build PASS; Python contracts19/19 PASS.
- diff --check PASS, CI N/A, no push. Native HTML controls screenshot NOT RUN:
  CUA reports Mac locked, automatic unlock failed, browser native pipe unavailable.
  Asked user asynchronously to unlock; renderer fixture runs independently.

Known scope: preview shapes only, revision rebuild synchronous; no realtime
animated Studio assets or production-performance claim. Shared animated model
GI already tested in Stage24 renderbench. GPU/CPU automatic capacity routing
retains complete geometry; low-res Luma shadow denoise quality limit remains.
Next: ICB GI ABI and actual submissions, Releasearm64 current tree performance,
full quick/editor/client gate, Vulkan/mobile hardware and UI screenshot when
available. User asked ALL; continue rather than stop at this checkpoint.
