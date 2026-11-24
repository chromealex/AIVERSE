# SpectraFX Stage 157 — fire and smoke shaped from visual reference

Branch: `codex/spectrafx-volumetric-scene`. Implementation commit: `706df45d4`. Starting feature head: `518fc45e1`. `origin/main` remained at `1b77c21bf`; no main merge or site publish in this stage. The prior fire-and-smoke roadmap item remains `in-progress` until integration.

## Result

The user supplied a visual reference with a bright fire source at the lower left, a long dark plume rising diagonally right, irregular smoke and sparse embers. The editable `examples/spectrafx/fire_smoke_v2.vfx.json` graph now uses two ordered continuous volume sources: hot fuel near the base and denser smoke with lateral/upward acceleration. The 64³ domain, wind and buoyancy bend the plume. The effect remains shared SpectraFX volume data, not a genre-specific renderer or parallel gas model.

Optional `VolumeOutput.smokeDetail` (0..1) and `emberIntensity` (0..10) default to zero. World-space two-scale value noise modulates smoke extinction; a sparse hash produces emissive ember samples inside occupied smoke. CPU `GasGrid::march` and the shared Metal/GLSL render body use corresponding formulas. Asset read/write and native/HTML5 node defaults expose the fields. Existing assets without them keep the previous material defaults.

The actual Metal Studio frame is `docs/devlog/media/2026-11-24-spectrafx-fire-smoke-plume/01.png`. It was captured at 512×512 after 240 fixed simulation frames and inspected visually. The image is an engine capture, not the supplied reference or a generated illustration. The native test rejects stale fallback previews and checks the captured frame for warm flame pixels, a dark smoke region and sparse warm ember pixels.

## Checks actually run

- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/spectrafx-volumetric-scene --target aiverse_studio -j 4`: PASS including final native Studio link.
- `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/spectrafx-volumetric-scene --output-on-failure -R '^(studio_spectrafx_preview|studio_spectrafx_fire_smoke|vfx_graph_runtime)$'`: PASS 3/3 after the shader and asset changes. The final stronger fire/smoke pixel assertion was then rebuilt and its dedicated Metal test rerun: PASS 1/1.
- `npm run typecheck && npm run build` in `apps/studio-web`: PASS.
- Extracted the actual shared GLSL render body and compiled it with `glslangValidator -V -S comp`: PASS, SPIR-V emitted. No Vulkan device rendering claim.
- `/usr/local/bin/python3.12 tools/site/build.py check`: 51 entries, 0 errors.
- `git diff --check`: PASS before implementation commit.

## Limits and next step

The current result captures the main composition and colors of the reference, but its fluid detail and flame tongues are simpler. Embers are procedural emissive samples in the volume, not independent particle entities. The native Studio preview was exercised on Metal; authored game-scene placement, Vulkan pixels, multiple emitter performance and the complete interactive HTML5 editing flow were not verified. The two-scale noise adds GPU ray-march work; performance under production resolutions is not measured.

Next: place the exact published graph in a saved scene, inspect it against scene lighting and opaque geometry, then measure the optional detail cost at production viewport sizes. Keep the site entry `in-progress` until main integration, then reconcile the SpectraFX page and publish the site from clean `origin/main` through the documented workflow.
