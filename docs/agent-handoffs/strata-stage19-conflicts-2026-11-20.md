# STRATA Stage 19.2 — resolving two Renderer3D integration conflicts

Date: 2026-11-20. Repository: `chromealex/AIVERSE`.
Worktree: `~/Projects/AIVERSE-strata-stage19`, local branch `local/strata-stage19`.
Merge in progress: `origin/main` into `integration/strata-main-stage19`.
Base branch commit at worktree creation: `3a6f873695456afbf35f6ac1798ecfed2bef488b`.

## Observed merge result (user-supplied Git output)

Git automatically merged `Gfx.h`, `GfxMetal.mm`, `GlobalGeometryPool.cpp`, `CMakeLists.txt`, `AGENTS.md` and other paths. Only two paths remain unmerged:

- `engine/render/Renderer3D.h`
- `engine/render/Renderer3DModels.cpp`

## Conflict semantics

The `main` additions to `ModelDraw` are `primitiveMaterialFilter`, `useVertexParameters`, `vertexParameters` and `maxVertexOffsetMetres`. Main's renderer adds material filtering, bounded displacement visibility and LOD-safe bounds, and a rigid ShaderGraph uniform override. STRATA adds `GpuIndexedSelectionPacket`, per-primitive/per-pass GPU selection packets, `gpuPacketSource` exclusion from legacy material batching, render routing and shadows/reflection support. Both sets must be preserved.

## Resolution artifact

`resolve_strata_stage19.py` was prepared for the exact known in-progress merge. It reads Git stage `:2:` (STRATA) and checks stage `:3:` (main), transfers all the main-only `Renderer3D.h` and `Renderer3DModels.cpp` changes into STRATA source, preserving the full STRATA GPU model packet implementation, and performs strict unique-anchor checks. If merge inputs have drifted, abort rather than overwriting or guessing. It does not run tests, stage or commit changes.

The source is supplied as a downloadable file in the task conversation. Copy it into this repository or execute from a known absolute path. Before running, ensure only the above two files remain unmerged.

## Next commands on owner Mac

```sh
cd ~/Projects/AIVERSE-strata-stage19
python3 ~/Downloads/resolve_strata_stage19.py

git diff --check
git diff --name-only --diff-filter=U
git add engine/render/Renderer3D.h engine/render/Renderer3DModels.cpp
git diff --cached --check
git diff --cached --stat
```

If no unmerged paths remain, add this handoff file under `docs/agent-handoffs/strata-stage19-conflicts-2026-11-20.md`. Then finish the merge commit and **build only after the merge is fully resolved**. Confirm original `main` additions and STRATA draw packets survive by source review. The user requested one combined native validation pass after feature completion; do not report PASS without running it.

## Validations

- GitHub source comparisons for all main-only changes to these two files: **performed**.
- Local Python artifact syntax compilation: **performed**.
- Script executed on the user's conflicted worktree: **NOT RUN**.
- Merge commit: **NOT MADE**.
- Native CMake/C++ compilation, CTest, Metal/Vulkan/GLES parity: **NOT RUN**.

## Next stage

Run full integration validation and address any compilation/test failures. Preserve default-OFF STRATA GPU parity flags until hardware acceptance.
