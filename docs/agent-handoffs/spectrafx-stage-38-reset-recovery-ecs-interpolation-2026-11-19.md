# SpectraFX Stage 38 — fail-closed grouped reset, explicit graphics Device recovery, interpolated ECS poses and GPU preview history

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting feature HEAD: `cde185068852ef7e43ab834d8712ede918014455`
Last observed Stage 38 source/skill commit before this handoff: `f5ffc650df4f8e31f5bc154294fb92303e747f53`
At start, GitHub compare `feature/universal-entity-inspector-fps...main` indicated **main is entirely an ancestor of feature** (feature 513 commits ahead, zero behind). No unnecessary merge or rebase occurred. No changes to main or other agents' branches.

## Exact commits / changed files

### GPU reset and Device reconstruction
1. `f1873d180a42710046eff339795a7fb07dcea165` — `engine/fx/SpectraGpuPoolWorld.h`: `PoolResetReport`, `resetReport()`, fail-closed `presentationBlocked()`, per-group accepted reset tracking.
2. `3503b3de034400bb29566477935e7274a73ba464` — `engine/fx/SpectraGpuPoolWorld.cpp`: block FX entrypoints on partial reset/device loss; retry only not-yet-prepared GPU groups; invalidate CPU effect handles/renderer source generation *only after* all groups accepted checked reset uploads. A previously-lost graphics Device requires explicit rebuild.
3. `ba7a39189d593bf5451756cf23b9b099d8da7545` — `engine/fx/SpectraGpuPoolWorld.h`: CPU-only `PoolRecoveryEffect` and `PoolRecoveryManifest`, export/import APIs and definition cache.
4. `3fc536ca0be023eac823a0f4e36a4c92ed74fbc1` — `engine/fx/SpectraGpuPoolWorld.cpp`: retain compiled effect definitions and runtime parameter defaults on successful definition/redefinition; update default values on `setNamedParameter`; export manifests without old-device readback budget and import them into a fresh pool on a healthy graphics Device. A failed import shuts down the partially initialized new pool.

### Preview history
5. `e48d941d529fca8e24c34e5fd00346e38519c7d9` — `apps/editor/Editor.h`: bounded `SpectraGpuPreviewFrameRecord` history and `spectraGpuPreviewHistoryJson()` API.
6. `a99bfb1bd32e5bce0b6437d1023bcb4919734f0b` — `apps/editor/Editor.cpp`: record last 180 Metal preview frame attempts with RHI frame serial, successful visual elapsed time, estimated GPU particle count, checkpoint freshness, pending kills, acceptance status and GPU health; JSON export. Clear history on preview replacement/restart/stop.
7. `65757d86551afee4596e17ebd6aa9880b775cd9c` — `apps/editor/windows/SpectraGraphWindow.cpp`: compact ImGui history plot, one-click copy of diagnostic JSON, reset group progress and device-loss warning in live Metal diagnostics.

### Genre-neutral render-pose interpolation and explicit recovery sample
8. `af121be9a6e188f8abd2400956af50b4b85a693d` — **new** `libraries/include/aiverselib/genre/SpectraEcsRenderPose.h`: `GpuEcsPoseInterpolator`, a bounded, const-ECS-reader two-committed-tick render-pose cache keyed by `Entity::packed()` generation. Only interpolate adjacent ticks of same entity generation; snap on skipped ticks or first appearance; nonfinite/excessive input rejected without replacing stored data; explicit rollback invalidation.
9. `b76cc301ee74163b3ca73e5470f3d72712cf741a` — `libraries/include/aiverselib/genre/SpectraFX.h`: export interpolator from native-project public header.
10. `53f16ee1232bab3545af19685f4b9ffd439bb4b6` — `examples/spectrafx/native_ecs_attachments.h`: use interpolated renderer snapshot in `prepare(...,dt,alpha)` and invalidate pose history after successful presentation rollback.
11. `530e37fa0e186ee199af04b49e45ad3df3d6245e` — `libraries/include/aiverselib/genre/SpectraGpuAttachmentEcsAdapter.h`: `discardTrackingAfterPoolRecreation()` for old renderer source handles, retaining declared effect policies.
12. `52678849cf26002db1aceef0fd540b511a862226` — `examples/spectrafx/native_ecs_attachments.h`: `rebuildOnRecoveredDevice` captures old effect manifest, releases the pool while the old Device still lives, invalidates presentation source/pose histories and imports into the new healthy Device; next committed-frame sync creates fresh source handles.
13. `cc9ae5b629d35b8492c38c24bc7cfba4ed35da44` — `examples/spectrafx/native_ecs_attachments.md`: host API and graphics-device lifetime/recovery contract.
14. `f5ffc650df4f8e31f5bc154294fb92303e747f53` — `libraries/include/aiverselib/genre/SpectraEcsRenderPose.h`: explicit utility include for moving committed snapshots.

### Documentation and agent ownership
15. `1b2547739d254c1113ec0c540da3449eaea67d31` — `docs/spectrafx.md`: public Stage 38 APIs, constraints, example and state semantics.
16. `dee34dba008b39d746c2f69e0c67dbebb1354e39` — `skills/visuals/aiverse-spectrafx/SKILL.md`: agent integration instructions.
17. `bb7bb99dc3ced8850781e509a8b1eb0751c17c62` — `AGENTS.md`: latest handoff link, no genre coupling and safety boundaries.

## Architecture/API notes for next agent

```cpp
// Rollback at a safe Metal graphics-frame boundary (after Device::beginFrame).
if(!pool.resetForRollback()) {
    const auto status=pool.resetReport();
    // status.retryRequired, status.deviceLost, status.groupsAccepted,
    // status.groupCount, status.failedGroup, status.attempts
    // pool.presentationBlocked()==true: NO new sources/frame/draw.
}

// If Device was LOST, build another healthy Device through native host:
PoolRecoveryManifest recipe=oldPool.exportRecoveryManifest();
oldPool.shutdown(); // old gfx::Device MUST STILL BE ALIVE
GpuPoolWorld replacement;
bool rebuilt=replacement.importRecoveryManifest(&healthyDevice,recipe);
// Recreate the renderer-owned ECS attachment presenter from the committed
// World; old GPU particles, source IDs and render history are intentionally lost.

// ECS render-pose interpolation, once per newly committed simulation tick:
GpuEcsPoseInterpolator poses(4096);
poses.captureCommitted(committedTick,world,resolvePose);
pool.beginPresentationFrame(camera,viewProj,width,height);
presenter.syncCommitted(committedTick,world,poses.resolver(renderAlpha));
pool.step(dt); // before render pass
// pool.draw(fog) in pass; pool.endPresentationFrame after pass
```

`PoolResetReport::groupsAccepted` means **checked RHI upload acceptance**, not successful GPU execution, GPU fence completion or cross-group atomicity. On failure, the accepted bitmap persists and subsequent retries skip accepted groups. On all-group acceptance, CPU effects and renderer-source IDs are cleared. A GPU health failure during attempted reset latches `deviceLost`, requiring explicit host-level graphics stack reconstruction. Existing `GpuPoolWorld` can export its CPU-only effect catalog even if GPU unhealthy, while old Device still lives. Import is supported only in an uninitialized, fresh pool with a healthy Device; no silent GPU downgrade for `RequireGpu`.

`GpuEcsPoseInterpolator` holds only render float snapshots, NEVER graphics handles in ECS, and never mutates authoritative World. Both the previous and current entity identities must match including generation; nonadjacent committed ticks do not interpolate. Missing poses remain absent and the ECS attachment presenter can pause new emissions. On rollback, clear the pose history only after a successful visual reset. Native `NativeAttachmentExample` demonstrates these host responsibilities. Native devices and game modules are still caller-owned.

The editor uses **CPU-side diagnostic history** of at most 180 actual Metal preview frame attempts. These records are not GPU state checkpoints; the editor supports only forward visual steps and restart, not reverse seek or snapshot resurrection. JSON export is suitable for bug reports/agent handoffs.

## Source-level verification performed

- GitHub connector: read starting branch and main ancestry; inspected active GPU world, particles, graphics Device API, native ECS adapter, editor preview and sample APIs. Wrote code/doc commits with SHA-guarded file updates. No main branch mutation.
- **NOT RUN:** CMake/Clang, Objective-C++/MSL shader compilation, CTest, native editor launch, on-device Metal preview, device-loss recreation, multi-group partial-reset fault injection, ECS interpolation tests, rollback-generation id reuse, performance measurements or CPU/Metal parity.
- Status: **SOURCE COMMITTED, EXECUTION UNVERIFIED**. The implementation is not certified production-safe.

## Known limits / risks

1. Group-level reset acceptance is not GPU fence completion or atomic all-or-nothing memory update. The host must await GPU/Device health and handle asynchronous errors; failure to re-create a Device cannot be concealed by a healthy-looking CPU scene.
2. Reconstruction requires the old graphics Device to remain alive until the old pool shuts down. A `PoolRecoveryManifest` has effect definitions, options and default parameters only; it does not restore current GPU particles, source handles, checkpoint VM state, generation IDs, visual timelines or simulation clocks. ECS must reattach after import.
3. `PoolRecoveryManifest` is a native C++ in-memory recipe, not a stable serialized asset format or persistent save file. Custom RHI readback budgets from an old Device must be freshly provisioned.
4. Live preview history reports estimated alive particles and local checkpoint freshness/health, not exact GPU counters or a rewind-capable snapshot. A failed GPU step is recorded but not added to accepted visual time.
5. ECS render pose is read-only and generic, but native host still supplies committed tick and `ResolvePose`; interpolation assumes sequential snapshots and skips across missing or recycled entity generations.
6. Existing Metal GPU shader subset remains experimental; production native FPS/RTS pipelines must not automatically opt into it without real parity/health/performance tests. Prisma postprocess is separate.
7. No actual compiler, Metal device or CTest verification was run in this stage, per features-first priority.

## Concrete next Stage 39

1. Add explicit recovery-status/event reporting to the native game/render host (including Device recreation triggers) and validate preserving authored effects and generation-safe ECS reattachments without reintroducing stale source handles.
2. Build a richer editor Metal preview diagnostics/export path for real GPU checkpoint bytes ONLY when guaranteed complete, fresh and bounded; historical timeline samples should remain nonreplayable.
3. Improve GPU-world policy under memory pressure and verify per-source admission/fallback semantics without starving unrelated visual groups.
4. When feature development is complete, run native Mac CMake/ObjC++/MSL builds, SpectraFX CTest, Metal offscreen scatter-kill, partial reset, explicit Device recovery, interpolation/id recycle, editor screenshots and benchmark tests. Fix all failures before any main merge.
5. Preserve this per-stage handoff protocol: exact commit SHAs, source paths, tests RUN vs NOT RUN, limits, branch, next task. Avoid force pushes and other agents' concurrent files.
