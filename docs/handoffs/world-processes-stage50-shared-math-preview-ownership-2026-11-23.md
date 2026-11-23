# AIVERSE World Processes Stage50 — shared scaling math, numeric pivot marker and multi-window preview ownership
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles; main untouched; PR #112 draft.

## Implemented
- `libraries/include/aiverselib/world/EvolveAreaScaleMath.h`: shared pure integer-centimetre `evolve::scale_math::transform` for a scaled center and half extent, with integer ratio and individual/custom/shared pivot support and overflow checks.
- `apps/studio-native/StudioWorldProcessesEditing.h` and `apps/studio-native/StudioSession.cpp`: both numeric two-axis preview and batch resize commit now use the same axis transform, reducing rounding differences. Validation and bounds checks remain in their respective callers.
- `apps/studio-native/StudioHost.cpp`: numeric custom pivot input updates the transient native group pivot marker (without MapDoc/ECS mutation).
- `apps/studio-native/StudioHost.h` and `StudioHost.cpp`: preview owner token and per-owner revision checking. An inactive window's clear cannot erase another window's preview. New owner can take over preview, and stale revisions for the current owner are ignored.
- `apps/studio-web/src/components/EvolveGroupNumeric.tsx`: sends a per-window owner token with preview/clear requests.

## Limitations
- Owner tokens use a per-web-module session identifier; a second window can take over the preview when it sends an update. This is last-active-window ownership, not a multi-window preview compositor.
- Shared transform handles the axis math, but all UI validation, world-bound checks and MapDoc transactions are still distinct and must be tested together.
- Group-centroid computation and selection changes can reset transient custom pivot.
- No C++ compile, TypeScript typecheck, runtime Scene interaction, CTest, screenshots, Undo/Redo, persistence or gameplay QA executed (user defers tests). Source-only, not accepted.

## Next
1. Add explicit preview ownership release and lifecycle handling for floating Studio windows.
2. Align all selection/visibility and typed authoring validation across preview and commit.
3. Once authorized, compile C++/TS, run targeted tests and real Studio acceptance, and resolve any integration issues before merging.
4. Keep draft PR #112 unmerged until QA.

## Source commits
`a9af0e28`, `4be51b21`, `d46722b8`, `0710238f`, `04899219`, `02b30271`, `d2273770`.
