# TerraWeave Stage70 — Stage116 macOS Release test acceptance

Date: 2026-11-20
Repository: `chromealex/AIVERSE`
Branch: `integration/terraweave-physis-stage70-20261120`
Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## What was tested on real Mac Studio

The user provided output of the targeted post-main-sync Release build and
CTest from the isolated worktree
`/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa`,
build directory `build/terraweave-retirement-release`.

The output confirms successful build and link of
`aiverse_model_resource_owner_tests` and preceding target
`aiverse_retirement_pressure_tests` (the latter is inferred from
successful test invocation, while the visible beginning of the pasted
build log is truncated). The final CTest output was:

```text
Start 51: model_resource_owner_contract
1/2 Test #51: model_resource_owner_contract .... Passed 0.43 sec

Start 53: render_retirement_pressure
2/2 Test #53: render_retirement_pressure ....... Passed 0.31 sec

100% tests passed, 0 tests failed out of 2
Total Test time (real) = 0.75 sec
```

**Mac Studio Release Stage116: 2/2 PASS; zero failures.**
CTest names were cross-checked against the CMake registrations.
This is execution evidence supplied by the user, not a conjectural
status from queued GitHub Actions.

## Exact source and provenance limits

The validated changes originate in real two-parent merge
`910461a6d910dc0583762cb6dedfa4e9b97858b7`,
which integrates main
`f9aed9a705982e2c6b77b29869e309995ce8b163`
into Stage70. Only two pre-existing tests and one upstream
handoff were changed in that merge:
- `tests/render/model_resource_provider_contract.cpp`
- `tests/render/retirement_pressure.cpp`
- `docs/handoffs/aiverse-stage-116-release-asserts-pressure-model-owner.md`

At time of the user's report, GitHub branch head was
`9d60a37d3a749d81094a6759428523fbc720a696`.
Comparison from merge `910461a6` to head showed only
`docs/terrain/CURRENT_HANDOFF.md` and
`docs/terrain/qa-2026-11-20-stage70-stage116-main-merge-handoff.md`
changed (documentation-only, ahead=3). GitHub comparison with
main found ahead=191, behind=0.

**Caveat:** the pasted terminal output lacks
`git rev-parse HEAD` or a Release provenance report. It therefore
confirms two Mac Release tests, but does not independently prove
the exact Git SHA executing them. The recommended run was on the
postmerge integration branch, and the merge-to-head differences
were documentation-only. Do not assert that a new full suite
ran at `9d60a37d`.

## Existing distinct accepted evidence

- Full native/editor/Box3D Mac acceptance 271/271 PASS:
  exact `b17e775a922d9dc50981011c8f6e25d85fc7adec`.
- Real SpectraFX Metal 4/4 PASS, no skips and 5/5 PPM predicates:
  exact `b1bb27c8f1e9f803ff49af29ea7dc0769b9cb264`.
- Real TerraWeave Arbor/Flora Metal forest 1/1 PASS, 2.43 sec,
  six actual PPM captures: exact
  `0133eb21b211af62d205952db77c50589ae55a13`.
- Stage114 Mac Release retirement 2/2 PASS, separately
  recorded in `qa-2026-11-20-stage70-stage114-mac-release-retirement-green-handoff.md`.
- Stage116 Mac Release retirement pressure/model owner 2/2 PASS
  (this stage), no C++ or shader modifications.

## GitHub Actions status at handoff time

The dedicated new
`TerraWeave real Vulkan GLES3 forest acceptance` workflow had a
queued run `44729549836` on commit `15a1fbeef5d0...`.
General integration and TerraWeave isolated runs associated with
later documentation/source commits were still queued or pending.
A queued/pending workflow does NOT qualify as successful CI.
Check the run at
https://github.com/chromealex/AIVERSE/actions/runs/44729549836
for real Linux configure, compiler and backend execution results.
No Linux ARM64 GCC `-Werror` success is claimed here.

## Files changed in this stage

Only this standalone Markdown handoff and a pointer in
`docs/terrain/CURRENT_HANDOFF.md`. No production C++, CMake,
Vulkan, GLES3, Metal, game simulation or save schema changed.
No tests were executed by the documentation authoring session.

## Remaining work / next agent step

1. Require a completed result for Linux Vulkan and GLES3
   `terraweave_vulkan_forest` and `terraweave_gles3_forest`
   with real captured PPM evidence; diagnose the first error
   if CI reports one.
2. Run/author a long authentic single-World
   PHYSIS × NEREID × TerraWeave × Box3D erosion/fall/rollback
   acceptance, not merely the earlier 128-step pure nutrient
   sampler longevity fixture.
3. Visually inspect the six saved Metal forest frames, evaluate
   actual Studio viewport resource lifetimes/LOD transition.
4. Confirm relevant native/GCC CI outcomes on the final merged SHA.
5. Keep PR #53 Draft until the outstanding gates have evidence
   or an explicit, documented reduced-scope release decision.

After each bounded work stage, commit a fresh standalone handoff
that identifies what code actually changed, commands/tests that
really ran, exact SHA when proven, limitations and the next step.
