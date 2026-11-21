# STRATA Stage21 — system QA, 2026-11-20

Branch: codex/strata-batching-integration. Tested main base: `076139d12d9ea2380a5445f823323f4e2c096811`. QA report commit: `5efea1c2285fd79610a3c04c0e30009da8de63fc` (`docs/qa/strata-system-2026-11-20.md`). This stage changes documentation only, no public API, runtime, save/ECS, shader or production defaults. The user requested verification, not product fixes; do not silently fix findings as part of this report.

## Executed

All builds/cache/temp/logs on KINGSTON. Evidence root `/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120`; report.md, cases.json, sha256.json. Build root `/Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120/debug`. macOS15.6/Apple M3 Ultra; actual executable is x86_64 under Rosetta, not native arm64. Assertions enabled Debug -g0.

- CMake build all 21 existing aiverse_strata_* executable targets: PASS, no warnings/errors. Target list saved build-targets.json.
- `ctest --test-dir <build> --verbose --output-on-failure --no-tests=error --parallel 4 -R '^strata_' --output-junit <evidence>/ctest.xml`: 15/15 CTest Passed, 1.34s. IMPORTANT: Vulkan GPU portion printed loader-missing SKIP and returned0. Only its GLSL compiler portion ran. JUnit skipped=0 is misleading; not Vulkan runtime acceptance.
- Six actual Metal fixtures: culling, temporal(3 submissions), raster, depth/multiview, reflection/material, portable: 6/6 PASS.
- `ctest ... --repeat until-fail:10 --parallel 4 -R '^strata_(disk_pages|streaming_coordinator|index_pages|index_residency|portable)$'`: 50/50 invocations PASS.
- CPU LOD benchmark three runs: deterministic checksums; 240frames, h0 transitions122 vs h.2 transitions2; retained triangle sum880 vs1120. CSV malformed in all3 runs (literal backslash-n, one parsed row). CPU Debug/Rosetta timing is not GPU/FPS proof.
- Existing Stage18 image-audit selftest PASS and partial sun-depth manifest 8/8 pairs exact. 16/16 saved PPM pairs exact and nonblank. PPM is RGB; native fixtures compare complete RGBA in memory. Real reflection preview visually inspected.
- Rebuilt material_batches and ran unit/CPU MDI/static packets/GPU ICB:4/4 PASS, exact pixel parity.

## Findings and limits

Report has23 cases:12PASS/4FAIL/2BLOCKED/3NOT RUN/2OBSERVED. Four reproducible findings: Linux GCC ARM64 build blocker at tests/render/ktx2_transcode.cpp:120 (-Werror=address), Vulkan runtime skip reported Passed, malformed benchmark CSV, outdated Current public contract in strata.md. First issue is adjacent Texture RHI, not a proven STRATA rendering bug. Actual logs of jobs136370758504 and136378899190 retained; both fail at identical KTX2 assertion. Current-main CI https://github.com/chromealex/AIVERSE/actions/runs/44762599366/job/136378899190 . Focused Vulkan remains queued, not green.

NOT RUN / BLOCKED: real Vulkan/GLES/GL/WebGL visuals, spot-shadow depth, lossy mixed-depth seam ROI, hardware residency-pressure orbit and240-frame hardware popping, native arm64 binary coverage, sanitizers, production assets/performance budget. Recording backend names do not establish hardware parity. No editor UI end-to-end claim. No global test settings changed, fixture processes exited; own artifacts remain on KINGSTON.

## Next

Get a separate fix request for QA01–04, fix the minimal diagnosed test/docs issues, then execute uncovered hardware/visual acceptance. Never enable experimental production defaults from this limited pass. QA report and this separate handoff are committed locally on the same branch; no product/main edits, no new Jira ticket or external messages.

## Authorized fix follow-up

User subsequently requested all four fixes and publication. Code `32535df16573d3c6bcfd8b0be28aa5b26d0e29ed` and [Stage22 handoff](strata-stage22-qa-fixes-2026-11-20.md) correct the diagnosed issues and record16PASS/1SKIP, strict Vulkan failure, CSV and compiler checks. Missing hardware/visual acceptance remains open.
