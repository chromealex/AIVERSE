# SpectraFX Stage 156 — fire with smoke

Branch: `codex/spectrafx-volumetric-scene`. Roadmap-first commit: `c9cf25a21`. Effect and test commit: `f1fa520cb`. Base was the pushed Stage 155 feature head `d35266858`; `origin/main` was `1b77c21bf` and already merged into this worktree before Stage 156.

## Result

`examples/spectrafx/fire_smoke_v2.vfx.json` is an editable Graph v2 volume effect. One continuous source supplies smoke, heat and fuel; shared advection, combustion and pressure nodes form warm flame near the source and a rising gray plume. There is no parallel solver or genre-specific path. To place it in a flat project scene, copy it to `content/fx/graphs/fire_smoke.vfx.json`, attach an enabled ECS `SpectraSource` with effect `fire_smoke`, and enable host HDR. This scene-placement recipe is documented but was not exercised in a game project in this stage.

The native Studio Metal preview loaded the exact example file and captured two real frames: `docs/devlog/media/2026-11-24-spectrafx-fire-smoke/01.png` at early burn and `02.png` with a developed smoke plume. The second capture is 512×512. The dedicated CTest requires native load, published volume rendering, a captured GPU frame, warm emission pixels and gray plume pixels. A failed load initially left the prior preview alive; the test now rejects that by checking `error` and `volume` on the returned status before capture.

## Verification actually run

- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/spectrafx-volumetric-scene --target aiverse_studio_spectrafx_preview_tests -j 4`: PASS.
- `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/spectrafx-volumetric-scene --output-on-failure -R '^studio_spectrafx_(preview|fire_smoke)$'`: PASS 2/2 on macOS Metal.
- The same dedicated fire/smoke test with `AIVERSE_SPECTRA_FIRE_CAPTURE=/private/tmp/spectrafx-fire-smoke.ppm`: PASS; output converted to PNG and inspected visually.
- `/usr/local/bin/python3.12 tools/site/build.py check`: 50 entries, 0 errors.
- `git diff --check`: PASS.

## Limits and next step

The current flame is a smooth column and the smoke a broad gray plume; turbulent tongues, sparks and lighting interaction are not part of this asset. The exact asset has been validated in native Studio preview on Metal. Authored game-scene placement, Vulkan pixels, multiple-emitter performance and full interactive HTML5 editing were not run. Keep the devlog entry and roadmap item `in-progress` until main integration.

Next: place the published graph into a saved project scene and inspect the result under game lighting, then consider a separate graph/material stage for turbulence and sparks. After integration into main, mark the roadmap item done, reconcile the SpectraFX system page, and publish the site from clean `origin/main` through the documented workflow.
