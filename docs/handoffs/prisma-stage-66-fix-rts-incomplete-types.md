# PRISMA Stage 66 — macOS full-build blocker at 74%: incomplete RTS view types

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: feature/prisma-postfx
PR: https://github.com/chromealex/AIVERSE/pull/26 (Draft, no merge)
Previous handoffs: docs/handoffs/prisma-stage-66.md and
docs/handoffs/prisma-stage-66-fix-vfx-test.md

## Evidence supplied by user

Local validation worktree:
`~/Projects/AIVERSE-prisma-validation-20261119-222509`
Build: `build/prisma-final` (full project Release with tests/editor/HDR)
Local result: **BUILD FAIL, exit code 2 at 74%**.
The immediately preceding Stage-66 VFX test compilation blocker
is fixed: `aiverse_vfx_ecs_tests` linked successfully at 68%.

Clang diagnostics appeared in at least `RtsView.cpp`,
`RtsTerrain.cpp` and `RtsFx.cpp`, all including
`modules/rts/view/RtsView.h`:

```text
RtsView.h:222:33: error: member access into incomplete type 'aiverse::game::RtsRoadView'
    if (roads_) merge(roads_->ownedGpuMemoryStats());
RtsView.h:223:35: error: member access into incomplete type 'aiverse::game::RtsShoreView'
    if (shores_) merge(shores_->ownedGpuMemoryStats());
RtsView.h:141:7: note: forward declaration of 'aiverse::game::RtsRoadView'
RtsView.h:142:7: note: forward declaration of 'aiverse::game::RtsShoreView'
```

This is a **C++ include/completeness mistake** in GPU-memory
statistics code compiled by RTS presentation targets. It is
not evidence that PRISMA GPU history, HDR or Shader Graph
failed at runtime. CTest and HDR graphics were not reached.

## Root cause and actual source fix

`RtsView.h` declares `RtsRoadView` and `RtsShoreView`
as incomplete forward-declared classes, but had an inline
`RtsView::ownedPresentationGpuMemoryStats(const gfx::Device&) const`
body that called methods on `roads_` and `shores_`.
Every translation unit including that header therefore
required their full class definitions, causing the
three reported compiler failures.

Fixed **without** adding heavy road/shore headers to
the public view API and without changing the GPU
memory accounting algorithm or resource ownership:

1. `88794593f254d0169554b7c3075f9380249d662d` —
   `modules/rts/view/RtsView.h`: keep the public method
   declaration but remove its inline body.
2. `854e6263eb20000ad149e360b36b6741d6661c79` —
   `modules/rts/view/RtsView.cpp`: move the entire
   existing body out of line, directly after
   `RtsView::~RtsView()`. This translation unit already
   includes `RtsRoadView.h` and `RtsShoreView.h`,
   so both types are complete before the call.

This fixes header completeness without changing
signature, callsites, object lifetime, data layout,
`GpuOwnedMemoryCounter` saturation or per-owner
GPU-memory accounting.

## Verification actually done

- Retrieved both the original header from commit
  `d08737397da8b52ab026568ea9115cf512581b7a`
  and the post-fix source/header from connected GitHub.
- **9/9 static checks PASSED**: declaration-only header,
  out-of-line definition, exact *byte-for-byte* preservation
  of the old method body (1765 bytes), both full class
  includes, no duplicate method definition, road/shore,
  terrain, planets, ground FX, projectile stats retained,
  and out-of-line class destructor.
- **NOT RUN:** this change has NOT been compiled on
  the user's Mac; the prior user build was FAIL at 74%.
  Full CTest, native HDR, Metal/GL screenshot/pixels,
  backend GPU fence stress, profiler and CI still need
  actual results. Do not claim any of these passed.

## Exact next steps: continue existing Mac worktree

```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
git log -1 --oneline
bash run-prisma-validation.sh
```

Reuse `build/prisma-final` for incremental CMake compile;
do NOT delete it or recreate the worktree. Review any
locally modified tracked files before switching, never
reset/force-push another agent's work. The script records
new `prisma-logs` and `prisma-validation-logs.zip`.
On next failure, report the first actual compiler
or CTest message plus logs so it can be fixed on this
same feature branch. Never merge to main/base without
explicit approval.

This handoff was committed separately from the source
edits, following the AIVERSE agent documentation policy.
