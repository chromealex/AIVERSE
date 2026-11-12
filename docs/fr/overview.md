# AIVERSE — vue d’ensemble détaillée

Une référence pour chaque partie du moteur : structure, compilation, formats, sous-systèmes, plateformes et état des
vérifications. L’introduction courte — à quoi sert le moteur et quels problèmes il résout — est dans le [README](../../README.fr.md).
Les règles pour les agents qui modifient le moteur sont dans [AGENTS.md](../../AGENTS.md).

## Structure

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

## Démarrage rapide (macOS)

```bash
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release      # requires Xcode and CMake
cmake --build build/mac -j 8
open build/mac/AiverseEditor.app                              # editor (project templates/skirmish)
open build/mac/AiverseGame.app                                # game
./build/mac/aiverse_tests
```

Un autre jeu dans l’éditeur : `AiverseEditor.app/Contents/MacOS/AiverseEditor --project templates/mushrooms`.
Pour que le jeu démarre avec un autre jeu de données, passez `-DAIVERSE_CONTENT_DIR=.../templates/mushrooms`.

### Plusieurs éditeurs à la fois

Chaque processus d’éditeur peut ouvrir son propre projet avec `--project <dossier>`. Sans `--listen`, la première instance
prend le port MCP `47321` ; s’il est occupé, le système en attribue un libre. Le port réel est affiché dans la barre d’état ;
la fenêtre **Agent** l’ajoute à la configuration de connexion (`aiversemcp --port <port>`). Un port attribué automatiquement
peut changer après un redémarrage de l’éditeur. `--listen <port>` fixe un port précis (en cas de conflit, MCP ne démarre pas),
`--listen 0` désactive MCP. Les clients MCP existants ne passent pas d’une fenêtre à l’autre d’eux-mêmes.

## Compiler pour les plateformes — aiversebuild

```bash
./build/mac/aiversebuild --project templates/skirmish --target macos     # Builds/macos/<Name>.app (ad-hoc signed)
./build/mac/aiversebuild --project templates/skirmish --target ios       # Builds/ios/xcode/AIVERSE.xcodeproj + simulator build
./build/mac/aiversebuild --project templates/skirmish --target android   # Builds/android/<Name>-release.apk when SDK/NDK and gradle are present
./build/mac/aiversebuild --project templates/skirmish --target web       # Builds/web (requires emsdk)
./build/mac/aiversebuild --project templates/skirmish --target windows   # .exe via mingw-w64 (brew install mingw-w64) or sources + build.bat
./build/mac/aiversebuild --project templates/skirmish --target all
```

Le résultat va dans `<projet>/Builds/<plateforme>/` — uniquement les builds terminés et le rapport de taille. Tout
l’intermédiaire (arbres CMake, paquets, audio réencodé, projet Gradle, données xcodebuild, estimation de taille de l’éditeur)
vit dans le dossier caché `Builds/.temp/`, supprimable à tout moment. Avec `--generate-only`, le projet Gradle pour Android
Studio va dans `Builds/android/<Name>`. Le packager supprime lui-même les anciens dossiers visibles `_build`, `_pack`,
`_audio`, `_estimate`.

La même chose existe dans l’éditeur : le menu **Build**. Les projets iOS et Android générés sont autonomes (les sources du
moteur et le contenu y sont copiés) et peuvent être transmis. Pour iOS sur un appareil : ouvrez le projet dans Xcode et
choisissez une équipe sous Signing (ou `--team <TeamID>`, ou `"appleTeamId"` dans game.json).

Identifiant, nom, version, orientation et icône (`icon.png`, 1024×1024 ; générée si absente) sont dans `game.json`.

### Outils de compilation : vérification et installation automatique

Avant de compiler, `aiversebuild` vérifie ce dont la plateforme a besoin et le cherche au-delà du `PATH` (un éditeur lancé
depuis le Finder ne voit que `/usr/bin:/bin:/usr/sbin:/sbin`) : Homebrew (`/opt/homebrew/bin`, `/usr/local/bin`),
`~/.local/bin`, `~/Library/Python/*/bin`, `~/emsdk`, `$EMSDK`, `ANDROID_HOME` / `ANDROID_SDK_ROOT`, `~/Library/Android/sdk`,
`~/Android/Sdk`, `%LOCALAPPDATA%\Android\Sdk`, `JAVA_HOME`, `/usr/libexec/java_home`, Visual Studio (vswhere) et son propre
dossier `~/.aiverse/toolchains`. Les dossiers trouvés sont ajoutés au `PATH` des processus enfants.

| Plateforme | Nécessite | Installé automatiquement |
|---|---|---|
| web | CMake, Python 3, Emscripten (emsdk), Ninja — facultatif | emsdk (≈ 400 Mo à télécharger, ≈ 1,5 Go sur le disque), CMake + Ninja (≈ 50 Mo) |
| android | JDK 17–23, Android SDK (platform 35, build-tools, platform-tools), NDK, CMake du SDK, Gradle | tout : Temurin JDK 17, command line tools + `sdkmanager` (licences acceptées), NDK ≈ 700 Mo, Gradle 8.10.2 |
| macos | Xcode Command Line Tools, CMake | CLT — via l’installateur d’Apple (`xcode-select --install`), CMake |
| ios | Xcode (App Store), CMake | CMake |
| windows (sous Windows) | Visual Studio 2022 / Build Tools (C++), CMake | CMake ; VS — une indication avec une commande `winget` |
| linux | compilateur C++, CMake | CMake |

```bash
aiversebuild --check-toolchain web            # what is present and what is missing (--json for the editor and MCP)
aiversebuild --install-toolchain web          # install what is missing (or one id: emsdk, cmake, jdk, android-ndk, gradle, ...)
aiversebuild --project ... --target web --install-missing   # install and build right away
```

Sans `--install-missing`, des outils manquants arrêtent la compilation avec le code de sortie 3 et la ligne
`AIVERSE_MISSING_TOOLCHAIN web: emsdk` (l’éditeur la reconnaît). Dans l’éditeur, la fenêtre **Build** montre pour la plateforme
choisie ce qui a été trouvé (chemins, versions) et ce qui manque, avec un bouton **Installer automatiquement** et une case
« Installer automatiquement ce qui manque » ; la progression s’affiche dans la **Console** (le texte peut être sélectionné et
copié ; « Ouvrir le dossier des journaux » ouvre `<moteur>/logs`). Les agents font de même via MCP : `toolchain_check`,
`toolchain_install`, `build_start {"installMissing": true}`.

Tout est installé dans `~/.aiverse/toolchains` (Windows : `%USERPROFILE%\.aiverse\toolchains` ; un autre dossier — la variable
`AIVERSE_TOOLCHAINS_DIR`) sans `sudo` ni modification du système : `emsdk/`, `py/` (un venv avec cmake et ninja), `cmake/`,
`ninja/`, `jdk-17/`, `android-sdk/`, `gradle-8.10.2/`. Relancer est sans risque (ce qui est prêt est sauté). Pour tout supprimer,
effacez ce dossier. Exceptions : les Command Line Tools sont installés par l’installateur d’Apple, et si l’Android SDK de
l’utilisateur est trouvé, `sdkmanager` y installe les paquets manquants.

### Paquets de ressources (.rpak) et taille des builds

Le contenu du jeu (le dossier du jeu + `engine/assets`) entre dans un build sous la forme d’un seul fichier
`content/content.rpak` : chaque fichier est compressé séparément en zstd, les fichiers identiques sont stockés une fois, les
formats déjà compressés (png, ogg, mp3, …) tels quels. Le moteur monte `content.rpak` automatiquement ; les fichiers du paquet
priment sur ceux du dossier, donc sans paquet (en développement) tout fonctionne comme avant. `platform::mountPack("patch.rpak")`
monte un paquet par-dessus (patch, DLC). Chaque build affiche un rapport de taille et l’écrit dans
`dist/<plateforme>/size-report.txt`.

```bash
aiversebuild ... --no-pack          # ship folders as is (debugging)
aiversebuild ... --pack-level 9     # zstd level (default 19 for Release, 3 for Debug)

assetpipe pack -o content.rpak templates/skirmish engine/assets=engine   # build a pack by hand
assetpipe list content.rpak       # files, sizes, compression
assetpipe verify content.rpak     # unpack everything and check hashes
assetpipe extract content.rpak out/
assetpipe diff old.rpak new.rpak  # what changed between versions (for patches)
```

Ce dont le build n’a pas besoin est listé dans `.rpakignore` dans le dossier du jeu (masques `*`, `?` ; `dossier/` — dossiers
seulement). Les fichiers cachés (`.git`, `.DS_Store`), les replays `*.rpl` et les `*.rpak` imbriqués sont toujours ignorés. La
construction des paquets est reproductible : un contenu identique donne un fichier identique octet pour octet ; le « hachage
de contenu » du paquet identifie le jeu de données.

### Builds fractionnés et téléchargement de contenu

En option, un build peut être fractionné : l’application n’embarque qu’un paquet de base avec ce qu’il faut pour démarrer, le
reste va dans des parties (`content.001.rpkc`, …) d’une taille donnée que le jeu télécharge depuis n’importe quelle source.
L’auteur du jeu décide de tout, dans `game.json` (réglages communs et par plateforme) ou avec les options d’`aiversebuild` :

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

Les parties sont placées à côté du build (`dist/<plateforme>/chunks/`) et publiées à l’adresse indiquée dans `sources`
(`{hash}` est le hachage de contenu du build, les versions ne se mélangent pas). En jeu, un fichier d’une partie pas encore
téléchargée est demandé automatiquement (sur ordinateur et mobile, `readContentFile` l’attend, sur le web il renvoie false) ;
la progression est `platform::contentProgress()`, une demande explicite `platform::requestContent(paths)`. Chaque partie est
vérifiée par SHA-256 (une partie altérée venant d’un miroir est refusée et prise sur le suivant), conservée en cache et non
retéléchargée au lancement suivant. Protocoles : https/http (libcurl sous Linux, WinHTTP, NSURLSession, HttpURLConnection sous
Android, fetch sur le web), `file://` et dossiers ; un jeu peut enregistrer son propre schéma (Steam, Play Asset Delivery,
P2P). `assetpipe pack --chunk-size 8MB ...` fait la même chose à la main, et `assetpipe verify` vérifie aussi les parties.

### Importer des archives étrangères (décompresseurs)

Les formats inconnus du moteur (zip, tar, dmg, archives d’autres jeux) s’ajoutent sous forme de plugins de décompression sans
recompiler le moteur : un dossier avec `unpacker.json` et un programme dans n’importe quel langage (protocole :
[`tools/unpackers/PROTOCOL.md`](../../tools/unpackers/PROTOCOL.md)). Les décompresseurs ne tournent que dans les outils de
compilation : une archive étrangère devient des fichiers dans un `.rpak`, et le jeu ne lit toujours que des `.rpak`.

Fournis : zip (et `.oramap`), tar, dmg, archives de Warcraft I/II (`.WAR/.SUD/.INS`), `.PAK` de Westwood (Dune II, Lands of
Lore, Kyrandia), Z des Bitmap Brothers (`.PAC`, `.RSC`).

```bash
assetpipe formats                                   # which formats are known
assetpipe probe ~/Downloads/Warcraft2/war2/DATA/*   # what these files are
assetpipe list MAINDAT.WAR                          # what is inside
assetpipe import -o war2.rpak MAINDAT.WAR=imported/war2 SFXDAT.SUD=imported/war2/sfx
assetpipe unpackers new tools/unpackers myformat    # scaffold for a new unpacker
assetpipe unpackers check                           # check all plugins
```

Chaque commande a `--json` — c’est ainsi que le serveur MCP les appelle. `game.json` peut lister des `"imports"`, et
`aiversebuild` les décompresse dans le paquet pendant la compilation (`"optional": true` — si le fichier manque sur cette
machine, la compilation continue sans lui). Un paquet contenant du contenu tiers importé est marqué « external » : ces builds
sont réservés à un usage personnel et ne doivent être ni commités ni distribués.

### Importer des jeux (importateurs)

Un importateur transforme votre copie d’un jeu classique en dossier de jeu pour le moteur. Dune II est disponible :

```bash
python3 tools/importers/dune2/import.py --source "~/Downloads/Dune II" --out ~/AiverseGames/dune2
aiversebuild --project ~/AiverseGames/dune2 --target macos
```

Le résultat est un jeu 2D au format du moteur (`docs/game-format.md`, « 2D games ») : caméra vue de dessus, 66 missions de
campagne avec une couche de tuiles (`tilesets/dune.json` depuis ICON.ICN : sable, rocher, dunes, montagnes, épice avec
autotiling), sprites d’unités depuis SHP (5 directions avec symétrie, tourelles de chars posées sur la caisse) et bâtiments
depuis les tuiles ICN (construction, ruines, animation), couleur de maison à partir des teintes magenta. Plus `rules.json`
(notre équilibrage de Dune II sur les blocs du moteur), images CPS et animations WSA en PNG, sons VOC → WAV + `audio.json`,
noms en anglais et en russe. Le terrain est approximatif : le générateur de paysage d’origine n’est pas utilisé, les bases
sont sur le rocher, l’épice vient de `Field=`, les quotas de mission sont des déclencheurs. Infanterie, ornithoptère et
transporteur sont encore dessinés en formes 3D. Le dossier est marqué `"external": true` — il ne doit être ni commité ni
distribué.

## Fichier de projet .aiverse

Un projet est un dossier n’importe où sur le disque (`game.json`, `rules.json`, `maps/`, ...). Sa racine contient le fichier
de projet **`<Nom>.aiverse`** — il s’ouvre d’un double clic, comme `.uproject` dans Unreal ou `project.godot`. `game.json`
reste le manifeste du jeu (lu par le client, aiversebuild, les paquets) ; `.aiverse` marque seulement le dossier comme projet
et stocke des informations de l’éditeur :

```json
{"format": 1, "name": "My Strategy", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
```

- Il est créé avec le projet (hub « Nouveau projet », MCP `create_project`) ; un ancien projet sans lui en reçoit un à la
  première ouverture dans l’éditeur. Les modèles de `templates/*/` en ont déjà un (il n’est pas copié dans un nouveau projet :
  celui-ci reçoit le sien).
- Partout où l’éditeur accepte un chemin de projet, `.aiverse` comme le dossier conviennent : `aiverse_editor "My
  Strategy.aiverse"`, `--project`, « Ouvrir… » dans le hub (montre les `.aiverse` et les dossiers de projets), projets récents,
  MCP `open_game`, `aiversebuild --project x.aiverse`. `.aiverse` n’entre pas dans un build.
- Les projets créés avant le renommage en AIVERSE gardent leur `<Nom>.rtsproj` : il s’ouvre partout de la même façon, est
  réécrit sur place et jamais renommé ; si le dossier contient les deux, `.aiverse` l’emporte.
- **Double clic.** Au démarrage, l’éditeur associe `.aiverse` à lui-même (par utilisateur, sans droits d’administrateur) s’il
  n’y a pas d’association ou si elle pointe vers une autre instance de l’éditeur ; il ne remplace pas un autre programme que
  vous avez choisi. À la main : « Projet → Faire de l’éditeur l’application par défaut pour .aiverse » ; l’état s’affiche en
  bas à gauche du hub. Désactiver : `AIVERSE_FILE_ASSOC=off`.
  - **macOS :** le type `com.aiverse.project` est déclaré dans l’Info.plist d’`AiverseEditor.app`
    (`platform/macos/EditorInfo.plist.in` ; l’icône de document `rtsproj.icns` est dessinée par `aiverse_projicon` à partir
    du logo de l’éditeur `apps/editor/branding/icon_1024.png`) ; l’éditeur enregistre le bundle dans LaunchServices
    (`lsregister -f`) et devient le gestionnaire par défaut. Un double clic et `open -a AiverseEditor x.aiverse` ouvrent le
    projet dans l’éditeur en cours (si la carte n’est pas enregistrée, il demande s’il faut l’enregistrer).
  - **Windows :** `HKCU\Software\Classes\.aiverse` (+ `.rtsproj`) → `AIVERSE.Project` (icône `%USERPROFILE%\.aiverse\icons\rtsproj.ico`,
    commande `"<aiverse_editor.exe>" "%1"`). Si Windows a déjà un autre programme choisi pour `.aiverse`, le hub suggère
    « Ouvrir avec → Toujours ».
  - **Linux :** le type `application/x-aiverse-project` (`~/.local/share/mime/packages/aiverse-project.xml`),
    `~/.local/share/applications/aiverse-editor.desktop` (`Exec=<éditeur> %f`), icônes dans `~/.local/share/icons/hicolor`,
    application par défaut dans `~/.config/mimeapps.list` ; `update-mime-database`, `update-desktop-database`, `xdg-mime`
    sont appelés s’ils sont installés.

## Créer un jeu (assistant)

Le bouton **Créer un jeu** dans la fenêtre des projets (et **Projet → Créer un jeu…**) : donnez un dossier ou un lien avec des
illustrations, de la musique et des textes, décrivez l’idée (ou rien — l’assistant choisit lui-même) et obtenez un projet
`.aiverse` terminé : un jeu sur un modèle adapté, un tutoriel pas à pas (guidé) et des conseils en combat (discrets), des
niveaux avec étoiles et récompenses, la méta (menu, améliorations, boutique, récompense quotidienne, profil, réglages),
publicité et achats si souhaité, langues, icône et écran de démarrage tirés des illustrations. L’assistant joue lui-même le
tutoriel avec un bot et vérifie les niveaux ; ce qui a été attribué et ce qu’il faut vérifier figure dans le `WIZARD.md` du
projet. Pour les agents : MCP `game_make_options` / `game_make_plan` / `game_make`. Détails : [game-wizard.md](game-wizard.md).

## Comment un jeu est décrit

Un type d’objet est un ensemble de blocs facultatifs ; ce qui est défini, l’objet sait le faire :

| Bloc | Sens |
|---|---|
| `health` | santé, armure, type d’armure, régénération |
| `move` | vitesse, couche (`ground`/`air`/`naval`), rayon |
| `structure` | un bâtiment avec une taille `footprint` sur la grille |
| `weapons` | portée, dégâts, type de dégâts, temps de recharge, projectile, dégâts de zone, couches touchées |
| `cost` | ressources, temps, ravitaillement |
| `produces` / `builds` | ce qu’il forme / ce qu’il construit |
| `gather` / `resourceNode` / `dropoff` | collecte de ressources (Warcraft/StarCraft) |
| `income` / `storage` + ressource `"kind": "flow"` | économie de flux (Total Annihilation) |
| `supply` | fournit du ravitaillement |
| `requires` | arbre technologique |
| `garrison` + `entersGarrison` | bâtiments de garnison, envoi d’un pourcentage des troupes, capture (Mushroom Wars) |
| `view` | apparence : `shape`, `scale`, `color` |

Global : `resources`, `damageTable` (type de dégâts × type d’armure), `supplyCap`, `victory`.
Carte : taille, terrain (`rows` : `.` sol, `~` eau, `#` rocher, `,` construction interdite, `=` gué ; `heights`), joueurs et
équipes, ressources de départ, objets, routes, `roadsOnly`.

## Localisation

Un jeu se traduit sans modifier `rules.json` : les noms dans les données sont du texte dans la langue source du jeu, les
traductions sont dans `<projet>/lang/<langue>.json` sous des clés comme `type.footman`, `resource.gold`, `map.valley.title`…
Les chaînes du menu et du HUD sont dans `engine/assets/lang/` (actuellement anglais et russe) ; un jeu peut remplacer
n’importe laquelle.

```jsonc
// game.json
"sourceLanguage": "en",
"languages": ["en", "ru"]
```

```bash
./build/mac/aiverse_i18n extract --project templates/skirmish --lang de --write   # lang/de.json skeleton, new strings marked // TODO
./build/mac/aiverse_i18n check   --project templates/skirmish                     # placeholders, plural forms, untranslated strings
```

La langue est choisie ainsi : le choix du joueur dans le menu (mémorisé) → la langue du système → la langue source du jeu. Pour
tester : `--lang ru`, `AIVERSE_LANG=ru`, la pseudo-langue `--lang qps` (toutes les chaînes déformées et plus longues — le
non-traduit et les débordements sautent aux yeux). Les formes du pluriel suivent le CLDR (« 1 seconde / 2 secondes /
5 secondes »). Plus : `engine/i18n/README.md`.

## Interface du jeu (RmlUi, HTML/CSS)

Le menu, le HUD, la carte de commandes, la minicarte et la pause du client sont des documents RmlUi (balisage `.rml`, styles
`.rcss`), pas du code. Le moteur les dessine via son RHI (GL/Metal) ; les polices passent par stb_truetype (latin + cyrillique).

- Documents par défaut : `engine/assets/ui/` (`menu.rml`, `hud.rml`, `theme.rcss`).
- Un jeu remplace n’importe quel fichier en mettant `ui/<même nom>` dans son dossier : par exemple seulement `ui/theme.rcss` —
  ses propres couleurs avec le balisage par défaut. La police du jeu est `fonts/ui.ttf`.
- Les données viennent du modèle `game` (la liste des champs est dans un commentaire de `hud.rml`) ; les appuis sur les
  boutons deviennent des commandes de simulation (`apps/client/ClientUi.cpp`).
- Tailles en `dp` — l’interface s’adapte à la densité de l’écran ; sur les appareils tactiles la racine a la classe `touch`
  (boutons plus grands).
- F8 dans le client ouvre le débogueur RmlUi (arbre des éléments, styles, journal).
- L’habillage du jeu est `ui/skin.rcss` (chargé après `theme.rcss` dans chaque document : couleurs, bordures, boutons,
  disposition) et la police des titres `fonts/title.ttf` (famille `title`). Chaque prototype de `templates/` a son habillage.
  La disposition portrait (`@media (orientation: portrait)`) est dans le thème du moteur ; sur PC, vérifiez-la avec
  `--size 540x960`.
- Les textes des documents s’écrivent sous forme de clés `@menu.play` : la traduction vient de `engine/assets/lang` (et du
  `lang/` du jeu) au chargement du document ; changer de langue dans le menu recharge les documents.
- Clavier à l’écran (`engine/ui/UiKeyboard.*`) : construit sur RmlUi, il s’ouvre quand un champ de saisie prend le focus sur
  les appareils tactiles (sur PC — `AIVERSE_OSK=1`). Les dispositions (en, ru, symboles ; Maj, Verr. Maj par double appui,
  changement de langue) sont dans `keyboard.json`, l’apparence dans `keyboard.rml` / `keyboard.rcss` ; un jeu remplace
  n’importe lequel de ces fichiers dans son `ui/`. Si le curseur est en bas de l’écran, le clavier passe en haut.
- Multijoueur (`lobby.rml`, `apps/client/Lobby.cpp`) : adresse du serveur et nom, partie rapide via les files du serveur, liste
  des salons (rejoindre / regarder), son propre salon (emplacements, bots, prêt, départ, chat). La partie est menée par
  `NetClient` : `attach(sim)`, `advance(dt)` au lieu de `sim.advance(dt)`, les commandes viennent de l’emplacement
  `match().localSlot` (0 — spectateur) ; les bots que le serveur a confiés à ce client sont calculés ici aussi.

## Déterminisme

La simulation est identique au bit près sur toutes les plateformes : uniquement de la virgule fixe, son propre générateur
aléatoire, des commandes en ordre canonique, des sommes de contrôle dans les replays. Vérifié : Linux x86-64 (GCC, Clang,
MinGW), Linux ARM64 (GCC), macOS ARM64 (AppleClang) — sommes de contrôle des parties identiques.

## Graphismes et mobile

Une seule passe forward : soleil (Lambert + spéculaire), une carte d’ombres ajustée à la partie visible de la carte et
alignée sur les texels, PCF matériel (1/4/9 échantillons), ambiance hémisphérique, brouillard de distance, ACES. La qualité
des ombres est choisie selon le GPU (basse pour les vieux Mali/Adreno/PowerVR) et modifiable dans le menu pause.

## Multijoueur

`engine/net` est le multijoueur de tout jeu sur le moteur, sans code de jeu. Seules les commandes circulent sur le réseau :
la simulation est déterministe, donc tout le monde a le même état.

- **Transport :** UDP avec sa propre couche de fiabilité (iOS, Android, PC) et WebSocket (HTML5 ; en natif — solution de
  repli si l’UDP est bloqué). Adresse : `host[:27015]`, `udp://…`, `ws://host:27016`.
- **Lockstep avec rollback.** Le serveur confirme les entrées de chaque tick quand les commandes de tous les joueurs sont
  arrivées ou que l’échéance (`lagToleranceMs`) est passée ; les commandes en retard passent au tick suivant — un joueur qui
  rame n’arrête pas les autres. Le client n’attend pas la confirmation : il joue en avance avec ses propres commandes et, en
  cas d’écart, restaure un instantané et recalcule (un instantané prend ~10 µs). `rollbackWindow: 0` est un lockstep
  classique pour les appareils faibles.
- **L’`aiverse_server`** n’a pas besoin du contenu du jeu : salons (emplacements, bots, spectateurs, prêt, vérification de la
  version des données), matchmaking automatique via les files de `server.json` (classement Elo avec fenêtre qui s’élargit,
  carte commune, équilibrage des équipes, complément par des bots), comparaison des sommes de contrôle (désynchronisation),
  replays (`aiverse_headless --replay` les rejoue), reconnexion, spectateurs différés.
- **Partir = abandonner.** Un joueur parti (ou non revenu dans `reconnectGraceSec`) abandonne par une commande dans le flux
  d’entrées commun : l’issue est décidée par la simulation, identiquement pour tous, et visible dans le replay. Le classement
  est débité.
- **Verdict du serveur.** Si personne n’a confirmé l’issue (tout le monde est parti, les résultats divergent), le serveur
  rejoue la partie dans un thread d’arrière-plan sur ses propres données de jeu (`"games"` dans `server.json`) et désigne le
  vainqueur lui-même.
- **Reconnexion :** après une coupure réseau, le client se reconnecte seul (`NetClient(address, …)` ou `setReconnect`), la
  simulation continue — le serveur n’envoie que les entrées postérieures au dernier tick confirmé. Si le jeu a été redémarré,
  le client reçoit tout l’historique et recalcule la partie depuis le début.
- **Dans le jeu :** `NetClient` (`engine/net/NetClient.h`) gère le salon et la partie. Le jeu construit une `Simulation` à
  partir de `match()` et appelle `attach(sim)`, puis `advance(dt)` au lieu de `sim.advance(dt)` ; l’interface soumet toujours
  les commandes via `sim.submit()`.
- **Textes :** le serveur n’envoie pas de phrases toutes faites — seulement des clés de localisation avec arguments
  (`NetText`) : les joueurs d’une même partie peuvent utiliser des langues différentes. Le client les traduit avec
  `net::translate(loc, event.text)` et `net::displayName(...)` de `engine/net/NetLocalize.h` ; les chaînes sont dans
  `engine/assets/lang/*.json` sous `net.*`. Les noms des files dans `server.json` sont aussi des clés (`"@net.queue.1v1"`).

```bash
./build/mac/aiverse_server --config apps/server/server.json            # UDP 27015, WebSocket 27016
./build/mac/aiverse_netbot --server 127.0.0.1 --project templates/mushrooms --queue 1v1   # a bot in the queue
./build/mac/aiverse_netbot --server ws://127.0.0.1:27016 --project templates/mushrooms --queue 1v1
./build/mac/aiverse_net_tests                                          # network tests
docker build -f apps/server/Dockerfile -t aiverse-server .             # server in a container
```

Pour `wss://` (une page de jeu servie en HTTPS), placez un proxy TLS (nginx, Caddy) devant le serveur.

## Audio

`engine/audio` est un mixeur, des banques de sons issues des données, une synthèse procédurale et la musique ; la sortie
passe par [miniaudio](../../third_party/miniaudio) (CoreAudio, AAudio/OpenSL ES, Web Audio, WASAPI, ALSA/PulseAudio).
`modules/rts/audio` transforme les événements de simulation en sons ; il n’écrit rien dans la simulation. Pas de
périphérique — le jeu tourne en silence. Pas d’`audio.json` — le jeu n’a simplement pas de son.

Le son d’un jeu est le fichier `audio.json` à côté de `game.json` (autre nom : `"audio"` dans game.json). Le format complet est
dans `docs/game-format.md`, section audio.json ; en bref :

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

| Quoi | Sens |
|---|---|
| formats | WAV, OGG Vorbis, MP3, FLAC ; `"synth"` — un son à partir de paramètres (`sine/square/saw/triangle/noise`, balayage de fréquence, enveloppe, vibrato, filtre, couches) |
| bus | `sfx` et `ambient` sont positionnels (3D : atténuation, panoramique, hauteur) ; `voice`, `ui`, `music` ne le sont pas |
| distance | `spatial` : modèle d’atténuation, portées, « air » (les aigus s’estompent avec la distance), la réverbération croît avec la distance, vitesse du son ; un son peut avoir ses propres portées et `far` — une variante lointaine avec fondu enchaîné |
| `maxInstances`, `cooldown`, `priority` | élagage dans les grandes batailles : 40 archers ne produisent pas 40 tirs identiques |
| `groups` | limites communes à plusieurs sons (toute la mêlée, toutes les explosions, un bus entier) : `max`, `cooldown`, lequel abandonner — `farthest` (par défaut : un combat lointain ne coupe pas ce qui est à l’écran), `oldest`, `quietest`, `none` |
| événements (cues) | `attack hit miss died spawned trainStarted ready buildPlaced buildDone gathered captured lost playerDefeated victory defeat notEnough notEnoughSupply noRoom researched levelUp ability readyToPlace sold regionClaimed wave lowPower upgraded message underAttack baseUnderAttack select move attackOrder buildOrder click error` |
| recherche du son | type → arme/capacité par id → `events` |
| musique | playlists `menu`, `match`, `battle`, `victory`, `defeat` (et `match.<carte>`) ; fondu enchaîné, intro + boucles sans coupure, couches selon l’intensité du combat, transitions sur les temps/mesures, jingles, atténuation sous la voix |
| compression à la compilation | `aiversebuild` réencode l’audio par plateforme : ADPCM pour les effets, Vorbis pour la musique, fréquence réduite, mono ; profils `desktop`/`mobile`/`web` dans `audio.json` → `build` ; `--audio off` — sans réencodage |

Les sons de combat ne s’entendent que là où il n’y a pas de brouillard de guerre ; « nous sommes attaqués » arrive avec une
pause et seulement si la caméra regarde ailleurs. Le volume (général et musique) est dans le menu pause et stocké dans
`userDataDir/audio.cfg`. Sur téléphone, le son se met en pause quand l’application passe en arrière-plan.

Déboguer sans carte son : `AIVERSE_AUDIO=null` (un vrai thread audio sans matériel), `AIVERSE_AUDIO=off`,
`AIVERSE_AUDIO_CAPTURE=/tmp/mix.wav` — enregistrer tout ce qui a été joué dans un WAV.

## Modèles, matériaux, animation

Un type est dessiné d’après son bloc `"view"` : une forme intégrée (`"shape"`) ou un fichier de modèle (`"model"`) —
**glTF 2.0** (`.glb`, `.gltf` + `.bin` + images) et **OBJ + MTL**. Tout passe par un seul pipeline :

- **Matériaux PBR** (metallic-roughness) : couleur de base, normales, metallic-roughness, occlusion, émissif ; transparence
  opaque / mask / blend, double face, unlit, variantes de matériaux (`KHR_materials_variants`), surcharges dans `"materials"`.
  Couleur du joueur : sur tout le matériau, par texture de masque, par alpha de texture (comme dans Warcraft III) ou par alpha
  des sommets.
- **Animation squelettique** sur le GPU (4 influences, nombre d’os quelconque), clips par état d’unité (`idle`, `move`,
  `attack`, `gather`, `build`, `die`, `spawn`, `hit`, `cast`…), transitions douces, couches avec masque d’os (attaquer avec
  le haut du corps en marchant), cadence de pas d’après la vitesse réelle.
- **Animation procédurale** sur n’importe quel nœud de modèle ou forme : balancement, rotation (moulins, hélices), roues selon
  la distance parcourue, tourelle visant la cible, recul, inclinaison dans les virages, chute et enfoncement à la mort et
  plus ; pilotée par les états et les événements de combat.
- **Niveaux de qualité** des matériaux (`low` / `medium` / `high`) choisis selon le GPU, comme les ombres ;
  `AIVERSE_MATERIALS=low|medium|high` les force pour les tests.
- **Foules** : les clips sont cuits dans une texture, les unités identiques dessinées en instanciation (300 unités animées —
  61 appels de dessin au lieu de 1801) ; LOD des modèles selon la distance ; l’animation hors écran et au loin est mise à jour
  moins souvent.
- **Animation avancée** : blend space marche/course, clips additifs (respiration, recul), IK des pieds sur le terrain, tête qui
  se tourne vers la cible, véhicules inclinés sur les pentes, morph targets (blend shapes) sur le GPU.
- **Sockets** : une arme en main, une charge sur le dos (selon la ressource portée), points de départ des projectiles, points
  d’effets (fumée, feu, échappement) ; événements de clips et `RtsView::events()/effects()` pour le son et les particules.
- **Étapes** : échafaudages et étapes de construction, variante endommagée selon la santé, dissolution à l’apparition et à la
  mort, contour de sélection, fantôme translucide pendant le placement d’un bâtiment, portraits.
- **Formats** : aussi **FBX** (ufbx) et textures compressées **KTX2** (ETC2/BC/ASTC, qui restent compressées sur le GPU) ;
  **Collada**, **3DS**, **PLY**, **STL** ; voxels **MagicaVoxel .vox** et **.vxl/.hva** (Tiberian Sun, Red Alert 2) ; modèles de
  RTS classiques depuis la copie de l’utilisateur : **Warcraft III .mdx/.mdl** (+ BLP), **Total Annihilation .3do**,
  **C&C Generals .w3d** (+ DDS). Tout le reste lisible par assimp (.blend, .lwo, .x, .ms3d, .md2/.md3, .smd, .b3d…) est converti
  en glTF par l’utilitaire **modelconv** : l’éditeur s’en charge lui-même à l’ouverture et à l’enregistrement, et le packager
  ne met que le résultat dans le paquet.
- **Matériaux à la demande** : une bibliothèque commune `materials.json` (héritage via `base`), l’affectation d’un matériau à
  un nœud de modèle (`"assign": {"Turret": "steel"}`), des textures procédurales sans raccord avec cartes de normales et
  masques de couleur du joueur ; la fenêtre **Matériaux** avec aperçu et les opérations MCP `material_*` /
  `texture_generate` pour les agents.

Un modèle cassé ne casse pas le jeu : le type est dessiné comme une forme et la raison va dans la console de l’éditeur.
Visualisation : `aiverse_modelview model.glb@Walk` ou `aiverse_modelview --game templates/showcase` (tous les types d’un jeu
tels que le jeu les dessine) ; dans l’éditeur — la fenêtre **Models** (clips, nœuds, matériaux, portrait, rechargement à
l’enregistrement du fichier de modèle). Exemple : `templates/showcase` ; format complet : [models.md](../models.md).

## Éclairage

L’éclairage dynamique est conçu pour les vieux appareils Android (Mali-T, Adreno 3xx/4xx) : toutes les lumières sont
dessinées dans une petite carte de lumière au-dessus du sol visible, et le shader lit la lumière avec un seul échantillon de
texture quel que soit le nombre de lumières. Niveaux : `off`, `low` (carte 128², mise à jour une image sur deux), `medium`
(256²), `high` (+ 8 lumières par pixel avec spéculaire). Choisi selon le GPU, comme les ombres ; à la main :
`AIVERSE_LIGHTS=…`. Si les images dépassent durablement le budget en partie, le client abaisse lumières et ombres par paliers
(le plus coûteux et le moins visible d’abord) et remonte d’un palier après une bataille intense ; choisir les ombres à la main
dans le menu désactive ce mécanisme (`AIVERSE_ADAPTIVE=off`).

- **Lumières des types et du combat** — `view.light` (feux de camp, forges, champignons lumineux), éclairs des tirs, impacts
  et explosions, lumière des projectiles (`attackLight`, `projectileLight`, `hitLight`, `deathLight`), lumière des capacités
  (`abilityLights` ; par défaut la couleur suit l’effet : dégâts — orange, soin — vert), halos `halo` au lieu d’un bloom
  coûteux. Le brouillard de guerre cache les éclairs.
- **Jour et nuit, météo** — le bloc `lighting` dans `game.json` ou dans une carte : `time`, `dayLength`, `nightLevel`,
  `clouds`, `wind`, `storm` (ombres des nuages et éclairs, identiques dans les replays), `preset` : `desert`, `snow`,
  `volcanic`, `overcast`, `cave` (souterrain : nuit éternelle, lumière uniquement des sources). La nuit, les matériaux
  émissifs brillent davantage et les lumières se reflètent sur l’eau.
- **Lumières de la carte** — le tableau `lights` d’une carte (lanternes, feux de camp, lave).
- **Ombres de contact et AO** — taches douces sous les unités et autour des bâtiments, à tous les niveaux (`contactShadow`,
  `contactShape` sur le type).
- **Éditeur** — la fenêtre « Lighting » : aperçu de l’heure et de l’orage, niveau de qualité de la lumière, liste des
  sources. Pour les agents : `light_add`, `light_update`, `light_delete`, `light_list`, `lighting_set`, `view_lighting`.

Le format des champs est dans l’en-tête de `modules/rts/view/RtsLights.h`. Variables de test : `AIVERSE_TIME_OF_DAY`,
`AIVERSE_NIGHT`, `AIVERSE_CLOUDS`, `AIVERSE_STORM`.

## Vérifier les mécaniques via MCP

Un agent qui construit un jeu sans code vérifie les mécaniques de façon ciblée, pas seulement par une partie de bots.
- **`scenario_run`** — un scénario sur une copie de la carte ouverte (rien n’est enregistré) : `clear` (une carte vide sans
  objets, vagues ni unités de départ), `spawn` avec des noms (`"id": "foot", "count": 3` → `foot#1..3`, `"foot"` — tous),
  commandes à la seconde (mêmes clés que `play_command`), `bots` pour certains emplacements, `checks` avec un résultat
  réussi/échoué (`alive`, `dead`, `count`, `hp`, `resource`, `event`, `trigger`, `variable`, `distance`, `status`, `result` +
  comparaison), `watch` + `every` (état des objets dans le temps), `events` (journal des événements).
- **`play_units` / `play_command`** — commandes dans un test en direct de la fenêtre Game (auparavant seulement dans
  l’éditeur, désormais visibles des clients MCP). Références d’objets : ids issus de `play_units`, noms `id` de la carte ou un
  filtre `{type, owner, tags, kind, area, near + radius}` ; `queue: true` — file (Maj), `ability` par id.
- **`match_run`** — plus un journal des déclencheurs activés et `events` (journal des événements par nom).
- **`rules_lint`** — les clés de `rules.json` et de la carte que le chargeur ne lit pas (fautes de frappe, bloc mal placé),
  avec une suggestion (« did you mean 'armor'? »). Il tourne sur le vrai chargeur (`JsonAccessLog` dans `engine/data/Json.h`) ;
  `rules_set`, `rules_merge` et `map_validate` signalent ces clés immédiatement.
- **`rules_schema`** — `part: "keys"` : chaque clé que le chargeur demande dans chaque bloc (`types.*.weapons[]`), collectée
  sur tous les jeux, et les clés absentes de `docs/game-format.md` ; `part: "doc"` — le document lui-même.
- L’événement de simulation `kEvTriggerFired` (pour les outils et la présentation seulement, hors somme de contrôle).
- Branche `agent-mech-tools`.

## Post-effets

Les effets plein écran au-dessus de la scène 3D se définissent par des données : le bloc `"postfx"` dans `game.json` (une
carte peut le remplacer). Il y a bloom, étalonnage, LUT, vignette, tilt-shift, rayons de soleil, FXAA, grain, aberration,
netteté, pixellisation, postérisation, teinte et bandes cinéma, plus des ensembles prêts (cinéma, vif, noir, rétro,
miniature, coucher de soleil, hiver, vision nocturne). Chaque effet a `minQuality`. Le niveau est choisi selon le GPU : les
Android faibles ont « bas », les téléphones « moyen », les PC « haut » ; `AIVERSE_POSTFX` le remplace. Sans effets, le
post-traitement ne coûte rien. Les effets légers sont regroupés en une passe ; les lourds (bloom, FXAA, rayons) ajoutent les
leurs.

Éditeur : la fenêtre **Post-effets** (aperçu des niveaux, tableau du coût par image selon la classe d’appareil).
MCP : `postfx_catalog/get/add/set/remove/preset/enable/estimate/lut_neutral`, `view_postfx`. Les réponses aux modifications
contiennent une estimation en ms/image et des avertissements sur les effets lourds. Détails : `docs/postfx.md`.

## Vérifié / non vérifié

- ✅ Linux : client, éditeur, tests ; .exe Windows (compilation croisée MinGW, exécuté sous Wine).
- ✅ macOS : compilation, tests, jeu sur Metal ; aiversebuild → .app et un projet iOS (+ build pour le simulateur).
- ✅ Web : compile (Emscripten 3.1.50), une partie tourne dans Chromium headless (WebGL2/SwiftShader).
- ⏳ Android : le projet Gradle est généré ; l’APK et l’exécution dans l’émulateur sont dans la CI (job `android`), non compilés en local.
- ⏳ Windows sur vrai matériel et iOS sur appareil — pas exécutés (dans la CI : build MSVC et build iOS pour le simulateur).

## Plateformes et CI

`.github/workflows/ci.yml` à chaque push sur `master`/`agent-*` et sur les pull requests :

| Job | Ce qu’il fait |
|---|---|
| `linux` | gcc et clang (x64) + gcc (ARM64), avertissements en erreurs, tests, parties de bots, captures sous Xvfb |
| `windows-msvc` | Visual Studio 2022, tests, parties, `aiversebuild --target windows` |
| `windows-mingw` | compilation croisée MinGW-w64, tests et parties sous Wine |
| `macos` | build Xcode universel, tests, parties, `aiversebuild` → .app et un projet iOS avec build pour le simulateur |
| `android` | `.so` via le NDK, APK via `aiversebuild` ; dans l’émulateur : lancement → toucher → arrière-plan → retour → « retour » → relance, logcat vérifié pour les plantages |
| `web` | Emscripten, une partie dans Chromium headless, vérification de la console et de l’image |
| `determinism` | compare les sommes de contrôle des parties (les deux jeux × 3 seeds) de toutes les plateformes — elles doivent correspondre au bit près |

Captures, APK, build web et .app sont conservés comme artefacts de l’exécution.

En local :

```bash
# Windows .exe from Linux/macOS
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64.cmake
# bot matches on every game → a checksum file (compare between machines with diff)
.github/scripts/headless-checksums.sh build/linux-release/aiverse_headless sums.txt
# smoke test of the web build
node platform/web/smoke.mjs build/web
```

Dans le build web, les paramètres de ligne de commande passent par l’URL : `index.html?map=0&spectate` = `--map 0 --spectate`.

Clavier à l’écran : `platform::startTextInput(area)` / `stopTextInput()` l’ouvrent et le ferment sous iOS, Android et dans les
navigateurs mobiles (sur ordinateur, ils ne font rien). Le texte arrive sous forme d’événements `Char` dans n’importe quelle
langue, Retour arrière/Entrée comme `KeyDown`/`KeyUp` ; `softKeyboardHeight()` et l’événement `KeyboardChanged` indiquent
quelle part de l’écran est masquée en bas, `TextInputEnded` que l’utilisateur a fermé le clavier lui-même. L’interface
(`UiLayer`) appelle cela elle-même au focus dans `<input type="text|password|search|email|number">` et `<textarea>`.

Autres fonctions mobiles et navigateur de `Platform.h` :
- `safeAreaInsets()` — marges pour l’encoche de la caméra, les coins arrondis, l’indicateur d’accueil et les barres système
  (iOS, Android, Safari/Chrome mobiles ; 0 sur ordinateur, sur un MacBook à encoche — en plein écran).
- `setFullscreen()` / `isFullscreen()` ; bascules intégrées — F11 (Windows, Linux, web), Alt+Entrée (Windows). Dans le
  navigateur, le plein écran verrouille aussi l’orientation (`AppConfig::orientation`).
- Android : le bouton Retour arrive au jeu comme `Key::Escape` ; plein écran avec barres masquées automatiquement ; un clavier
  et une souris physiques (Chromebook, DeX) produisent des événements de touches et de souris ordinaires.
- iPad : Magic Keyboard / clavier Bluetooth, souris et trackpad (clic droit, survol, défilement).
- Le presse-papiers fonctionne partout ; dans le navigateur, Ctrl/Cmd+V atteint le jeu une fois que le navigateur a remis le texte.
- Web : `userDataDir()` est stocké dans IndexedDB — réglages et sauvegardes survivent au rechargement de la page.

### Plugins natifs : paiements et publicité

Tout SDK de plateforme se branche comme un plugin dans son langage natif — Java (Android), Objective-C (iOS/macOS), JavaScript
(web) ou C++ — via un pont asynchrone unique, `engine/platform/Plugins.h`. Un jeu utilise les API communes `aiverse::store`
(achats) et `aiverse::ads` (publicité) de `engine/services`, et le fournisseur est choisi par plateforme dans `game.json`.
Plugins prêts dans `plugins/` : **App Store**, **Google Play**, **RuStore**, **Yandex Games** (web : achats et publicité),
**Steam** (achats via l’Inventory Service et DLC, Windows/Linux/macOS), **Yandex Mobile Ads**, **AdMob**, **AppLovin MAX**,
**ironSource (Unity LevelPlay)**. Un build ne contient que les plugins choisis par le jeu (`"plugins"` dans game.json ou
`--plugins`) : aucun SDK, dépendance, entrée de manifeste ou clé d’Info.plist étrangers — la CI le vérifie. Pour déboguer sur
ordinateur, il existe des simulations de boutique et de publicité (`mock-store`, `mock-ads` ; `AIVERSE_MOCK_STORE`,
`AIVERSE_MOCK_ADS`) : tout le parcours d’achat fonctionne sans téléphone, et ces simulations ne sont pas compilées dans les
builds de release. aiversebuild inscrit lui-même les plugins dans Gradle/le manifeste, Info.plist/Podfile et le HTML ;
compilez Google Play et RuStore séparément : `--plugins googleplay,admob` et `--plugins rustore,yandex-ads`. Détails :
[plugins.md](../plugins.md).

### Versions

`.github/workflows/release.yml` : un tag `v1.2.3` (ou un lancement manuel avec le dossier de jeu de votre choix) compile toutes
les plateformes avec `aiversebuild` et publie une GitHub Release :

| Plateforme | Résultat | Signature (secrets du dépôt) |
|---|---|---|
| Windows | installateur (Inno Setup, sans droits d’administrateur) + .zip portable | `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` |
| Android | .apk et .aab pour Google Play | `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` |
| macOS | .app dans un .zip, notarisée | `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD` (Developer ID) + une clé App Store Connect |
| iOS | projet Xcode ; avec une clé — un build TestFlight | `APPLE_TEAM_ID`, `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_P8_BASE64` |
| Linux | .tar.gz | — |
| Web | .zip ; avec `DEPLOY_WEB_PAGES=true` (variable du dépôt) — publication sur GitHub Pages | — |

Sans secrets, la release sort quand même : l’APK est signé avec une clé de débogage, la .app en ad-hoc, Windows sans signature.

### Moteur précompilé (branche `builds`) et aiversemcp sans compilateur

L’éditeur et les outils précompilés sont publiés sur la branche **`builds`** :
`<platform>/AIVERSE-<version>-<commit>-<platform>.zip` (actuellement `windows` x86_64 et `macos` arm64 ; `linux` x86_64 quand
l’intégrateur le publiera). Dedans, un dossier `AIVERSE/` avec les sources du moteur à ce commit (modèles `templates/`,
`engine/assets`, `docs/` — le serveur MCP les lit) et `AIVERSE/bin/` avec les programmes. Empaqueté par
`tools/release/package_engine.py`, publié par `tools/release/publish_builds.py` (AGENTS.md §8) ; le paquet Linux peut venir
sans l’éditeur — seulement `aiversemcp` et `aiverse_headless`.

Le serveur MCP depuis le dossier décompressé (les parties de bots et `balance_*` tournent dans aiversemcp, il n’a pas besoin
d’un `aiverse_headless` séparé ; l’éditeur est facultatif, sans lui utilisez `--no-editor`) :

```bash
# ARCHIVE is the path to the AIVERSE archive downloaded from index.json.
unzip "$ARCHIVE" -d ~/aiverse      # unzip keeps the executable bit
~/aiverse/AIVERSE/bin/aiversemcp --no-editor --engine ~/aiverse/AIVERSE
```

`--engine` peut être omis dans les builds issus de commits où aiversemcp cherche le moteur à côté de lui
(`engine/platform/EngineDir.h`, comme l’éditeur) ; dans les builds publiés plus tôt, il est obligatoire. Avec des
décompresseurs qui ne restaurent pas les droits (Python `zipfile`), faites `chmod +x` sur les fichiers de `bin/`. Sous macOS,
un fichier téléchargé par un navigateur peut être mis en quarantaine : `xattr -dr com.apple.quarantine ~/aiverse/AIVERSE`.

Les programmes qui téléchargent eux-mêmes le moteur lisent **`index.json`** à la racine de la branche
(`https://raw.githubusercontent.com/chromealex/AIVERSE/builds/index.json`), pas le README. Ci-dessous un exemple d’entrée
historique RTSEngine ; dans les nouveaux builds, les chemins contiennent AIVERSE et la clé `rtsmcp` est conservée :

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

- `packages` va du plus récent au plus ancien ; la première entrée avec le `platform` + `arch` voulus est le build le plus récent.
- `platform` : `windows` | `macos` | `linux` ; `arch` : `x86_64` | `arm64` | `universal` (macOS fat) | `null` (non reconnu) —
  déterminé à partir de l’en-tête d’`aiversemcp` dans le zip.
- `rtsmcp`, `editor`, `headless` sont des chemins dans l’archive, ou `null` si le programme manque ; `--engine` est le dossier
  `root`. Sous macOS, `editor` est un bundle `.app`.
- `url` pointe vers une branche réécrite à chaque publication (5 versions par plateforme sont gardées) : les anciennes entrées
  disparaissent, téléchargez donc à partir d’un `index.json` récent et vérifiez `sha256`.
- De nouveaux champs peuvent apparaître ; un changement de format incompatible augmentera `schema`.

Vérification : `python3 tools/release/test_release.py [build/linux-release/aiversemcp]` (CTest `release_tools`) — le format
d’`index.json`, et avec un chemin vers aiversemcp : l’empaquetage d’un paquet Linux, sa décompression dans un dossier
temporaire et des appels MCP depuis celui-ci sans `--engine` (`tools/list`, `create_project`, `map_generate`,
`map_validate`, `map_ascii`).

Le mode capture (`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`) avance d’exactement 1/60 s par image, donc l’image N est
la même sur toute machine.

## La suite

Modèles glTF et animation, audio, réseau (lockstep), capacités et bonus depuis les données, flow fields et RVO pour les
grandes armées, améliorations de l’IA, scripts en C# (AOT pour iOS).

## Modules de jeu en C++

En plus de Lua, un projet peut contenir `native/CMakeLists.txt` et des modules C++20. Ils sont ajoutés avec
`aiverse_native_module(...)` à la compilation et activés par le tableau `nativeModules` de `rules.json`. Ticks, commandes,
rollback et sauvegarde de l’état sont pris en charge ; Lua et C++ fonctionnent ensemble. Après une modification du C++, le jeu
et l’éditeur doivent être recompilés. [API, exemple et instructions de compilation](../native-modules.md).

## Bibliothèque méta C++ réutilisable

`aiverse::meta` — boutique/offres/artisanat configurables, monnaies et objets, quêtes et passes, récompenses quotidiennes,
énergie, courrier, classements, chats et segments. Disponible pour le code natif du projet sans Lua/GPU ; les définitions
sont en JSON, les algorithmes n’ont pas besoin d’être régénérés. Démarrage :
`python3 tools/meta/scaffold.py --preset mobile --out <project>`. Documentation : [meta-game.md](../meta-game.md) ; pour les
agents — `skills/meta-game/SKILL.md`. Les politiques locales fonctionnent sans serveur ; la communication entre appareils et
les classements mondiaux nécessitent un vrai backend. Un service cloud prêt à l’emploi ne fait pas partie du paquet.

Le preset `full --with-ui` ajoute amis/clans, une collection d’instances, l’équipement, la fusion, les abonnements et les
ligues de tournoi. `aiverse::meta::Service` exécute les opérations de façon atomique ; `tools/meta/server.py` est un hôte
HTTP/SQLite local avec vérification des sessions. Sont inclus des écrans de méta pour navigateur et un catalogue de recettes
courtes pour les agents. [Compilation, lancement et limites](../meta-service.md).

`full` comprend aussi 15 groupes LiveOps : coffres avec garantie, albums/échanges, files de tâches, recherche/prestige, quêtes
composées, événements, guerres de clans, PvP asynchrone, matchmaking de groupes, Elo/Glicko-2, parrainages/codes promo,
publicités récompensées, chaînes d’offres, notifications et revenus hors ligne. Tous les paramètres sont dans
`extensions.liveops` ; ce preset n’installe pas de nouveaux écrans d’interface séparés ni de SDK externes.
`python3 tools/meta/catalog.py describe matchmaking` n’affiche que le schéma et l’exemple nécessaires.
[API, politiques et limites](../meta-liveops.md).

### Physique 3D optionnelle

Box3D s’active avec l’option `AIVERSE_ENABLE_PHYSICS=ON` ; les projets RTS ordinaires n’en ont pas besoin. Un exemple
séparé, `aiverse_physics_demo`, montre des corps, des collisions, un pendule et la sauvegarde de l’état.
[Compilation, API et limites d’intégration](../physics.md).

### Tissu XPBD visuel

Le module optionnel `AIVERSE_ENABLE_CLOTH=ON` : son propre solveur de tissu XPBD sur CPU ou en Metal compute, lots partagés,
LOD temporel, mise en veille automatique et rendu de maillages denses en un seul draw instancié. Sur le GPU, les positions
restent en mémoire vidéo ; les backends non pris en charge utilisent le CPU. L’exemple peut comparer le GPU à une référence
CPU et vérifier la mise en veille/le réveil. [Exemple autonome, limites et mesures](../cloth.md).
