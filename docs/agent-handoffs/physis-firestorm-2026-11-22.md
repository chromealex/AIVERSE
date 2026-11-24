# PHYSIS fire whirls, water / waterspouts, ground scour (2026-11-22)

**Branch:** `agent-physis-firestorm` (worktree `/Volumes/KINGSTON/AIVERSE-builds/worktrees/agent-physis-firestorm`,
locked), from `origin/main`, origin/main merged before the build stage. Not merged into main.

## Commits

| SHA | What |
|---|---|
| `50889978a` | `engine/physis/PhysisTornadoNature.h` (TornadoNature, TornadoEmberField, GroundScourField), fire / water / scour in `advanceTornadoes`, `GameModule::environmentWaterDepth`, map keys (tornado `fire` whirl keys, `surface`, `erosion`; `physis.ignitions`, `disasterConfig`, `noFuelOnWater`, `scourCellMeters`), persistence, presentation (fire funnel / spray / ember / scar FX, flame roar), tests, sample map `templates/skirmish/maps/firestorm_valley.json` + ru names |
| `f163ed78e` | merge origin/main (game core moved to `engine/game`: the water hook now lives on `GameCore`, `engine/game/Combat.cpp`) |
| `8b0b0f375` | no fire intake over water, wider scars, sample timing; docs/physis.md + docs/ru/physis.md, skills, AGENTS.md, devlog entry + roadmap item |
| (this file) | handoff |

## API

- `TornadoNature` next to `TornadoTrack`; `spawnTornado(world, storm, track, nature)`, `defaultTornadoNature(rate)`.
- `TornadoHooks::waterDepth`; `GameModule::environmentWaterDepth(sim, x, z)` (default 0; GameCore: `~` 2 m, `=` 0.3 m).
- `GroundScourField` / `groundScourAt(world, x, z)` / `applyScour`; `TornadoEmberField`.
- `parseIgnitions`, `applyIgnition`, `addIgnitionSystem`, `clearFuelOnWater` (PhysisTornadoAssets.h / PhysisProjectConfig.h).
- Presentation: `NatureTornado::{fireness, spray, soil, waterspout, overWater}`, `frame.embers`, `frame.scour`;
  `TornadoFxLayer::{FireFunnel, Spray, Ember, Scar}`, `TornadoSound::FireRoar`, `tornadoFireSoundName()`.

## Verified (user's Mac, AppleClang, arm64 Release, Unix Makefiles, build `/Volumes/KINGSTON/AIVERSE-builds/physis-firestorm/mac`)

- `ctest -R '^(aiverse_tests|physis_)'`: 41/41 PASS (incl. new `physis_firestorm`, extended `physis_tornado_cues`), twice
  (before and after the last fix). Logs `physis-firestorm/logs/ctest-physis.log`.
- Linux VM g++ 11 (-O1, hand-linked): `physis_firestorm`, `physis_tornado`, `physis_tornado_cues` PASS.
- Headless `templates/skirmish --map maps/firestorm_valley.json`: seed 1 twice -> `fa942ca6b8d975ad` both; record seed 3 +
  `--replay` -> "Replay OK: 8521 ticks"; `--verify-restore-at 300 --verify-restore-ticks 400` -> snapshot, serialized and
  fresh-serialized restore OK. Default skirmish seeds 1–3 equal `tests/checksums/baseline.txt`. Tornado Valley
  (a map with a tornado, now with TornadoNature) runs; its checksum changed by design (new hashed component).
- `tools/genre/check_engine_genre_free.py`: rc 0 (same pre-existing debt: 5 files, 52 names).
- Client `AiverseGame --map 3 --spectate`, screenshots `/Volumes/KINGSTON/AIVERSE-builds/physis-firestorm/shots/`:
  `01_dust_before.png` (tick ~119, fireness 0), `03_firewhirl.png` / `04_firewhirl_close.png` (fireness 0.90),
  `05_river.png` (over the river fireness drops to 0.01, then 0.14 back on land), `02_spout.png` (waterspout, spray 1.0,
  rings), `06_lake.png`, `07_scour_track.png`. `AIVERSE_PHYSIS_DEBUG` lines in `logs/shot-*.log`.
- Full build + full ctest: see the result section appended below.

## Not done / not verified

- Scour is PHYSIS data + a ground mark (SpectraFX ground particles): the terrain mesh / heightfield / navigation is not
  lowered; no TerraWeave Earthworks adapter for `GroundScourField`.
- Water rings are SpectraFX particles, not NEREID surface waves (`WorldScene` ripple ingest needs NEREID ECS events).
- Water under a vortex comes only from `GameModule::environmentWaterDepth` (GameCore map cells) or PHYSIS local flood;
  native games with NEREID ECS water must override the hook (no NEREID query in PHYSIS by design).
- Fire is on coarse PHYSIS cells (4 m in the sample); flames of the whirl follow the vortex. Audio (flame roar) not
  listened to (null audio in captures). `tools/site/build.py check` not run: no Python >= 3.12 on the Mac (3.8) or VM (3.10).
- Windows / Linux / Web CI not run.

## Next

1. TerraWeave Earthworks adapter for `GroundScourField` (host-owned, engine/world), so the scar lowers the terrain.
2. NEREID ring waves: publish tornado water contacts as NEREID presentation events when NEREID ECS water is present.
3. Per-cell fuel from vegetation (TerraWeave grass/forest density) instead of uniform PHYSIS fuel.

## Full build and ctest (same Mac, same build dir, after `8b0b0f375`)

- `cmake --build -- -k` of every target: rc 0 (723 s). Full `ctest -j 6`: **100% tests passed, 0 failed out of 466**
  (1 skipped: `lens_metal_frame`). Logs `physis-firestorm/logs/build-full.log`, `ctest-full.log`.

## Fire textures (same day, later)

- `488edeba7`, `180ce5070`: CC0 flipbooks from Unity's free VFX set in `engine/assets/fx/physis/`
  (`flame_16x4.png` <- SmallFlame01, `flame_tall_16x4.png` <- Flame02, `fireball_8x8.png` <- FireBall01,
  `smoke_8x8.png` <- WispySmoke01; 1024/512 px, frame-edge alpha fade, `LICENSE-fx.txt`). Built-in
  `physis_flame` / `physis_smoke` / `physis_tornado_fire` use them. Originals (zip/tga/exr) stay outside the
  repo in `kingston/AIVERSE-builds/assets/fire-textures/`.
- `50eec4069` merge origin/main (f36403a2d). Build `kingston/AIVERSE-builds/physis-firestorm/mac2` (arm64 Release).
  Full ctest: 466/466 PASS (1 skipped). The run before it had one `aiverse_tests` SEGFAULT after
  `mech_campaign` under `-j 6`; the same code passed alone (41/41) and in the next full run — flaky, not in PHYSIS.
- Firestorm Valley checksum unchanged (`fa942ca6b8d975ad`), default skirmish seeds = baseline.
- Frames before/after: `kingston/AIVERSE-builds/physis-firestorm/shots/{before,after}_{ground_fire,firewhirl,wide}.png`.
