# SpectraFX Stage 126 — Mac regression retests green

Branch: `feature/spectrafx-html5-data-interfaces-20261123`
Expected tested revision: `0c3cdae2545aa4af94fcd54cba26d741ab2d7907` (Stage 125 prescribed checkout; supplied output does not independently print git HEAD).

User supplied CTest output: 100% tests passed, 0 failed out of 6. PASS: skill_catalog, aiverse_gameplay_tests, skill_audit, lens_metal_frame, native_project_build, meta_native_link. Native meta consumer compiled and linked its generated NativeRegistry.cpp and printed Native meta consumer passed (33.64 s). LENS GPU/Metal test passed (3.84 s). No failure or skip among these six.

Stage 125's prior full run on 2f0aba8f had 446 PASS, 5 FAIL, 1 SKIP of 452. All six outstanding tests now pass on the reported follow-up. This is not a single 452/452 run on the new revision. No source changes in this stage. Next: reuse existing build, enable AIVERSE_LENS_METAL_VALIDATE=1 and rerun full CTest with output-on-failure; preserve exact checkout SHA and complete log. Full current-revision acceptance, Vulkan and interactive Studio verification remain outstanding; merged-tree Mac quick gate required before main. Do not merge main in this stage.
