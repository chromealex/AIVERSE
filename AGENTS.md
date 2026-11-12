# AGENTS.md — rules for coding agents working on AIVERSE

Read this file completely before changing anything. It is short on purpose. `README.md` has the
user-facing overview (in Russian); this file is the engineering contract.

## 1. What this is

AIVERSE is a from-scratch C++20 game engine with RTS and FPS modules. The RTS module covers
real-time strategy games (Warcraft, StarCraft, Red Alert, Dune II,
Total Annihilation, Age of Empires, Company of Heroes, They Are Billions, Northgard, Mushroom
Wars, lane battlers, ...). Targets: Windows, macOS, iOS, Android, HTML5. No third-party engine.

**The engine is a fixed library of mechanics; games are data.** A game is a project folder
anywhere on disk (`<project>/`, created in the editor from a template in `templates/<id>/`) with `game.json`, `rules.json`, `maps/*.json`, `ui/`, `models/`, ... Agents that
build *games* (through the editor or the MCP server) never touch engine code. Agents that work on
the *engine* add mechanics generically — never a special case for one game.

## Engine naming (all agents)

- Product name: **AIVERSE**. Use this name in UI, documentation, logs and agent responses.
- Shared C++ namespace: `aiverse`; CMake project/exports: `AIVERSE` / `AIVERSE::`.
- Common targets, macros and environment variables use `aiverse_` / `AIVERSE_`.
- Apps: `AiverseEditor`, `AiverseGame`; tools: `aiversebuild`, `aiversemcp`.
- RTS is a genre, not the engine name: keep `modules/rts`, `RtsModule`, `RtsData`,
  `Rts*` gameplay types and JSON `module: "rts"`. FPS remains `modules/fps`.
- Keep existing project/save/pack formats, published URLs and compatibility fields stable.
  `.rtsproj`, binary magic values and the release-index `rtsmcp` key are intentional legacy names.
- See `docs/aiverse-migration.md` for native project and build configuration migration.

## 2. Layout

| Path | What | Notes |
|---|---|---|
| `engine/core` | Fixed (Q47.16), Random (PCG32), Hash (FNV-1a), ECS (sparse sets) | sim-safe primitives |
| `engine/data` | JSON reader (comments allowed, numbers parsed as Fixed) | no floats in parsing |
| `engine/sim` | Simulation (lockstep tick, commands, events, checksums), Replay | deterministic |
| `engine/gfx` | RHI `Gfx.h`; backends `gl/` (GLES3, WebGL2, GL 3.3) and `metal/` | never call GL/Metal outside |
| `engine/fx` | particles: effect data (`FxEffect`), `ParticleSystem` (CPU sim, RHI draw, quality tiers), own GLSL + MSL shaders; textures via `render::TextureCache` (docs/particles.md) | float OK, presentation only |
| `engine/render` | Renderer3D (forward, shadows, PCF tiers), shaders (GLSL + MSL in `Shaders.h`, `ModelShaders.h`), models (glTF/OBJ, `Model.h`), animation (`Animation.h`), ImGui layer, camera math | float OK; format: docs/models.md |
| `engine/platform` | Cocoa, UIKit, Android NativeActivity, Emscripten, Win32, X11 | `Platform.h` is the only API |
| `engine/services`, `plugins/` | store / ads / analytics / privacy APIs over native plugins (Java, Obj-C, JS, C++); ready plugins: App Store, Google Play, RuStore, Yandex Games, Steam, Yandex Ads, AdMob, AppLovin MAX, ironSource LevelPlay, GameAnalytics, Facebook, AppMetrica, ATT; a build carries only the plugins the game selects (CI: check-plugin-isolation.py) | docs/plugins.md |
| `engine/assets` | fonts, default assets copied into every build | data only (no code: it is bundled) |
| `engine/atlas` | texture atlas manifest + `.rtex` pages, `TextureLibrary` (path -> texture + uv + trim) | packer in `tools/atlas` (host only) |
| `engine/pack` | resource packs `.rpak`: reader (`Pack`), writer (`PackWriter`, host tools only) | zstd, reproducible output |
| `modules/rts` | generic RTS gameplay: data model (`RtsData`), map/pathfinding (`RtsMap`), module (`RtsModule*.cpp`, `RtsEconomy`, `RtsUnits`, `RtsCombat`, `RtsWorld`, `RtsTriggers`), AI (`RtsAI`) | deterministic |
| `modules/rts/view` | presentation of the sim (shapes, terrain, fog, overlays) | float OK, read-only on sim |
| `apps/client` | the game executable | |
| `apps/editor` | editor: dockable windows (Scene, Game + device simulator, Hierarchy, Inspector, Tools, Project, Rules, Statistics, Agent, Console); `core/` = Workspace/MapDoc shared with the MCP server | new windows: docs/editor-windows.md |
| `tools/mcp` | `aiversemcp`: MCP server (stdio) for agents; forwards to the running editor | game data only, never engine code |
| `apps/headless` | bot matches, replays, CI | |
| `apps/modelview` | model viewer and type gallery (`--game`) through the real view | models agent |
| `tools/aiversebuild` | packager: macOS .app, iOS Xcode project, Android Gradle project, web, Windows | ships content as `content.rpak` |
| `tools/assetpipe` | build / list / verify / extract / diff `.rpak`, import foreign archives (`--json` for MCP) | |
| `tools/unpackers` | unpacker plugins for foreign archive formats (zip, tar, dmg, Warcraft WAR, Westwood PAK, Z PAC/RSC) | protocol: `tools/unpackers/PROTOCOL.md`; host tools only |
| `tools/importers` | importers of old games into game folders (`dune2/import.py` → 2D game: tiles + sprites), Westwood codecs | Python stdlib; output is external data |
| `templates/*` | sample games | data only |
| `tests/tests.cpp` | unit + mechanics + determinism tests | must stay green |
| `third_party/` | vendored libraries with licenses | MIT/BSD/zlib/OFL/CC0/CC-BY only |

## 3. Non-negotiable rules

**Determinism (simulation = `engine/core`, `engine/sim`, `modules/rts` except `view/`):**
- No `float`/`double` anywhere in simulation state or logic. Use `Fixed`, integer math, `fxSqrt`.
- Randomness only via `sim.rng()`; never `rand()`, time, addresses, thread ids, `std::hash`, unordered containers iteration.
- Iterate in deterministic order (ECS order, sorted vectors, `std::map`). Tie-break by entity index.
- Never add/remove components or create entities inside `world.each<...>()` — collect, then apply after the loop. Destroy with `destroyLater`.
- Everything that influences outcomes must be hashed in `RtsModule::hashState`.
- Input reaches the sim only as `Command`s; invalid commands are ignored silently (never assert/crash).
- Presentation reads sim state and `SimEvent`s; it never writes into the sim.
- After touching the sim run the determinism tests and a headless bot match on every sample game.

**Rendering/platform:**
- Before shader or Shader Graph work, read [the agent guide](docs/shader-graph-agents.md)
  and follow its validation, preview and application workflow.
- All GPU work through `gfx::Device` (RHI). Every shader exists in GLSL *and* MSL in the same change.
- Must run on weak Android GPUs: every expensive feature has a quality tier (`ShadowQuality`, etc.).
- Content files are read via `platform::readContentFile` (works on Android assets, web preload, packs).
- No threads in code that runs on the web build unless guarded.

**Editor windows:** see `docs/editor-windows.md` (one class per file in `apps/editor/windows/`, `AIVERSE_EDITOR_WINDOW`).

**Data model:**
- New mechanics = new optional JSON blocks on types/rules/maps, parsed in `RtsData.cpp`, with
  readable errors (`type 'x': unknown resource 'y'`). Old game files must keep loading.
- When a fixed block can't express something, extend the trigger system (`RtsTriggers`) instead
  of special-casing a game.

**Licenses/assets:** only vendor code/assets with permissive licenses and keep their license files.
Original assets of commercial games are never committed; importers read them from the user's own
copy and builds mark them external.

## 4. Build, test, verify

```sh
# Linux / CI
cmake --preset linux-release && cmake --build build/linux-release
build/linux-release/aiverse_tests                                   # all must pass
build/linux-release/aiverse_headless --project templates/skirmish --seed 3   # must end with a winner
build/linux-release/aiverse_headless --project templates/mushrooms --seed 3

# macOS (Xcode generator) / iOS
cmake --preset macos && cmake --build build/macos --config Release
build/linux-release/aiversebuild macos --project templates/skirmish      # packaged .app in dist/

# Screenshot check without a display (Linux): any visual change must be looked at
Xvfb :97 -screen 0 1280x800x24 &
DISPLAY=:97 AIVERSE_SHADOWS=medium AIVERSE_SCREENSHOT=/tmp/shot.ppm AIVERSE_EXIT_AFTER_FRAMES=300 \
  build/linux-release/AiverseGame --map 0 --spectate
```

Environment knobs: `AIVERSE_CONTENT` (content root), `AIVERSE_SHADOWS=off|low|medium|high`, `AIVERSE_AUDIO=off|null`
(null: real audio thread without hardware), `AIVERSE_AUDIO_CAPTURE=mix.wav` (record what was played),
`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`, `AIVERSE_SPLASH=off|full` (loading screen; default: full splash, only the loading time with `--map`), `AIVERSE_FAST_FORWARD=seconds`
(bots play that long before the first frame), `AIVERSE_SCRIPT_LOG=1` (client: script log lines `[script] t=<tick> ...` on stdout, as
`aiverse_headless` prints them).

A change is done when: it builds without warnings on gcc/clang (`-Wall -Wextra`), tests pass, the
headless matches still finish, visual changes were checked on a screenshot, Metal code paths that
couldn't be compiled are listed in the commit message as unverified.

## 5. Working in parallel

Several agents work at the same time. **Each agent works in its own branch/worktree** and only in
its area. Need to change a file outside your area? Keep the change minimal, additive, and say so in
the commit message; prefer adding a new file over editing a shared one.

| Area | Paths | Owner |
|---|---|---|
| Simulation & mechanics, AI, triggers | `engine/core`, `engine/sim`, `modules/rts/*.{h,cpp}` | main agent |
| Editor, MCP server, device simulator | `apps/editor`, `tools/mcp`, `docs/game-format.md` | main agent |
| Models, PBR materials, skinning, procedural animation | `engine/render`, `engine/gfx`, `modules/rts/view`, `apps/modelview`, `tools/models`, `docs/models.md`, `templates/showcase` | models agent |
| In-game UI (RmlUi, HTML/CSS) | `engine/ui`, `apps/client`, `engine/assets/ui` | UI agent |
| Resource packs `.rpak`, build size/compression, unpackers/importers of foreign game data | `engine/pack`, `tools/assetpipe`, `tools/unpackers`, `tools/importers`, `tools/aiversebuild` | packs agent |
| Texture atlases (packer, manifest, runtime lookup) | `engine/atlas`, `tools/atlas`, atlas step in `tools/aiversebuild` / `tools/assetpipe` | atlas agent |
| Localization | `engine/i18n`, `engine/assets/lang`, `templates/*/lang`, `tools/i18n`, `modules/rts/RtsLocalize.*` | i18n agent |
| Audio | `engine/audio`, `modules/rts/audio`, `tools/audiopack`, `third_party/miniaudio`, `third_party/vorbis`, `templates/*/audio.json`, `docs/game-format.md` § audio.json | audio agent |
| 2D games: ortho camera, sprites, tile maps + autotile, tile tool | `engine/render/{SpriteBatch,SpriteShaders,TextureCache}*`, `modules/rts/RtsTiles.*`, `modules/rts/view/{Rts2DView.h,RtsSprites,RtsTileView,RtsCamera.h}*`, `apps/editor/core/TileOps.*`, `apps/editor/windows/TilesWindow.cpp`, `templates/tiles2d`, `templates/km2d`, `tools/art2d` | 2D-mode agent |
| Dynamic lighting (point lights, light map, flashes, night) | `engine/render/Lights.*`, `engine/render/LightShaders.h`, `modules/rts/view/RtsLights.*` | lighting agent |
| Platforms, device testing, CI (window/input/keyboard per OS, packaging templates) | `engine/platform`, `platform/`, `.github/` | platforms agent |
| Native plugins: bridge (`Plugins.h`), store/ads services, SDK plugins, aiversebuild plugin step | `engine/platform/Plugins*`, `engine/platform/apple/`, `engine/services`, `plugins/`, `tools/aiversebuild/Plugins.*`, `docs/plugins.md` | platforms agent |
| Particles: CPU sim + RHI render, event effects, Effects window | `engine/fx`, `modules/rts/fx`, `templates/*/fx`, `apps/client/ClientFx.*`, `apps/editor/windows/FxWindow.cpp`, `docs/particles.md` | particles agent |
| Terrain simulation (heightfield model, slopes, LOS, `cornersFile`, HPA* for big maps) | `modules/rts/RtsHpa.cpp`, heightfield parts of `modules/rts/RtsMap.*` / `RtsData` (`terrain` block), `MapDoc` corners plumbing, `docs/terrain.md` (sim part), `templates/metaltide` map | agent-terrain-sim (MAIN merges) |
| Terrain overlays: territory colours + glowing borders, diggable rock blocks / veins / dig marks, MW2 lanes, road ribbons (visual `terrain.territory/dig/lanes/roads`) | `modules/rts/view/RtsGroundFx*`, `docs/game-format.md` § Terrain overlays; hooks: `RtsView.{h,cpp}` (member, init/draw/shutdown, `RtsScene` lane/viewer fields), `RtsTerrain.cpp` (invalidate + flat dig cells), `Renderer3D::bindLitFrame` | agent-terrain-fx (MAIN merges) |
| Terrain rendering: chunked terrain + CDLOD, heightfield surface (`RtsView::heightAt`), sub-cell detail surface (`terrain.style`/`terrain.detail`), splat materials / quality tiers, water bodies, sculpt / material / heightmap ops + **Рельеф** window + MCP | `modules/rts/view/RtsTerrain*.{h,cpp}` (`RtsTerrainRender`, `RtsTerrainDetail*`, `RtsTerrainSurface.h`, `RtsTerrainShaders.h`), `apps/editor/core/TerrainOps.*`, `apps/editor/core/TerrainDetailOps.cpp`, `docs/terrain.md` § Detail surface, `apps/editor/windows/TerrainWindow.cpp`, `tools/terrain`, `templates/frostcrown` terrain block + textures, `docs/game-format.md` § Terrain materials; hooks: `Renderer3D::drawCustomOpaque`, `TextureCache::resolve(tiling)`, Metal vertex textures, pause-menu material tier | agent-terrain (MAIN merges) |
| Overlays: data-driven helper elements (build grids, range rings, rally lines, damage numbers, bars, HUD labels), built-in selection / hover / markers / placement / bars as default overlays, **Оверлеи** window + MCP `overlay_*` | `modules/rts/view/RtsOverlay*.{h,cpp}` (`RtsOverlayData` = schema / validation / templates, GPU-free lib `aiverse_overlay_data`; `RtsOverlays` = evaluator + renderer, `RtsOverlayShaders.h`), `apps/editor/core/OverlayOps.*`, `apps/editor/windows/OverlaysWindow.cpp`, `docs/game-format.md` § Overlays; hooks: `RtsView.{h,cpp}` (member, init/draw/drawOverlays, `overlayTop`), client `main.cpp` (OverlayContext, footprint moved to the `placement` overlay) + `Minimap` marks, `Editor.{h,cpp}` (context, preview fields) | overlays agent (MAIN merges) |
| Splash & icons: game.json `icons` / `splash`, icon + launch screen generation per platform, in-engine loading screen, Branding window + MCP | `engine/splash`, `tools/branding`, `tools/aiversebuild/Branding.*`, `apps/client/ClientSplash.*`, `apps/editor/core/BrandingOps.*`, `apps/editor/windows/BrandingWindow.cpp`, `templates/_tools/brandart.py`, `templates/skirmish/branding`, `docs/game-format.md` § Icons and splash; hooks: `GameClient::initStep`, `aiverseMain`, `platform/web/shell.html` splash, `platform/ios/Info.plist.in` storyboard key, window colour in PlatformWin32/PlatformMac | splash agent (MAIN merges) |
| Editor look: theme (dark/light, accents), fonts (Inter + Noto Symbols 2 + Lucide icons), top/status bars, toolbar helpers | `apps/editor/EditorTheme.*`, `apps/editor/EditorIcons.h`, `ui::` toolbar helpers in `apps/editor/EditorUtil.h`, `apps/editor/EditorShell.cpp` chrome, `third_party/fonts` | editor-ui agent (MAIN merges) |
| Scripting: Lua 5.4 VM (sim sandbox with fx numbers + view scripts), `game` API, script effects/trigger hooks, script MCP ops, hot reload, `templates/scripted` | `third_party/lua`, `modules/rts/RtsScript*`, `modules/rts/lua/*.lua` (engine standard library: victory, waves, triggers runtime, auto-build/sell — the C++ versions are gone, edit the Lua), `tests/checksums`, `modules/rts/view/RtsViewScript.*`, `apps/editor/core/ScriptOps.*`, `apps/editor/windows/ScriptsWindow.cpp`, `docs/scripting.md`, `templates/scripted`; hooks: `RtsModule` (setup/system/hash/member), `EffectKind::Script`, trigger `script` action/condition, `embedScripts` calls in loaders | scripting agent (MAIN merges) |
| VFX tools: Unity particle importer, fx MCP ops (list/get/set/delete/templates/import/bind/preview), optional particle modules (trails, sub-emitters, noise, lights, bursts, ...) | `tools/importers/unity_vfx`, `tests/data/unity_vfx`, `apps/editor/core/FxOps.*`, `apps/editor/core/FxTemplates.cpp`, `apps/editor/EditorFxOps.cpp`, `docs/particles.md` § MCP / Unity import; hooks: `engine/fx` modules (with the particles agent), `gfx::BlendMode::Premultiplied`, `Change::Fx`, `Editor::tickFx`, `ClientFx::addLights` | vfx tools agent |
| Sprite shadows & lighting: 2D sprites cast into the sun shadow map, receive shadows / scene light, projected blob + ground-sprite decals, `sprites` block | `modules/rts/view/RtsSpriteShadow.*`, sprite parts of `RtsSprites.cpp` / `SpriteBatch` / `SpriteShaders.h`, `docs/game-format.md` § Sprite shadows; hooks: `Renderer3D::drawCustomShadow` + `lightViewProj()`, `RtsLights::addContactShadows` (skips sprite types), `RtsView` (`sprites` defaults, `isSprite`) | sprite-shadows agent (MAIN merges) |
| Planet rendering: cube-sphere CDLOD terrain, ocean, atmosphere, stars, orbits, orbit camera (surface / planet / system), objects on spheres, planet fog (seam-aware blur), ground overlays on spheres (territory, dig rock, roads, lanes), sphere minimap, editor planet camera + 3D brushes, planet MCP params; territory / dig / rooms / lanes on planets in the sim | `modules/rts/view/RtsPlanet*` (incl. RtsPlanetGroundFx, PlanetFieldBlur), `apps/client/{ClientPlanet,MinimapPlanet}.cpp`, `apps/editor/core/PlanetOps.*`, `docs/planets.md` §5 (+ §3 territory / lanes), `docs/game-format.md` § Planets: visual keys; sim: the `g.surf` branches in `RtsDungeon.cpp`, `RtsLanes.cpp` (rebuildLanesSphere), `RtsAI::dungeon` dig planning; hooks: curved-world hooks in `engine/fx` (ParticleSystem up / altitude / view space) and `RtsFx::setPlaceFn`, `FogOfWar::setBlur`, `RtsGroundFx` look accessors, `RtsView` (planet members, draw / overlay anchors), `RTSCamera::zNear/zFar`, client / editor camera + picking behind `planet()`; content: `templates/planets` fx / territory / roads | agent-planet-render (MAIN merges) |
| Terrain / map generation: integer noise, erosion, rivers, biomes, symmetric fair starts, content from the game's types, presets (flat + planets), match-time `generate`, `aiverse_headless --generate`, editor **Генерация** window + MCP `map_generate` / `gen_presets` | `modules/rts/gen/*`, `apps/editor/core/GenOps.*`, `apps/editor/windows/GenWindow.cpp`, `docs/game-format.md` § Map generation, `docs/planets.md` §4; hooks: `RtsModule::setup` (applyGenerate + `generatedMap()`), `applyTilesToMap` (prepareGenerate), client / editor view read `generatedMap()`, headless flags | agent-terrain-gen (MAIN merges) |
| Planets in the simulation: cube-sphere maths, `Surface`, world block, orbit layer, transit, planet MapDoc / validate, `templates/planets` | `modules/rts/RtsSphere.*`, `RtsSurface.*`, `RtsMapSphere.cpp`, `RtsHpaSphere.cpp` (planet HPA*), `RtsPlanets.cpp`, `apps/editor/core/MapDocPlanet.cpp`, `templates/planets`, `templates/_tools/planetgen.py`, `docs/planets.md` §3; hooks in `RtsMap`, `RtsModule`, `RtsUnits`, `RtsWorld`, `RtsAI`, `MapDoc`, `Workspace` map_info | agent-planet-sim (MAIN merges) |
| Post effects: `postfx` block (game.json / map), effect catalog + presets + cost model, PostFx renderer (GLSL + MSL, quality tiers), **Постэффекты** window + MCP `postfx_*` / `view_postfx` | `engine/render/PostFx*.{h,cpp}` (`PostFxData` = format / cost, GPU-free lib `aiverse_postfx_data`; `PostFx` + `PostFxShaders.h` = renderer), `apps/editor/core/PostFxOps.*`, `apps/editor/windows/PostFxWindow.cpp`, `docs/postfx.md`; hooks: `Renderer3D::render(pass, post)` + `resolvePostFx()`, client `main.cpp` / `Lobby.cpp` / `GameClient.h` (member, configure, governor), `Editor.{h,cpp}` (member, `postFxInScene`), `Workspace.cpp` registration | agent-postfx (MAIN merges) |
| Toolchains: per-target requirement detection (beyond PATH), installers into `~/.aiverse/toolchains`, `aiversebuild --check-toolchain / --install-toolchain / --install-missing`, Build window toolchain panel, selectable Console, MCP `toolchain_*` | `tools/aiversebuild/Toolchain*`, `apps/editor/core/ToolchainOps.*`, README § Инструменты сборки; hooks: `aiversebuild` main (per-target check), `Workspace::startTool` / `toolchainCheck`, `BuildWindow`, `ConsoleWindow` | toolchains agent (MAIN merges) |
| Flow fields (shared integration fields for groups / hordes on flat maps and planets), `movement.flowField`, headless `--bench-group` | `modules/rts/RtsFlowField.cpp`, `FlowField` in `RtsMap.h`, `docs/pathfinding.md`; hooks in `RtsUnits` (moveToFlow, sysFlowFields), `RtsModule` (group orders), `RtsWorld` (waves), `RtsAvoidance` / `RtsPlanets` (stuck fallback), `RtsData` (keys); `templates/lastcolony/maps/horde_plain.json`, `templates/planets/maps/big_planet.json` | agent-flowfield (MAIN merges) |
| Computer player: terrain analysis (high ground, chokepoints, routes, watch posts), terrain-aware waves / rally / siege spots, towers, planets invasions, stalemate escalation, own build spots, expansion, bot save states, rules `ai` knobs | `modules/rts/RtsAI*.{h,cpp}` (`RtsAITerrain.*`, `RtsAITactics.cpp`), `docs/game-format.md` § Computer player; hooks: `RtsData` (`ai` block), `RtsModule::freeSpotNear` (`toward`: spawn on the rally side), `RtsEconomy` (spawn uses it), headless `--save/--load` (`.ai` file) | agent-ai-terrain (MAIN merges) |
| Mechanics consolidation: one implementation per duplicated mechanic (armor / shields / upkeep / worker limit / crews / garrison-paid upgrades / flow fields), agent-mech keys as aliases | `modules/rts/RtsOriginals*.cpp` (damage pipeline, crews), alias parsing in `RtsOriginalsData.cpp` / `RtsMechData4.cpp`, `docs/game-format.md` § Fidelity mechanics + agent-mech alias sections | mech-consolidate (MAIN merges) |
| Crowds: flow-field arrival / pass-through, `movement.blockedGoals` | hooks in `RtsUnits` (sysFlowFields), `RtsAvoidance` (same-crowd ORCA, settled obstacles, no yield), `RtsMap` (findPath blocked goals), `RtsData` / `RtsModule` (keys); `docs/pathfinding.md` § Crowds | agent-crowds (MAIN merges) |
| Lua coverage audit: capability matrix, gap list, Lua prototypes of missing mechanics, Lua perf bench | `docs/lua-coverage.md`, `templates/lua_lab` (+ `bench/`), test `lua_lab_prototypes`, `lua_lab` lines in `tests/checksums/baseline.txt` | lua-audit agent (MAIN merges) |
| Project files `.rtsproj` + OS file association (double-click opens the editor: macOS document type / LaunchServices, Windows HKCU ProgID, Linux mime + .desktop), editor / document icons | `apps/editor/core/ProjectFile.*`, `apps/editor/core/FileAssoc.*`, `apps/editor/EditorOpenFile.cpp`, `tools/branding/ProjectIcon.*` + `projicon_main.cpp` (`aiverse_projicon`), `platform/macos/EditorInfo.plist.in`, `templates/*/*.rtsproj`, README § Файл проекта .rtsproj; hooks: `EventType::OpenFile` + `detail::requestOpenFile` (Platform.h / PlatformCommon, PlatformMac `openURLs`), `Workspace::openGame` (resolve + migration), `createProject`, hub folder browser / status, editor `main.cpp` positional arg, aiversebuild `--project` + `*.rtsproj` ignore | project-file agent (MAIN merges) |
| Grid fields (G5): rules `fields` + map `gridFields`, C++ kernels (decay / diffuse / spread / flow / consume), per-cell effects, Lua `field` table + `on_field_threshold`, field overlay (GLSL + MSL), **Поля** window + MCP `field_*`, `templates/wildfire` | `modules/rts/RtsGridField*`, `modules/rts/view/RtsFieldView.*`, `apps/editor/core/FieldOps.*`, `apps/editor/windows/FieldsWindow.cpp`, `docs/fields.md`, `templates/wildfire`, `templates/_tools/wildfire.py`, `tests/tests_fields.inc`; hooks: `RtsModule` (setup / system / hash / stat speed / member), `RtsSave` (append), `RtsWorld::sysVision` (occluders, reveal), `EffectKind::Field`, `RtsScriptEngine` (`on_field_threshold`), `RtsView` (member, draw, `RtsScene::fields`), client `main.cpp` (particles), `Editor` (Scene preview) | agent-grid-fields (MAIN merges) |
| Vision performance: cached / incremental fog-of-war passes bit-identical to the brute-force reference, `AIVERSE_VISION=ref\|check`, headless `--profile` | `modules/rts/RtsVision.cpp` (sysVision, flat + sphere, reference), `GameState::visionToken`, `Simulation::setProfileClock`, docs/terrain.md § Vision | agent-vision |
| TEST_TERRITORIES game and GDD completion | External `/Users/aleksandrfeer/AiverseProjects/TEST_TERRITORIES`; minimal generic scripting API extensions in `modules/rts/RtsScript*`, explicit entity-action controls in `modules/rts/view/EntityActionControls.h`, restoration verification in `apps/headless`, and minimal generic view-effect/orthographic particle fixes with tests/docs | territory game task (`codex/territory-lua-front`) |
| Native C++ game modules | `modules/rts/RtsNative.*`, `modules/rts/native`, `examples/native`, `docs/native-modules.md`; hooks: RtsModule, RtsSave, CMake, aiversebuild source export | cpp-modules agent |
| Editor with the project's C++ modules: detect missing `nativeModules`, build `<project>/Builds/.temp/editor-<host>`, switch / restart into it, MCP `native_status` / `native_build` | `apps/editor/core/NativeBuild*.{h,cpp}`, `apps/editor/EditorNativeBuild.*`, `docs/native-modules.md` § Редактор с модулями проекта; hooks: `Workspace::startCommand` / `appendBuildLog`, `LineServer::close`, `Editor` (open / Play / frame / init, unsaved prompt `drawUnsavedChanges`, no file association in a project build), editor `main.cpp` (`--native-editor`, `--tools-from`) | native-editor agent (MAIN merges) |
| OpenWar port (Superpowers at War 1:1 in Lua): generic engine support only — script commands `mapClick` (client + editor input), territory / script HUD / region bindings it uses; presentation hooks: game.json `playerColors`, flat lighting keys (`day`, `fogDensity`, `tonemap`), view `camera` / `set_camera`, categorical cell paint layers | `modules/rts/RtsScriptCommands.cpp` (`mapClick`), `apps/client/ClientScriptCmd.cpp`, `apps/editor/windows/GameWindow.cpp` (script card), `docs/scripting.md` § Script commands / Camera / Categorical cell layers; hooks: `RtsOverlayData` (palette), `RtsLightsData` + `Renderer3D` (`Lighting::tonemap`, U1 skyColor.w) + the ACES line of the lit shaders, `RtsViewScript`, `RtsPaintLayers` (cells pipeline), client `runViewScripts`; game data lives outside the engine (`OpenWar/`) | agent-openwar (MAIN merges) |
| Game wizard «Сделать игру»: a whole project from art + idea (template choice, content import, hard tutorial from the rules, soft hints, levels, meta screens, monetization, languages, bot check), MCP `game_make*`, hub page; `"card": false` script commands | `apps/editor/core/GameWizard*`, `apps/editor/core/WizardOps.*`, `apps/editor/EditorGameWizard.cpp`, `apps/editor/EditorHub.h`, `templates/_wizard`, `docs/game-wizard.md`; hooks: hub tab / menu (`EditorProjects.cpp`, `EditorShell.cpp`, `Editor.h`), `Workspace` registration, `ScriptCommandDef::card` (`RtsScript*`, client / editor command cards) | agent-gamewizard (MAIN merges) |
| Engine project import | `tools/importers/{lib,port,unity_project,godot_project,unreal_export}`, `tests/data/port`, `docs/engine-import.md`; minimal editor/MCP/i18n hooks | agent-engine-import (MAIN merges) |
| Built-in terrain texture quality: filtered world-space grain and micro normals for untextured ground | `modules/rts/view/RtsTerrainShaders.h`, material tier selection in `RtsTerrainRender.*`, `docs/terrain-textures.md` | rt-fx continuation (user assigned) |
| Linear UI blending: game.json `ui.blend`, sRGB layer + linear composite of the RmlUi layer, RHI `PixelFormat::RGBA8_sRGB` / `copyColor` / `setScreenReadable` (GL + Metal) | `engine/ui/UiColor.h`, `engine/ui/UiLinear.*`, linear parts of `engine/ui/UiRender.cpp` / `UiLayer.*`; hooks: `engine/gfx` (format, pipeline `colorFormat`, copy), client `initUi` / frame `setTarget`, editor `GameHud` | ui-linear (MAIN merges) |
| Editor world tree: Hierarchy of every world object (edit-mode preview of the map's setup / live play world), multi-selection, Inspector «Детали» (components, stats, script data, mixed values), `editor.hierarchy` game.json hooks, MCP `world_entities` / `world_entity` / `editor_select` | `apps/editor/core/WorldInspect.*`, `apps/editor/EditorWorld.cpp`, `apps/editor/EditorWorldEdit.h`, `apps/editor/DetailsPanel.*`, `apps/editor/windows/HierarchyWindow.cpp`, entity part of `InspectorWindow.cpp`, `docs/editor-world-tree.md`; hooks: `Editor.h` (Selection kinds, selection set, world members), `Editor.cpp` (frame / startPlay / buildEditScene / renderGame / pickEntity), `SceneWindow.cpp` (multi-select, hover, play picking) | editor-world-tree (MAIN merges) |
| World tree performance: consumer-driven reads, background preview builds, cheap row reads, Details read passes, `AIVERSE_EDITOR_PROFILE` / `AIVERSE_HOSTED_MAP` | `apps/editor/EditorProfile.h`, perf parts of the world-tree files above, `docs/editor-world-tree.md` § Performance; hooks: `ScriptEngine::peekData` (`RtsScript.h` / `RtsScriptEngine.cpp`), atomic component type ids (`engine/core/ECS.h`), `ClientHost` (`AIVERSE_HOSTED_MAP`) | world-perf (MAIN merges) |
| Reusable libraries and AI skills | `libraries/`, `tools/libraries/`, `tests/libraries/`, `skills/`, `docs/libraries.md` | reusable-libraries agent |
| Weapons and ballistics library policies: penetration, explosions, ammo feeds, charge, attachments, aim solver | `libraries/include/aiverselib/genre/{Ballistics,WeaponMods,WeaponData,WeaponPersistence}.h`, `skills/aiverse-weapon-policies`, `examples/libraries/genre/weapon*.json`; additive hooks: `libraries/spec/gameplay.json` sections + generated outputs, `weapon-policies` catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp`, `docs/genre-packages.md` § Weapons and ballistics | agent-weapons (MAIN merges) |
| Meta-game C++ policies, JSON presets and agent recipes | `engine/meta`, `tools/meta`, `tests/meta`, `examples/meta`, `skills/meta-*`, `docs/meta-game.md`; minimal CMake/native dependency hooks | meta-library agent |
| FPS, authoritative sessions and large worlds | `modules/fps`, `engine/net/authority`, `engine/world`, `apps/fps`, `tests/fps`, `examples/fps`, `docs/fps.md`; additive build/platform hooks | FPS task (current agent) |
| AIVERSE naming migration | Engine-wide branding, shared namespaces, build/tool names, platform bridges, documentation; preserve genre RTS | aiverse-rename (user assigned) |
| Shader Graph: material, postfx and particle graphs, compiler, editor and MCP | `engine/render/graph`, shader graph integration in render/fx/view, editor window/core, tests/shader_graph, docs/shader-graph.md | shader-graph agent |
| Programmable animation: trait stacks, subgraphs, Motion Matching, full-body IK contacts/collisions/balance, surface locomotion, visual editors and C++/Lua controls | `engine/render/AnimationGraph*`, `engine/render/AnimationRig*`, `engine/render/AnimationMotion*`, `engine/render/FullBodyIK*`, `engine/render/AnimationContacts*`, `engine/render/SurfaceLocomotion*`, `apps/editor/animation`, `apps/editor/windows/Animation*`, `apps/editor/core/Animation*`, `tests/animation`, `examples/animation`, `docs/animation-graphs.md`; minimal view/model/Lua hooks | animation-system agent |
| Model authoring and clip tools | `tools/modeling`, `tests/modeling`, `apps/editor/core/ModelingOps.*`, `apps/editor/windows/Model*`, `docs/modeling.md`; minimal registry, rig-limit, screenshot and localization hooks | codex/modeling-tools |
| Optional Box3D runtime and physics sandbox | `engine/physics`, `third_party/box3d`, `examples/physics`, `tests/physics`, `docs/physics.md`; additive CMake hooks | codex/box3d-runtime |
| Clusters, HLOD, conservative occlusion, baked surfaces and geometry performance | `engine/render/Geometry*`, `tools/modeling/SurfaceProxy*`, `apps/editor/core/{GeometryOps,SurfaceProxyOps}.*`, `tests/geometry`, `tools/geometrybench`, `tools/models/autolod.py`, `docs/geometry*.md`; minimal model LOD, renderer/view, Metal binding/uniform and macOS benchmark pacing hooks | codex/geometry-visibility |
| XPBD cloth, CPU/GPU sleeping, Metal compute solver, instanced cloth rendering and benchmarks | `engine/cloth`, `examples/cloth`, `tests/cloth`, `docs/cloth.md`; shared projected animation LOD helper in `engine/render/AnimationLod.h`, minimal `RtsAnimate` hook; optional compute/storage/readback/timing RHI hooks in `engine/gfx/Gfx.h` and `metal/GfxMetal.mm`; additive CMake/localization hooks | codex/xpbd-cloth |
| Genre audit implementation: shared strategy kernels and native integration | `libraries/include/aiverselib/strategy`, `tests/libraries/strategy*`, `examples/native/strategy`, matching catalog skills, `docs/genre-implementation.json`, `libraries/include/aiverselib/net`, `tests/meta/local*.py`, `tools/meta/local_match.py`, `docs/local-network.md`; minimal FPS host and loopback transport hooks | codex/genre-libraries |
| Combat actions: hitboxes/hurtboxes, dodge i-frames, guard arc + perfect parry, poise/stagger, lock-on (`combat-actions` package over melee-combat) | `libraries/include/aiverselib/genre/Combat*.h`, `skills/aiverse-combat-actions`, `examples/libraries/genre/hitboxes.json`, docs/genre-packages.md § Combat actions; additive hooks: `combat` section of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`combatActions`) | agent-combat (MAIN merges) |
| Stealth and senses: sight cones, hearing/occlusion/noise budgets, trails, suspicion, ally alerts, search, disguises, hiding (`stealth-senses` package, PerceivedTarget records for bots) | `libraries/include/aiverselib/genre/Senses*.h`, `skills/aiverse-stealth-senses`, `examples/libraries/genre/senses*.json`, docs/genre-packages.md § Stealth and senses; additive hooks: `senses` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp` function, tests appended to `tests/libraries/genre_tests.cpp`; record kinds 48..55 | agent-senses (MAIN merges) |
| Meta composition and skill consolidation | `engine/meta/Client.*`, `tools/meta`, `tests/meta`, `examples/meta`, `skills/`, catalog/skill links and meta docs | codex/meta-composition |
| Character locomotion modes: slopes, ledges, ladders, swimming, wall climbing, mantle/vault mode machine on top of stepMotion | `libraries/include/aiverselib/genre/LocomotionModes.h`, `skills/aiverse-character-locomotion`, `locomotion` keys in `libraries/spec/gameplay.json` (+ generated files), locomotion tests at the end of `tests/libraries/genre_tests.cpp`, `docs/genre-packages.md` § Locomotion modes; hooks: `examples/native/libraries/GenreConsumer.cpp`, catalog entry | agent-locomotion (MAIN merges) |
| Match objective modes: capture/hold zones + KotH, payload, elimination lives/respawn waves, extraction, bomb/defuse, route gates, contested overtime clock, spectator transitions (`match-rules` 0.2.0) | `libraries/include/aiverselib/genre/MatchObjectives*.h`, `skills/aiverse-match-rules`, `examples/libraries/genre/objective*.json`, docs/genre-packages.md § Match objectives; additive hooks: `objectives` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`matchObjectives`); save record kinds 40..47 | agent-match (MAIN merges) |
| Genre bot adapters: melee/shooter bot policies, squad roles + formations, objective choice hook (`genre-bots` package, record kinds 56..63) | `libraries/include/aiverselib/bots/{GenreCommon,MeleeBot,ShooterBot,ObjectivePolicy,SquadRoles,GenreBotPersistence,GenreBots,GenreBotConfig}.h`, `skills/aiverse-genre-bots`, `examples/libraries/genre/bot-settings.json`, docs/genre-packages.md § Genre bots; additive hooks: `genreBots` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`genreBots`), docs/bots.md row | agent-genre-bots (MAIN merges) |
| Narrative and social scenes: typed facts, relationship meters, knowledge, deferred consequences, chapters, speakers, cinematic CueRequest events, barks, visual-novel backlog/skip/auto (`narrative-graphs` 0.2.0, record kinds 64..71) | `libraries/include/aiverselib/genre/Narrative{Scenes,Cues,SceneData,ScenePersistence,Configured}.h`, `skills/aiverse-narrative-graphs`, `examples/libraries/genre/{story-social,narrative-settings}.json`, docs/genre-packages.md § Narrative scenes; additive hooks: `narrative` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`narrativeScenes`) | agent-narrative (MAIN merges) |
| Physical interactions: pick up/carry/throw with arc preview, push/pull, XPBD ropes, doors (locks/keys/one-way/auto-reverse), moving platforms with rider carry, breakables with debris requests (`physical-interactions` package, record kinds 80..87) | `libraries/include/aiverselib/genre/{PhysicalCommon,InteractionConfig,Carry,Push,Ropes,Doors,Platforms,Breakables,PhysicalInteractions,InteractionConfigured,InteractionData,InteractionPersistence}.h`, `skills/aiverse-physical-interactions`, `examples/libraries/genre/{interactables,interaction-settings}.json`, docs/genre-packages.md § Physical interactions; additive hooks: `interaction` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry (+ object-interactions cross-reference), `GenreConsumer.cpp`, `examples/native/libraries/CMakeLists.txt`, `tests/libraries/genre_tests.cpp` (`physicalInteractions`), skill search aliases | agent-interaction (MAIN merges) |
| Cinematic gameplay: time-scale domains (hit-stop/slow-mo), scene timelines, dialogue/animation sync waits with timeouts, QTE with accessibility, interaction staging + input locks, checkpoints around scenes, narrative cue triggers (`cinematic-gameplay` package, record kinds 72..79) | `libraries/include/aiverselib/genre/Cinematic*.h`, `skills/aiverse-cinematic-gameplay`, `examples/libraries/genre/{scene,cinematic-settings}.json`, docs/genre-packages.md § Cinematic gameplay; additive hooks: `cinematic` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp` function, tests appended to `tests/libraries/genre_tests.cpp` (`cinematicGameplay`) | agent-cinematic (MAIN merges) |
| Extraction raids (Drone Fall port): rules / map `raid` block, match phases, extraction points (zones, cycles, called helicopters, channels, contest / interrupt rules), raid inventory over the item bags, hold-to-use interactables + loot tables, raid objectives, receipts for the meta, raid bot, Lua `raid` table, MCP `raid_*` + **Рейд** window, `templates/extraction` | `modules/rts/RtsRaid*.{h,cpp}`, `apps/editor/core/RaidOps.*`, `apps/editor/windows/RaidWindow.cpp`, `tests/tests_raid.inc`, `docs/raid.md`, `templates/extraction`, `extraction` lines in `tests/checksums/baseline.txt`; hooks: `RtsModule` (member, setup, "raid" system before "death", `kCmdRaid`, hash), `RtsSave` (append), `RtsData` (`Rules::raid`, loadRules call), `GameEvent` `kEvRaid*` + `kCmdRaid` (appended), `RtsAI::update` (raid players -> `raidBotThink`), `ScriptEngine::allowNamed`, `lua/victory.lua` (steps aside while a raid runs), Lua event names, headless `Raid receipt:` lines, `Workspace` registration | agent-extraction (MAIN merges) |
| SDK plugins for analytics / attribution / privacy: `analytics` + `privacy` services (fan-out, consent, iOS ATT), their UI Lua tables, plugins `gameanalytics` (Android, iOS, web), `facebook`, `appmetrica`, `att`, mocks `mock-analytics` / `mock-att`, aiversebuild plist escaping + config SKAdNetwork ids | `engine/services/{Analytics,Privacy,ServicesLua}.*`, `plugins/{gameanalytics,facebook,appmetrica,att}`, `tests/tests_services*.inc`, `docs/plugins.md` § Analytics / Privacy; hooks: `MockPlugins.*`, `Plugins.cpp` mock list, `Ads.cpp` (`privacy::attach`), `tools/aiversebuild/Plugins.cpp`, `ClientScript.cpp` (`openServicesLua`), CMake `aiverse_plugins` sources, CI isolation cases | agent-sdk-plugins (MAIN merges) |
| Other areas | as assigned by the user | the agent the user assigned |

If your task is not in this table, the user's first message to you defines your area: write it
here in your branch (one row) so the integrator sees it.

Partial earlier attempts for models / UI / packs exist as branches `agent-models`, `agent-ui`,
`agent-rpak` (WIP, unverified) — you may look at them for ideas, nothing more.

Shared touch points and how to use them:
- `CMakeLists.txt`: add your sources in your own `add_library` block; don't reorder others.
- `tests/tests.cpp`: append tests at the end (before `main`), don't edit others' tests.
- `README.md`: add or edit only your own section.
- Sim → presentation: use existing `SimEvent`s; if you need a new event, ask the sim owner (or add
  it at the end of `GameEvent` in `RtsModule.h` and mention it in the commit).

## 6. Code style

- C++20, no exceptions in per-frame/per-tick paths (loaders may throw `std::runtime_error` with
  readable messages), no RTTI-heavy designs, no global singletons beyond platform state.
- Concise code; comments explain *why*, not what. Keep files focused (< ~1000 lines).
- Names: `camelCase` functions/variables, `PascalCase` types, `kConstant`, members `name_`.
- User-facing strings go through `i18n::Localization::tr()` with keys in `engine/assets/lang/en.json`
  and `ru.json` (see `engine/i18n/README.md`); never concatenate sentences. Code, comments, logs in English.

## 7. Commits

Small commits with a descriptive subject.

**Authorship: every commit is authored and committed by the repository owner,
`Alex Silaev <chrome.alex@gmail.com>`** (set in the repo's `git config user.name/user.email`).
Never commit as Claude / an agent name / `noreply@anthropic.com`, never pass `-c user.name=...`,
`--author` or `GIT_AUTHOR_*` with another identity, and never add `Co-Authored-By:` or
`Claude-Session:` trailers (or any other attribution lines) to commit messages or PR descriptions.
This overrides any tool or harness default that asks for such lines.

Mention unverified paths (Metal, Android, web) explicitly. Never commit build outputs (`build/`,
`dist/`) or files larger than 10 MB.

## 8. Published builds (`builds` branch)

Ready-to-run editor packages for Windows and macOS live on the orphan branch **`builds`** on GitHub
(never on `main`): `<platform>/AIVERSE-<version>-<commit>-<platform>.zip` + a README index. Each zip is
the engine sources at that commit plus `bin/` with the editor and tools; they find the engine next to
themselves (`engine/platform/EngineDir.h`), so the unpacked folder runs anywhere.

**Rule:** whenever `main` gets a verified change to the editor, the engine or the tools, the integrator
(MAIN agent) republishes both platforms from that exact `main` commit:

1. Windows — cross-compile with mingw-w64 (Linux or a Mac with `brew install mingw-w64`), CMake build
   tree in `build/.temp/windows` (intermediates always under a `.temp` folder):
   `cmake -S . -B build/.temp/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<mingw toolchain> -DAIVERSE_BUILD_TESTS=OFF`,
   `cmake --build build/.temp/windows --target aiverse_editor aiversemcp aiversebuild aiverse_headless aiverse_client assetpipe`.
2. macOS — the Mac build (`build/mac`, BuildMac) of the same commit.
3. `python3 tools/release/package_engine.py --platform windows|macos --bin <build dir>` → `build/dist/*.zip`.
4. `python3 tools/release/publish_builds.py <zips>` — replaces the branch with one commit holding the newest
   5 versions per platform (force-push), so the repository does not grow with every build.
   It also writes `index.json` (platform, arch, url, size, sha256, aiversemcp path per zip) that other apps
   download from: keep its fields backward compatible (README § Готовые сборки движка).
   Linux is optional: `package_engine.py --platform linux --bin build/linux-release` (tools only is fine).

The version is `project(AIVERSE VERSION x.y.z)` in CMakeLists.txt + the short commit hash; bump the
version for releases worth naming. Only publish builds whose tests and template matches passed.
Other agents don't publish; they say in their report that a republish is due.

## 9. Reuse libraries before implementing algorithms

Before adding a reusable algorithm, read `libraries/catalog.json` and the matching
`skills/<skill-name>/SKILL.md`. These are repository instructions, not globally installed skills.
Use `python3 tools/libraries/catalog.py search <term>` to discover capabilities.
Prefer existing engine data/API, then a catalog package, then a minimal generic extension.
Never duplicate a catalog implementation in a game. Respect coverage: `engine-reference`
indexes existing functionality; `cpp-foundation` is a public C++20 header API, not the entire roadmap.
The full target scope and remaining work are in `docs/libraries.md`.
Pin package versions and content hashes in the project. Never silently overwrite modified packages.
Keep simulation, presentation and tools separate. Persist all gameplay state through the engine's
state mechanisms, use its RNG, and preserve deterministic iteration/tie-breaking.
A third-party adapter is not simulation-safe just because it accepts integers.
New dependencies need a pinned revision, approved license, provenance, platform checks and tests;
candidates mentioned in discussions are not installed or approved by implication.
When extending a package update its API, version, examples, tests, catalog and skill together.
Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py` and
`python3 tests/libraries/native_consumer.py`.
Integration changes to simulation still require the full tests and all template matches from §4.

Native game projects consume individual `AIVERSE::lib_<package>` targets after
`aiverse_native_module(...)`; include `<aiverselib/Name.h>`. This target must exist before project
native CMake code executes. Keep `libraries/` in aiversebuild source exports. No Lua dependency
or engine-source edit is required for a game to consume these headers. Do not store mutable
library objects as NativeModule members: persist their fields through NativeState instead.

## 10. Reuse meta-game policies before generating code

For shops, offers, crafting, currencies/inventory, quests, passes, daily rewards, energy,
inbox, leaderboards, chat and segmentation, start with `skills/meta-game/SKILL.md`.
Load only the matching recipe (`skills/meta-economy`, `skills/meta-progression`,
`skills/meta-social`) and relevant sections of `docs/meta-game.md`.
Use `aiverse::meta` from `engine/meta`; configure project-local JSON via
`tools/meta/scaffold.py` and validate with `aiverse_meta_validate`. Do not reimplement the
same algorithms in game scripts/native code. Native consumers select client or server policies;
registered object modules use `aiverse_native_use_meta(aiverse_native_<id> CLIENT)` or a selected kit target.
Meta clocks/network/storage stay outside deterministic simulation. Online authority,
receipt validation and durable CAS storage belong to the backend. Local social
policies are not an online service. Never describe an offline/mock service as deployed.
Reuse Store providers; do not double-grant alongside the existing Lua shop.
Policy changes must pass `tests/meta` and the external C++ consumer in `examples/meta`.



### Gameplay subsystem packages and project recipes

Before implementing controls, cameras, construction UX, abilities, inventory/equipment,
squad controls, feedback, tutorials, save migrations, diagnostics, content workflows or
acceptance gates, consult `docs/gameplay-packages.md` and the catalog's `cpp-subsystem` /
`tool-workflow` entries. Use `AIVERSE::gameplay_native` for native command adapters.
Existing engine mechanics remain authoritative: custom preview/helper state must not
become a second source of truth for built-in costs, cooldowns, inventories or navigation.
`libraries/spec/gameplay.json` owns settings; regenerate C++ types, schema and reference
with `python3 tools/libraries/generate_gameplay.py`. Never hand-edit generated outputs.
Use `tools/libraries/gameplay.py recipe` / `apply` for supported rules changes; apply
requires real engine validation and refuses stale plans. Read the recipe's checks and
supply game-specific definitions rather than fabricating assets or UI bindings.
Run generated-file checks, gameplay tools tests, C++ tests and external native/export
consumer tests. Presentation adapters require host UI/render wiring and visual QA before
claiming an in-game UX is complete. JSON backup tools do not replace engine binary saves.

### Application packages

For UI bindings/screens/themes, application lifecycle, match rewards, network session policy,
content dependencies, balance experiments, play scenarios, performance budgets, genre recipes
and change planning, read `docs/application-packages.md` and the matching catalog skill first.
Use `libraries/include/aiverselib/app` from project native/host code; never move presentation/session
state into simulation. Match rewards use `grantMatch` with the existing `aiverse::meta::Economy` and
its durable CAS store; no parallel wallet or receipt implementation. A session policy is not a
transport/backend; a genre recipe is not a complete game. Verify integration before claiming it.
RmlUi bindings must die before document roots. Localize all display labels, escape dynamic text,
respect modal input, and validate gameplay actions again on the authoritative command path.
Run app C++/CLI tests, native/export consumers and actual visual QA for client wiring.

### Complete meta-service packages

For friends/clans, instance collections/equipment, entitlements, tournament cohorts,
backend integration and meta screens, search `tools/meta/catalog.py` and read only the
matching `skills/meta-{clans,items,tournaments,backend,ui}/SKILL.md`.
Use `aiverse::meta::Service` from native host code; never run wall-clock/network policy
inside simulation ticks. Preserve authenticated Principal, atomic aggregate Commit,
operation IDs and authoritative payment/match inputs. Do not duplicate the wallet.
Generate only selected features (full only when requested), edit meta source JSON, then run tools/meta/compose.py
with the real C++ validator. Pin/verify with tools/meta/catalog.py. Run tests/meta and
the external examples/meta consumer. Browser screens are not automatic RmlUi bindings;
HTTP handler tests are not proof of live network/TLS or deployment. Document unverified
platforms and visual/network checks. Details: docs/meta-service.md.

LiveOps policies live under `extensions.liveops`: loot, albums, jobs, research, quests,
events, wars, asyncPvp, matchmaking, ratings, referrals, ads, chains, notifications, offline.
Use `tools/meta/catalog.py search INTENT`, then `describe PACKAGE` and only that recipe.
Do not load or regenerate the entire schema. Full preset includes examples for all groups.
Preserve host authority, trusted RewardDraw, provider-event uniqueness and aggregate Commit.
Glicko/Elo are host-only; queue rosters do not start servers and ad completion does not verify
provider signatures. Run `meta_liveops` and `meta_liveops_schema` with the existing meta tests.
Details and explicit policy limits: docs/meta-liveops.md.


### Native code generation and integration toolkit

For module scaffolding, typed NativeState/Command code, reference validation, hot settings,
bounded storage, deterministic work budgets, state diffs and library upgrade plans, consult
`docs/native-toolkit.md` and matching catalog skills before writing another implementation.
Generate native code into a new directory; review the diff and preserve custom handlers.
Generated command validation covers payload shape/ranges only: validate player, ownership,
resources and targets in the authoritative handler. Never bypass engine save VERSION checks.
Hot reload is for host settings; gameplay changes must be explicit replicated commands.
Operation budgets must be deterministic, with cursors/counters saved and hashed. Wall time
must never decide when gameplay computation commits. Diagnostic traces must identify sampled
fields/ticks; they do not replace the complete simulation checksum.
Use the opt-in library showcase for the integration chain; `--verify` is a headless acceptance
check, while `--auto` exercises the RmlUi action and captures the real rendered screen.
Do not report visual validation if the GUI could not start. Run toolkit_tools.py, runtime C++
tests, catalog checks and external native/export builds after toolkit changes.

### Reusable gameplay systems

Before adding attributes/modifiers, status effects, tags/conditions, interactions, custom reservations,
action sequences, loot/pity, progression curves, AI memory or decision explanations, consult
`docs/gameplay-systems.md` and the matching catalog entry. Built-in RtsModule stats/statuses,
Inventory.reserved, effects and commands remain authoritative; do not fork their mechanics.
Use systems/Persistence.h for custom state in NativeState and preserve stable IDs and tick-based
ordering. Feed only visibility-authorized observations to AI memory. Interaction previews do not
authorize mutations. Gather explanation facts from the actual calculation, never guessed formulas.
Sampling loot is not granting rewards; use the existing inventory/meta transaction owner.
Run pure systems tests, real engine systems tests, generated config checks, external native/export
builds and a native restore match after integration. Report platform/visual gaps explicitly.

### Genre bots and automatic navmesh

For RTS/FPS AI controllers and navigation meshes, read `docs/bots.md` and catalog bots-rts,
bots-fps, navmesh and bot-matches. Reuse RtsAI rather than duplicating RTS economy/tactics.
FPS controllers run on one authority and submit ordinary commands; disable the reference fallback
for their slots. RtsNative lifecycle is not an FPS host hook. Persist bot state with simulation.
Build regional navmeshes from authoritative collision and agent dimensions; reject stale geometry
and budget exhaustion. AABB raster meshes do not imply triangle/slope/off-mesh/streaming support.
Use generated mesh components for reachable patrol targets. Run actual motor detour and bot restore
checks, external native/export builds, and full engine/template checks for simulation changes.

### Cross-genre action and narrative policies

For character jump/motion policies, melee timing/combos, recoil/projectiles, rounds/capture and
story choices, read `docs/genre-packages.md` and matching catalog skills. Reuse built-in movement,
weapon/ammo, damage/status and victory owners where applicable; these are explicit custom policies.
Use generated settings adapters and stable localized narrative graph IDs. Persist typed state with
its immutable definition versions. Native OBJECT modules use aiverse_native_use_genre to propagate
backend dependencies. Test collision, repeated commands, invalid choices, overflow and restore.
Do not claim climbing, a complete fighting game, bomb rules or cinematic playback from these policies.
Scene flow, QTEs, time scales and staging use `cinematic-gameplay`; presentation stays a host that reports
progress by tick and never drives the timeline.

## Procedural climbing, spider gaits and surface locomotion

Before implementing procedural footsteps, wall/ceiling crawling or multi-leg IK movement,
read [Surface locomotion](docs/surface-locomotion.md) and [Animation graphs](docs/animation-graphs.md).
Reuse `engine/render/SurfaceLocomotion` / the `surfaceLocomotion` rig unit; do not create a
second per-game gait planner. Use `examples/animation/spider_showcase.py` to generate an
original articulated model and the executable floor/wall/ceiling example. Do not assume
that a humanoid model or a single-bone leg can satisfy the required two-bone chains.

- Configure 3..16 independent hip/knee/foot chains, bind-relative home/pole positions,
  clearance/probes, step timing, minSupport/maxSwing and optional FullBodyIK capsules.
- Drive declared velocity/adhesion/reset parameters through C++ or view Lua. Keep state
  per instance; call graph update once per frame. Evaluate must not advance gait twice.
- Stance anchors must remain fixed relative to their surface until a step/lost contact/reset.
  Check reach and support budgets; missing geometry must not fabricate support. Preserve
  residual diagnostics when goals or collision constraints cannot be satisfied.
- `AnimationEnvironment` queries are read-only. Use stable surface IDs and real host
  collision queries for exact geometry; default per-primitive oriented bounds are approximate. Surface
  locomotion accepts wall/ceiling normals; ordinary full-body foot contacts retain slope limits.
- This controller moves the presentation root, not the RTS authoritative entity. Gameplay
  climbing/navigation, networking, saves and gravity remain with their existing owner.
  Never put floating-point rig state into deterministic simulation or bypass Commands.
- Inner-corner floor/wall/ceiling transitions and adhesive support are implemented. Do not
  advertise outer-edge wrapping, automatic navigation, physical adhesion forces, falling,
  deformable-surface transport, CCD or self-collision as implemented by this controller.
- Put the controller last in the pose graph. Subsequent blends/traits may invalidate its
  contacts and status. Use full weight for locked feet; reset after teleport or full detachment.
- Verify `aiverse_animation_tests`, `aiverse_tests animation_`, and the generated spider in
  the editor/client. Report actual tested platforms; retain the existing C++/Lua/JSON schema.

Dynamic scene support: provide `AnimationEnvironment::surfaceTransform` using the same
stable IDs as raycast hits. `surfaceLocomotion` stores local stance/swing-target anchors,
transports its body with the support, and releases removed surfaces. Use followSurface=false
when the authoritative host already carries the actor. RtsView supplies per-primitive
oriented bounds and current rigid transforms (including entity_fx); custom physics queries
are required for exact triangles/deforming skins. Test translation, rotation, tilt, steps
on a moving support and removal. Use `spider_showcase.py ... --dynamic-tank` and the editor
`animation_runtime_status` / `rig_runtime_status` operations to verify the actual scene;
a static plane-only preview is not evidence of dynamic-object support.

## Meta generation with minimal context

All repository agent skills live in `skills/`. Resolve the exact skill path from
`libraries/catalog.json` or `tools/meta/catalog.py`; general engine skills use
`aiverse-*`, RTS-only navigation/spatial/generation/bots recipes keep `rts-*`.
Keep package IDs and lock identities stable when moving recipes. Do not recreate
skills under docs or load the entire catalog/schema to configure one subsystem.

For a complete or selected meta layer start with `skills/meta-compose/SKILL.md`.
Use `tools/meta/kits.py plan/apply --features ...` (or `--preset full`), with the real
C++ validator. The generated `meta/AGENTS.md` lists only selected recipes. Read
`catalog.py describe PACKAGE --section PATH` for a narrow example. Configure source
JSON and recompose; keep the authoritative wallet, receipts and membership owner.
Native UI/network hosts reuse `engine/meta/Client.h` with a durable per-account outbox;
read `skills/meta-client/SKILL.md`. The same operation survives a lost reply/restart.
Browser UI is selectable through `metadata.metaScreens`; it is not a native RmlUi
adapter. Validate kits, native linking, client retries and selected UI screens.


## Adding and updating repository skills

- Store each recipe in `skills/<name>/SKILL.md`; match the frontmatter name to the
  directory. Use `aiverse-*` for shared gameplay, `rts-*` only for RTS-specific
  behavior, and `meta-*` for meta-game policies. Do not add recipes under `docs/`.
- Before adding a recipe, search the existing catalogs. Extend the existing owner
  instead of creating a duplicate algorithm or overlapping skill. Register gameplay
  packages in `libraries/catalog.json`, meta packages in `tools/meta/catalog.py`,
  and meta composition dependencies in `tools/meta/kits.py`. Keep IDs stable.
- State when to use the skill, its responsibility, public API/header, exact CMake
  target, dependencies, minimal working example and required validation. Link to
  detailed references; do not require loading unrelated recipes or whole schemas.
- Every new code-providing skill must have an independently selectable library
  target with only required dependencies. Header-only implementations may use
  INTERFACE targets; compiled optional libraries must not build by default.
  Workflow-only skills declare that they have no runtime library. An alias to an
  all-features library does not satisfy per-skill isolation. Existing aggregate
  targets are compatibility paths, not a pattern for new packages.
- Explicitly separate client, server and database work. Client builds link
  `AIVERSE::meta_client`; authoritative hosts link `AIVERSE::meta_server`. Native
  OBJECT modules use `aiverse_native_use_meta(target CLIENT|SERVER)`. The legacy
  `aiverse::meta` includes both sides. The server target is the aggregate/transaction core only; select policies with
  `aiverse_add_meta_server(game_meta MODULES ...)` and construct
  `Service(kits::game_meta(), config, state, commit, draw)`.
- Keep credentials, trusted grants, policy configuration, sessions and SQL on the
  server. Document schemas, migration ownership, compatibility, atomicity and
  restart behavior when persistence is involved. Never ship private DB contents.
- Change API, catalog/version, dependencies, recipe and examples together. Verify
  frontmatter and catalog links, run affected tests, build a standalone consumer
  and a real native consumer/export when linking changes. Verify that selecting
  one library does not build/link unrelated libraries (including transitive deps).
  Report actual coverage and unverified platforms; do not describe routing or
  isolation requirements as completed implementations without evidence.

## Skill orchestration and isolated builds

Start cross-subsystem tasks with `skills/aiverse-orchestrator/SKILL.md` and
`python3 tools/skills/catalog.py search INTENT`. Generate a plan with exact IDs and
`--side client|server|both`; load only selected recipes. `skills/catalog.json` is a
GENERATED responsibility/index map, not a replacement for the source catalogs.

- New code skills get distinct targets. Gameplay header libraries use
  `AIVERSE::lib_<package>` and `aiverse_native_use_libraries(target package ...)`.
  Existing-engine reference/workflow recipes explicitly declare runtime none.
- `engine/meta/modules.json` owns policy IDs, registration and selection dependencies.
  `aiverse_add_meta_server` uses that manifest; a selected kit references only its
  policies. No static registration or all-policy link is permitted on minimal paths.
  `aiverse::meta` and the legacy Service constructor explicitly select everything.
- Native OBJECT modules propagate the selected kit via
  `aiverse_native_use_meta(target game_meta)`; clients use CLIENT. The generated
  factory header must be used by the host. Missing-policy configuration fails closed.
- Register source catalog entries, update CMake/source/API/skill/examples together,
  then run `python3 tools/skills/catalog.py generate` and `check`. Update search
  aliases only for meaningful domain terms; unknown/ambiguous requests need explicit IDs.
- Run `python3 tests/skills/catalog_test.py`, `python3 tests/skills/isolation.py`,
  meta tests and external native/export consumers after selection/linking changes.
  Isolation checks cover default build, actual link commands and binary symbols;
  do not rely solely on dead stripping or a successful compile. Core engine libraries
  already required by a game remain its baseline, not optional skill packages.
