# SpectraFX Stage 155 — volumetric particle scenes

Branch: `codex/spectrafx-volumetric-scene`. Implementation commit: `49e581547`.
Base: `origin/feature/spectrafx-html5-data-interfaces-20261123` at `f75e8c170`, with `origin/main` at `1b77c21bf` merged in the isolated worktree. Roadmap first commit: `9fa5d4284`.

## Shipped on this branch

- `VolumetricParticleOutput` has a bounded native GPU scene route for visible ECS `SpectraSource` instances in ClientFx and StudioSpectraSources. Sixteen pure particle runtimes can be resident. The existing `SceneVolumeWorld` owns hybrid gas plus particle graphs; there is no second gas solver or simulation authority.
- A separate RGBA16F target receives scene color after the geometry pass, then draws depth-aware particles while sampling detached opaque depth. PRISMA receives that HDR result before UI. Non-depth particles draw in the ordinary scene pass. Vulkan HDR copies use a matching-format image copy; Vulkan runtime pixels have not been verified.
- Studio native preview accepts depth-aware volumetric material. The preview and scene owner clear the GPU targets before Device teardown. Studio web type fixes allow its production bundle to build.
- The native Studio test captures a real Metal frame of a depth-aware volumetric particle. `docs/devlog/media/2026-11-24-spectrafx-studio/01.png` is that frame, not a mockup. The development log entry remains `in-progress` until main integration.
- The explosion graph preset now supplies radial acceleration expected by its existing graph runtime fixture. StudioHost's existing invalid lambda declaration and const Workspace access were repaired so the native app can compile.

## Checks actually run on macOS

- `npm run typecheck && npm run build` in `apps/studio-web`: PASS. Dependencies came from the existing local `node_modules` because this branch has no package lock and offline npm install could not resolve uncached packages.
- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/spectrafx-volumetric-scene --target aiverse_client -j 4`: PASS, including final link, after hybrid routing.
- Same CMake build for `aiverse_studio`: PASS, including final link.
- `ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/spectrafx-volumetric-scene --output-on-failure -R '^vfx_graph_runtime$'`: PASS 1/1.
- Same CTest command with `-R '^spectrafx_metal_volumetric_scene$'`: PASS 1/1. It admits a pure and a hybrid graph, advances the GPU runtime, composes depth-aware HDR passes and waits for GPU completion.
- Same CTest command with `-R '^studio_spectrafx_preview$'`: PASS 1/1 after adding volumetric load and real frame capture.
- `/usr/local/bin/python3.12 tools/site/build.py check`: 49 entries, 0 errors. System `python3` is 3.8 and cannot parse the existing site's Python 3.12 f-string syntax.
- `git diff --check`: PASS before implementation commit.

## Limits and next step

The native captured frame and test establish Metal execution and presentation; a person has not yet exercised the complete HTML5 authoring flow in the running Studio UI. Planet/world-space scenes, moving or scaled source anchors, Vulkan on-device rendering and pixels, performance under multiple emitters, packaging/replay and full CTest/CI coverage remain unverified. Source admission requires unit scale, a GPU-compatible graph and no live data-interface bindings. The scene keeps its initial anchor until the source revision changes. Do not claim these gaps complete.

Next: run the full authoring flow in Studio with a saved project and inspect an authored game scene on Metal; then verify Vulkan on a real device and decide whether moving/planetary placement belongs in the next roadmap item. After main integration, mark the roadmap item done, update site system descriptions as needed, and publish the site from clean `origin/main` using the documented workflow.
