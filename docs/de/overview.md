# AIVERSE — ausführliche Übersicht

Eine Referenz zu allen Teilen der Engine: Aufbau, Build, Formate, Subsysteme, Plattformen und Prüfstatus.
Die kurze Einführung — wozu die Engine dient und welche Probleme sie löst — steht im [README](../../README.de.md).
Regeln für Agenten, die die Engine ändern, stehen in [AGENTS.md](../../AGENTS.md).

## Aufbau

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

## Schnellstart (macOS)

```bash
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release      # requires Xcode and CMake
cmake --build build/mac -j 8
open build/mac/AiverseEditor.app                              # editor (project templates/skirmish)
open build/mac/AiverseGame.app                                # game
./build/mac/aiverse_tests
```

Ein anderes Spiel im Editor: `AiverseEditor.app/Contents/MacOS/AiverseEditor --project templates/mushrooms`.
Damit das Spiel mit einem anderen Datensatz startet, übergeben Sie `-DAIVERSE_CONTENT_DIR=.../templates/mushrooms`.

### Mehrere Editoren gleichzeitig

Jeder Editor-Prozess kann mit `--project <Ordner>` sein eigenes Projekt öffnen. Ohne `--listen` belegt die erste Instanz den
MCP-Port `47321`; ist der Port belegt, vergibt das Betriebssystem einen freien. Der tatsächliche Port steht in der
Statusleiste; das Fenster **Agent** fügt ihn in die Verbindungskonfiguration ein (`aiversemcp --port <Port>`). Ein automatisch
vergebener Port kann sich nach einem Neustart des Editors ändern. `--listen <Port>` legt einen festen Port fest (bei einem
Konflikt startet MCP nicht), `--listen 0` schaltet MCP ab. Bestehende MCP-Clients wechseln nicht selbstständig zwischen Fenstern.

## Build für Plattformen — aiversebuild

```bash
./build/mac/aiversebuild --project templates/skirmish --target macos     # Builds/macos/<Name>.app (ad-hoc signed)
./build/mac/aiversebuild --project templates/skirmish --target ios       # Builds/ios/xcode/AIVERSE.xcodeproj + simulator build
./build/mac/aiversebuild --project templates/skirmish --target android   # Builds/android/<Name>-release.apk when SDK/NDK and gradle are present
./build/mac/aiversebuild --project templates/skirmish --target web       # Builds/web (requires emsdk)
./build/mac/aiversebuild --project templates/skirmish --target windows   # .exe via mingw-w64 (brew install mingw-w64) or sources + build.bat
./build/mac/aiversebuild --project templates/skirmish --target all
```

Die Ausgabe landet in `<Projekt>/Builds/<Plattform>/` — nur fertige Builds und der Größenbericht. Alles Zwischenzeitliche
(CMake-Bäume, Pakete, neu kodiertes Audio, das Gradle-Projekt, xcodebuild-Daten, die Größenschätzung des Editors) liegt im
versteckten `Builds/.temp/`, das jederzeit gelöscht werden kann. Mit `--generate-only` landet das Gradle-Projekt für
Android Studio in `Builds/android/<Name>`. Alte sichtbare Ordner `_build`, `_pack`, `_audio`, `_estimate` räumt der
Packager selbst weg.

Dasselbe gibt es im Editor: das Menü **Build**. Erzeugte iOS- und Android-Projekte sind eigenständig (Engine-Quellen und
Inhalte werden hineinkopiert) und lassen sich weitergeben. Für iOS auf einem Gerät: Projekt in Xcode öffnen und unter
Signing ein Team wählen (oder `--team <TeamID>`, oder `"appleTeamId"` in game.json).

Kennung, Name, Version, Ausrichtung und Icon (`icon.png`, 1024×1024; fehlt es, wird es erzeugt) stehen in `game.json`.

### Build-Werkzeuge: Prüfung und automatische Installation

Vor dem Build prüft `aiversebuild`, was die Plattform braucht, und sucht es nicht nur im `PATH` (ein aus dem Finder
gestarteter Editor sieht nur `/usr/bin:/bin:/usr/sbin:/sbin`): Homebrew (`/opt/homebrew/bin`, `/usr/local/bin`),
`~/.local/bin`, `~/Library/Python/*/bin`, `~/emsdk`, `$EMSDK`, `ANDROID_HOME` / `ANDROID_SDK_ROOT`, `~/Library/Android/sdk`,
`~/Android/Sdk`, `%LOCALAPPDATA%\Android\Sdk`, `JAVA_HOME`, `/usr/libexec/java_home`, Visual Studio (vswhere) und den eigenen
Ordner `~/.aiverse/toolchains`. Gefundene Ordner werden dem `PATH` von Kindprozessen hinzugefügt.

| Plattform | Benötigt | Automatisch installiert |
|---|---|---|
| web | CMake, Python 3, Emscripten (emsdk), Ninja — optional | emsdk (≈ 400 MB Download, ≈ 1,5 GB auf der Platte), CMake + Ninja (≈ 50 MB) |
| android | JDK 17–23, Android SDK (platform 35, build-tools, platform-tools), NDK, CMake aus dem SDK, Gradle | alles: Temurin JDK 17, Command Line Tools + `sdkmanager` (Lizenzen werden akzeptiert), NDK ≈ 700 MB, Gradle 8.10.2 |
| macos | Xcode Command Line Tools, CMake | CLT — über Apples Installer (`xcode-select --install`), CMake |
| ios | Xcode (App Store), CMake | CMake |
| windows (unter Windows) | Visual Studio 2022 / Build Tools (C++), CMake | CMake; VS — ein Hinweis mit einem `winget`-Befehl |
| linux | C++-Compiler, CMake | CMake |

```bash
aiversebuild --check-toolchain web            # what is present and what is missing (--json for the editor and MCP)
aiversebuild --install-toolchain web          # install what is missing (or one id: emsdk, cmake, jdk, android-ndk, gradle, ...)
aiversebuild --project ... --target web --install-missing   # install and build right away
```

Ohne `--install-missing` bricht fehlendes Werkzeug den Build mit Exit-Code 3 und der Zeile
`AIVERSE_MISSING_TOOLCHAIN web: emsdk` ab (der Editor erkennt sie). Im Editor zeigt das Fenster **Build** für die gewählte
Plattform, was gefunden wurde (Pfade, Versionen) und was fehlt, mit dem Knopf **Automatisch installieren** und dem
Kontrollkästchen „Fehlendes automatisch installieren“; der Fortschritt steht in der **Konsole** (Text lässt sich markieren und
kopieren; „Log-Ordner öffnen“ öffnet `<Engine>/logs`). Agenten tun dasselbe über MCP: `toolchain_check`,
`toolchain_install`, `build_start {"installMissing": true}`.

Alles wird nach `~/.aiverse/toolchains` installiert (Windows: `%USERPROFILE%\.aiverse\toolchains`; ein anderer Ordner — Variable
`AIVERSE_TOOLCHAINS_DIR`), ohne `sudo` und ohne Systemänderungen: `emsdk/`, `py/` (ein venv mit cmake und ninja), `cmake/`,
`ninja/`, `jdk-17/`, `android-sdk/`, `gradle-8.10.2/`. Ein erneuter Lauf ist sicher (Fertiges wird übersprungen). Um alles zu
entfernen, löschen Sie diesen Ordner. Ausnahmen: Die Command Line Tools installiert Apples Installer, und wird das Android
SDK des Nutzers gefunden, installiert `sdkmanager` fehlende Pakete dort hinein.

### Ressourcenpakete (.rpak) und Build-Größe

Spielinhalte (der Spielordner + `engine/assets`) kommen als eine Datei `content/content.rpak` in den Build: Jede Datei wird
einzeln mit zstd komprimiert, identische Dateien werden einmal gespeichert, bereits komprimierte Formate (png, ogg, mp3, …)
unverändert. Die Engine bindet `content.rpak` automatisch ein; Dateien im Paket haben Vorrang vor Dateien im Ordner, sodass
ohne Paket (bei der Entwicklung) alles wie bisher funktioniert. `platform::mountPack("patch.rpak")` bindet ein Paket darüber
ein (Patch, DLC). Jeder Build gibt einen Größenbericht aus und schreibt ihn nach `dist/<Plattform>/size-report.txt`.

```bash
aiversebuild ... --no-pack          # ship folders as is (debugging)
aiversebuild ... --pack-level 9     # zstd level (default 19 for Release, 3 for Debug)

assetpipe pack -o content.rpak templates/skirmish engine/assets=engine   # build a pack by hand
assetpipe list content.rpak       # files, sizes, compression
assetpipe verify content.rpak     # unpack everything and check hashes
assetpipe extract content.rpak out/
assetpipe diff old.rpak new.rpak  # what changed between versions (for patches)
```

Was der Build nicht braucht, steht in `.rpakignore` im Spielordner (Masken `*`, `?`; `Ordner/` — nur Verzeichnisse). Versteckte
Dateien (`.git`, `.DS_Store`), `*.rpl`-Replays und verschachtelte `*.rpak` werden immer übersprungen. Paket-Builds sind
reproduzierbar: Gleicher Inhalt ergibt eine byte-identische Datei; der „Inhalts-Hash“ des Pakets identifiziert den Datensatz.

### Geteilte Builds und Nachladen von Inhalten

Optional lässt sich ein Build teilen: Die App enthält nur ein Basispaket mit dem, was zum Start nötig ist, der Rest kommt in
Teile (`content.001.rpkc`, …) festgelegter Größe, die das Spiel aus beliebigen Quellen nachlädt. Alles entscheidet der
Spielautor, in `game.json` (gemeinsam und pro Plattform) oder mit `aiversebuild`-Flags:

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

Die Teile liegen neben dem Build (`dist/<Plattform>/chunks/`) und werden unter der Adresse aus `sources` veröffentlicht
(`{hash}` ist der Inhalts-Hash des Builds, Versionen geraten nicht durcheinander). Im Spiel wird eine Datei aus einem noch
nicht geladenen Teil automatisch angefordert (auf Desktop und Mobilgeräten wartet `readContentFile` darauf, im Web gibt es
false zurück); Fortschritt: `platform::contentProgress()`, explizite Anforderung: `platform::requestContent(paths)`. Jeder Teil
wird per SHA-256 geprüft (ein manipulierter Teil von einem Spiegel wird abgelehnt und vom nächsten geholt), im Cache gehalten
und beim nächsten Start nicht erneut geladen. Protokolle: https/http (libcurl unter Linux, WinHTTP, NSURLSession,
HttpURLConnection unter Android, fetch im Web), `file://` und Ordner; ein Spiel kann ein eigenes Schema registrieren (Steam,
Play Asset Delivery, P2P). `assetpipe pack --chunk-size 8MB ...` macht dasselbe von Hand, `assetpipe verify` prüft auch die Teile.

### Import fremder Archive (Entpacker)

Formate, die die Engine nicht kennt (zip, tar, dmg, Archive anderer Spiele), kommen als Entpacker-Plugins ohne Neubau der
Engine hinzu: ein Ordner mit `unpacker.json` und einem Programm in beliebiger Sprache (Protokoll:
[`tools/unpackers/PROTOCOL.md`](../../tools/unpackers/PROTOCOL.md)). Entpacker laufen nur in den Build-Werkzeugen: Ein fremdes
Archiv wird zu Dateien innerhalb eines `.rpak`, das Spiel liest weiterhin nur `.rpak`.

Mitgeliefert: zip (und `.oramap`), tar, dmg, Archive von Warcraft I/II (`.WAR/.SUD/.INS`), Westwood `.PAK` (Dune II, Lands of
Lore, Kyrandia), Z von den Bitmap Brothers (`.PAC`, `.RSC`).

```bash
assetpipe formats                                   # which formats are known
assetpipe probe ~/Downloads/Warcraft2/war2/DATA/*   # what these files are
assetpipe list MAINDAT.WAR                          # what is inside
assetpipe import -o war2.rpak MAINDAT.WAR=imported/war2 SFXDAT.SUD=imported/war2/sfx
assetpipe unpackers new tools/unpackers myformat    # scaffold for a new unpacker
assetpipe unpackers check                           # check all plugins
```

Jeder Befehl hat `--json` — so ruft der MCP-Server sie auf. `game.json` kann `"imports"` auflisten, und `aiversebuild`
entpackt sie beim Build ins Paket (`"optional": true` — fehlt die Datei auf diesem Rechner, läuft der Build ohne sie weiter).
Ein Paket mit importierten Fremdinhalten wird als „external“ markiert: Solche Builds sind nur für den persönlichen Gebrauch
und dürfen weder committet noch verbreitet werden.

### Import von Spielen (Importer)

Ein Importer macht aus Ihrer Kopie eines klassischen Spiels einen Spielordner für die Engine. Verfügbar ist Dune II:

```bash
python3 tools/importers/dune2/import.py --source "~/Downloads/Dune II" --out ~/AiverseGames/dune2
aiversebuild --project ~/AiverseGames/dune2 --target macos
```

Das Ergebnis ist ein 2D-Spiel im Format der Engine (`docs/game-format.md`, „2D games“): Kamera von oben, 66 Kampagnenmissionen
mit Tile-Ebene (`tilesets/dune.json` aus ICON.ICN: Sand, Fels, Dünen, Berge, Spice mit Autotiling), Einheiten-Sprites aus SHP
(5 Richtungen mit Spiegelung, Panzertürme über der Wanne) und Gebäude aus ICN-Tiles (Bau, Ruinen, Animation), Hausfarbe aus
Magenta-Tönen. Dazu `rules.json` (unser Dune-II-Balancing auf Engine-Blöcken), CPS-Bilder und WSA-Animationen als PNG,
VOC-Sounds → WAV + `audio.json`, Namen auf Englisch und Russisch. Das Gelände ist angenähert: Der originale
Landschaftsgenerator wird nicht verwendet, Basen stehen auf Fels, Spice stammt aus `Field=`, Missionsquoten sind Trigger.
Infanterie, Ornithopter und Carryall werden noch als 3D-Formen gezeichnet. Der Ordner ist mit `"external": true`
markiert — er darf weder committet noch verbreitet werden.

## Projektdatei .aiverse

Ein Projekt ist ein Ordner irgendwo auf der Platte (`game.json`, `rules.json`, `maps/`, ...). In seinem Wurzelordner liegt die
Projektdatei **`<Name>.aiverse`** — sie wird per Doppelklick geöffnet, wie `.uproject` in Unreal oder `project.godot`.
`game.json` bleibt das Spielmanifest (gelesen von Client, aiversebuild, Paketen); `.aiverse` markiert den Ordner nur als
Projekt und speichert Editor-Informationen:

```json
{"format": 1, "name": "My Strategy", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
```

- Sie wird mit dem Projekt erzeugt (Hub „Neues Projekt“, MCP `create_project`); ein älteres Projekt ohne sie bekommt eine
  beim ersten Öffnen im Editor. Vorlagen in `templates/*/` haben bereits eine (sie wird nicht in ein neues Projekt kopiert:
  das bekommt seine eigene).
- Überall, wo der Editor einen Projektpfad annimmt, funktionieren `.aiverse` und Ordner: `aiverse_editor "My
  Strategy.aiverse"`, `--project`, „Öffnen…“ im Hub (zeigt `.aiverse` und Projektordner), zuletzt verwendete Projekte, MCP
  `open_game`, `aiversebuild --project x.aiverse`. `.aiverse` kommt nicht in den Build.
- Projekte von vor der Umbenennung in AIVERSE behalten ihre `<Name>.rtsproj`: Sie öffnet sich überall genauso, wird an
  Ort und Stelle überschrieben und nie umbenannt; liegen beide im Ordner, gilt `.aiverse`.
- **Doppelklick.** Beim Start verknüpft der Editor `.aiverse` mit sich selbst (pro Nutzer, ohne Administratorrechte), wenn es
  keine Verknüpfung gibt oder sie auf eine andere Editor-Instanz zeigt; ein von Ihnen gewähltes anderes Programm überschreibt
  er nicht. Manuell: „Projekt → Editor als Standard-App für .aiverse festlegen“; der Status steht unten links im Hub.
  Abschalten mit `AIVERSE_FILE_ASSOC=off`.
  - **macOS:** Der Typ `com.aiverse.project` ist in der Info.plist von `AiverseEditor.app` deklariert
    (`platform/macos/EditorInfo.plist.in`; das Dokument-Icon `rtsproj.icns` zeichnet `aiverse_projicon` aus dem Editor-Logo
    `apps/editor/branding/icon_1024.png`); der Editor registriert das Bundle bei LaunchServices (`lsregister -f`) und wird
    Standard-Handler. Doppelklick und `open -a AiverseEditor x.aiverse` öffnen das Projekt im laufenden Editor (bei
    ungespeicherter Karte fragt er, ob gespeichert werden soll).
  - **Windows:** `HKCU\Software\Classes\.aiverse` (+ `.rtsproj`) → `AIVERSE.Project` (Icon `%USERPROFILE%\.aiverse\icons\rtsproj.ico`, Befehl
    `"<aiverse_editor.exe>" "%1"`). Ist in Windows bereits ein anderes Programm für `.aiverse` gewählt, schlägt der Hub
    „Öffnen mit → Immer“ vor.
  - **Linux:** Typ `application/x-aiverse-project` (`~/.local/share/mime/packages/aiverse-project.xml`),
    `~/.local/share/applications/aiverse-editor.desktop` (`Exec=<Editor> %f`), Icons in `~/.local/share/icons/hicolor`,
    Standard-App in `~/.config/mimeapps.list`; `update-mime-database`, `update-desktop-database`, `xdg-mime` werden
    aufgerufen, falls installiert.

## Spiel erstellen (Assistent)

Der Knopf **Spiel erstellen** im Projektfenster (und **Projekt → Spiel erstellen…**): Geben Sie einen Ordner oder Link mit
Grafiken, Musik und Texten an, beschreiben Sie die Idee (oder nichts — der Assistent wählt selbst) und erhalten Sie ein fertiges
`.aiverse`-Projekt: ein Spiel auf einer passenden Vorlage, ein schrittweises Tutorial (hart) und Hinweise im Kampf (weich),
Level mit Sternen und Belohnungen, Meta (Menü, Upgrades, Shop, Tagesbelohnung, Profil, Einstellungen), auf Wunsch Werbung und
Käufe, Sprachen, Icon und Startbildschirm aus den Grafiken. Der Assistent spielt das Tutorial selbst mit einem Bot durch und
prüft die Level; was zugewiesen wurde und was zu prüfen ist, steht in `WIZARD.md` des Projekts. Für Agenten: MCP
`game_make_options` / `game_make_plan` / `game_make`. Details: [game-wizard.md](game-wizard.md).

## Wie ein Spiel beschrieben wird

Ein Objekttyp ist eine Menge optionaler Blöcke; was gesetzt ist, kann das Objekt:

| Block | Bedeutung |
|---|---|
| `health` | Lebenspunkte, Rüstung, Rüstungstyp, Regeneration |
| `move` | Geschwindigkeit, Ebene (`ground`/`air`/`naval`), Radius |
| `structure` | ein Gebäude mit `footprint`-Größe im Raster |
| `weapons` | Reichweite, Schaden, Schadenstyp, Abklingzeit, Projektil, Flächenschaden, welche Ebenen getroffen werden |
| `cost` | Ressourcen, Zeit, Versorgung |
| `produces` / `builds` | was es ausbildet / was es baut |
| `gather` / `resourceNode` / `dropoff` | Ressourcenabbau (Warcraft/StarCraft) |
| `income` / `storage` + Ressource `"kind": "flow"` | Flusswirtschaft (Total Annihilation) |
| `supply` | liefert Versorgung |
| `requires` | Technologiebaum |
| `garrison` + `entersGarrison` | Garnisonsgebäude, Senden eines Truppenanteils, Einnahme (Mushroom Wars) |
| `view` | Aussehen: `shape`, `scale`, `color` |

Global: `resources`, `damageTable` (Schadenstyp × Rüstungstyp), `supplyCap`, `victory`.
Karte: Größe, Gelände (`rows`: `.` Boden, `~` Wasser, `#` Fels, `,` kein Bauen, `=` Furt; `heights`), Spieler und Teams,
Startressourcen, Objekte, Straßen, `roadsOnly`.

## Lokalisierung

Ein Spiel wird ohne Änderung an `rules.json` übersetzt: Namen in den Daten sind Text in der Ausgangssprache des Spiels,
Übersetzungen liegen in `<Projekt>/lang/<Sprache>.json` unter Schlüsseln wie `type.footman`, `resource.gold`,
`map.valley.title`… Menü- und HUD-Texte liegen in `engine/assets/lang/` (derzeit Englisch und Russisch); ein Spiel kann
jeden davon überschreiben.

```jsonc
// game.json
"sourceLanguage": "en",
"languages": ["en", "ru"]
```

```bash
./build/mac/aiverse_i18n extract --project templates/skirmish --lang de --write   # lang/de.json skeleton, new strings marked // TODO
./build/mac/aiverse_i18n check   --project templates/skirmish                     # placeholders, plural forms, untranslated strings
```

Die Sprache wird so gewählt: Wahl des Spielers im Menü (gespeichert) → Systemsprache → Ausgangssprache des Spiels. Zum Testen:
`--lang ru`, `AIVERSE_LANG=ru`, die Pseudosprache `--lang qps` (alle Texte verzerrt und länger — Unübersetztes und Überlauf
fallen sofort auf). Pluralformen folgen CLDR („1 Sekunde / 2 Sekunden / 5 Sekunden“). Mehr: `engine/i18n/README.md`.

## Spiel-UI (RmlUi, HTML/CSS)

Menü, HUD, Befehlskarte, Minikarte und Pause des Clients sind RmlUi-Dokumente (`.rml` Markup, `.rcss` Stile), kein Code. Die
Engine zeichnet sie über ihr RHI (GL/Metal); Schriften laufen über stb_truetype (Latein + Kyrillisch).

- Standarddokumente: `engine/assets/ui/` (`menu.rml`, `hud.rml`, `theme.rcss`).
- Ein Spiel überschreibt jede Datei, indem es `ui/<gleicher Name>` in seinen Ordner legt: zum Beispiel nur `ui/theme.rcss` —
  eigene Farben mit dem Standard-Markup. Die Spielschrift ist `fonts/ui.ttf`.
- Daten kommen aus dem Modell `game` (die Feldliste steht in einem Kommentar in `hud.rml`); Knopfdrücke werden zu
  Simulationsbefehlen (`apps/client/ClientUi.cpp`).
- Größen in `dp` — die UI skaliert mit der Bildschirmdichte; auf Touch-Geräten hat die Wurzel die Klasse `touch` (größere Knöpfe).
- F8 im Client öffnet den RmlUi-Debugger (Elementbaum, Stile, Log).
- Der Spiel-Skin ist `ui/skin.rcss` (geladen nach `theme.rcss` in jedem Dokument: Farben, Rahmen, Knöpfe, Layout) und die
  Überschriftenschrift `fonts/title.ttf` (Familie `title`). Jeder Prototyp in `templates/` hat seinen eigenen Skin. Das
  Hochformat-Layout (`@media (orientation: portrait)`) ist im Engine-Theme; auf dem PC prüfen Sie es mit `--size 540x960`.
- Texte in Dokumenten werden als Schlüssel `@menu.play` geschrieben: Die Übersetzung kommt beim Laden aus `engine/assets/lang`
  (und dem `lang/` des Spiels); ein Sprachwechsel im Menü lädt die Dokumente neu.
- Bildschirmtastatur (`engine/ui/UiKeyboard.*`): auf RmlUi gebaut, öffnet sich bei Fokus in einem Eingabefeld auf
  Touch-Geräten (am PC — `AIVERSE_OSK=1`). Layouts (en, ru, Symbole; Shift, Caps per Doppeltipp, Sprachwechsel) stehen in
  `keyboard.json`, das Aussehen in `keyboard.rml` / `keyboard.rcss`; ein Spiel ersetzt jede dieser Dateien in seinem `ui/`.
  Steht der Cursor unten am Bildschirm, wandert die Tastatur nach oben.
- Mehrspieler (`lobby.rml`, `apps/client/Lobby.cpp`): Serveradresse und Name, Schnellspiel über Serverwarteschlangen,
  Raumliste (beitreten / zuschauen), eigener Raum (Slots, Bots, Bereitschaft, Start, Chat). Das Match führt `NetClient`:
  `attach(sim)`, `advance(dt)` statt `sim.advance(dt)`, Befehle kommen von Slot `match().localSlot` (0 — Zuschauer); Bots,
  die der Server diesem Client zugewiesen hat, werden ebenfalls hier berechnet.

## Determinismus

Die Simulation ist auf allen Plattformen bitgenau gleich: nur Festkomma, eigener Zufallsgenerator, Befehle in kanonischer
Reihenfolge, Prüfsummen in Replays. Geprüft: Linux x86-64 (GCC, Clang, MinGW), Linux ARM64 (GCC), macOS ARM64 (AppleClang) —
identische Match-Prüfsummen.

## Grafik und Mobilgeräte

Ein einziger Forward-Pass: Sonne (Lambert + Glanzlicht), eine Shadow-Map, die an den sichtbaren Kartenausschnitt angepasst
und an Texel gerastert wird, Hardware-PCF (1/4/9 Abtastungen), hemisphärisches Umgebungslicht, Entfernungsnebel, ACES. Die
Schattenqualität wird nach GPU gewählt (niedrig für alte Mali/Adreno/PowerVR) und lässt sich im Pausenmenü ändern.

## Mehrspieler

`engine/net` ist Mehrspieler für jedes Spiel auf der Engine, ohne Spielcode. Über das Netz gehen nur Befehle: Die Simulation
ist deterministisch, daher haben alle denselben Zustand.

- **Transport:** UDP mit eigener Zuverlässigkeitsschicht (iOS, Android, PC) und WebSocket (HTML5; nativ als Rückfall, wenn
  UDP blockiert ist). Adresse: `host[:27015]`, `udp://…`, `ws://host:27016`.
- **Lockstep mit Rollback.** Der Server bestätigt die Eingaben jedes Ticks, sobald Befehle aller Spieler da sind oder die Frist
  (`lagToleranceMs`) abgelaufen ist; verspätete Befehle rücken in den nächsten Tick — ein laggender Spieler hält die anderen
  nicht auf. Der Client wartet nicht auf die Bestätigung: Er spielt mit seinen eigenen Befehlen voraus und stellt bei
  Abweichung einen Snapshot wieder her und rechnet neu (ein Snapshot dauert ~10 µs). `rollbackWindow: 0` ist klassischer
  Lockstep für schwache Geräte.
- **Der `aiverse_server`** braucht die Spielinhalte nicht: Räume (Slots, Bots, Zuschauer, Bereitschaft, Prüfung der
  Datenversion), automatisches Matchmaking über Warteschlangen aus `server.json` (Elo-Wertung mit wachsendem Fenster,
  gemeinsame Karte, Teambalance, Auffüllen mit Bots), Prüfsummenvergleich (Desync), Replays (`aiverse_headless --replay`
  spielt sie ab), Wiederverbindung, verzögerte Zuschauer.
- **Verlassen = Aufgeben.** Ein Spieler, der gegangen ist (oder nicht innerhalb von `reconnectGraceSec` zurückkehrt), gibt
  per Befehl im gemeinsamen Eingabestrom auf: Den Ausgang entscheidet die Simulation, bei allen gleich, sichtbar im Replay.
  Die Wertung wird abgezogen.
- **Server-Urteil.** Hat niemand den Ausgang bestätigt (alle gegangen, Ergebnisse widersprechen sich), spielt der Server das
  Replay in einem Hintergrund-Thread auf seinen eigenen Spieldaten (`"games"` in `server.json`) nach und bestimmt den Sieger selbst.
- **Wiederverbindung:** Nach einem Netzabbruch verbindet sich der Client selbst neu (`NetClient(address, …)` oder
  `setReconnect`), die Simulation läuft weiter — der Server schickt nur die Eingaben nach dem letzten bestätigten Tick. Wurde
  das Spiel neu gestartet, erhält der Client die gesamte Historie und rechnet das Match von vorn neu.
- **Im Spiel:** `NetClient` (`engine/net/NetClient.h`) übernimmt Lobby und Match. Das Spiel baut eine `Simulation` aus
  `match()` und ruft `attach(sim)` auf, dann `advance(dt)` statt `sim.advance(dt)`; die UI reicht Befehle weiterhin über
  `sim.submit()` ein.
- **Texte:** Der Server schickt keine fertigen Sätze — nur Lokalisierungsschlüssel mit Argumenten (`NetText`): Spieler eines
  Matches können verschiedene Sprachen nutzen. Der Client übersetzt sie mit `net::translate(loc, event.text)` und
  `net::displayName(...)` aus `engine/net/NetLocalize.h`; die Texte liegen in `engine/assets/lang/*.json` unter `net.*`.
  Warteschlangennamen in `server.json` sind ebenfalls Schlüssel (`"@net.queue.1v1"`).

```bash
./build/mac/aiverse_server --config apps/server/server.json            # UDP 27015, WebSocket 27016
./build/mac/aiverse_netbot --server 127.0.0.1 --project templates/mushrooms --queue 1v1   # a bot in the queue
./build/mac/aiverse_netbot --server ws://127.0.0.1:27016 --project templates/mushrooms --queue 1v1
./build/mac/aiverse_net_tests                                          # network tests
docker build -f apps/server/Dockerfile -t aiverse-server .             # server in a container
```

Für `wss://` (eine per HTTPS ausgelieferte Spielseite) stellen Sie einen TLS-Proxy (nginx, Caddy) vor den Server.

## Audio

`engine/audio` ist ein Mixer, Soundbanken aus Daten, prozedurale Synthese und Musik; die Ausgabe läuft über
[miniaudio](../../third_party/miniaudio) (CoreAudio, AAudio/OpenSL ES, Web Audio, WASAPI, ALSA/PulseAudio).
`modules/rts/audio` macht aus Simulationsereignissen Sounds; es schreibt nichts in die Simulation. Kein Gerät — das Spiel
läuft stumm. Kein `audio.json` — das Spiel hat einfach keinen Ton.

Der Ton eines Spiels ist die Datei `audio.json` neben `game.json` (ein anderer Name: `"audio"` in game.json). Das vollständige
Format steht in `docs/game-format.md`, Abschnitt audio.json; kurz:

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

| Was | Bedeutung |
|---|---|
| Formate | WAV, OGG Vorbis, MP3, FLAC; `"synth"` — ein Klang aus Parametern (`sine/square/saw/triangle/noise`, Frequenzverlauf, Hüllkurve, Vibrato, Filter, Schichten) |
| Busse | `sfx` und `ambient` sind positionell (3D: Dämpfung, Panorama, Höhe); `voice`, `ui`, `music` nicht |
| Entfernung | `spatial`: Dämpfungsmodell, Reichweiten, „Luft“ (Höhen verschwinden mit der Entfernung), Hall wächst mit der Entfernung, Schallgeschwindigkeit; ein Sound kann eigene Reichweiten haben und `far` — eine ferne Variante mit Überblendung |
| `maxInstances`, `cooldown`, `priority` | Ausdünnen in großen Schlachten: 40 Bogenschützen erzeugen nicht 40 gleiche Schüsse |
| `groups` | gemeinsame Limits für mehrere Sounds (Nahkampf, Explosionen, ein ganzer Bus): `max`, `cooldown`, was verworfen wird — `farthest` (Standard: ferner Kampf unterbricht nicht, was im Bild ist), `oldest`, `quietest`, `none` |
| Ereignisse (Cues) | `attack hit miss died spawned trainStarted ready buildPlaced buildDone gathered captured lost playerDefeated victory defeat notEnough notEnoughSupply noRoom researched levelUp ability readyToPlace sold regionClaimed wave lowPower upgraded message underAttack baseUnderAttack select move attackOrder buildOrder click error` |
| Suche des Sounds | Typ → Waffe/Fähigkeit nach ID → `events` |
| Musik | Playlists `menu`, `match`, `battle`, `victory`, `defeat` (und `match.<Karte>`); Überblendung, Intro + nahtlose Schleifen, Schichten nach Kampfintensität, Übergänge auf Schlägen/Takten, Stinger, Absenken unter Sprache |
| Kompression beim Build | `aiversebuild` kodiert Audio pro Plattform neu: ADPCM für Effekte, Vorbis für Musik, niedrigere Abtastrate, Mono; Profile `desktop`/`mobile`/`web` in `audio.json` → `build`; `--audio off` — ohne Neukodierung |

Kampfgeräusche sind nur dort zu hören, wo kein Nebel des Krieges liegt; „wir werden angegriffen“ kommt mit Pause und nur, wenn
die Kamera woanders hinschaut. Lautstärke (gesamt und Musik) steht im Pausenmenü und wird in `userDataDir/audio.cfg`
gespeichert. Auf dem Telefon pausiert der Ton, wenn die App in den Hintergrund geht.

Debuggen ohne Soundkarte: `AIVERSE_AUDIO=null` (ein echter Audio-Thread ohne Hardware), `AIVERSE_AUDIO=off`,
`AIVERSE_AUDIO_CAPTURE=/tmp/mix.wav` — alles Abgespielte in eine WAV aufnehmen.

## Modelle, Materialien, Animation

Ein Typ wird aus seinem Block `"view"` gezeichnet: eine eingebaute Form (`"shape"`) oder eine Modelldatei (`"model"`) —
**glTF 2.0** (`.glb`, `.gltf` + `.bin` + Bilder) und **OBJ + MTL**. Alles läuft durch eine Pipeline:

- **PBR-Materialien** (metallic-roughness): Grundfarbe, Normal, Metallic-Roughness, Occlusion, Emissive; Transparenz
  opaque / mask / blend, doppelseitig, unlit, Materialvarianten (`KHR_materials_variants`), Overrides in `"materials"`.
  Spielerfarbe: auf dem ganzen Material, per Maskentextur, per Texturalpha (wie in Warcraft III) oder per Vertex-Alpha.
- **Skelettanimation** auf der GPU (4 Einflüsse, beliebig viele Knochen), Clips pro Einheitenzustand (`idle`, `move`,
  `attack`, `gather`, `build`, `die`, `spawn`, `hit`, `cast`…), weiche Übergänge, Schichten mit Knochenmaske (Angriff mit dem
  Oberkörper im Laufen), Schrittgeschwindigkeit aus der tatsächlichen Geschwindigkeit.
- **Prozedurale Animation** an jedem Modellknoten oder jeder Form: Wippen, Drehen (Mühlen, Propeller), Räder nach
  zurückgelegter Strecke, Turm zielt aufs Ziel, Rückstoß, Neigen in Kurven, Fallen und Versinken beim Tod und mehr; gesteuert
  durch Zustände und Kampfereignisse.
- **Qualitätsstufen** für Materialien (`low` / `medium` / `high`) werden nach GPU gewählt, wie Schatten;
  `AIVERSE_MATERIALS=low|medium|high` überschreibt sie zum Testen.
- **Massen**: Clips werden in eine Textur gebacken, gleiche Einheiten mit Instancing gezeichnet (300 animierte Einheiten —
  61 Draw Calls statt 1801); Modell-LODs nach Entfernung; Animation außerhalb des Bildes und in der Ferne wird seltener
  aktualisiert.
- **Fortgeschrittene Animation**: Blend Space Gehen/Laufen, additive Clips (Atmen, Rückstoß), Fuß-IK auf dem Gelände,
  Kopfdrehung zum Ziel, Neigung von Fahrzeugen am Hang, Morph Targets (Blend Shapes) auf der GPU.
- **Sockets**: Waffe in der Hand, Last auf dem Rücken (nach getragener Ressource), Abschusspunkte von Projektilen,
  Effektpunkte (Rauch, Feuer, Auspuff); Clip-Ereignisse und `RtsView::events()/effects()` für Ton und Partikel.
- **Stufen**: Gerüst und Bauphasen, beschädigte Variante nach Lebenspunkten, Auflösen beim Erscheinen und Sterben,
  Auswahlumriss, durchscheinender Geist beim Platzieren eines Gebäudes, Porträts.
- **Formate**: außerdem **FBX** (ufbx) und komprimierte **KTX2**-Texturen (ETC2/BC/ASTC, bleiben auf der GPU komprimiert);
  **Collada**, **3DS**, **PLY**, **STL**; Voxel **MagicaVoxel .vox** und **.vxl/.hva** (Tiberian Sun, Red Alert 2);
  Modelle klassischer RTS aus der Kopie des Nutzers: **Warcraft III .mdx/.mdl** (+ BLP), **Total Annihilation .3do**,
  **C&C Generals .w3d** (+ DDS). Alles andere, was assimp liest (.blend, .lwo, .x, .ms3d, .md2/.md3, .smd, .b3d…), wandelt das
  Werkzeug **modelconv** in glTF um: Der Editor erledigt das selbst beim Öffnen und Speichern, der Packager legt nur das
  Ergebnis ins Paket.
- **Materialien auf Abruf**: eine gemeinsame Bibliothek `materials.json` (Vererbung über `base`), Zuweisung eines Materials an
  einen Modellknoten (`"assign": {"Turret": "steel"}`), prozedurale nahtlose Texturen mit Normal Maps und
  Spielerfarben-Masken; das Fenster **Materialien** mit Vorschau und die MCP-Operationen `material_*` / `texture_generate`
  für Agenten.

Ein defektes Modell macht das Spiel nicht kaputt: Der Typ wird als Form gezeichnet, der Grund steht in der Editor-Konsole.
Ansehen: `aiverse_modelview model.glb@Walk` oder `aiverse_modelview --game templates/showcase` (alle Typen eines Spiels so, wie
das Spiel sie zeichnet); im Editor — das Fenster **Models** (Clips, Knoten, Materialien, Porträt, Neuladen beim Speichern der
Modelldatei). Beispiel: `templates/showcase`; vollständiges Format: [models.md](../models.md).

## Beleuchtung

Dynamisches Licht ist für alte Android-Geräte (Mali-T, Adreno 3xx/4xx) ausgelegt: Alle Lichter werden in eine kleine Lightmap
über dem sichtbaren Boden gezeichnet, und der Shader holt das Licht mit einer einzigen Texturabtastung, egal wie viele Lichter
es gibt. Stufen: `off`, `low` (Karte 128², Aktualisierung jedes zweite Bild), `medium` (256²), `high` (+ 8 Lichter pro Pixel
mit Glanzlicht). Gewählt nach GPU, wie Schatten; manuell: `AIVERSE_LIGHTS=…`. Verfehlen die Bilder im Match dauerhaft das
Budget, senkt der Client Licht und Schatten stufenweise (das Teuerste und am wenigsten Auffällige zuerst) und hebt nach einer
schweren Schlacht eine Stufe wieder an; manuelle Schattenwahl im Menü schaltet das ab (`AIVERSE_ADAPTIVE=off`).

- **Licht von Typen und Kampf** — `view.light` (Lagerfeuer, Schmieden, leuchtende Pilze), Blitze von Schüssen, Treffern und
  Explosionen, Projektillicht (`attackLight`, `projectileLight`, `hitLight`, `deathLight`), Licht von Fähigkeiten
  (`abilityLights`; standardmäßig folgt die Farbe dem Effekt: Schaden — orange, Heilung — grün), `halo`-Schein statt teurem
  Bloom. Nebel des Krieges verbirgt die Blitze.
- **Tag und Nacht, Wetter** — der Block `lighting` in `game.json` oder in einer Karte: `time`, `dayLength`, `nightLevel`,
  `clouds`, `wind`, `storm` (Wolkenschatten und Blitze, identisch in Replays), `preset`: `desert`, `snow`, `volcanic`,
  `overcast`, `cave` (unterirdisch: ewige Nacht, Licht nur von Quellen). Nachts leuchten emissive Materialien heller, und
  Lichter spiegeln sich im Wasser.
- **Lichter auf der Karte** — das Array `lights` in einer Karte (Laternen, Lagerfeuer, Lava).
- **Kontaktschatten und AO** — weiche Flecken unter Einheiten und um Gebäude, auf allen Stufen (`contactShadow`,
  `contactShape` am Typ).
- **Editor** — das Fenster „Lighting“: Vorschau von Tageszeit und Gewitter, Lichtqualitätsstufe, Liste der Lichter. Für
  Agenten: `light_add`, `light_update`, `light_delete`, `light_list`, `lighting_set`, `view_lighting`.

Das Feldformat steht im Kopf von `modules/rts/view/RtsLights.h`. Testvariablen: `AIVERSE_TIME_OF_DAY`, `AIVERSE_NIGHT`,
`AIVERSE_CLOUDS`, `AIVERSE_STORM`.

## Mechaniken über MCP prüfen

Ein Agent, der ein Spiel ohne Code baut, prüft Mechaniken gezielt, nicht nur mit einem Bot-Match.
- **`scenario_run`** — ein Szenario auf einer Kopie der geöffneten Karte (nichts wird gespeichert): `clear` (eine leere Karte
  ohne Objekte, Wellen und Starteinheiten), `spawn` mit Namen (`"id": "foot", "count": 3` → `foot#1..3`, `"foot"` — alle),
  Befehle nach Sekunden (dieselben Schlüssel wie `play_command`), `bots` für einige Slots, `checks` mit Ergebnis
  bestanden/nicht bestanden (`alive`, `dead`, `count`, `hp`, `resource`, `event`, `trigger`, `variable`, `distance`, `status`,
  `result` + Vergleich), `watch` + `every` (Objektzustand über die Zeit), `events` (Ereignisprotokoll).
- **`play_units` / `play_command`** — Befehle in einem Live-Test im Game-Fenster (früher nur im Editor, jetzt für MCP-Clients
  sichtbar). Objektverweise: IDs aus `play_units`, `id`-Namen aus der Karte oder ein Filter
  `{type, owner, tags, kind, area, near + radius}`; `queue: true` — Warteschlange (Shift), `ability` nach ID.
- **`match_run`** — zusätzlich ein Protokoll ausgelöster Trigger und `events` (Ereignisprotokoll nach Namen).
- **`rules_lint`** — Schlüssel in `rules.json` und der Karte, die der Loader nicht liest (Tippfehler, Block an falscher Stelle),
  mit Hinweis („did you mean 'armor'?“). Es läuft auf dem echten Loader (`JsonAccessLog` in `engine/data/Json.h`);
  `rules_set`, `rules_merge` und `map_validate` melden solche Schlüssel sofort.
- **`rules_schema`** — `part: "keys"`: jeder Schlüssel, den der Loader in jedem Block abfragt (`types.*.weapons[]`), über alle
  Spiele gesammelt, und Schlüssel, die in `docs/game-format.md` fehlen; `part: "doc"` — das Dokument selbst.
- Das Simulationsereignis `kEvTriggerFired` (nur für Werkzeuge und Darstellung, nicht Teil der Prüfsumme).
- Branch `agent-mech-tools`.

## Post-Effekte

Vollbild-Effekte über der 3D-Szene werden mit Daten festgelegt: der Block `"postfx"` in `game.json` (eine Karte kann ihn
überschreiben). Es gibt Bloom, Farbkorrektur, LUT, Vignette, Tilt-Shift, Sonnenstrahlen, FXAA, Körnung, Aberration, Schärfen,
Verpixelung, Posterisierung, Tönung und Letterbox, dazu fertige Sets (Kino, satt, Noir, Retro, Miniatur, Sonnenuntergang,
Winter, Nachtsicht). Jeder Effekt hat `minQuality`. Die Stufe wird nach GPU gewählt: schwache Android-Geräte bekommen
„niedrig“, Telefone „mittel“, PCs „hoch“; `AIVERSE_POSTFX` überschreibt sie. Ohne Effekte kostet die Nachbearbeitung nichts.
Leichte Effekte werden in einem Pass zusammengefasst; schwere (Bloom, FXAA, Strahlen) fügen eigene hinzu.

Editor: das Fenster **Post-Effekte** (Vorschau der Stufen, eine Tabelle der Bildkosten pro Geräteklasse).
MCP: `postfx_catalog/get/add/set/remove/preset/enable/estimate/lut_neutral`, `view_postfx`. Antworten auf Änderungen
enthalten eine Schätzung in ms/Bild und Warnungen vor schweren Effekten. Details: `docs/postfx.md`.

## Geprüft / nicht geprüft

- ✅ Linux: Client, Editor, Tests; Windows .exe (MinGW-Cross-Build, unter Wine ausgeführt).
- ✅ macOS: Build, Tests, Spiel auf Metal; aiversebuild → .app und ein iOS-Projekt (+ Simulator-Build).
- ✅ Web: baut (Emscripten 3.1.50), ein Match läuft in Headless-Chromium (WebGL2/SwiftShader).
- ⏳ Android: Das Gradle-Projekt wird erzeugt; APK und Emulatorlauf sind in der CI (Job `android`), lokal nicht gebaut.
- ⏳ Windows auf echter Hardware und iOS auf einem Gerät — nicht ausgeführt (in der CI: MSVC-Build und iOS-Simulator-Build).

## Plattformen und CI

`.github/workflows/ci.yml` bei jedem Push auf `master`/`agent-*` und bei Pull Requests:

| Job | Was er tut |
|---|---|
| `linux` | gcc und clang (x64) + gcc (ARM64), Warnungen als Fehler, Tests, Bot-Matches, Screenshots unter Xvfb |
| `windows-msvc` | Visual Studio 2022, Tests, Matches, `aiversebuild --target windows` |
| `windows-mingw` | MinGW-w64-Cross-Build, Tests und Matches unter Wine |
| `macos` | universeller Xcode-Build, Tests, Matches, `aiversebuild` → .app und ein iOS-Projekt mit Simulator-Build |
| `android` | `.so` über das NDK, APK über `aiversebuild`; im Emulator: Start → Tippen → Hintergrund → Rückkehr → „Zurück“ → Neustart, Logcat auf Abstürze geprüft |
| `web` | Emscripten, ein Match in Headless-Chromium, Prüfung von Konsole und Bild |
| `determinism` | vergleicht Match-Prüfsummen (beide Spiele × 3 Seeds) aller Plattformen — sie müssen bitgenau übereinstimmen |

Screenshots, APK, Web-Build und .app werden als Artefakte des Laufs aufbewahrt.

Lokal:

```bash
# Windows .exe from Linux/macOS
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64.cmake
# bot matches on every game → a checksum file (compare between machines with diff)
.github/scripts/headless-checksums.sh build/linux-release/aiverse_headless sums.txt
# smoke test of the web build
node platform/web/smoke.mjs build/web
```

Kommandozeilenparameter im Web-Build werden über die URL übergeben: `index.html?map=0&spectate` = `--map 0 --spectate`.

Bildschirmtastatur: `platform::startTextInput(area)` / `stopTextInput()` öffnen und schließen sie unter iOS, Android und in
mobilen Browsern (auf dem Desktop tun sie nichts). Text kommt als `Char`-Ereignisse in jeder Sprache, Backspace/Enter als
`KeyDown`/`KeyUp`; `softKeyboardHeight()` und das Ereignis `KeyboardChanged` melden, wie viel vom Bildschirm unten verdeckt
ist, `TextInputEnded`, dass der Nutzer die Tastatur selbst geschlossen hat. Die UI (`UiLayer`) ruft das bei Fokus in
`<input type="text|password|search|email|number">` und `<textarea>` selbst auf.

Weitere Funktionen für Mobilgeräte und Browser in `Platform.h`:
- `safeAreaInsets()` — Abstände für Kameraaussparung, abgerundete Ecken, Home-Indikator und Systemleisten (iOS, Android,
  mobiles Safari/Chrome; 0 auf dem Desktop, auf einem MacBook mit Notch — im Vollbild).
- `setFullscreen()` / `isFullscreen()`; eingebaute Umschalter — F11 (Windows, Linux, Web), Alt+Enter (Windows). Im Browser
  fixiert der Vollbildmodus auch die Ausrichtung (`AppConfig::orientation`).
- Android: Die Zurück-Taste kommt im Spiel als `Key::Escape` an; Vollbild mit automatisch ausblendenden Leisten; physische
  Tastatur und Maus (Chromebook, DeX) liefern gewöhnliche Tasten- und Mausereignisse.
- iPad: Magic Keyboard / Bluetooth-Tastatur, Maus und Trackpad (Rechtsklick, Hover, Scrollen).
- Die Zwischenablage funktioniert überall; im Browser erreicht Strg/Cmd+V das Spiel, nachdem der Browser den Text übergeben hat.
- Web: `userDataDir()` liegt in IndexedDB — Einstellungen und Spielstände überstehen ein Neuladen der Seite.

### Native Plugins: Zahlungen und Werbung

Jedes Plattform-SDK wird als Plugin in seiner nativen Sprache angebunden — Java (Android), Objective-C (iOS/macOS),
JavaScript (Web) oder C++ — über eine asynchrone Brücke, `engine/platform/Plugins.h`. Ein Spiel nutzt die gemeinsamen APIs
`aiverse::store` (Käufe) und `aiverse::ads` (Werbung) aus `engine/services`, und der Anbieter wird pro Plattform in `game.json`
gewählt. Fertige Plugins in `plugins/`: **App Store**, **Google Play**, **RuStore**, **Yandex Games** (Web: Käufe und Werbung),
**Steam** (Käufe über den Inventory Service und DLC, Windows/Linux/macOS), **Yandex Mobile Ads**, **AdMob**, **AppLovin MAX**,
**ironSource (Unity LevelPlay)**. Ein Build enthält nur die Plugins, die das Spiel wählt (`"plugins"` in game.json oder
`--plugins`): keine fremden SDKs, Abhängigkeiten, Manifest-Einträge oder Info.plist-Schlüssel — die CI prüft das. Zum Debuggen
auf dem Desktop gibt es Shop- und Werbe-Attrappen (`mock-store`, `mock-ads`; `AIVERSE_MOCK_STORE`, `AIVERSE_MOCK_ADS`): Der
ganze Kaufablauf läuft ohne Telefon, und die Attrappen werden nicht in Release-Builds kompiliert. aiversebuild trägt die
Plugins selbst in Gradle/Manifest, Info.plist/Podfile und HTML ein; bauen Sie Google Play und RuStore getrennt:
`--plugins googleplay,admob` und `--plugins rustore,yandex-ads`. Details: [plugins.md](../plugins.md).

### Releases

`.github/workflows/release.yml`: Ein Tag `v1.2.3` (oder ein manueller Lauf mit einem Spielordner Ihrer Wahl) baut alle
Plattformen mit `aiversebuild` und veröffentlicht ein GitHub Release:

| Plattform | Ergebnis | Signatur (Repository-Secrets) |
|---|---|---|
| Windows | Installer (Inno Setup, ohne Administratorrechte) + portables .zip | `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` |
| Android | .apk und .aab für Google Play | `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` |
| macOS | .app in einem .zip, notarisiert | `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD` (Developer ID) + ein App-Store-Connect-Schlüssel |
| iOS | Xcode-Projekt; mit Schlüssel — ein TestFlight-Build | `APPLE_TEAM_ID`, `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_P8_BASE64` |
| Linux | .tar.gz | — |
| Web | .zip; mit `DEPLOY_WEB_PAGES=true` (Repository-Variable) — Veröffentlichung auf GitHub Pages | — |

Ohne Secrets erscheint das Release trotzdem: Die APK ist mit einem Debug-Schlüssel signiert, die .app ad-hoc, Windows unsigniert.

### Fertige Engine (Branch `builds`) und aiversemcp ohne Compiler

Der fertige Editor und die Werkzeuge werden im Branch **`builds`** veröffentlicht:
`<platform>/AIVERSE-<version>-<commit>-<platform>.zip` (derzeit `windows` x86_64 und `macos` arm64; `linux` x86_64, wenn der
Integrator ihn veröffentlicht). Darin liegt ein Ordner `AIVERSE/` mit den Engine-Quellen dieses Commits (Vorlagen
`templates/`, `engine/assets`, `docs/` — der MCP-Server liest sie) und `AIVERSE/bin/` mit den Programmen. Gepackt von
`tools/release/package_engine.py`, veröffentlicht von `tools/release/publish_builds.py` (AGENTS.md §8); das Linux-Paket kann
ohne Editor kommen — nur `aiversemcp` und `aiverse_headless`.

Der MCP-Server aus dem entpackten Ordner (Bot-Matches und `balance_*` laufen in aiversemcp, ein separates `aiverse_headless`
ist nicht nötig; der Editor ist optional, ohne ihn `--no-editor`):

```bash
# ARCHIVE is the path to the AIVERSE archive downloaded from index.json.
unzip "$ARCHIVE" -d ~/aiverse      # unzip keeps the executable bit
~/aiverse/AIVERSE/bin/aiversemcp --no-editor --engine ~/aiverse/AIVERSE
```

`--engine` kann bei Builds aus Commits entfallen, in denen aiversemcp die Engine neben sich sucht
(`engine/platform/EngineDir.h`, wie der Editor); in früher veröffentlichten Builds ist es Pflicht. Bei Entpackern, die keine
Rechte wiederherstellen (Python `zipfile`), führen Sie `chmod +x` auf die Dateien in `bin/` aus. Unter macOS kann eine vom
Browser geladene Datei in Quarantäne landen: `xattr -dr com.apple.quarantine ~/aiverse/AIVERSE`.

Programme, die die Engine selbst herunterladen, lesen **`index.json`** im Wurzelverzeichnis des Branches
(`https://raw.githubusercontent.com/chromealex/AIVERSE/builds/index.json`), nicht das README. Unten ein Beispiel eines
historischen RTSEngine-Eintrags; in neuen Builds enthalten die Pfade AIVERSE, der Schlüssel `rtsmcp` bleibt erhalten:

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

- `packages` sind von neu nach alt sortiert; der erste Eintrag mit passendem `platform` + `arch` ist der neueste Build.
- `platform`: `windows` | `macos` | `linux`; `arch`: `x86_64` | `arm64` | `universal` (macOS fat) | `null` (nicht erkannt) —
  ermittelt aus dem Header von `aiversemcp` im zip.
- `rtsmcp`, `editor`, `headless` sind Pfade im Archiv oder `null`, wenn das Programm fehlt; `--engine` ist der Ordner `root`.
  Unter macOS ist `editor` ein `.app`-Bundle.
- `url` zeigt auf einen Branch, der bei jeder Veröffentlichung überschrieben wird (5 Versionen pro Plattform bleiben): Alte
  Einträge verschwinden, laden Sie daher nach einer frischen `index.json` und prüfen Sie `sha256`.
- Neue Felder können hinzukommen; eine inkompatible Formatänderung erhöht `schema`.

Prüfung: `python3 tools/release/test_release.py [build/linux-release/aiversemcp]` (CTest `release_tools`) — das Format von
`index.json` und mit einem Pfad zu aiversemcp: Packen eines Linux-Pakets, Entpacken in einen temporären Ordner und MCP-Aufrufe
von dort ohne `--engine` (`tools/list`, `create_project`, `map_generate`, `map_validate`, `map_ascii`).

Der Screenshot-Modus (`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`) schreitet genau 1/60 s pro Bild voran, daher ist
Bild N auf jeder Maschine gleich.

## Als Nächstes

glTF-Modelle und Animation, Audio, Netzwerk (Lockstep), Fähigkeiten und Buffs aus Daten, Flow Fields und RVO für große Armeen,
Verbesserungen der KI, C#-Scripting (AOT für iOS).

## C++-Spielmodule

Neben Lua kann ein Projekt `native/CMakeLists.txt` und C++20-Module enthalten. Sie werden beim Build mit
`aiverse_native_module(...)` eingebunden und über das Array `nativeModules` in `rules.json` aktiviert. Ticks, Befehle, Rollback
und Zustandsspeicherung werden unterstützt; Lua und C++ arbeiten zusammen. Nach einer C++-Änderung müssen Spiel und Editor neu
gebaut werden. [API, Beispiel und Build-Anleitung](../native-modules.md).

## Wiederverwendbare C++-Meta-Bibliothek

`aiverse::meta` — konfigurierbarer Shop/Angebote/Crafting, Währungen und Gegenstände, Aufgaben und Pässe, tägliche
Belohnungen, Energie, Post, Ranglisten, Chats und Segmente. Für den nativen Code des Projekts ohne Lua/GPU verfügbar;
Definitionen sind JSON, die Algorithmen müssen nicht erneut erzeugt werden. Start:
`python3 tools/meta/scaffold.py --preset mobile --out <project>`. Dokumentation: [meta-game.md](../meta-game.md); für Agenten —
`skills/meta-game/SKILL.md`. Lokale Richtlinien funktionieren ohne Server; Kommunikation zwischen Geräten und globale
Ranglisten brauchen ein echtes Backend. Ein fertiger Cloud-Dienst ist nicht Teil des Pakets.

Das Preset `full --with-ui` ergänzt Freunde/Clans, eine Instanzsammlung, Ausrüstung, Verschmelzen, Abonnements und
Turnierligen. `aiverse::meta::Service` führt Operationen atomar aus; `tools/meta/server.py` ist ein lokaler
HTTP/SQLite-Host mit Sitzungsprüfung. Enthalten sind Browser-Meta-Bildschirme und ein Katalog kurzer Rezepte für Agenten.
[Build, Start und Grenzen](../meta-service.md).

`full` enthält außerdem 15 LiveOps-Gruppen: Truhen mit Pity, Alben/Tausch, Auftragswarteschlangen, Forschung/Prestige,
zusammengesetzte Aufgaben, Events, Clankriege, asynchrones PvP, Gruppen-Matchmaking, Elo/Glicko-2, Empfehlungen/Promo-Codes,
Rewarded Ads, Angebotsketten, Benachrichtigungen und Offline-Einkommen. Alle Parameter stehen in `extensions.liveops`; dieses
Preset installiert keine neuen separaten UI-Bildschirme oder externe SDKs. `python3 tools/meta/catalog.py describe matchmaking`
gibt nur das benötigte Schema und Beispiel aus. [API, Richtlinien und Grenzen](../meta-liveops.md).

### Optionale 3D-Physik

Box3D wird mit dem Flag `AIVERSE_ENABLE_PHYSICS=ON` aktiviert; gewöhnliche RTS-Projekte brauchen es nicht. Ein separates
Beispiel, `aiverse_physics_demo`, zeigt Körper, Kollisionen, ein Pendel und Zustandsspeicherung.
[Build, API und Integrationsgrenzen](../physics.md).

### Visuelle XPBD-Stoffsimulation

Das optionale Modul `AIVERSE_ENABLE_CLOTH=ON`: ein eigener XPBD-Stofflöser auf der CPU oder per Metal Compute, gemeinsame
Batches, zeitliches LOD, automatisches Schlafen und Zeichnen dichter Netze mit einem Instanced Draw. Auf der GPU bleiben die
Positionen im Videospeicher; nicht unterstützte Backends nutzen die CPU. Das Beispiel kann die GPU mit einer CPU-Referenz
vergleichen und Schlafen/Aufwachen prüfen. [Eigenständiges Beispiel, Grenzen und Messungen](../cloth.md).
