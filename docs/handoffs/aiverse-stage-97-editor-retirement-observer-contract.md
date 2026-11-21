# AIVERSE Stage 97 — Editor retirement observer after generic ModelResourceCache migration

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Base commit: ecac335ca5f93238f98003490d1ed8e7b91dddba
Previous handoff: docs/handoffs/aiverse-stage-96-rtsview-api-merge-fixes.md

## User-provided native failure
The full macOS Release build of ~/Projects/AIVERSE-prisma-motionweave-clean got to 92%:
```
apps/editor/Editor.cpp:185:5: error: 'this' argument to member function
'setRetirementDiagnosticObserver' has type 'const ... RtsModelResourceCache'
apps/editor/Editor.cpp:274:5: same error
engine/render/ModelResourceCache.h:469:
void setRetirementDiagnosticObserver(RetirementDiagnosticObserver observer)
```
The existing public `RtsView::modelResources() const` returns a
`const ModelResourceCache&`, intentionally read-only for callers such as
VisualAssetDependencyService::scan; the observer setter mutates the
retirement queue and is owner-thread constrained.

## Implemented API reconciliation
- modules/rts/view/RtsView.h:
  add `setModelRetirementDiagnosticObserver(ModelResourceCache::RetirementDiagnosticObserver)`
  as a *narrow forwarding method*. It delegates to owned
  `resources_.setRetirementDiagnosticObserver(std::move(observer))` and
  preserves owner-thread/detach-callback checks in ModelResourceCache.
  Keep `modelResources() const` and don't add a general nonconst accessor.
- apps/editor/Editor.cpp: both observer installation at editor init and
  removal after `view_.shutdown(*dev_)` call the new forwarding method.
  The GPU trace ring, Model vs Image tags and shutdown order stay unchanged.
- Shared GPU owner internals, rendering and unrelated genre systems unchanged.

## Actual verification
- Reviewed RtsView read-only accessor, cache's diagnostic observer setter,
  Editor init/teardown call sites, VisualAssetDependencyService use.
- Wrote source changes in a single integration-branch commit with this handoff.
- Textual checks: const read-only accessor preserved, new forwarding API
  present, exactly two Editor call sites replaced, no stale calls remain.
- Native compile, CTest, GPU tests: NOT RUN by assistant. The latest
  user-supplied failure occurred on the older local checkout, before
  Stage 97; build confirmation must come from Mac.

## Next step
Preserve the clean worktree and CMake cache:
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-clean
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
```
Once all targets link, run one consolidated CTest.
Do not modify main/source agent branches or force-reset local work.
