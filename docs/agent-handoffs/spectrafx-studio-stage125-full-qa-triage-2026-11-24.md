# SpectraFX Stage 125 — full Mac QA triage

Branch: `feature/spectrafx-html5-data-interfaces-20261123`
Implementation: `6c8d5b1e1f6a91df23fdcf84bd15fda72106fe0d`
Tested SHA: 2f0aba8fbc699b299c40da34b17de4dc17a74700 (user archive spectrafx-qa-v13.tar.gz; Darwin, isolated KINGSTON build). Not evidence for the new code SHA.

## Actual acceptance evidence
Configure, focused build, architecture, editor/client build, full build: exit 0. Full CTest exit 8: 446 PASS, 5 FAIL, 1 SKIP of 452, elapsed 710.93 s. Focused SpectraFX 23/23 PASS, Metal 6/6 PASS, real required hardware evidence 5/5 PASS, zero missing/skipped hardware tests in that subset; CPU/GPU parity PASS. Full-suite LENS test 316 skipped because AIVERSE_LENS_METAL_VALIDATE was not set (source checks env presence). LENS must be rerun with opt-in enabled. Vulkan and Studio interaction not certified.

## Changes
- tests/native_project_build.py: copy canonical NativeRegistrar.cpp into synthetic engine; declare minimal aiverse_engine target so real Build.cmake sees new separated registrar contract.
- tests/meta/native_link.py: replace old in-fixture duplicate registrar with actual NativeRegistrar.cpp compiled against minimal Native.h; stub Native.cpp remains for gameplay target, declare minimal aiverse_engine interface. Keeps external native meta registration and consumer link assertions.
- tests/libraries/gameplay_tests.cpp: positive rotated footprint test now returns the requested territory owner (1), rather than default neutral owner (0); preserve exact six cells and coordinate checks, add explicit negative territory assertion. No policy or runtime changes.
- Stage 124 addressed the remaining two failures (skill_catalog, skill_audit) on later SHA 455e3995e69347ab461f33a87cdae67e41a2ab28.

Only test fixtures changed here; no new capability or runtime API, save format, authoring, performance, dependencies, ECS authority or platform claims. Reviewed committed diff against full logs and actual Construction.h, Build.cmake, NativeRegistrar.cpp and LENS test source. Post-fix compilation and test execution NOT RUN in this continuation; no main merge.

## Next
Reuse same Mac checkout/build. Fetch this branch, check out latest handoff SHA, build aiverse_gameplay_tests, then run CTest regex '^(native_project_build|meta_native_link|aiverse_gameplay_tests|skill_catalog|skill_audit|lens_metal_frame)$' with AIVERSE_LENS_METAL_VALIDATE=1 and verbose output. Once green, run full acceptance with LENS opt-in, Werror and required Metal, capture logs. No main integration before required merged-tree Mac quick gate. Preserve other agents' work.
