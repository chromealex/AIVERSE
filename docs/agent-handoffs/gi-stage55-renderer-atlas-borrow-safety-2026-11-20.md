# AIVERSE GI — Stage 55: borrowed Renderer3D atlas lifetime interlock (2026-11-20)

## Branch / committed source / actual verification

- Repo: `chromealex/AIVERSE`; branch: `feature/gi-lighting-portable-backends-20261119`, PR #47 DRAFT targeting `feature/hybrid-dynamic-gi`, NOT the substantially diverged current `main`.
- `650515f51c973bca83894c33f12a58f14f1d96c2`: `engine/render/GiBorrowedPresentation.h` — genre-independent RHI owner-thread interlock tracking reader identity, with idempotent acquire/release and no use of ECS or graphical backend APIs.
- `a82ed07d95df9c9531e0610fc5b9a267a90fbdb8`: `engine/render/GiLightingController.h` — successful `bind(Renderer3D&)` registers reader; failed `bind` clears the old overlay and unregisters it; `unbind` releases. `shutdown()` and the optional Stage53 GPU→CPU local-BVH-overflow migration now RETURN FALSE / defer while any bound renderer could still be holding a raw atlas pointer. Public `borrowedRendererCount()` exposes diagnostics.
- `c1d98da21d69599fea6cc3e1f82c331f51290c97`: `tests/render/gi_borrowed_presentation.cpp`: deterministic no-GPU owner-token lifetime tests for repeated binding, multiple readers, partial/full release, idempotent unbind.
- `ef25fcd82aec86adad6a6358381fc26daa183429`: CMake target `aiverse_gi_borrowed_presentation_test` / CTest `gi_borrowed_presentation`.

**Actually performed:** inspected connected GitHub source, committed edits, recorded exact SHAs. **NOT RUN:** native CMake/Clang/GCC build, CTest, real Renderer3D-linked test, Metal/Vulkan command buffer lifetime and GPU diagnostics, GLES3 mobile, full suite. Do not mark pass or ship.

## Bug and invariant

The optional `GiLightingController::tryCpuOverflowFallback` from Stage53 could call `GiGpuDynamicRuntime::shutdown` and destroy the GPU probe atlas, even while a previously successful `controller.bind(renderer)` left a RAW borrowed `GiDynamicProbeTexture*` inside `Renderer3D`. If the actual render pass occurred later in the frame, that pointer could dangle. Documentation said the host had to unbind first, but there was no code-level rejection of the unsafe lifetime.

Stage55 tracks live Renderer3D borrow tokens through a small independent `GiBorrowedPresentation` registry. An admitted bound renderer holds a reader until the host calls `controller.unbind(renderer)`. If any reader remains, GPU→CPU fallback refuses and leaves `GiLightingController::pendingGeometry` presentation blocked; retry after all readers unbind and actual GPU fences finish. Ordinary `controller.shutdown` also refuses. A failure to bind clears the overlay and removes its registered reader. Duplicate `bind` for the same renderer does not multiply its reader count, and multiple distinct renderers all must unbind.

The registry is confined to the *same RHI owner thread* as Renderer3D; it does not protect arbitrary concurrent render-thread dereferences. It does not own Renderer3D pointers, invoke them on teardown, or allow accidental unbound lifetime inference. **The host must still explicitly unbind before destroying the renderer or controller.** This change prevents common explicit shutdown/fallback while borrowed but cannot stop C++ member destructors from executing when a client discards the entire controller object without respecting the API. Vulkan/Metal RHI deferred GPU resource destruction remains a separate driver-level obligation.

## Next precise actions

1. Native build and run `gi_borrowed_presentation`, `gi_gpu_residency_planner` and `gi_lighting_controller`; include a real renderer-linked fixture with `bind(renderer)`, `shutdown` blocked, `unbind(renderer)`, and then accepted fallback once GPU fence is confirmed. Record tested SHA with actual CTest evidence.
2. Check the engine's owning client/editor render-loop order: beginScene → source updates → controller.bind → render → controller.unbind → optional geometry/fallback/shutdown; a queued custom draw may sample GI later than source publication.
3. Repeat on real Metal/Vulkan GPUs while terrain changes, with burst GPU jobs, and on GLES3 CPU fallback. Confirm no atlas use-after-free or leaks.
4. Continue the full GI large-world/clipmap features, then final full native CMake/CTest + Metal/Vulkan/GLES graphics parity; reconcile with main only after test pass and conflict review.

## Stage 55 follow-up — exception-safe borrower registration

- `26da2162d5ed0b276d42b772c9636c61dc4242ce`: `GiLightingController::bind` now registers the reader **before** passing the raw atlas pointer into `Renderer3D`. An allocation failure in the tracking set can therefore never leave a newly successful but untracked borrowed pointer that `shutdown` might free. If binding fails, the method still clears the renderer overlay and releases the reader. **No native tests executed; source-only correctness review.**
