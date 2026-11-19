# PHYSIS Stage 103 — actual current-main two-parent merge created; native revalidation pending

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Staging branch: `integration/physis-terraweave-onto-main-20261119`
New main-target Draft PR: https://github.com/chromealex/AIVERSE/pull/46
Older source PRs: #40 (old PHYSIS base), #45 (source direct to main, was conflict/dirty)
Previous stage: `docs/agent-handoffs/physis-stage102-main-rebase-gate-2026-11-19.md`

## Real Git history

The integration is no longer just a proposal. The actual committed
two-parent Git merge is:

```
e3851a46ea2db22594348cf103c40c15b1017528
tree d7f8f3ef8151ce03a851414e582b24f0afa577a9
parent1 921ccad365be8df8b5b9f2ec105d2e2aead7d004
parent2 658285a6ec22054ef9d382ef324ddd01ebe9d9b8
```

Parent1 is the staging document commit on **main**
`d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`.
Parent2 is the original fully tested PHYSIS+TerraWeave+Box3D source.
Common ancestor for main and that source:
`d71c6d47e532b25fb816a8bb1f96c41e5275a165`.
`main` has **NOT** been modified by this stage.
The after-merge PR #46 was checked and reports
`mergeable=true`, `mergeable_state=unstable` (CI/checks pending),
with main still at `d7d9b3f4`.

## Exact Git tree merge method and preservation proof

Three recursive GitHub trees were read from the exact common ancestor,
current main and feature head; they were compared by full blob path,
SHA and file mode, not merely truncated REST compare pages.

Source:
- 388 feature-only changes: adopted feature blob SHAs/modes.
- 83 main-only changes: retained current main blob SHAs/modes.
- 11 files concurrently edited: content was three-way merged using
  exact original base-line anchors, and each output written as a new
  Git blob. Each original base-to-side line patch was reconstructed
  and byte-equal to the source before merging.
- 4,499 unchanged paths retained identically.
- 0 feature deletions; all added paths present.
- A separate Stage102 handoff was already on staging.

A recursive verification of the **new combined tree** checked all
4,981 paths in the union against expected feature/main/merged blob
SHA + mode. Result: **0 mismatches**; 4,982 blob paths total, the
only extra being the Stage102 handoff.
No whole-tree replacement with an old branch or force-push occurred.

The 11 concurrent files:

```
AGENTS.md
CMakeLists.txt
apps/editor/Editor.cpp
apps/editor/windows/SceneWindow.cpp
engine/gfx/Gfx.h
engine/render/GlobalGeometryPool.cpp
engine/render/Model.h
engine/render/Renderer3D.h
engine/render/Renderer3DModels.cpp
skills/catalog.json
tools/skills/catalog.py
```

## Semantic resolution details (nontrivial conflicts)

- `AGENTS.md`: keep separate newly appended main renderer/MDI
  rules and feature PHYSIS/TerraWeave ownership rules.
- `CMakeLists.txt`: keep main ECS collision namespaces test
  registration and independent feature TerraWeave Horizon baking target
  at their common insertion point, along with both distinct CMake
  edits elsewhere.
- `engine/render/GlobalGeometryPool.cpp`: main replaced legacy
  `canPool` with `geometryPoolAdmission`, introducing rigid LOD
  policy, retired-pair recycling and backend safety. Keep main's new
  admission policy, but reject assets with
  `!model.batching.allowGeometryPool` alongside explicit
  `PoolPolicy::Dedicated` to preserve TerraWeave's per-model opt-out.
  `engine/render/Model.h` retains both the new main
  `GeometryPoolPolicy` and TerraWeave `allowGeometryPool` field.
- `engine/render/Renderer3DModels.cpp`: keep main's
  cached static eligibility/shared material and packet-replay pipeline.
  Carry over TerraWeave's guarded light baker, bounded
  `maxVertexOffsetMetres` culling envelope and per-instance vertex
  graph/material draw behavior. Reject reuse/build of static cache
  or packet replay on `d.useVertexParameters` or
  `d.primitiveMaterialFilter>=0`; this avoids stale cached draw
  assumptions. Other independent source hunks were combined
  at exact base offsets.
- `apps/editor/Editor.cpp`, `SceneWindow.cpp`,
  `Gfx.h`, `Renderer3D.h`, `skills/catalog.json`,
  `tools/skills/catalog.py`: the two sides edited separate
  base line ranges; all unique edits preserved.
  The combined skills/catalog.json was parsed as JSON before writing.

## Verified vs NOT RUN

**Verified by Git object and static source checks:**
- Both merge parents, combined tree SHA, complete union-of-paths blob
  integrity, preservation of all main-only and feature-only paths.
- New PR #46 targets current main and has no textual merge conflict
  according to GitHub (`mergeable=true`); CI state may still be
  unstable/pending.

**NOT RUN on the new combined-main tree:**
- AppleClang/CMake configure and compile.
- Any native CTest, GPU/backend runtime or scene render parity.
- New combined-main 167+/167+ acceptance.
- Exact behavior of combined new GPU caching and feature vertex
  parameters on a physical GPU.

Previous **167/167 PASS** on Mac Studio covered original
`658285a6` source only. Do not extend that result to `e3851a46`.
In particular, current main adds ECS/static scene tests, so the
complete CTest count on the new merged source may differ from 167.

## Next run: reuse previous native build cache, keep main safe

There was only ~11 GiB free on the Mac Studio at last successful
acceptance. Before a large recompile of main's newer ECS/render code,
check `df -h ~/Projects` and free identified *stale, expendable*
build cache, not another agent's working tree or the 6.8GiB
acceptance cache.

Suggested reuse of the existing isolated final worktree, after
confirming it has no uncommitted changes:

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-onto-main-20261119

# Same full acceptance. This now tests the MAIN-COMBINED tree.
AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh
```

This invocation configures Debug + optional Box3D + optimized Place
hot dependencies, builds all selected desktop targets and runs the
FULL unfiltered CTest once, writing JUnit + logs under
`build/physis-terraweave-onepass/onepass-reports`.
If native failures appear, fix them ONLY on staging branch, record
real errors/test results and a new Stage104 handoff. Do not modify
`main` until combined-main acceptance is green.

## Separate Place roadmap, not silently rolled into merge

The user also requested:
- Rename the misleading aggregate `place` test and separate
  functional and server-performance coverage.
- Dedicated streaming stress tiers including 250 clients and 100k
  ECS world entities, distinguishing from 100k live Box3D bodies
  (current physics caps at 65,535; Place default 4,000).
- Independent server 10–15Hz authority, snapshots 10–15Hz and client
  prediction/render 30+Hz without network/physics time-stepping drift.

These are a subsequent separate branch, to preserve the full-green
PHYSIS source gate during current main reconciliation.
