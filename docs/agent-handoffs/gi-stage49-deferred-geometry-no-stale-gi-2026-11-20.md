# AIVERSE GI — Stage 49: never display stale GPU GI while geometry publish is deferred (2026-11-20)

## Branch, commits, actual verification

- Repo `chromealex/AIVERSE`, head `feature/gi-lighting-portable-backends-20261119`, draft PR #47 to original GI feature branch. Not main.
- `79450e2a0aece5d5bbe1d74bf35a722be8c6a6d3` — `engine/render/GiLightingController.h` adds `pendingGeometry_` fail-closed presentation state.
- `b508658a200ac33ca93de9ac5e823da4e8af22ff` — `tests/render/gi_gpu_residency_planner.cpp` checks that a relevant triangle-budget refusal blocks presentation and reports false in `StepStats::canPresent`.
- `42a60ae4825ad63daaa9fd1bd0d35a4d35ac7a1c` — `tests/render/gi_world_presentation_sync.cpp` checks GPU deferred world publication blocks atlas, and success clears the block before the next fully completed GI recomputation.
- **Native C++ build, CTest, GPU Metal/Vulkan/GLES testing and end-to-end visuals NOT RUN. This is a source safety fix, not a verified GPU parity pass.**

## Bug and corrected invariant

Stage 40's `GiWorldPresentationSync::publish` transactionally held dirty world change records if the old GPU compute job was still in flight or the new terrain/foliage BVH failed its budget. However the old atlas might still be fence-complete, and `GiGpuTicketQueue::safeToSample` could report true even though authoritative terrain/PHYSIS/ECS had just changed and new ray-query geometry **was not yet accepted by the GPU**. Displaying that atlas could leak light through a just-built wall or persist bounce from a removed obstacle while the publisher waited for the next GPU fence.

`GiLightingController` now sets `pendingGeometry_=true` whenever a correctly formed newer GPU geometry revision is **rejected** by the residency budget, the GPU scene or an in-flight job. The host-facing `presentationBlocked()` makes the state observable, `StepStats::canPresent` rejects stale computed frames, and `bind(Renderer3D&)` clears/rejects the renderer's borrowed dynamic GI overlay until the correct newer revision is accepted. A successful `setGeometry` or `setGeometryRegion` resets the block; the normal dirty scheduler then prevents sampling until all new probes are completed and fence-confirmed. `shutdown` also resets the block. This does not delete the old atlas or touch authoritative world state, and it does not attempt to publish partially updated GPU output.

The required host contract remains: call `GiWorldPresentationSync::publish(controller)` after an immutable ECS/world barrier and retry after fence completion; do not bypass the controller to bind a stale `GiGpuDynamicRuntime` directly. Clear/unbind renderer overlays before controller shutdown. A permanently too-complex near clip region remains dark until the host reduces geometry/proxy complexity or explicitly selects CPU fallback; this is preferable to incorrect lighting leaks.

## Next precise action

1. Native build/execute `gi_gpu_residency_planner`, `gi_world_presentation_sync`, and `gi_lighting_controller` CTests. Add renderer-linked image fixture proving the old atlas actually disappears on a refused geometry update while the new wall becomes visible, then returns only after accepted GPU fence.
2. Test a moving TerraWeave terrain edit under real Metal/Vulkan GPU compute and delayed command-buffer completion; verify no stale 1-frame GI, out-of-bounds texel writes or leaked borrowed texture.
3. Follow with producer-side TerraWeave/PHYSIS notification integration once current main is conflict-reviewed, and remaining large-world GPU paging.
4. Run full Metal/Vulkan/GLES compiler and hardware acceptance once remaining functions are complete; update handoffs with actual tested SHA and PASS/FAIL. Do not merge unvalidated draft PR into main.
