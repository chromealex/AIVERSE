# TerraWeave main promotion — 2026-11-21

The user explicitly instructed «Заливай в мейн» after the local-flow fix. This
authorizes publication now despite the earlier broader-art readiness condition;
it does not mean all previously requested environment/physics work is complete.

Source branch: feature/aiv87_terrain_pbr; published feature/terraweave head
ab5d360f4cb2f2f1f2e99b78b0262f596c48f8d9. Remote main before promotion:
973b32970654cc5318fa6b6571f35d020501dcdd. Fresh fetch confirmed main is an ancestor
of this feature head (0 main-only /492 feature-only commits), with no merge
conflicts. `git push origin HEAD:main` succeeded without force, and
`git ls-remote origin refs/heads/main` confirmed exact ab5d360f4cb2f2f1f2e99b78b0262f596c48f8d9.
This is a fast-forward of the full already-integrated TerraWeave branch, including
the previously combined main/HTML5 work, not a water-only cherry-pick.

Principal recent code: ff84b6648bd3825eea62471614611fced9f196fc local current GPU
field;6402e0bbd28ceffa575a86ee283d4c1841322920 active foam GPU checks. Details,
public renderer ABI extension and unchanged simulation/save formats are in the
latest docs/nereid/HANDOFF.md entry. No code modifications or conflict resolutions
were necessary for this promotion. Existing build artifacts correspond to that
code; no redundant full rebuild or new GPU acceptance run claimed this stage.

Actually repeated before publication, KINGSTON terraweave-unified/native:
- `ctest --test-dir <native> -R '^(nereid_flow_map|ecs_nereid_renderer_contract_test|ecs_nereid_river_grade_test|ecs_nereid_surface_mesh_test)$' --output-on-failure`:4/4PASS .03s.
- `<native>/aiverse_tests lens_`:440checks0failed; log
  /Volumes/KINGSTON/AIVERSE-builds/terraweave-unified/main-publish-lens-tests.log.
- git diff --check PASS; worktree clean before publication.

Previous-stage Editor/Studio builds,22Metal/GL captures and14linked GLES/GLSL
variants remain recorded in NEREID handoff; they were NOT rerun for this unchanged
code promotion. CI not verified this stage; no CI success or URL claimed.
Original user checkout remains on feature/studio-html5 with its existing dirty
AGENTS/npm files; no checkout/reset/project rewrite or user-app restart performed.
Owned QA47326 remains stopped, user editor47325 still uses its loaded older binary.

Still open: live erosion-driven rock support/fall through actual Terra colliders
and PHYSIS/Box3D host, broader vegetation/age/season/live ecology, remaining
performance work, PRISMA video parity/deformed picking/native mouseY Undo, and
final combined GI/STRATA/VIVID art/site acceptance. Publication is not completion
of these requirements. Next: continue those stages from the published code,
retaining per-stage handoffs and actual tests; restart the editor to load changes.
