# Stage 38V — cross-owner procedural image access (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `2d4ca590347281bf9750ace1e923872c45ef1e48`
Plan: `6bbb00289091ba41b5d2f25992976676c25c6594`
Implementation: `881ac31e4b23123e851aeb8a4e65ddf0e173a1a2`

Stage 38V: VIVID sculpt GPU mask eligibility and both mask/vertex-source binding paths use the shared checked renderer resolver. Existing same-frame raw handle borrow contract remains; no independent GPU lifetime lease added. HumanAppearance CPU bake rejects runtime graph or invalid source binding and clears replaced base/MR pins and obsolete graph slots. HumanWardrobe requires offline CPU images before base composition, attachment append and final compaction, rejects live GPU ownership/pins instead of duplicating or remapping resident slots, and uses checked base image resolution for mask baking. TerraWeave impostor sampling validates generations and pixel extent. BakeScene uses checked source resolution and excludes runtime graph images from its offline index-keyed cache; fallback material factors remain. Existing human_appearance fixture extended with valid-pin runtime-source refusal, NOT RUN. Fourteen static source assertions passed. Cross-owner changes are limited to the texture access boundary; VIVID/TerraWeave/bake owners must review before integration. No default-on certificate or repository-wide completeness claim.

## Actual evidence

Read back seven exact committed blobs from GitHub. Fourteen static checks returned true: sculpt eligibility and binding, vertex source refuses invalid handle, bake checks source/refuses runtime/clears pins/clears graph slots, wardrobe offline guard at three entry points and mask setter, TerraWeave resolver and byte bound, offline bake cache exclusion, authored regression. These are source checks, not C++ compilation or runtime tests. Native build, VIVID sculpt hardware, wardrobe and appearance regressions, TerraWeave atlas, bake tests, GPU timing and merged-tree Mac gate NOT RUN. VIVID local owner instructions inspected; no direct coordination/approval from another agent obtained.

## Next

Stage 38W: audit remaining model image copy/export/merge consumers (GeometryScene, ModelMerge, tools/modeling and native model APIs), establish an explicit owner-scoped external read/borrow policy for stock client and Studio. Do not enable automatic collection by a constant true root callback. Investigate stale pin retention or untracked material mutations and model/device lease coverage. Consolidate previously authored regression tests and run on complete checkout before main. Keep all shared changes reviewable by subsystem owners.
