# AIVERSE Stage 102 — Avoid false material-library dependencies from literal texture paths

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Prior: docs/handoffs/aiverse-stage-101-headless-gpu-idle-tests.md

## Evidence
The user shared an incomplete 201-test CTest output (results 200/201
visible, no final summary). animation_model_dependencies failed at
tests/animation/model_dependencies.cpp:61:
```
CHECK failed: textureImpact.models.count("models/tree.glb")==0
```
The fixture type for tree.glb declares only normalMap
"textures/bark-normal.png"; the other type refers to the material
library material "steel" and its texture "textures/steel.png".
Changing the steel texture must NOT include the unrelated tree model.

## Root cause
RtsModelSourceDependencies.h::usesLibrary() recursively treated
EVERY string in an authored material JSON as a library-material ID,
including literal normalMap/texture/team/alpha values. Consequently
a type with only `normalMap` was added to libraryUsers_, and
`collectTextures(materialLibrary,type)` associated the entire
global material library with that unrelated type.

## Source change
Commit `042fca8f1a49b2cc6bdfef07cbd71eb3c3b17c96`:
- For material/assignment choice tables, resolve library usage from
  the direct choice values. A direct string denotes a library ID.
- A material object denotes library usage only when it has explicit
  string `use` or `base` (including nested objects/arrays).
- Literal string properties inside a material object no longer imply
  a library reference. Preserve nested explicit `use`/`base` detection.
- No new genre dependencies or GPU calls, and the source index remains
  CPU-only. Existing model-dependency regression covers tree/steel case.

## Verification and limitations
Actual: GitHub source read, exact anchored edit and update response.
Native build and CTest on updated SHA: NOT RUN by assistant.
A material authoring schema that allows unqualified string aliases
inside arbitrarily nested, nonstandard material objects may need
an explicit schema-aware resolver in future. Current top-level choice
aliases and explicit use/base are preserved.

## Remaining CTest work
Previously published fixes (still NOT RUN): Stage 99 ragdoll and
MotionWeave test fixtures; Stage 100 skills catalog/audit; Stage 101
headless GPU idle mocks. This Stage 102 covers the dependency index.
The copied CTest log lacked final results for #184 skill_library_isolation
and lacked CTest failure summary. Confirm remaining test outcomes from
the actual finished log, then rerun the nine known failed tests.
Only then run the complete CTest gate.

```sh
cd ~/Projects/AIVERSE-prisma-motionweave-clean
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
ctest --test-dir build/merge-validation --output-on-failure -R '^(aiverse_deferred_loading_tests|animation_ragdoll_asset|animation_model_dependencies|motionweave_cost_balancing|motionweave_source_bridge|aiverse_tests|skill_catalog|skill_routing|skill_audit)$' --parallel 4
```
Record the full results and any timeouts. Don't update main/source agents
or mark integration production-ready before native and GPU verification.
