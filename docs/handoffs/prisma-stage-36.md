# PRISMA Stage 36 — deferred hosted clients, safe screenshot lifetime, preview textures and RenderGraph telemetry

Date: 2026-11-19. Branch: `feature/prisma-postfx`.
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous handoff: `docs/handoffs/prisma-stage-35.md`.
No changes merged to main or other agents' feature branches.

## Actual implemented code, changes and exact commits

1. `f29e1bafa6d1ad69f548bff2e89755a85d696b92` — `apps/editor/ClientHost.h`: new `RetiredClient` archive (old GameClient, saved HostedView, owning Device, GPU submission token, initialization flag), `collectGpuRetirements()`, `drainAfterGpuIdle()`, `pendingRetiredClients()`.
2. `6dee5b7e6a511ee66008344ea72c50660c062a4f` — `apps/editor/ClientHost.cpp`: `stop()` removes client from active UI immediately but archives it without destroying its GPU resources while in-flight. Subsequent graphics-owner collector checks the actual backend GPU completion fence, restores the archived platform HostedView and ImGui context during GameClient::shutdown. A confirmed idle drain supports unavailable/zero GPU fence tokens.
3. `8a21108cb6f2985e219ec6bc9bb416e4f1fce0ed` — `apps/editor/Editor.cpp`: collect completed hosted clients at every pre-render UI frame, drain after successful submitted-work GPU-idle on memory pressure and at Editor shutdown where confirmed.
4. `686ada20b43ebe9418cb225e60609fa4ead455a5` — `apps/editor/Editor.h`: shared atomic screenshot lifetime token.
5. `b3fda2c8a82174bc9e34df7dd83bbbc2992a2096` — `apps/editor/Editor.cpp`: fresh token at initialization; invalidate and discard screenshot requests at shutdown.
6. `d9b7514fd505e9654167786bfa7dd1fad08f34ed` — `apps/editor/EditorAgent.cpp`: `Device::captureNextFrame` callback checks the shared lifetime token before dereferencing the Editor, changing captureArmed or sending an MCP response.
7. `94bc7f08d8103d9f309c17a61cb379251ea6b4de` — `engine/render/PrismaRhiExecutor.h`: `gpuMemoryStats()` enumerates live graph-pool textures and pending graph-retirement queue textures, using actual RHI `textureInfo` to report lower-bound known bytes, plus count of handles lacking metadata.
8. `b2b632d1e62ea9651e99f0eacd8fd453f00b4f60` — `engine/render/Prisma.h`: add graphLiveTextures, graphUnknownTextures, graphLiveKnownBytes and graphPendingKnownBytes to public `RetirementStats`.
9. `5c5bada8891ca4b30443197c1d8762b13e457ca0` — `engine/render/Prisma.cpp`: accurately aggregate active and retired `Sized::graphExecutor` allocations. Archived graph pools count as pending, including formerly-live pool resources; active pools count separately. Include genuinely pending graph bytes in global known-pending total.
10. `5106a84d98e8b583098c8e99db5aa2fef33c24bd` — `apps/editor/windows/ShaderGraphPreview.h`: bind Editor's shared `ViewportGpuRetirement` manager; retire the preview's own 256×256 color and depth texture handles on graph reload and preview shutdown instead of immediate destruction.
11. `7aa5511601a38fc4c4afdd243eec6f403428955f` — `apps/editor/windows/StatsWindow.cpp`: display pending hosted client count and RenderGraph live/pending textures, MiB and unknown-metadata allocations in the existing PRISMA GPU lifetime panel.
12. `f0fed5cd56ed67c786ab8c8ef273a0baad5986ab` — `apps/editor/ClientHost.cpp`: ensure completion tokens are collected only on the same graphics Device; at destructor try the submitted-work GPU-idle wait before draining.
13. `074f72857631e862402e1d88e70f6df3fa011bc2` — `docs/prisma.md`: Stage 36 owner/API integration and limitations.
14. `3f730ebc2eca50c83eff23d2fff763017b3e3ef1` — `apps/editor/Editor.cpp`: archived hosted client count greater than two triggers an additional attempt at **real** pre-render GPU-idle pressure cleanup even though full GameClient bytes are not enumerable; number of clients is NOT a completion/fence signal.
15. `c2bfd9e67bf2402f0a9999347789eb260b886c03` — `docs/prisma.md`: clarify count-based hosted cleanup pressure.
16. `683fac41cf08de815e14cd54325ce77e3ea4e2bc` — `apps/editor/Editor.h`: invalidate screenshot lifetime token during destruction even if explicit `Editor::shutdown` was skipped.
17. This distinct Stage 36 handoff `docs/handoffs/prisma-stage-36.md` records architecture, exact commits, verification and next owners.

## Native API / ownership

**Hosted GameClient:** `ClientHost::stop()` no longer invokes `GameClient::shutdown` synchronously from a Game tab UI action. The previous hosted `GameClient` is moved into `retired_` with its original `platform::HostedView` and `Device::gpuSubmissionFence()`. The editor's next frames call `collectGpuRetirements()`, using the **same Device's** `gpuCompletedFence()`. If a token is present and confirmed completed, the archived client's shutdown occurs under its original platform hosted context on the graphics owner thread. This prevents use-after-free of textures still in the current frame's GPU command buffer. For no-fence backends, an explicit `drainAfterGpuIdle()` may be called after a genuine `Device::waitForSubmittedGpuWork()` success. A count over two archived clients triggers a pre-render idle attempt, not a guessed CPU frame completion. `pendingRetiredClients()` reports resource-owning archived clients waiting for release. Backend teardown still needs guaranteed GPU idle.

**Screenshots:** `EditorAgent::deliverShots` continues using the existing backend-owned framebuffer capture, not a new GPU render target. The callback is now gated on an atomic shared liveness token. It must execute on the backend's documented graphics/UI owner thread; a callback delivered after shutdown does not touch Editor or MCP. The token is invalidated on both explicit shutdown and Editor destruction.

**ShaderGraph:** only preview **color/depth render targets** were migrated to `ViewportGpuRetirement`. Those now survive a graph replacement/window close until completion. `ShaderGraphPreview` still shuts down its other subresources — `game::Portrait`, `render::Prisma`, `fx::ParticleSystem`, `TextureCache`, shader, pipeline and vertex buffer — through their existing owner policies; do not claim the entire preview is GPU-fenced.

**RenderGraph diagnostics:** `PrismaRhiExecutor::gpuMemoryStats()` enumerates pool entries (live handles) and its retired queue (pending handles), requesting `gfx::Device::textureInfo` for format/dimensions. `Prisma::retirementStats()` includes pending graph bytes in `knownPendingBytes`; actively reused pool allocations are tracked separately under `graphLiveKnownBytes`. `StatsWindow` reports both, unknown count, and hosted client count. No GPU timing/readback or authoritative ECS code is involved.

No changes to the `postfx` schema, genre-specific systems, game determinism, existing MotionBlur flag, external Water/FX ownership or resource format policies.

## Verification status

**Performed:** GitHub connector source search and targeted fetches of ClientHost, GameClient, Editor capture, ShaderGraphPreview, PrismaRhiExecutor, PrismaGraphResourcePool and StatsWindow; implementation commits; repeated readback of ten changed source files to confirm new declarations and call sites exist and source text matches.

**NOT RUN, per user's ongoing features-first directive:** native CMake/C++/Objective-C++ compilation, CTest, GL/Metal/Vulkan/WebGL runtime, GPU fence signal tests, editor or hosted-client smoke, screenshot/MCP callbacks, ShaderGraph preview visual checks, profiling, CI. Actual compilation correctness and GPU safety are not verified.

## Limitations / next Stage 37

1. Hosted GameClient GPU resources are deferred until GPU completion, but a backend without a supported fence can accumulate client objects. On full Editor/device teardown, ensure the platform has synchronized GPU idle; `ClientHost` destruction without a valid graphics device/confirmed idle has the same unresolved shutdown contract as other graphics owners.
2. `ClientHost` completion uses the GPU submission timeline of its owning `gfx::Device`. Do not free objects on completion tokens from another device. Test client switching while the old render is still in flight; restore old `HostedView` before shutdown.
3. The async screenshot callback guard prevents post-destruction Editor dereferences but the backend's own staging texture/buffer lifetime remains owned by the RHI. A screenshot that never completes due to driver loss remains a backend concern.
4. ShaderGraphPreview's remaining portrait/FX/shader resources can still be released synchronously during a graph change. Stage 37 should migrate its **whole preview resource bundle** to a GPU-retirement-aware owner or ensure preview shutdown is deferred to a verified GPU fence, so no shader/buffer can be freed while in flight.
5. Graph memory stats are lower bounds, not a total VRAM figure: unknown metadata, driver/compression overhead and hosted GameClient allocations are not included. Integrate editor hosted client estimated GPU allocations or a generic `gfx::Device` allocation registry only after carefully preserving performance.
6. The Stage 33 Vulkan `gpuCompletedFence()` and GL/Metal pressure barriers still need native validation. A user interface action must not force a GPU wait inside an active render encoder; pressure cleanup should remain pre-render.
7. Optional feature extension: a generic per-device **deferred shader/buffer/pipeline retirement** service (not just textures), using verified GPU-completion tokens. This is prerequisite for completely safe ShaderGraphPreview/Portrait/FX hot reloads.
8. When user authorizes tests: run CMake/CTest, editor and hosted GameClient stop/restart, multiple start-stop cycles without GPU resource reuse, screenshot after Editor shutdown, ShaderGraph preview hot reload, and graph memory accounting on Metal/GL/Vulkan. Record true PASS/FAIL.
9. After Stage 37 write `docs/handoffs/prisma-stage-37.md`, exact branch/SHAs/actual tests/API and next concrete action. **Do not merge** feature PR #26 into main without explicit request.
