# AIVERSE — detailed overview

A reference for every part of the engine: layout, building, formats, subsystems, platforms and verification status.
The short introduction — why the engine exists and what problems it solves — is in the [README](../README.md).
Rules for agents that change the engine are in [AGENTS.md](../AGENTS.md).

## Layout

```
engine/
  core/      Fixed (Q47.16), Vec3, Random (PCG32), Hash, ECS
  data/      JSON without floats (numbers go straight to fixed-point)
  sim/       deterministic simulation, commands, replays
  net/authority/ authoritative network sessions for FPS
  world/     spatial structures and chunked world streaming
  gfx/       RHI + Metal and GL (ES3/WebGL2/GL3.3) backends, render to texture
  render/    3D rendering: lighting, shadows (shadow map + PCF), fog, tone mapping; Dear ImGui over the RHI;
             glTF/OBJ models, PBR materials, skeletal and procedural animation
  platform/  window/input/loop: macos, ios, android, web, windows, linux
  assets/    shared engine assets (Noto Sans font with Cyrillic, OFL)
modules/rts/ generic RTS layer: types from data, orders, A*, combat, economy, construction,
             production, garrisons (Mushroom Wars), roads, fog of war, AI
             view/ — draws any type from the "view" block in rules.json
modules/fps/ FPS module (in development): movement, combat and match state
apps/
  fps/       standalone FPS client and server
  client/    the game (menu, match with bots, HUD, minimap, mouse and touch)
  editor/    map editor (terrain, objects, roads, players, validation, bot test, Build menu)
  headless/  bot matches and replay checks without graphics
  modelview/ viewer for models, clips, materials and every type of a game
tools/mcp/      MCP server through which AI agents work with the editor
tools/aiversebuild/  packager: exe / app / Xcode project / Gradle project / web
templates/
  skirmish/  "Iron and Gold" — a Warcraft/Red Alert-style sample
  mushrooms/ "Mushroom Clash" — a Mushroom Wars-style sample
  showcase/  Iron and Gold rules with imported models (glTF, OBJ, animations)
platform/    packaging templates (Info.plist, Gradle project, HTML shell)
```

## Quick start (macOS)

```bash
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release      # requires Xcode and CMake
cmake --build build/mac -j 8
open build/mac/AiverseEditor.app                              # editor (project templates/skirmish)
open build/mac/AiverseGame.app                                # game
./build/mac/aiverse_tests
```

Another game in the editor: `AiverseEditor.app/Contents/MacOS/AiverseEditor --project templates/mushrooms`.
To make the game start with a different data set, pass `-DAIVERSE_CONTENT_DIR=.../templates/mushrooms`.

### Several editors at once

Each editor process can open its own project with `--project <folder>`.
Without `--listen` the first instance takes MCP port `47321`; if the port is busy, the OS assigns
a free one. The actual port is shown in the status bar; the **Agent** window adds it to the connection
config (`aiversemcp --port <port>`). An automatically assigned port may change after the editor restarts.
`--listen <port>` pins an exact port (on conflict MCP does not start), `--listen 0` disables MCP.
Existing MCP clients do not switch between windows on their own.

## Building for platforms — aiversebuild

```bash
./build/mac/aiversebuild --project templates/skirmish --target macos     # Builds/macos/<Name>.app (ad-hoc signed)
./build/mac/aiversebuild --project templates/skirmish --target ios       # Builds/ios/xcode/AIVERSE.xcodeproj + simulator build
./build/mac/aiversebuild --project templates/skirmish --target android   # Builds/android/<Name>-release.apk when SDK/NDK and gradle are present
./build/mac/aiversebuild --project templates/skirmish --target web       # Builds/web (requires emsdk)
./build/mac/aiversebuild --project templates/skirmish --target windows   # .exe via mingw-w64 (brew install mingw-w64) or sources + build.bat
./build/mac/aiversebuild --project templates/skirmish --target all
```

Output goes to `<project>/Builds/<platform>/` — only finished builds and the size report. Everything
intermediate (CMake trees, packs, re-encoded audio, the Gradle project, xcodebuild data, the editor's
size estimate) lives in the hidden `Builds/.temp/`, which can be deleted at any time. With `--generate-only`
the Gradle project for Android Studio goes to `Builds/android/<Name>`. The packager removes the old visible
`_build`, `_pack`, `_audio`, `_estimate` folders itself.

The same is available in the editor: the **Build** menu. Generated iOS and Android projects are self-contained
(engine sources and content are copied into them), so they can be handed over. For iOS on a device: open the
project in Xcode and choose a team under Signing (or `--team <TeamID>`, or `"appleTeamId"` in game.json).

Identifier, name, version, orientation and icon (`icon.png`, 1024×1024; generated if missing) are in `game.json`.

### Build toolchains: check and automatic install

Before building, `aiversebuild` checks what the platform needs and looks for it beyond `PATH` (an editor
launched from Finder only sees `/usr/bin:/bin:/usr/sbin:/sbin`): Homebrew (`/opt/homebrew/bin`,
`/usr/local/bin`), `~/.local/bin`, `~/Library/Python/*/bin`, `~/emsdk`, `$EMSDK`, `ANDROID_HOME` /
`ANDROID_SDK_ROOT`, `~/Library/Android/sdk`, `~/Android/Sdk`, `%LOCALAPPDATA%\Android\Sdk`, `JAVA_HOME`,
`/usr/libexec/java_home`, Visual Studio (vswhere) and its own folder `~/.aiverse/toolchains`. Found folders
are added to `PATH` for child processes.

| Platform | Needs | Installed automatically |
|---|---|---|
| web | CMake, Python 3, Emscripten (emsdk), Ninja — optional | emsdk (≈ 400 MB download, ≈ 1.5 GB on disk), CMake + Ninja (≈ 50 MB) |
| android | JDK 17–23, Android SDK (platform 35, build-tools, platform-tools), NDK, CMake from the SDK, Gradle | everything: Temurin JDK 17, command line tools + `sdkmanager` (licenses accepted), NDK ≈ 700 MB, Gradle 8.10.2 |
| macos | Xcode Command Line Tools, CMake | CLT — through Apple's installer (`xcode-select --install`), CMake |
| ios | Xcode (App Store), CMake | CMake |
| windows (on Windows) | Visual Studio 2022 / Build Tools (C++), CMake | CMake; VS — a hint with a `winget` command |
| linux | C++ compiler, CMake | CMake |

```bash
aiversebuild --check-toolchain web            # what is present and what is missing (--json for the editor and MCP)
aiversebuild --install-toolchain web          # install what is missing (or one id: emsdk, cmake, jdk, android-ndk, gradle, ...)
aiversebuild --project ... --target web --install-missing   # install and build right away
```

Without `--install-missing`, missing tools stop the build with exit code 3 and the line
`AIVERSE_MISSING_TOOLCHAIN web: emsdk` (the editor recognizes it). In the editor, the **Build** window shows for
the selected platform what was found (paths, versions) and what is missing, with an **Install automatically**
button and an "Install missing tools automatically" checkbox; progress is in the **Console** (text can be selected
and copied; "Open logs folder" opens `<engine>/logs`). Agents do the same over MCP: `toolchain_check`,
`toolchain_install`, `build_start {"installMissing": true}`.

Everything is installed into `~/.aiverse/toolchains` (Windows: `%USERPROFILE%\.aiverse\toolchains`; another folder —
the `AIVERSE_TOOLCHAINS_DIR` variable) without `sudo` and without system changes: `emsdk/`, `py/` (a venv with cmake and
ninja), `cmake/`, `ninja/`, `jdk-17/`, `android-sdk/`, `gradle-8.10.2/`. Re-running is safe (finished parts are
skipped). To remove everything installed, delete that folder. Exceptions: Command Line Tools are installed by
Apple's installer, and if the user's Android SDK is found, `sdkmanager` installs the missing packages into it.

### Resource packs (.rpak) and build size

Game content (the game folder + `engine/assets`) goes into a build as a single file `content/content.rpak`:
each file is zstd-compressed separately, identical files are stored once, already compressed formats (png, ogg,
mp3, …) are stored as is. The engine mounts `content.rpak` automatically; files in the pack take precedence over
files in the folder, so without a pack (during development) everything works as before. `platform::mountPack("patch.rpak")`
mounts a pack on top (patch, DLC). Every build prints a size report and writes it to `dist/<platform>/size-report.txt`.

```bash
aiversebuild ... --no-pack          # ship folders as is (debugging)
aiversebuild ... --pack-level 9     # zstd level (default 19 for Release, 3 for Debug)

assetpipe pack -o content.rpak templates/skirmish engine/assets=engine   # build a pack by hand
assetpipe list content.rpak       # files, sizes, compression
assetpipe verify content.rpak     # unpack everything and check hashes
assetpipe extract content.rpak out/
assetpipe diff old.rpak new.rpak  # what changed between versions (for patches)
```

What the build does not need is listed in `.rpakignore` in the game folder (masks `*`, `?`; `folder/` — directories only).
Hidden files (`.git`, `.DS_Store`), `*.rpl` replays and nested `*.rpak` are always skipped. Pack builds are
reproducible: identical content gives a byte-identical file; the pack's "content hash" identifies the data set.

### Split builds and content download

Optionally a build can be split: the app carries only a base pack with what is needed to start, the rest goes into
parts (`content.001.rpkc`, …) of a given size that the game downloads from any source. The game author decides
everything, in `game.json` (shared settings and per platform) or with `aiversebuild` flags:

```json
"packaging": {
  "split": true,                 // whether to split
  "chunkSize": "8MB",            // size of one part
  "baseSize": "40MB",            // limit for what ships inside the app
  "totalSize": "1GB",            // limit for all content
  "fillBase": false,             // true: fill the base up to baseSize (less to download)
  "base": ["maps/intro.json"],   // what must be in the base (besides the default: game.json, rules, lang, ui, atlases, first map)
  "order": ["maps/scena00*"],    // download order
  "sources": ["https://cdn.example.com/mygame/{hash}/", "file:///Volumes/DLC/"],   // mirrors in order
  "autoDownload": true,          // download everything in the background right after start
  "targets": {"web": {"baseSize": "15MB"}, "windows": {"split": false}}
}
```

```bash
aiversebuild --project templates/x --target web --split --chunk-size 4MB --base-size 15MB   # flags override game.json
```

Parts are placed next to the build (`dist/<platform>/chunks/`) and published at the address from `sources`
(`{hash}` is the build's content hash, so versions don't get mixed up). In the game, a file from a part that is
not downloaded yet is requested automatically (on desktop and mobile `readContentFile` waits for it, on the web it
returns false); progress is `platform::contentProgress()`, an explicit request is `platform::requestContent(paths)`.
Every part is checked by SHA-256 (a tampered part from a mirror is rejected and taken from the next one), kept in a
cache and not downloaded again on the next start. Protocols: https/http (libcurl on Linux, WinHTTP, NSURLSession,
HttpURLConnection on Android, fetch on the web), `file://` and folders, and a game can register its own scheme
(Steam, Play Asset Delivery, P2P). `assetpipe pack --chunk-size 8MB ...` does the same by hand, and `assetpipe verify`
checks the parts too.

### Importing foreign archives (unpackers)

Formats the engine does not know (zip, tar, dmg, archives of other games) are added as unpacker plugins without
rebuilding the engine: a folder with `unpacker.json` and a program in any language (protocol:
[`tools/unpackers/PROTOCOL.md`](../tools/unpackers/PROTOCOL.md)). Unpackers run only in build tools: a foreign
archive becomes files inside an `.rpak`, and the game still reads only `.rpak`.

Out of the box: zip (and `.oramap`), tar, dmg, Warcraft I/II archives (`.WAR/.SUD/.INS`), Westwood `.PAK` (Dune II,
Lands of Lore, Kyrandia), Z by the Bitmap Brothers (`.PAC`, `.RSC`).

```bash
assetpipe formats                                   # which formats are known
assetpipe probe ~/Downloads/Warcraft2/war2/DATA/*   # what these files are
assetpipe list MAINDAT.WAR                          # what is inside
assetpipe import -o war2.rpak MAINDAT.WAR=imported/war2 SFXDAT.SUD=imported/war2/sfx
assetpipe unpackers new tools/unpackers myformat    # scaffold for a new unpacker
assetpipe unpackers check                           # check all plugins
```

Every command has `--json` — that is how the MCP server calls them. `game.json` can list `"imports"`, and
`aiversebuild` unpacks them into the pack during the build (`"optional": true` — if the file is missing on this
machine, the build goes on without it). A pack with imported third-party content is flagged "external": such
builds are for personal use only and must not be committed or distributed.

### Importing games (importers)

An importer turns your copy of a classic game into a game folder for the engine. Dune II is available now:

```bash
python3 tools/importers/dune2/import.py --source "~/Downloads/Dune II" --out ~/AiverseGames/dune2
aiversebuild --project ~/AiverseGames/dune2 --target macos
```

The result is a 2D game in the engine's format (`docs/game-format.md`, "2D games"): a top-down camera, 66 campaign
missions with a tile layer (`tilesets/dune.json` from ICON.ICN: sand, rock, dunes, mountains, spice with autotiling),
unit sprites from SHP (5 directions with mirroring, tank turrets layered over the hull) and buildings from ICN tiles
(construction, ruins, animation), house colour from magenta shades. Plus `rules.json` (our Dune II balance on engine
blocks), CPS images and WSA animations as PNG, VOC sounds → WAV + `audio.json`, names in English and Russian.
Terrain is approximate: the original landscape generator is not used, bases stand on rock, spice comes from
`Field=`, mission quotas are triggers. Infantry, the ornithopter and the carryall are still drawn as 3D shapes.
The folder is marked `"external": true` — it must not be committed or distributed.

## Project file .aiverse

A project is a folder anywhere on disk (`game.json`, `rules.json`, `maps/`, ...). Its root holds the project file
**`<Name>.aiverse`** — opened with a double click, like `.uproject` in Unreal or `project.godot`. `game.json`
stays the game manifest (read by the client, aiversebuild, packs); `.aiverse` only marks the folder as a project
and stores editor information:

```json
{"format": 1, "name": "My Strategy", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
```

- It is created with the project (hub "New project", MCP `create_project`); an older project without it gets one
  on first open in the editor. Templates in `templates/*/` already have one (it is not copied into a new project:
  that gets its own).
- Everywhere the editor accepts a project path, both `.aiverse` and the folder work: `aiverse_editor "My
  Strategy.aiverse"`, `--project`, "Open…" in the hub (shows `.aiverse` and project folders), recent projects,
  MCP `open_game`, `aiversebuild --project x.aiverse`. `.aiverse` does not go into a build.
- Projects made before the AIVERSE rename keep their `<Name>.rtsproj`: it opens the same way everywhere, is rewritten
  in place and is never renamed; when a folder has both, `.aiverse` wins.
- **Double click.** On start the editor associates `.aiverse` with itself (per user, without admin rights) if there
  is no association or it points to another editor instance; it does not override another program you chose.
  Manually: "Project → Make the editor the default app for .aiverse"; the state is shown at the bottom left of the hub.
  Disable with `AIVERSE_FILE_ASSOC=off`.
  - **macOS:** the `com.aiverse.project` type is declared in the Info.plist of `AiverseEditor.app`
    (`platform/macos/EditorInfo.plist.in`; the `rtsproj.icns` document icon is drawn by `aiverse_projicon` from the
    editor logo `apps/editor/branding/icon_1024.png`); the editor registers the bundle in LaunchServices
    (`lsregister -f`) and becomes the default handler. A double click and `open -a AiverseEditor x.aiverse` open the
    project in the running editor (if the map has unsaved changes, it asks whether to save).
  - **Windows:** `HKCU\Software\Classes\.aiverse` (+ `.rtsproj`) → `AIVERSE.Project` (icon
    `%USERPROFILE%\.aiverse\icons\rtsproj.ico`, command `"<aiverse_editor.exe>" "%1"`). If Windows already has
    another program chosen for `.aiverse`, the hub suggests "Open with → Always".
  - **Linux:** the `application/x-aiverse-project` type (`~/.local/share/mime/packages/aiverse-project.xml`),
    `~/.local/share/applications/aiverse-editor.desktop` (`Exec=<editor> %f`), icons in
    `~/.local/share/icons/hicolor`, the default app in `~/.config/mimeapps.list`;
    `update-mime-database`, `update-desktop-database`, `xdg-mime` are called if installed.

## Make a game (wizard)

The **Make a game** button in the projects window (and **Project → Make a game…**): give a folder or a link with art,
music and texts, describe the idea (or nothing — the wizard chooses itself) and get a finished `.aiverse` project:
a game on a suitable template, a step-by-step tutorial (hard) and in-battle hints (soft), levels with stars and
rewards, meta (menu, upgrades, shop, daily reward, profile, settings), ads and purchases if wanted, languages, an icon
and a splash screen from the art. The wizard plays through the tutorial with a bot and checks the levels itself; what
was assigned and what to check is in the project's `WIZARD.md`. For agents: MCP `game_make_options` /
`game_make_plan` / `game_make`. Details: [game-wizard.md](game-wizard.md).

## How a game is described

An object type is a set of optional blocks; whatever is set, the object can do:

| Block | Meaning |
|---|---|
| `health` | health, armor, armor type, regeneration |
| `move` | speed, layer (`ground`/`air`/`naval`), radius |
| `structure` | a building with a `footprint` size on the grid |
| `weapons` | range, damage, damage type, cooldown, projectile, splash, which layers it hits |
| `cost` | resources, time, supply |
| `produces` / `builds` | what it trains / what it builds |
| `gather` / `resourceNode` / `dropoff` | resource gathering (Warcraft/StarCraft) |
| `income` / `storage` + resource `"kind": "flow"` | flow economy (Total Annihilation) |
| `supply` | provides supply |
| `requires` | tech tree |
| `garrison` + `entersGarrison` | garrison buildings, sending a percentage of troops, capture (Mushroom Wars) |
| `view` | appearance: `shape`, `scale`, `color` |

Global: `resources`, `damageTable` (damage type × armor type), `supplyCap`, `victory`.
Map: size, terrain (`rows`: `.` ground, `~` water, `#` rock, `,` no building, `=` ford; `heights`),
players and teams, starting resources, objects, roads, `roadsOnly`.

## Localization

A game is translated without changing `rules.json`: names in the data are text in the game's source language,
translations live in `<project>/lang/<language>.json` under keys like `type.footman`, `resource.gold`, `map.valley.title`…
Menu and HUD strings are in `engine/assets/lang/` (currently English and Russian); a game can override any of them.

```jsonc
// game.json
"sourceLanguage": "en",
"languages": ["en", "ru"]
```

```bash
./build/mac/aiverse_i18n extract --project templates/skirmish --lang de --write   # lang/de.json skeleton, new strings marked // TODO
./build/mac/aiverse_i18n check   --project templates/skirmish                     # placeholders, plural forms, untranslated strings
```

The language is chosen as follows: the player's choice in the menu (remembered) → the system language → the game's
source language. For testing: `--lang ru`, `AIVERSE_LANG=ru`, the pseudo-language `--lang qps` (all strings distorted
and longer — untranslated text and overflow are obvious). Plural forms follow CLDR
("1 second / 2 seconds / 5 seconds"). More: `engine/i18n/README.md`.

## Game UI (RmlUi, HTML/CSS)

The client's menu, HUD, command card, minimap and pause are RmlUi documents (`.rml` markup, `.rcss` styles),
not code. The engine draws them through its RHI (GL/Metal); fonts go through stb_truetype (Latin + Cyrillic).

- Default documents: `engine/assets/ui/` (`menu.rml`, `hud.rml`, `theme.rcss`).
- A game overrides any file by putting `ui/<same name>` into its folder: for example, only `ui/theme.rcss` —
  its own colours with the default markup. The game font is `fonts/ui.ttf`.
- Data comes from the `game` model (the field list is in a comment in `hud.rml`); button presses become
  simulation commands (`apps/client/ClientUi.cpp`).
- Sizes are in `dp` — the UI scales with screen density; on touch devices the root has the `touch` class
  (bigger buttons).
- F8 in the client opens the RmlUi debugger (element tree, styles, log).
- The game skin is `ui/skin.rcss` (loaded after `theme.rcss` in every document: colours, borders, buttons,
  layout) and the heading font `fonts/title.ttf` (family `title`). Each prototype in `templates/` has its own skin.
  The portrait layout (`@media (orientation: portrait)`) is in the engine theme; on a PC, check it with
  `--size 540x960`.
- Texts in documents are written as keys `@menu.play`: the translation is taken from `engine/assets/lang` (and the
  game's `lang/`) when the document loads; changing the language in the menu reloads the documents.
- On-screen keyboard (`engine/ui/UiKeyboard.*`): built on RmlUi, opens when an input field gets focus on touch
  devices (on a PC — `AIVERSE_OSK=1`). Layouts (en, ru, symbols; Shift, Caps on double tap, language switch) are in
  `keyboard.json`, the look in `keyboard.rml` / `keyboard.rcss`; a game replaces any of these files in its `ui/`.
  If the cursor is at the bottom of the screen, the keyboard moves to the top.
- Multiplayer (`lobby.rml`, `apps/client/Lobby.cpp`): server address and name, quick play through server queues,
  the room list (join / watch), your own room (slots, bots, ready state, start, chat). The match is run by `NetClient`:
  `attach(sim)`, `advance(dt)` instead of `sim.advance(dt)`, commands come from slot `match().localSlot`
  (0 — spectator); bots the server assigned to this client are computed here too.

## Determinism

The simulation is bit-identical on every platform: fixed-point only, its own RNG, commands in canonical order,
checksums in replays. Verified: Linux x86-64 (GCC, Clang, MinGW), Linux ARM64 (GCC), macOS ARM64 (AppleClang) —
identical match checksums.

## Graphics and mobile

A single forward pass: sun (Lambert + specular), a shadow map fitted to the visible part of the map and snapped
to texels, hardware PCF (1/4/9 taps), hemispheric ambient, distance fog, ACES. Shadow quality is chosen by GPU
(low for old Mali/Adreno/PowerVR) and can be changed in the pause menu.

## Multiplayer

`engine/net` is multiplayer for any game on the engine, with no game code. Only commands travel over the network:
the simulation is deterministic, so everyone has the same state.

- **Transport:** UDP with its own reliability layer (iOS, Android, PC) and WebSocket (HTML5; on native — a fallback
  when UDP is blocked). Address: `host[:27015]`, `udp://…`, `ws://host:27016`.
- **Rollback lockstep.** The server confirms each tick's input: when commands from all players have arrived or the
  deadline (`lagToleranceMs`) has passed; late commands move to the next tick — one lagging player does not stop the
  others. The client does not wait for confirmation: it plays ahead with its own commands and, on divergence, restores
  a snapshot and re-simulates (a snapshot takes ~10 µs). `rollbackWindow: 0` is classic lockstep for weak devices.
- **The `aiverse_server`** does not need the game content: rooms (slots, bots, spectators, ready state, data version
  check), automatic matchmaking through queues from `server.json` (Elo rating with a widening window, a shared map,
  team balance, filling with bots), checksum comparison (desync), replays (`aiverse_headless --replay` plays them),
  reconnection, delayed spectators.
- **Leaving = surrender.** A player who left (or did not return within `reconnectGraceSec`) surrenders with a command
  in the shared input stream: the outcome is decided by the simulation, identically for everyone, and is visible in
  the replay. Rating is deducted.
- **Server verdict.** If nobody confirmed the outcome (everyone left, results disagree), the server replays the match
  in a background thread on its own game data (`"games"` in `server.json`) and decides the winner itself.
- **Reconnection:** after a network drop the client reconnects on its own (`NetClient(address, …)` or `setReconnect`),
  the simulation keeps running — the server sends only the input after the last confirmed tick. If the game was
  restarted, the client receives the whole history and re-simulates the match from the start.
- **In the game:** `NetClient` (`engine/net/NetClient.h`) handles the lobby and the match. The game builds a
  `Simulation` from `match()` and calls `attach(sim)`, then `advance(dt)` instead of `sim.advance(dt)`; the UI still
  submits commands through `sim.submit()`.
- **Texts:** the server sends no ready-made phrases — only localization keys with arguments (`NetText`): players in
  one match may use different languages. The client translates them with `net::translate(loc, event.text)` and
  `net::displayName(...)` from `engine/net/NetLocalize.h`; strings live in `engine/assets/lang/*.json` under `net.*`.
  Queue names in `server.json` are keys too (`"@net.queue.1v1"`).

```bash
./build/mac/aiverse_server --config apps/server/server.json            # UDP 27015, WebSocket 27016
./build/mac/aiverse_netbot --server 127.0.0.1 --project templates/mushrooms --queue 1v1   # a bot in the queue
./build/mac/aiverse_netbot --server ws://127.0.0.1:27016 --project templates/mushrooms --queue 1v1
./build/mac/aiverse_net_tests                                          # network tests
docker build -f apps/server/Dockerfile -t aiverse-server .             # server in a container
```

For `wss://` (a game page served over HTTPS), put a TLS proxy (nginx, Caddy) in front of the server.

## Audio

`engine/audio` is a mixer, sound banks from data, procedural synthesis and music; output goes through
[miniaudio](../third_party/miniaudio) (CoreAudio, AAudio/OpenSL ES, Web Audio, WASAPI, ALSA/PulseAudio).
`modules/rts/audio` turns simulation events into sounds; it writes nothing into the simulation.
No device — the game runs silently. No `audio.json` — the game simply has no sound.

A game's audio is the `audio.json` file next to `game.json` (another name: `"audio"` in game.json). The full format
is in `docs/game-format.md`, section audio.json; in short:

```jsonc
{
  "volume": { "sfx": 0.9, "music": 0.6, "voice": 1, "ui": 0.7, "ambient": 0.7 },
  "sounds": {
    "sword": { "files": ["sfx/sword1.ogg", "sfx/sword2.ogg"], "pitch": [0.9, 1.1], "maxInstances": 3, "cooldown": 0.06 },
    "alarm": { "file": "voice/under_attack.ogg", "bus": "voice", "priority": 10 },
    "blip":  { "synth": { "wave": "square", "freq": [880, 440], "duration": 0.08 }, "bus": "ui" }
  },
  "music":     { "menu": "music/menu.ogg", "match": { "files": ["music/a.ogg", "music/b.ogg"], "shuffle": true } },
  "events":    { "died": "death", "underAttack": "alarm", "notEnough.gold": "need_gold" },
  "weapons":   { "bow": { "attack": "bow_release", "hit": "arrow_hit" } },
  "abilities": { "heal": "heal_cast" },
  "types":     { "footman": { "select": "footman_what", "move": "footman_yes", "died": "death_human" } }
}
```

| What | Meaning |
|---|---|
| formats | WAV, OGG Vorbis, MP3, FLAC; `"synth"` — a sound from parameters (`sine/square/saw/triangle/noise`, frequency sweep, envelope, vibrato, filter, layers) |
| buses | `sfx` and `ambient` are positional (3D: attenuation, panning, height); `voice`, `ui`, `music` are not |
| distance | `spatial`: attenuation model, ranges, "air" (highs fade with distance), reverb grows with distance, speed of sound; a sound can have its own ranges and `far` — a distant variant with a crossfade |
| `maxInstances`, `cooldown`, `priority` | thinning out in big battles: 40 archers don't produce 40 identical shots |
| `groups` | shared limits for several sounds (all melee, all explosions, a whole bus): `max`, `cooldown`, which to drop — `farthest` (default: distant combat does not cut off what is on screen), `oldest`, `quietest`, `none` |
| events (cues) | `attack hit miss died spawned trainStarted ready buildPlaced buildDone gathered captured lost playerDefeated victory defeat notEnough notEnoughSupply noRoom researched levelUp ability readyToPlace sold regionClaimed wave lowPower upgraded message underAttack baseUnderAttack select move attackOrder buildOrder click error` |
| sound lookup | type → weapon/ability by id → `events` |
| music | playlists `menu`, `match`, `battle`, `victory`, `defeat` (and `match.<map>`); crossfade, intro + seamless loops, layers by combat intensity, transitions on beats/bars, stingers, ducking under voice |
| compression at build time | `aiversebuild` re-encodes audio per platform: ADPCM for effects, Vorbis for music, lower sample rate, mono; `desktop`/`mobile`/`web` profiles in `audio.json` → `build`; `--audio off` — no re-encoding |

Combat sounds are heard only where there is no fog of war; "we are under attack" comes with a pause and only if the
camera is looking elsewhere. Volume (master and music) is in the pause menu and stored in `userDataDir/audio.cfg`.
On a phone, audio pauses when the app goes to the background.

Debugging without a sound card: `AIVERSE_AUDIO=null` (a real audio thread without hardware), `AIVERSE_AUDIO=off`,
`AIVERSE_AUDIO_CAPTURE=/tmp/mix.wav` — record everything that played into a WAV.

## Models, materials, animation

A type is drawn from its `"view"` block: a built-in shape (`"shape"`) or a model file (`"model"`) —
**glTF 2.0** (`.glb`, `.gltf` + `.bin` + images) and **OBJ + MTL**. Everything goes through one pipeline:

- **PBR materials** (metallic-roughness): base colour, normal, metallic-roughness, occlusion, emissive; opaque / mask /
  blend transparency, double-sided, unlit, material variants (`KHR_materials_variants`), overrides in `"materials"`.
  Player colour: on the whole material, by a mask texture, by texture alpha (as in Warcraft III) or by vertex alpha.
- **Skeletal animation** on the GPU (4 influences, any number of bones), clips per unit state (`idle`, `move`,
  `attack`, `gather`, `build`, `die`, `spawn`, `hit`, `cast`…), smooth transitions, layers with a bone mask (attacking
  with the upper body while walking), stride speed from the actual speed.
- **Procedural animation** on any model node or shape: bobbing, spinning (mills, propellers), wheels by distance
  travelled, turret aiming at the target, recoil, leaning into turns, falling and sinking on death and more; driven by
  states and combat events.
- **Quality tiers** for materials (`low` / `medium` / `high`) are chosen by GPU, like shadows;
  `AIVERSE_MATERIALS=low|medium|high` overrides it for testing.
- **Crowds**: clips are baked into a texture, identical units are drawn with instancing (300 animated units —
  61 draw calls instead of 1801); model LODs by distance; animation off screen and far away updates less often.
- **Advanced animation**: walk/run blend space, additive clips (breathing, recoil), foot IK on terrain, head turning
  towards the target, vehicles tilting on slopes, morph targets (blend shapes) on the GPU.
- **Sockets**: a weapon in the hand, cargo on the back (by the carried resource), projectile launch points, effect
  points (smoke, fire, exhaust); clip events and `RtsView::events()/effects()` for sound and particles.
- **Stages**: scaffolding and construction stages, a damaged variant by health, dissolve on spawn and death, selection
  outline, a translucent ghost while placing a building, portraits.
- **Formats**: also **FBX** (ufbx) and compressed **KTX2** textures (ETC2/BC/ASTC, staying compressed on the GPU);
  **Collada**, **3DS**, **PLY**, **STL**; **MagicaVoxel .vox** and **.vxl/.hva** voxels (Tiberian Sun, Red Alert 2);
  classic RTS models from the user's copy of the game: **Warcraft III .mdx/.mdl** (+ BLP), **Total Annihilation .3do**,
  **C&C Generals .w3d** (+ DDS). Everything else assimp reads (.blend, .lwo, .x, .ms3d, .md2/.md3, .smd, .b3d…) is
  converted to glTF by the **modelconv** utility: the editor does it itself on open and save, and the packager puts
  only the result into the package.
- **Materials on request**: a shared `materials.json` library (inheritance via `base`), assigning a material to a
  model node (`"assign": {"Turret": "steel"}`), procedural seamless textures with normal maps and player-colour masks;
  the **Materials** window with a preview and the MCP operations `material_*` / `texture_generate` for agents.

A broken model does not break the game: the type is drawn as a shape and the reason goes to the editor console.
Viewing: `aiverse_modelview model.glb@Walk` or `aiverse_modelview --game templates/showcase` (every type of a game, as
the game draws it); in the editor — the **Models** window (clips, nodes, materials, portrait, reload when the model file
is saved). Example: `templates/showcase`; full format: [models.md](models.md).

## Lighting

Dynamic lights are designed for old Android devices (Mali-T, Adreno 3xx/4xx): all lights are drawn into a small light
map above the visible ground, and the shader takes light with a single texture sample however many lights there are.
Tiers: `off`, `low` (128² map, updated every other frame), `medium` (256²), `high` (+ 8 per-pixel lights with
specular). Chosen by GPU, like shadows; manually: `AIVERSE_LIGHTS=…`. If frames keep missing the budget during a match,
the client lowers lights and shadows step by step (the most expensive and least noticeable first) and restores one step
after a heavy battle; choosing shadows manually in the menu turns this off (`AIVERSE_ADAPTIVE=off`).

- **Type and combat lights** — `view.light` (campfires, forges, glowing mushrooms), flashes from shots, hits and
  explosions, projectile lights (`attackLight`, `projectileLight`, `hitLight`, `deathLight`), ability lights
  (`abilityLights`; by default the colour follows the effect: damage — orange, healing — green), `halo` glows instead
  of expensive bloom. Fog of war hides the flashes.
- **Day and night, weather** — the `lighting` block in `game.json` or in a map: `time`, `dayLength`, `nightLevel`,
  `clouds`, `wind`, `storm` (cloud shadows and lightning, identical in replays), `preset`: `desert`, `snow`, `volcanic`,
  `overcast`, `cave` (underground: eternal night, light only from sources). At night emissive materials glow brighter
  and lights reflect on water.
- **Map lights** — the `lights` array in a map (lanterns, campfires, lava).
- **Contact shadows and AO** — soft spots under units and around buildings, on every tier (`contactShadow`,
  `contactShape` on the type).
- **Editor** — the "Lighting" window: preview of the time of day and storms, light quality tier, list of lights.
  For agents: `light_add`, `light_update`, `light_delete`, `light_list`, `lighting_set`, `view_lighting`.

The field format is in the header of `modules/rts/view/RtsLights.h`. Test variables: `AIVERSE_TIME_OF_DAY`,
`AIVERSE_NIGHT`, `AIVERSE_CLOUDS`, `AIVERSE_STORM`.

## Checking mechanics over MCP

An agent building a game without code checks mechanics precisely, not only with a bot match.
- **`scenario_run`** — a scenario on a copy of the open map (nothing is saved): `clear` (an empty map without objects,
  waves and starting units), `spawn` with names (`"id": "foot", "count": 3` → `foot#1..3`, `"foot"` — all), commands
  by second (the same keys as `play_command`), `bots` for some slots, `checks` with a pass/fail result (`alive`, `dead`,
  `count`, `hp`, `resource`, `event`, `trigger`, `variable`, `distance`, `status`, `result` + comparison), `watch` +
  `every` (object state over time), `events` (event log).
- **`play_units` / `play_command`** — commands in a live test in the Game window (previously only inside the editor,
  now visible to MCP clients). Object references: ids from `play_units`, `id` names from the map or a filter
  `{type, owner, tags, kind, area, near + radius}`; `queue: true` — queue (Shift), `ability` by id.
- **`match_run`** — plus a log of fired triggers and `events` (event log by name).
- **`rules_lint`** — keys in `rules.json` and the map that the loader does not read (typos, a block in the wrong place),
  with a hint ("did you mean 'armor'?"). It runs on the real loader (`JsonAccessLog` in `engine/data/Json.h`);
  `rules_set`, `rules_merge` and `map_validate` report such keys immediately.
- **`rules_schema`** — `part: "keys"`: every key the loader asks for in each block (`types.*.weapons[]`), collected
  across all games, and keys missing from `docs/game-format.md`; `part: "doc"` — the document itself.
- The simulation event `kEvTriggerFired` (for tools and presentation only, not part of the checksum).
- Branch `agent-mech-tools`.

## Post effects

Full-screen effects on top of the 3D scene are set with data: the `"postfx"` block in `game.json` (a map can override
it). There is bloom, colour grading, LUT, vignette, tilt-shift, sun rays, FXAA, grain, aberration, sharpening,
pixelation, posterization, tint and letterbox, plus ready-made sets (cinematic, vivid, noir, retro, miniature, sunset,
winter, night vision). Each effect has `minQuality`. The tier is chosen by GPU: weak Android devices get "low", phones
"medium", PCs "high"; `AIVERSE_POSTFX` overrides it. Without effects, post-processing costs nothing. Light effects are
combined into one pass; heavy ones (bloom, FXAA, rays) add their own.

Editor: the **Post effects** window (preview of tiers, a table of frame cost per device class).
MCP: `postfx_catalog/get/add/set/remove/preset/enable/estimate/lut_neutral`, `view_postfx`. Responses to changes include
an ms/frame estimate and warnings about heavy effects. Details: `docs/postfx.md`.

## Verified / not verified

- ✅ Linux: client, editor, tests; Windows .exe (MinGW cross-build, run under Wine).
- ✅ macOS: build, tests, running the game on Metal; aiversebuild → .app and an iOS project (+ simulator build).
- ✅ Web: builds (Emscripten 3.1.50), a match runs in headless Chromium (WebGL2/SwiftShader).
- ⏳ Android: the Gradle project is generated; the APK and an emulator run are in CI (job `android`), not built locally.
- ⏳ Windows on real hardware and iOS on a device — not run (in CI: MSVC build and iOS simulator build).

## Platforms and CI

`.github/workflows/ci.yml` on every push to `master`/`agent-*` and on pull requests:

| Job | What it does |
|---|---|
| `linux` | gcc and clang (x64) + gcc (ARM64), warnings as errors, tests, bot matches, screenshots under Xvfb |
| `windows-msvc` | Visual Studio 2022, tests, matches, `aiversebuild --target windows` |
| `windows-mingw` | MinGW-w64 cross-build, tests and matches under Wine |
| `macos` | universal Xcode build, tests, matches, `aiversebuild` → .app and an iOS project with a simulator build |
| `android` | `.so` via the NDK, APK via `aiversebuild`; in the emulator: launch → tap → background → return → "back" → relaunch, logcat checked for crashes |
| `web` | Emscripten, a match in headless Chromium, console and frame checks |
| `determinism` | compares match checksums (both games × 3 seeds) from all platforms — they must match bit for bit |

Screenshots, the APK, the web build and the .app are kept as run artifacts.

Locally:

```bash
# Windows .exe from Linux/macOS
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64.cmake
# bot matches on every game → a checksum file (compare between machines with diff)
.github/scripts/headless-checksums.sh build/linux-release/aiverse_headless sums.txt
# smoke test of the web build
node platform/web/smoke.mjs build/web
```

Command-line parameters in the web build are passed through the URL: `index.html?map=0&spectate` = `--map 0 --spectate`.

On-screen keyboard: `platform::startTextInput(area)` / `stopTextInput()` open and close it on iOS, Android and in
mobile browsers (on desktop they do nothing). Text arrives as `Char` events in any language, Backspace/Enter as
`KeyDown`/`KeyUp`; `softKeyboardHeight()` and the `KeyboardChanged` event report how much of the screen is covered
at the bottom, and `TextInputEnded` reports that the user closed the keyboard. The UI (`UiLayer`) calls this itself on
focus in `<input type="text|password|search|email|number">` and `<textarea>`.

Other mobile and browser features in `Platform.h`:
- `safeAreaInsets()` — insets for the camera notch, rounded corners, the home indicator and system bars
  (iOS, Android, mobile Safari/Chrome; 0 on desktop, on a MacBook with a notch — in full screen).
- `setFullscreen()` / `isFullscreen()`; built-in toggles — F11 (Windows, Linux, web), Alt+Enter (Windows).
  In the browser, full screen also locks the orientation (`AppConfig::orientation`).
- Android: the Back button reaches the game as `Key::Escape`; full screen with auto-hiding bars; a physical keyboard
  and mouse (Chromebook, DeX) produce ordinary key and mouse events.
- iPad: Magic Keyboard / Bluetooth keyboard, mouse and trackpad (right click, hover, scrolling).
- The clipboard works everywhere; in the browser Ctrl/Cmd+V reaches the game after the browser hands over the text.
- Web: `userDataDir()` is stored in IndexedDB — settings and saves survive a page reload.

### Native plugins: payments and ads

Any platform SDK is connected as a plugin in its native language — Java (Android), Objective-C (iOS/macOS),
JavaScript (web) or C++ — through one asynchronous bridge, `engine/platform/Plugins.h`. A game uses the shared APIs
`aiverse::store` (purchases) and `aiverse::ads` (ads) from `engine/services`, and the provider is chosen per platform in
`game.json`. Ready plugins in `plugins/`: **App Store**, **Google Play**, **RuStore**, **Yandex Games** (web: purchases
and ads), **Steam** (purchases through the Inventory Service and DLC, Windows/Linux/macOS), **Yandex Mobile Ads**,
**AdMob**, **AppLovin MAX**, **ironSource (Unity LevelPlay)**. A build contains only the plugins the game selects
(`"plugins"` in game.json or `--plugins`): no foreign SDKs, dependencies, manifest entries or Info.plist keys — CI checks
this. For desktop debugging there are store and ads stubs (`mock-store`, `mock-ads`; `AIVERSE_MOCK_STORE`,
`AIVERSE_MOCK_ADS`): the whole purchase flow runs without a phone, and the stubs are not compiled into release builds.
aiversebuild writes the plugins into Gradle/the manifest, Info.plist/Podfile and HTML itself; build Google Play and
RuStore separately: `--plugins googleplay,admob` and `--plugins rustore,yandex-ads`. Details: [plugins.md](plugins.md).

### Releases

`.github/workflows/release.yml`: a `v1.2.3` tag (or a manual run with a game folder of your choice) builds every
platform with `aiversebuild` and publishes a GitHub Release:

| Platform | Output | Signing (repository secrets) |
|---|---|---|
| Windows | installer (Inno Setup, no admin rights) + portable .zip | `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` |
| Android | .apk and .aab for Google Play | `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` |
| macOS | .app in a .zip, notarized | `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD` (Developer ID) + an App Store Connect key |
| iOS | Xcode project; with a key — a TestFlight build | `APPLE_TEAM_ID`, `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_P8_BASE64` |
| Linux | .tar.gz | — |
| Web | .zip; with `DEPLOY_WEB_PAGES=true` (repository variable) — published to GitHub Pages | — |

Without secrets the release still comes out: the APK is signed with a debug key, the .app ad-hoc, Windows unsigned.

### Prebuilt engine (the `builds` branch) and aiversemcp without a compiler

The prebuilt editor and tools are published on the **`builds`** branch:
`<platform>/AIVERSE-<version>-<commit>-<platform>.zip` (currently `windows` x86_64 and `macos` arm64; `linux` x86_64
when the integrator publishes it). Inside is an `AIVERSE/` folder with the engine sources at that commit (templates
`templates/`, `engine/assets`, `docs/` — the MCP server reads them) and `AIVERSE/bin/` with the programs. Packaged by
`tools/release/package_engine.py`, published by `tools/release/publish_builds.py` (AGENTS.md §8); the Linux package may
come without the editor — only `aiversemcp` and `aiverse_headless`.

The MCP server from the unpacked folder (bot matches and `balance_*` run inside aiversemcp, it does not need a separate
`aiverse_headless`; the editor is optional, without it use `--no-editor`):

```bash
# ARCHIVE is the path to the AIVERSE archive downloaded from index.json.
unzip "$ARCHIVE" -d ~/aiverse      # unzip keeps the executable bit
~/aiverse/AIVERSE/bin/aiversemcp --no-editor --engine ~/aiverse/AIVERSE
```

`--engine` can be omitted in builds from commits where aiversemcp looks for the engine next to itself
(`engine/platform/EngineDir.h`, like the editor); in earlier published builds it is required. With unpackers that do not
restore permissions (Python `zipfile`), run `chmod +x` on the files in `bin/`. On macOS a file downloaded by a browser
may get quarantined: `xattr -dr com.apple.quarantine ~/aiverse/AIVERSE`.

Programs that download the engine themselves read **`index.json`** at the root of the branch
(`https://raw.githubusercontent.com/chromealex/AIVERSE/builds/index.json`), not the README. Below is an example of a
historical RTSEngine entry; in new builds the paths contain AIVERSE, and the `rtsmcp` key is kept:

```json
{
  "schema": 1,
  "repository": "chromealex/AIVERSE",
  "branch": "builds",
  "packages": [
    {
      "platform": "macos",
      "arch": "arm64",
      "version": "0.2.0",
      "commit": "77126fa9",
      "file": "macos/RTSEngine-0.2.0-77126fa9-macos.zip",
      "url": "https://raw.githubusercontent.com/chromealex/AIVERSE/builds/macos/RTSEngine-0.2.0-77126fa9-macos.zip",
      "size": 43055202,
      "sha256": "c08044fd5a13648016f4d0f2184edb6054d338a814c2b2de2caab09b4cf4513d",
      "root": "RTSEngine",
      "rtsmcp": "RTSEngine/bin/rtsmcp",
      "editor": "RTSEngine/bin/RTSEditor.app",
      "headless": "RTSEngine/bin/rts_headless"
    }
  ]
}
```

- `packages` go from newest to oldest; the first entry with the needed `platform` + `arch` is the latest build.
- `platform`: `windows` | `macos` | `linux`; `arch`: `x86_64` | `arm64` | `universal` (macOS fat) | `null`
  (not recognized) — determined from the `aiversemcp` header inside the zip.
- `rtsmcp`, `editor`, `headless` are paths inside the archive, or `null` if the program is not in the package;
  `--engine` is the `root` folder. On macOS, `editor` is an `.app` bundle.
- `url` points to a branch that is rewritten on every publish (5 versions per platform are kept): old entries disappear,
  so download from a fresh `index.json` and check `sha256`.
- New fields may be added; an incompatible format change will increase `schema`.

Check: `python3 tools/release/test_release.py [build/linux-release/aiversemcp]` (CTest `release_tools`) —
the `index.json` format, and with a path to aiversemcp — packaging a Linux package, unpacking it into a temporary folder
and MCP calls from there without `--engine` (`tools/list`, `create_project`, `map_generate`, `map_validate`, `map_ascii`).

Screenshot mode (`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`) steps exactly 1/60 s per frame, so frame N is the
same on any machine.

## Next

glTF models and animation, audio, networking (lockstep), abilities and buffs from data, flow fields and RVO for large
armies, AI improvements, C# scripting (AOT for iOS).

## C++ game modules

Besides Lua, a project can contain `native/CMakeLists.txt` and C++20 modules. They are added with
`aiverse_native_module(...)` at build time and enabled by the `nativeModules` array in `rules.json`. Ticks, commands,
rollback and state saving are supported; Lua and C++ work together. After changing C++ the game and the editor must be
rebuilt. [API, example and build instructions](native-modules.md).

## Reusable C++ meta library

`aiverse::meta` — configurable shop/offers/crafting, currencies and items, quests and passes, daily rewards, energy,
mail, leaderboards, chats and segments. Available to the project's native code without Lua/GPU; definitions are JSON,
so the algorithms don't need to be generated again. Start: `python3 tools/meta/scaffold.py --preset mobile --out <project>`.
Documentation: [meta-game.md](meta-game.md); for agents — `skills/meta-game/SKILL.md`. Local policies work without a
server; communication between devices and global leaderboards need a real backend. A ready cloud service is not part
of the package.

The `full --with-ui` preset adds friends/clans, an instance collection, equipment, merging, subscriptions and tournament
leagues. `aiverse::meta::Service` executes operations atomically; `tools/meta/server.py` is a local HTTP/SQLite host with
session checks. Included are browser meta screens and a catalog of short recipes for agents.
[Building, running and limits](meta-service.md).

`full` also includes 15 LiveOps groups: chests with pity, albums/trades, job queues, research/prestige, compound quests,
events, clan wars, async PvP, party matchmaking, Elo/Glicko-2, referrals/promo codes, rewarded ads, offer chains,
notifications and offline income. All parameters live in `extensions.liveops`; this preset does not install new separate
UI screens or external SDKs. `python3 tools/meta/catalog.py describe matchmaking` prints only the needed schema and example.
[API, policies and limits](meta-liveops.md).

### Optional 3D physics

Box3D is enabled with the `AIVERSE_ENABLE_PHYSICS=ON` flag; ordinary RTS projects don't need it. A separate example,
`aiverse_physics_demo`, shows bodies, collisions, a pendulum and state saving.
[Building, API and integration limits](physics.md).

### Visual XPBD cloth

The optional `AIVERSE_ENABLE_CLOTH=ON` module: its own XPBD cloth solver on the CPU or Metal compute, shared batches,
temporal LOD, automatic sleeping and drawing dense meshes with one instanced draw. On the GPU, positions stay in video
memory; unsupported backends use the CPU. The example can compare the GPU with a CPU reference and check sleep/wake.
[Standalone example, limits and measurements](cloth.md).
