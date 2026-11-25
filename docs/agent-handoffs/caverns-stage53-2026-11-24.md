# Caverns Stage53 — STRATA paging and source admission

Date: 2026-11-24. Branch: feature/terraweave-caverns-core-20261123. Main unchanged.

## Actual commits

- 01f28f4ef559a9d8aaccf63587bb25800d9a4098: CaveStrataPaging configuration and existing GeometryScene setters.
- 745e1334100bc3b2ab95874e4cbf70c6fe2a96eb: apply paging configuration to staged swap owners.
- 56d8ef203b4d989c3856d1cc5b22821e76f45dc6: pass policy through shard staging.
- 80e9b342eacf5149d58dfabd6cda7d88e54f2f17: estimated active/staged/retired source payload and STRATA arena telemetry.
- 5d5949ccb42467b78253194fdd5d112599b97924: aggregate live source/scene admission limits, ModelVertex size estimate.
- 451e9a5eff07daa172b591a5244cdd256ca7bb04: shard host aggregate counters and admission.
- ffd51a7080afe14659b3e5597bc138394396bc27: freeze batch policy, expose telemetry, use worker inFlight accessor.
- e56d1f8fc5a756bf222ffc6d6e96275993d29e10: runtime telemetry accessor.
- 94fe6e4ff187c370fb70e0b8501c7fce87c83ff8: skill update.

## Contract and limits

Paging is opt-in via CaveBatchSpec. Supply matching nonzero per-source, per-shard and device-wide index and upload limits; global settings must agree with other GeometryScene users on the same device. Existing STRATA determines supported paging and fallback routes. Initial root uploads are not globally throttled by these frame update budgets. Source VB/IB remain resident; whole-shard loadPrepared remains synchronous. Source admission is optional via setGpuUploadLimits, local to the shard host and based on estimated ModelVertex/index payload, not physical VRAM. Retired and failed swap owners continue consuming source estimate until released. Ensure enough replacement headroom for a complete atomic batch; insufficient permanent budget requires a host policy change or smaller shards. The existing nearest-first CPU stream scheduler is unchanged; no new GPU priority scheduler is implemented. No duplicate STRATA pool or ledger.

## Next stage

Stage54: integrate Caverns into the actual HTML5 Studio branch using the existing editor style library, native MCP/authoring APIs, object references, brush controls, streaming/paging settings and diagnostics. Inspect current studio HEAD and coordinate integration without modifying main or silently overwriting concurrent changes. Then close remaining native integration gaps and run the mandatory KINGSTON native QA gate. Outstanding active compact-buffer replacement lifetime, catalog watermark growth, external stream ownership/restore, native NavMesh residency, cross-shard retirement, deterministic replay parity. No native compilation, CTest, GPU or memory validation performed. Feature not complete.
