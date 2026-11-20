# TerraWeave Stage70 — exact Mac acceptance provenance CONFIRMED

Date: 2026-11-20  
Repo: `chromealex/AIVERSE`  
Working branch: `integration/terraweave-physis-stage70-20261120`  
Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## Evidence received (real native Mac Studio execution)

The user supplied the full-script footer from an unfiltered 271-test
`tools/terraweave-main-integration-acceptance.sh` run:

- `271/271` CTests PASS; `0` fail.
- Real CTest wall time: `882.51 sec`.
- `aiverse_tests` PASS (`856.37 sec`) and `place` PASS (`26.12 sec`).
- Suites included labeled MetalGPU, PHYSIS, NEREID, SpectraFX, EDITOR,
  RHI, ECS, PERSISTENCE, PHYSICS, NETWORK and Renderer3D.
- Script footer: `PASS: merged native/editor/Box3D CTest`.

The user subsequently pasted the saved
`build/terraweave-main-acceptance/acceptance-reports/provenance.txt`
from the isolated QA checkout:

```text
repo=/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa
git_sha=b17e775a922d9dc50981011c8f6e25d85fc7adec
git_branch=
required_main_sha=d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb
observed_origin_main_sha=d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb
```

**Provenance status: CONFIRMED** for exact tested source
`b17e775a922d9dc50981011c8f6e25d85fc7adec`.
Empty `git_branch` is expected for a detached HEAD. No inference
from the branch name was needed; the report explicitly identifies
the source commit and required main ancestor.

## After-run source integrity

Connected GitHub compare from tested `b17e775a` to the branch
HEAD at that checkpoint `ddb7f2efad5b6e19c1736a2c1cebf5cb3b70b967`:
`ahead=2`, `behind=0`, exactly two changed paths:

- `docs/terrain/qa-2026-11-20-stage70-mac-271-full-ctest-pass-handoff.md`
- `docs/terrain/CURRENT_HANDOFF.md`

Both were documentation-only. Following this evidence, QA status
was updated in the first document by source commit
`de1e796eeba0efc0b41226222aa10a54812d239d`.
That commit also modifies documentation only. Changes made to
documentation after a validated SHA do **not** require repeating
271 expensive CTests; if future commits modify code/config/test
sources, QA must be assessed again on those changes.

No production C++/Metal/Vulkan/GLES, native ABI, ECS persistence
format, nutrient ledger, TerraWeave/PHYSIS/NEREID water integration,
render paths, CMake targets, compiler flags or test selection was
changed in this provenance-recording stage.

## Exact checks performed

- PASS: user-provided full unfiltered CTest and script PASS footer.
- PASS: user-provided exact saved provenance SHA and main SHA.
- PASS: connected GitHub two-commit diff showed docs-only changes
  between tested source and later integration HEAD.
- PASS: updated the main 271-test QA document to remove its prior
  unconfirmed-SHA caveat.
- NOT RUN here: any new compile, CTest, CLI log collection or GPU frame
  capture. Do not confuse documentation updates with new runtime QA.

## Remaining mandatory gates (outside this checkpoint)

1. Actual Metal forest/SpectraFX preview GPU screenshots and
   create/resize/close GPU lifetime observation; Vulkan/GLES build
   and runtime/pixel checks where appropriate.
2. Prolonged integrated PHYSIS × NEREID × TerraWeave × Box3D world
   simulation, including erosion, tree fall, nutrient mass conservation,
   snapshot COW replay, deterministic rollback, large-world resource
   limits and performance.
3. Completed relevant CI from an exact reviewed SHA. Historic pending
   workflows are not proof of passing CI.
4. PR #53 is still Draft; do not merge until remaining acceptance
   requirements have explicit evidence or an approved scope decision.
5. Start Stage71 only as a separately documented feature change;
   NEREID remains water authority, PHYSIS weather authority,
   TerraWeave terrain/ecology/nutrient authority.

**Next agent:** perform the outstanding runtime/GPU acceptance on an
isolated checkout without changing the authoritative Stage70 v3 archive
or disabling test targets. Always append another committed handoff
with actual commands, measured outcomes, branch and SHA.
