# TerraWeave Stage70 — Mac Studio full 271/271 native acceptance

Date: 2026-11-20  
Repository: `chromealex/AIVERSE`  
Integration branch: `integration/terraweave-physis-stage70-20261120`  
Draft PR: https://github.com/chromealex/AIVERSE/pull/53

## User-observed real native acceptance

The user supplied terminal output from the **full unfiltered** Mac Studio
`tools/terraweave-main-integration-acceptance.sh` run in:
`/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa`.

The output concluded:

```text
269/271 Test #265: fps_animation_actions ... Passed 0.15 sec
270/271 Test #110: aiverse_tests ......... Passed 856.37 sec
271/271 Test #188: place ................. Passed 26.12 sec

100% tests passed, 0 tests failed out of 271
Total Test time (real) = 882.51 sec
PASS: merged native/editor/Box3D CTest
```

Labels from the supplied summary:

| Label | Count or observed CTest timing |
|---|---|
| Architecture | 0.61 sec*proc (4 tests) |
| ECS | 1.65 sec*proc (5 tests) |
| EDITOR | 5.36 sec*proc (4 tests) |
| MetalGPU | 2.33 sec*proc (4 tests) |
| NEREID | 0.36 sec*proc (1 test) |
| NETWORK | 0.64 sec*proc (2 tests) |
| PERSISTENCE | 1.47 sec*proc (6 tests) |
| PHYSICS | 0.81 sec*proc (2 tests) |
| PHYSIS | 11.81 sec*proc (31 tests) |
| RHI | 1.90 sec*proc (2 tests) |
| Renderer3D | 1.26 sec*proc (1 test) |
| SECURITY | 0.59 sec*proc (3 tests) |
| SpectraFX | 6.82 sec*proc (16 tests) |
| TERRAWEAVE | 0.48 sec*proc (1 test) |
| TOOLS | 0.33 sec*proc (2 tests) |

The user-supplied log includes the script's PASS footer and report location:
`build/terraweave-main-acceptance/acceptance-reports`.
The **271** CTest result supersedes the previous historical 191/191 suite
as a newer real native editor/Box3D local run, without retroactively
changing the provenance of the earlier run.

## Provenance and exact code state

GitHub readback after receipt of the terminal report showed current
integration branch HEAD:
`b17e775a922d9dc50981011c8f6e25d85fc7adec`
and current `main`:
`d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb`.

GitHub comparison `main...integration/terraweave-physis-stage70-20261120`
returned **ahead=155, behind=0**. PR #53 was open,
`draft=true`, and `mergeable=true`.

**Exact Mac SHA subsequently confirmed:** after the 271/271 report,
the user supplied the saved
`build/terraweave-main-acceptance/acceptance-reports/provenance.txt`:

```text
repo=/Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa
git_sha=b17e775a922d9dc50981011c8f6e25d85fc7adec
git_branch=
required_main_sha=d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb
observed_origin_main_sha=d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb
```

An empty `git_branch` is expected in detached-HEAD QA.
The GitHub comparison from exact tested SHA
`b17e775a922d9dc50981011c8f6e25d85fc7adec` to the
then-current branch HEAD `ddb7f2efad5b6e19c1736a2c1cebf5cb3b70b967`
confirmed **ahead=2, behind=0**, with **only**
`docs/terrain/CURRENT_HANDOFF.md` and this QA Markdown file changed.
No production code, CMake, tests or GPU source changed after the
confirmed 271/271 pass at that checkpoint.

The script's clean-worktree and mandatory-CTest-inventory gates were
reached before the PASS footer. The exact tested SHA and the exact
main ancestor are now confirmed from the actual Mac provenance;
a separate direct read of the local `ctest.xml` has not been performed.

No production gameplay/source code was modified in this QA
documentation stage.

## What the result validates

- Full native/editor/physics build completed far past earlier Mac
  compiler failures in `GfxMetal.mm` and the SpectraFX viewport windows.
- A single complete 271-test CTest pass reported **zero failed tests**.
- At least the listed named PHYSIS, NEREID, SpectraFX, MetalGPU, ECS,
  RHI, Renderer3D and editor test groups completed; the separate
  `aiverse_tests` and `place` executables also passed.
- The strict local acceptance script requires registration of
  `aiverse_tests`, `terraweave`, `terraweave_micro_nutrient`
  and `physis_terraweave_fall_bridge` before full CTest.
  Its PASS footer indicates that this guard was satisfied,
  provided the version of the script was the committed one.

## Remaining gates — NOT proven by 271/271

1. **Completed:** exact SHA and required main ancestor were confirmed
   from the user's saved `acceptance-reports/provenance.txt`.
   The optional `ctest.xml` remains available for deeper run forensics
   but is not required to attribute the observed 271/271 CTest result.
2. Actual rendered-output/pixel parity and visual inspection on
   **Metal** (including SpectraFX preview, forest foliage LOD,
   resource lifetime and resize), and **Vulkan/GLES** build/runtime.
   Passing labeled MetalGPU CTests is not a blanket proof of
   backend parity or screenshots.
3. Long-running real **PHYSIS × NEREID × TerraWeave × Box3D** terrain
   erosion, uprooting/fall, water/nutrient conservation, rollback,
   replay and performance stress, beyond existing automated coverage.
4. GitHub CI at exact tested SHA: at last remote inspection,
   jobs associated with `b17e775a` were queued; no completed
   passing CI was available as evidence.
5. PR #53 remains Draft. No merge to `main`, no claim of
   release-grade feature completeness and no silent changes to
   Stage70 archive schema/authoritative state.

## Immediate reproducible check on Mac Studio

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
cat build/terraweave-main-acceptance/acceptance-reports/provenance.txt
git rev-parse HEAD
git status --short
grep -E '100% tests passed|Total Test time' \
  build/terraweave-main-acceptance/acceptance-reports/ctest.log
```

**Provenance check COMPLETE:** the user supplied the saved report
and confirmed `git_sha=b17e775a922d9dc50981011c8f6e25d85fc7adec`,
`required_main_sha=d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb`.
The two later integration commits at that checkpoint were docs-only.

The next agent should handle actual GPU rendering and sustained
four-owner runtime acceptance; do not repeat the same entire CTest
only to revalidate documentation changes.
Once those are reproducible, decide whether to promote PR #53 or
start Stage71 as a **separate bounded feature** with its own
implementation/tests/handoff. Avoid duplicating NEREID's hydrology or
PHYSIS weather ownership.
