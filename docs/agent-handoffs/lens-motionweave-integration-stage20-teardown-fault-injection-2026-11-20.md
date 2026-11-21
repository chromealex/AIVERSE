# AIVERSE LENS Stage 20 — sprite teardown audit and GPU failure injection (2026-11-20)

## Branch and exact commits
- Branch: `feature/lens-motionweave-integration`; `main` unchanged.
- Implementation `6475c13af0adbb5955ca52755592619a0e2aea04`: `modules/rts/view/RtsView.cpp` — on project/rules reload, replace unguarded `twoD_->shutdown(dev)` with `shutdownAfterGpuIdle(dev)` and abort reload on failed barrier without resetting/destroying the owned sprite view.
- Test source `78672431780d0f642d0d055fa968c89f4803e17a`: `tests/render/lens_sprite_gpu_fault_injection.cpp` — fake RHI/device allocation faults, GPU idle refusal and subsequent retry, idempotent guarded cleanup and clean reinitialization.
- Test registration `ad963c377fed380183c53e93ce28bb1471de49a2`: `CMakeLists.txt`, `lens_sprite_gpu_faults` tagged LENS/RHI.
- Previous: `docs/agent-handoffs/lens-motionweave-integration-stage19-early-init-recovery-2026-11-20.md`.

## Audit findings
- The final `RtsView::shutdown(dev)` already establishes a device-wide idle barrier before freeing its presentation resources. Its nested `twoD_->shutdown(dev)` is therefore permitted after that proof.
- In contrast, `RtsView`'s *reload* path previously destroyed `twoD_` before establishing a GPU idle proof; this could retire GPU vertex buffers/textures used by preceding camera submissions.
- This reload path now calls the Stage 19 guarded API. A refused idle barrier throws and leaves the owned sprite view object intact for later recovery.
- `GameClient::shutdown()` establishes an idle barrier and retires LENS camera resources before `view_.shutdown`. This remains unchanged.
- The new test harness overrides shader/pipeline/buffer allocations in an existing FakeDevice-derived mock and tracks live handles. It injects failures across 32 allocation positions, verifies cleanup after confirmed idle, verifies retention after failed idle, and successful retry/reinit. **The tests are authored but have NOT run**.
- The GPU resource release contract remains: low-level `SpriteBatch::shutdown` and `Rts2DView::shutdown` require a caller-known safe GPU boundary. Use the guarded methods during uncertain device state.

## Actual validation
- Examined and compared GitHub source for `RtsView::init/shutdown`, `GameClient::shutdown`, `SpriteBatch` initialization/teardown and latest Stage 19 handoff.
- Created a CPU-only test target and CMake registration.
- **NOT RUN:** CMake configuration, native C++ compiler, CTest target, fault injection executable, Metal/Vulkan/GLES backend build or GPU runtime validation. No pass/green claim.
- The test target should be checked for CMake dependency/link compatibility with `aiverse_runtime` and the pre-existing `tests/cloth/FakeDevice.h` when run.

## Known risks / next steps
1. RtsView reload mutates some non-sprite state *before* reaching the guarded sprite teardown, so an idle failure can leave an interrupted reload; make staging transactional across all presentation owners if hot-reload reliability is required.
2. The fake RHI test does not test real GPU in-flight hazards, failures inside `TextureCache`, or the secondary-batch fault path of `Rts2DView`. Add targeted failure tests as part of final validation.
3. Audit exception paths of Stage 18 local RAII class and Stage 19 retryable recovery under native C++ compiler.
4. Resolve any failures from the newly authored `lens_sprite_gpu_faults` test. Test with multiple active captures, changes in camera basis, >16384 drawIndexed quads, and same-frame Metal snapshot buffer updates.
5. Integrate latest MotionWeave head using inspected three-way reconciliation without overwriting changed client/modelview/editor code.
6. Run one consolidated CMake build/CTest and supported Metal/Vulkan/GLES compile/GPU evidence before claiming parity or merging to main.
