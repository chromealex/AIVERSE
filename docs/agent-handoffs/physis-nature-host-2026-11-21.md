# PHYSIS nature presentation host — wildfire on grass, wind, smoke, sound (2026-11-21)

**Branch:** `agent-physis-host` (worktree `.worktrees/agent-physis-host`), based on
`main` d84e958b1, then merged `origin/main` 7f3a9fbb4 (merge `3ee85b8e6`).
Not merged into main (only the MAIN agent merges).

## Commits

| SHA | What |
|---|---|
| `64141a215` | PHYSIS fire spread: wind pushes fire **downwind** (bug fix) + regression test |
| `86dd1bf82` | `engine/world/PhysisNaturePresentation.h` + `physis_nature_presentation` test |
| `698ae615f` | SpectraFX / audio planners and adapters, TerraWeave grass overlay, `physis_nature_cues` test |
| `5f2e6dba4` | Editor «Живая природа»: Scene preview, PhysisWindow UI, ignite-on-click, MCP op, `physis_nature_preview` test |
| `3ee85b8e6` | Merge origin/main (conflicts in `Editor.h`, `TerraWeaveGrassRender.h` resolved) |

## Problem that was fixed

Before this stage the editor had only the diagnostic heatmap and it needed a
running play-test; docs/physis.md said no TerraWeave/foliage/VFX/audio host
existed. Also, `advanceDisasters` used the **target** cell's wind with an
inverted sign: a tailwind sped fire up **upwind**. New test
`windDrivesFireDownwind` fails on the old code and passes now.

**Behaviour change:** with nonzero wind, fire spread differs from before
(tailwind ×2 at 20 m/s, headwind down to ×¼). Zero wind is bit-identical.
Save schemas unchanged; replays recorded before this commit with wind+fire
will not reproduce. Flag it to owners of any shipped content that relies on it.

## Architecture (who owns what)

```
PHYSIS World (authoritative, Fixed)          read-only
   └─ engine/world/PhysisNaturePresentation.h  buildNaturePresentation(world, policy)
        NaturePresentationFrame: bilinear fire / charred(=1-fuel) / wetness / snow / windAt
        flame points (stable keys, LOD lattice), fire clusters, patch signatures
   ├─ engine/world/PhysisNatureCues.h        NatureFxPlanner, NatureAudioPlanner (pure, testable)
   │     built-in physis_flame / physis_smoke effects, addPhysisNatureSounds(bank)
   ├─ engine/world/PhysisNatureFxHost.h      -> fx::ParticleSystem (SpectraFX CPU) + setWind
   ├─ engine/world/PhysisNatureAudioHost.h   -> audio::AudioEngine (looping fire, wind bed)
   └─ engine/world/PhysisNatureGrass.h       -> terraweave::GrassPatchRenderer overlay
engine/render/TerraWeaveGrassRender.h        GrassPresentationOptions (optional, trailing arg)
apps/editor/PhysisNaturePreview.h            editor Scene preview (private World or live read-only)
```

- PHYSIS core is untouched except the fire-wind fix; no new ECS components or
  persistence. Charred ground comes from authoritative `HazardCell::fuel`.
- TerraWeave keeps grass coverage ownership; the overlay only changes how
  blades look (ash stubble with ragged per-blade edge, burning blades in an
  emissive 2nd primitive, wind sway through the existing `canopyWindGraph`).
  Empty overlay = bit-identical model; `terraweave_model_test` passes.
- SpectraFX owns flames/smoke/embers/fire light; PRISMA owns bloom. No
  postprocess was added.
- Projects override the built-ins by shipping `fx/physis_flame.json`,
  `fx/physis_smoke.json` and sounds `physis_fire_loop` / `physis_wind_loop`
  in `audio.json`.

## Editor

PHYSIS window → «Живая природа в «Сцене»»: enable, play/pause, speed,
«Поджигать кликом (ЛКМ)», wind speed / direction, humidity, detail
(m per cell; 0 = map grid), grass / flames+smoke / sound toggles, grass quality,
volume, live stats. Source:
- running game with PHYSIS → read-only (no ticks, no ignition);
- otherwise a **private** `World` from the map's `physis` block (grid, climate,
  authored wind/storm/gravity fields) at preview detail with a steerable
  prevailing wind and grassland fuel (faster front, short burn). Never saved,
  never sent to the game.
The 3D heatmap overlay now also reads this preview World, so it no longer
needs a play-test. MCP op `physis_nature_preview` (enable, wind, humidity,
cellMeters, ignite {x,z}, advanceTicks, reset → status counts) for agents and
the HTML studio bridge (no studio-web panel was added; the studio branch is
another agent's).

Grass Models replaced during the fire are retired through
`Editor::retirePreviewModel` (`GrassPatchRenderer::setModelRetirement`).

## Tests actually run (Linux VM, g++ 11.4, -std=c++20, sources compiled by hand)

Not CMake/CTest, not AppleClang, not macOS:
- PASS: physis disasters (incl. new wind test), core, climate, environment,
  integration, presentation, diagnostics, storm, wind, forces, spatial_index,
  nature_stress, fracture_outbox, project_config, field_assets,
  nature_fall_host, field_runtime, editor_view, scene_geometry — after the merge.
- PASS: new `physis_nature_presentation`, `physis_nature_cues`,
  `physis_nature_preview` (the last two linked with
  `--unresolved-symbols=ignore-all` because only CPU paths run).
- PASS: `terraweave_model_test` with the modified grass header.
- Old disasters code + new wind test: FAIL (confirms the bug).
- `-fsyntax-only` OK: `Editor.cpp`, `EditorFxOps.cpp`, `SceneWindow.cpp`,
  `PhysisWindow.cpp`, all new headers.

## NOT RUN / unverified

- macOS CMake build of `aiverse_editor` and the 3 new CTest targets.
- Visual result in the Scene (Metal): grass sway via `canopyWindGraph` on a
  `vertexColors` material, emissive burning primitive, flame/smoke look,
  light count, PRISMA bloom. Audio on a real device (synth loop seams).
- Performance (grass rebuilds ≤ 6 patches/frame, ~100 patches × ≤4608 blades).
- Game client (`apps/client`) hookup: the host classes are ready, but the
  native client does not call them yet; Game view shows nothing new.

## Next concrete steps

1. On the Mac:
   ```sh
   cd .worktrees/agent-physis-host
   cmake -S . -B build/physis-host -DAIVERSE_BUILD_TESTS=ON
   cmake --build build/physis-host --target aiverse_editor \
     aiverse_physis_disasters_test aiverse_physis_nature_presentation_test \
     aiverse_physis_nature_cues_test aiverse_physis_nature_preview_test \
     aiverse_terraweave_model_test
   ctest --test-dir build/physis-host -R '^(physis_|terraweave_model)' --output-on-failure
   ```
2. Editor: open any RTS map → PHYSIS window → «Живая природа» → «Поджигать
   кликом» → click grass; screenshot via `physis_nature_preview` +
   `view_screenshot {window:"scene"}`. Tune `physis_flame` scale/colours and
   grass density from the picture.
3. Wire `PhysisNatureFxHost`/`AudioHost`/grass into the native game client
   for projects that install PHYSIS (opt-in, `apps/client`).
4. Optional: a studio-web panel calling `physis_nature_preview`.
