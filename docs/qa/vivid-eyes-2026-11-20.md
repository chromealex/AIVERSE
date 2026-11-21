# Ada eye material investigation — 2026-11-20

Branch: integration/motionweave-studio-facial-stage184.
All build and capture outputs: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/.
Artifacts below are relative to full-character/eye-review/.

## Evidence

Original baked eye PNG has brown iris and dark pupil (center RGB10). Removing
only eyeshell and eyeEdge did not remove gray glare (`no-shell.png`). Diagnostic
unlit left / roughness1 PBR right recovered color (`unlit.png`); unlit is not
the delivered material. A .45/.65 roughness pair with overlays confirmed the
PBR improvement (`roughness.png`). Final uses .6 PBR roughness, retained eye
geometry, occlusion alpha .12 and wet-edge alpha .04. No iris pixel repainting.

Matched native Metal catalog-light captures, portrait pose/time0:
`before-closeup.png` versus `final-closeup.png`. Brown iris/dark pupil now visible.
Both use identical camera and existing animation. Existing coarse iris detail,
bright sclera and eyelid/lash edge artifacts remain; no MetaHuman quality parity
or corneal refraction claim.

## Validation

KINGSTON native build: aiverse_studio, aiverse_vivid_creator,
aiverse_studio_human_appearance_session_tests PASS (`build.log`).
Node test_bake_materials.cjs PASS (`test-baker.log`), including translucent ocular
fallback regression. Scoped run-tests.sh --no-build --filter
'^studio_human_appearance_session$' PASS1/1; report native/test-reports/
20261120-193153-13311. GUI/CI NOT RUN.

New assembled candidate: VIVID-Look-Lab/content/Characters/Ada-Eyes-Reviewed.
Working content/Ada model factors and eyes.roughness value updated after visual
comparison; original model.gltf/appearance.json backed up in source-backup/.
Historical assembled assets remain unchanged. No save schema changes.

Second-light native capture: neutral-motion/ completed168 frames at offline24fps
with exaggerated head sweep. Inspected frame36 (turn) and41 (blink), saved as
neutral-turn.png and neutral-blink.png. Iris/pupil contrast survives turn; eyes
close, but jagged lash/eyelid edges remain visible. pipelineFailures=0. This is
not a realtime performance or full frame-by-frame quality claim.
