# AIVERSE LENS Stage 15 — compact prepared sprite packets (2026-11-20)

## Branch and implementation commits
- Branch: `feature/lens-motionweave-integration`; `main` unchanged.
- Implementation commit `725805dbb9c251268d8cfd23aa17a2b8bfc1e66a`: `modules/rts/view/Rts2DView.h`.
- Implementation commit `8f600871130c73277529be11cadf628ac1681a97`: `modules/rts/view/RtsSprites.cpp`.
- Preceded by `docs/agent-handoffs/lens-motionweave-integration-stage14-universal-light-snapshot-2026-11-20.md`.

## Architecture and API
- The previous Stage 13 `PreparedSprite` stored a *complete* `SpriteLook` by value for every sprite. Each look contains a `std::string`, a texture ref, animation descriptors and shadow parameters; this was costly to copy for repeated units.
- `PreparedSprite` now borrows `const SpriteLook*` from the fixed-size `looks_` table and records two per-entity dimensions (`width`, `height`) in addition to pose, facing, action/time, colors, corpse/fade.
- The private `emit` signature now receives an explicit `sourceLook` pointer. Normal, scaled and corpse call sites pass the canonical `&L`, **never** the address of the stack-local scaled look.
- On a secondary camera, ordinary packets emit directly from the canonical shared look. Only packets with non-default dimensions instantiate a temporary `SpriteLook` and override its width/height. All visual animations, directional atlas rows and per-camera billboarding still use the frozen frame clock and view basis.
- The primary camera's rendering, entity FX and corpse rules remain unchanged. ECS schema, save format, public `Rts2DView` API and camera renderer APIs remain unchanged.

## Lifetime assumptions and hazards
- `looks_` is initialized to a fixed size and `loadType` replaces entries in place; source pointers are stable only while the `Rts2DView` remains initialized. Secondary submission MUST complete before `shutdown` or reinitialization. Asynchronous/off-thread capture would require owned immutable look storage or stable reference-counted handles.
- Asset hot reload between primary snapshot creation and secondary drawing is unsupported until explicit version-pinned asset lifetime management exists.
- `secondaryBatch_` still rebuilds and uploads dynamic sprite geometry for each camera. This step reduces CPU snapshot copies but does not claim fewer GPU uploads, render passes or draw calls.
- Camera-specific quality/visibility, order of callback invocation, resource retirement and exception safety still require verification.

## Actually performed checks
- GitHub source inspection for all `Rts2DView::emit` call sites and Stage 13 geometry workflow: done.
- After commit, inspected the fetched new source: all direct calls provide source look, no by-value packet copy remains, primary basis restoration and secondary flush code still present.
- CMake/native compilation: **NOT RUN**, this session had GitHub connector access but no checked-out native AIVERSE build.
- CTest and GPU backend Metal/Vulkan/GLES validation: **NOT RUN** (user requests consolidated test pass).
- Benchmarks: **NOT RUN**; no performance percentage or improvement proven.

## Next actionable steps
1. Reduce redundant dynamic `SpriteBatch` CPU vertex building and GPU buffer uploads per camera without sharing a mutable GPU buffer across concurrently outstanding passes. Respect existing GPU retirement and dynamic buffer ring constraints.
2. Add acceptance cases for identical sprites with different `EntityFxValues` scales (including deaths), two differently oriented secondary cameras and stable atlas rows; compare to Stage 13 output.
3. Integrate latest MotionWeave branch only via inspected conflict reconciliation, preserving editor/animation contributions.
4. Perform consolidated native configure/build/CTest and Metal, Vulkan and GLES compilation; run GPU visual parity checks on a compatible host.
