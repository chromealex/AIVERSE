# SpectraFX Stage 34 — source-scoped GPU kills, bounded continuous emitters and opt-in Metal editor preview

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting branch HEAD: `a45938a558ce35f0df32ffb6a703402fde4537fb`.
Final observed HEAD before this handoff: `832526bcaeecf8f4f3c60ae8dee7850396c24221`.
All changes made directly with GitHub connector's guarded contents API. No merge with main, rebase, force push or test claim.

## Exact code/document commits

- `5a38939765c732ea55bc41e3c8ddae8d7620e9e4` — `engine/fx/SpectraGpuParticles.h`: tagged `burstAt(...,sourceToken)`, `killSourceParticles(token)` and physical-slot source owners plus pending kills.
- `b3e6e05707a52bf117477b11cd6878c3f5b476a9` — `engine/fx/SpectraGpuParticles.cpp`: tag births, cancel matching pending unsent births, queue kills of submitted physical slots, issue coalesced dead-state `updateBuffer` writes ordered before the compute pass, recycle killed slots only after successful submission, reset owner tags on expiry/clear.
- `0bfcd32a7a8f2b80527988f69e59062eacd11ddc` — `engine/fx/SpectraGpuPoolWorld.h`: `PoolContinuousEmission`, `stopSource`, `killSourceParticles`, transform attachment/detachment, bounded continuous emission options and pool telemetry.
- `4633583dfb80543945bf3cb51ecbdcf85d509573` — `engine/fx/SpectraGpuPoolWorld.cpp`: per-pool source token allocation, GPU tagged births and source-scoped CPU FX handles; source stop/kill, transform updates, continuous config.
- `b8d6ee695237e9f9366841e93533e373299ede91` — `engine/fx/SpectraGpuPoolWorld.cpp`: time/rate/distance continuous-emission scheduler in `step`, bounded by source and global burst budgets.
- `0b79c2c5fa71d4e0dbe47b7a2d46ca3782fe279a` — `apps/editor/Editor.h`: deferred GPU preview request and renderer lifecycle APIs.
- `7dc67334cbc23b662015f8de34c2c0654156008e` — `apps/editor/Editor.h`: preview loop, position and instance parameter state.
- `b0b9c57acae79800b211efa1493ebbfdcef568bd` — `apps/editor/Editor.cpp`: create preview Metal GPU pool at safe pre-pass, submit GPU step before `r3d_.render`, draw inside scene/game pass, teardown after pass; connect telemetry automatically.
- `8f17466c940cfaa8e7782ccb1d63d0787c9daf25` — `apps/editor/EditorFxOps.cpp`: `fx_graph_preview {gpu:true}` opt-in and strict whole-effect/Metal eligibility.
- `f601f42a2658f6e2b0897b2f1d92af2cea6869ff` — `apps/editor/windows/SpectraGraphWindow.cpp`: explicit **Metal GPU** preview button while keeping existing CPU preview.
- `5e7a56761690787cd7cc45d72ca118be251850ec` — `engine/fx/SpectraGpuPoolWorld.cpp`: avoid expiring continuous emitters at zero dt and reject non-finite movement path distance.
- `a737bf66af5ad2a2860ee5782d7a82333b1d12f9` — `engine/fx/SpectraGpuParticles.cpp`: pending selective kills block CPU restore and async snapshot capture; clear invalidates retained checkpoint state; intentional kills excluded from fallback loss accounting.
- `dbb679d4f8a60204a97d626378efcbf1dcfcebb1` — `engine/fx/SpectraGpuParticles.cpp`: also block synchronous capture while kills remain unsubmitted.
- `b7b5466c94cf6dec74e76cc6d7374aa319ffa36d` — `apps/editor/EditorFxOps.cpp`: regular CPU preview stops prior Metal preview.
- `10cba152415d554af6df4bf19bea227079dc97dc` — `apps/editor/windows/SpectraGraphWindow.cpp`: focus returned Scene/Game view.
- `a5647a2dff5b5d617cd2d20002ecca391f265acd` — `engine/fx/SpectraGpuPoolWorld.cpp`: kill evicted CPU source handles when source ownership ring is bounded.
- `bfccfdbd5abaa6e5f5c16b922da89fad8529dc96` — `docs/spectrafx.md`: Stage 34 public API and limitations.
- `2a80166816a2cb996e85e2cec177d7362ce469a9` — `skills/visuals/aiverse-spectrafx/SKILL.md`: updated agent-facing usage and verification warnings.
- `832526bcaeecf8f4f3c60ae8dee7850396c24221` — `AGENTS.md`: Stage 34 handoff link and architecture ownership contract.

## Architecture and public API

```cpp
using namespace aiverse::spectra;
auto handle=pool.createSource("fire");
pool.setSourceTransform(handle,{0,2,0},{0,1,0},{1,0.5f,0.2f,1});
PoolContinuousEmission emission;
emission.burstsPerSecond=10;
emission.metresPerBurst=0.4f;
emission.lifetimeSeconds=0; // infinite
emission.maxBurstsPerFrame=8;
pool.configureContinuous(handle,emission);

// Host: Device.beginFrame -> pool.beginPresentationFrame ->
// update attached render transforms -> pool.step(dt) BEFORE render pass ->
// pool.draw(fog) WITHIN pass -> end pass -> pool.endPresentationFrame.
pool.pauseSource(handle);       // no new births
pool.resumeSource(handle);
pool.stopSource(handle,true);   // stop + selective live particle kill
pool.retireSource(handle);      // invalidates handle
```

- A renderer-owned `Source` maintains its own generation-safe `PoolSourceId`, independent monotonic 64-bit token and per-source parameters. Tokens are unique in the current pool lifetime, including recycled source slots.
- GPU `ComputeParticles` tags the physical slots belonging to the token. Unsent births are removed from all three pending zip vectors before submission. Submitted slots are marked CPU-dead, and contiguous runs receive 64-byte dead particle state uploads before the next compute encoder. Physical slots are returned to the free list only after accepted ordered compute submission; source-independent pools and GPU-indirect compact remain intact.
- CPU pooled effects track `fx::FxHandle` per source; selective kill invokes shared CPU `BatchWorld::kill`. At source handle history limit, the oldest is killed before the record is discarded.
- Source stop/retire are independent: `retireSource` does NOT forcibly kill existing particles; call `stopSource(handle,true)` first if a hard stop is intended. `pauseSource` only stops future births. A live source may be updated with the render transform each frame; the pool does not own authoritative scene hierarchies or ECS transforms.
- Continuous scheduler adds time-based and distance-based births, bounded by per-source cap and global frame budget, with skipped excess counted. `PoolWorldStats::continuousBursts`, `continuousSkipped` and `selectiveKillRequests` are presentation diagnostics, not gameplay event counts.
- Checkpoint capture/recovery is forbidden when source-kill GPU invalidations have not yet submitted. `clear()` invalidates retained checkpoint revision, preventing rollback to physically overwritten states.
- The editor `fx_graph_preview {graph,gpu:true}` and visual **Metal GPU** button opt into strict eligibility and required Metal hardware. The preview is allocated only in a later scene/game pre-pass, stepped before `r3d_.render`, drawn in the open depth-tested pass, and automatically attached to the GPU diagnostics view. Default CPU preview remains unchanged. No silent downgrade for unsupported graphs.

## Verification actually performed

- Inspected current GitHub branch source interfaces and render-pass order; read RHI Metal storage-buffer updates, command submission and callback paths.
- Successfully wrote source/docs changes and re-fetched GitHub files to confirm latest Stage 34 symbols.
- **NOT RUN**: local repository checkout/build, Clang/CMake, MSL compiler, CTest, editor window, screenshot, interactive GPU preview, macOS Metal device tests, GPU-fence ordering test, multi-source stress, CPU/GPU material parity, fault injection, performance measurements.
- Status is **SOURCE IMPLEMENTED, EXECUTION UNVERIFIED**, not production ready. Do not claim build/test success.

## Known limits and risks

1. `killSourceParticles` currently uses one coalesced dead-state storage upload per contiguous physical-slot run. Highly fragmented kills could issue many small Metal blits (up to capacity) and need performance profiling. GPU kills require a healthy RHI and safe pre-render pass phase. Error checking is limited by `gfx::Device::updateBuffer` being void; Metal memory allocation or enqueue faults need explicit fault-injection testing.
2. Slot reuse is deferred until successful compute **submission**, not an explicit CPU-visible completed GPU fence. This relies on ordered blit/compute/next-frame queue semantics and must be audited on Metal. If the RHI adds parallel queues, this implementation needs a stricter barrier/fence.
3. Continuous source transform is explicit renderer-side pose snapshotting, **not** an ECS entity-attachment API; clients must update transforms before calling `step`. Teleports can incur skipped distance births rather than creating vast queues.
4. The GPU path remains a strict single additive soft-billboard subset; unsupported graphs are CPU in the main runtime, or explicitly rejected when editor GPU preview requires Metal.
5. The optional GPU preview uses one-shot bursts and simple 0.5-second repeat for `loop`; it does not yet match the CPU preview's `advance`, `freeze`, trails, multiple emitters or complex shader behavior. The live editor pool exists only when the Metal preview is requested and a matching viewport renders.
6. No generic presentation-pose ECS adapter yet automatically attaches `PoolSourceId` to persistent entities across rollback; other adapters remain read-only and use existing CPU cue scheduler until a full bridge is implemented.
7. Multiple caller-owned GPU worlds sharing one Device must still cooperate via the Stage 32 shared checkpoint budget. No claim of global hardware readback arbitration.
8. Host must destroy the editor GPU pool before the Device, and explicitly detach any external non-owning telemetry pointer before destroying its pool. The editor's own pool is detached automatically on replacement/shutdown.

## Concrete next Stage 35

1. Strengthen selective-kill GPU-state ordering with explicit RHI completion/fence or robust deferred ring if required by hardware fault tests; bound blit run count via a kill-index compute buffer if profiling shows fragmented kill cost.
2. Add a real read-only ECS presentation attachment adapter for per-entity/transform sources, with committed tick handling, predictable pause/retire/kill and stable generation identity, no graphics handles in authoritative ECS.
3. Extend editor GPU preview options (play/pause/loop/freeze with honest backend restrictions, partial GPU material features) and diagnostics for selection/paused source counts.
4. After the user finishes function work: compile native C++, Metal shaders and editor; run existing + new CTest, GPU device-loss and selective-kill/compaction smoke, screenshot visual parity, multi-source stress and hardware profiling. Fix discovered issues before main merge.
5. Record exact new source SHAs, branch, tests RUN/NOT RUN, ownership boundaries and a new per-stage handoff. No force pushes or overwriting concurrent agents' edits.
