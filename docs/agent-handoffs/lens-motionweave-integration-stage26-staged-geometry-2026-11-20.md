# AIVERSE LENS Stage 26 — staged GeometryScene GPU replacement (2026-11-20)

## Repository, branch, commits
- Repository `chromealex/AIVERSE`, branch `feature/lens-motionweave-integration`. No merge to `main`.
- `2e65bca207f1372af874b0eb3a80f6cc4d40ae85`: `engine/render/GeometryScene.h`, new public `loadTransactional(gfx::Device&,const Json&,const FileReader&)`.
- `452740139063e79586534a5eda3d38659579ceb7`: `engine/render/GeometryScene.cpp`, build and upload into an independent staging scene before replacing resident model/proxy/instance/group containers.
- `845038857ff45bf67ee0e2615a790b092f5be686`: `modules/rts/view/RtsView.cpp`, opt into transactional geometry loading, retain old geometry on staging failure, clear only on intentionally absent descriptor.

## Architecture and behavior
- A local `GeometryScene staged` receives `loadImpl` and performs all file/CPU model/HLOD/upload work separately from the current live scene.
- Any exception during staging triggers best-effort `staged.retirement_.completeForShutdown(dev)`. The existing `loadImpl` exception path already routes partial uploads to the staged retirement queue. If GPU idle cannot be established, that queue's destructor deliberately orphans GPU-live model data instead of freeing it unsafely. The old live scene remains untouched.
- Only after staging succeeds, the live instance calls `retireResident(dev)` to route old GPU model handles through its existing `NativePresentationRetirement` queue; then it transfers staged model/instance/group containers and policy/stats. It does not replace the existing retirement queue, which may contain earlier in-flight models.
- `RtsView::init` no longer clears the active geometry before attempting the replacement. Missing/unreadable/invalid optional geometry descriptor is still reported as a warning; old geometry remains on failure. Explicitly absent descriptor clears the old geometry.
- No ECS schema, save-format, camera API or MotionWeave change. Generic `GeometryScene` method is additive and independent of genre.

## Actually performed checks
- GitHub source read of `GeometryScene.h/.cpp`, `NativePresentationRetirement.h`, and `RtsView.cpp`; commit and post-commit readback.
- Source inspection confirms staging `loadImpl` precedes live `retireResident`, `RtsView` calls `loadTransactional`, and failure no longer pre-clears live geometry.
- **NOT RUN:** CMake configuration, native compile, CTest, fault injection, GPU Metal/Vulkan/GLES, visual parity, stress/perf. No passing tests or correctness claim.

## Remaining risks and next exact work
1. Run actual compiler on `GeometryScene::loadTransactional` and `RtsView::init`. Check move assignment of model/group containers, no stale pointer after transfer, and stage GPU upload failures.
2. Add fault injection for source model/HLOD upload failure proving old models, static caches and stats survive; success swaps only once and retirement queue remains valid.
3. On failed idle of the temporary staged scene, `NativePresentationRetirement` intentionally retains GPU-owned allocations to avoid use-after-free, but these resources cannot be retried by the host. Implement transferable failed-staging retirement ownership if device recovery is required; do not claim no leaks on permanent device loss.
4. Sprite staging is still published before later fog/overlay/terrain/road/shore initialization: full `RtsView` atomicity is **NOT** achieved. Audit next resource owner and change to staged publication where feasible.
5. Sync latest `feature/universal-ecs-animation-pipeline` with conflict-by-conflict merge, especially `apps/modelview/main.cpp`. Complete the user-requested final one-pass build and CTest plus Metal/Vulkan/GLES checks before merging main.
