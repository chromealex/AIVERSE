---
name: aiverse-gpu-batching
description: Profile and verify AIVERSE material/instance batching, Metal GPU counters, draw-call limits, cache lifetime and real A/B renderer captures without mistaking synthetic workloads for shipped gains.
---

# GPU batching performance and correctness

Use when modifying or assessing instanced rendering, material batch grouping, shader/vertex workload, ICB paths, GPU counters or renderer cache lifetime. Keep it separate from general STRATA geometry/HLOD policy work.

## Source of truth

Read `docs/geometry-performance.md` and `docs/metal-batching-agent-handoff.md`; inspect `engine/render/Renderer3D.cpp`, `engine/render/Model.h`, and `engine/render/ModelMerge.cpp`. Consult the latest relevant `docs/agent-handoffs/` batching notes for exact hardware and capture evidence.

## Procedure

1. State the backend, GPU, frame resolution, scene fixture, shading path, material variants and frame count. Changes to draw counts must not alter occlusion, transparency ordering, lighting or visibility.
2. Keep baselines comparable: portable uniform path is 32 instances per draw; Metal may expose a larger per-draw limit, but selected limits and shader variants must match. Use the documented `Renderer3D::setInstanceLimit(32)` for a control and zero for backend-selected limits.
3. Measure **both** CPU preparation/submission and GPU vertex, fragment and raster cost with actual captures. Report draw calls, total visible instances, triangle counts, GPU timing, counter availability and error bars where appropriate.
4. Validate cache ownership: `StaticModelCache` lifetime, material identity, borrowed texture owner lifetime and shadow/transparent exclusions. Never treat matching texture filenames as proof of compatible GPU resources.
5. Compare real scene captures (including Mac Metal where relevant) against a stable baseline. Keep synthetic load tests separate from production metrics and explain backend-specific differences.
6. Run the matching repository renderer tests and A/B scripts documented in `docs/geometry-performance.md` or the current batching handoff. Do not invent a successful run, performance uplift or ICB path support.
7. Record exact tested SHA and platform, commands, logs, captures, limitations and next action in `docs/agent-handoffs/`.

Workflow over existing renderer code and profiling tools; no separate runtime library.
