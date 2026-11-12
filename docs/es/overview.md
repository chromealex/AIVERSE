# AIVERSE — visión completa

Una referencia de cada parte del motor: estructura, compilación, formatos, subsistemas, plataformas y estado de verificación.
La introducción breve —para qué sirve el motor y qué problemas resuelve— está en el [README](../../README.es.md).
Las reglas para los agentes que modifican el motor están en [AGENTS.md](../../AGENTS.md).

## Estructura

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

## Inicio rápido (macOS)

```bash
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release      # requires Xcode and CMake
cmake --build build/mac -j 8
open build/mac/AiverseEditor.app                              # editor (project templates/skirmish)
open build/mac/AiverseGame.app                                # game
./build/mac/aiverse_tests
```

Otro juego en el editor: `AiverseEditor.app/Contents/MacOS/AiverseEditor --project templates/mushrooms`.
Para que el juego arranque con otro conjunto de datos, pasa `-DAIVERSE_CONTENT_DIR=.../templates/mushrooms`.

### Varios editores a la vez

Cada proceso del editor puede abrir su propio proyecto con `--project <carpeta>`. Sin `--listen`, la primera instancia ocupa
el puerto MCP `47321`; si está ocupado, el sistema asigna uno libre. El puerto real aparece en la barra de estado; la ventana
**Agent** lo añade a la configuración de conexión (`aiversemcp --port <puerto>`). Un puerto asignado automáticamente puede
cambiar tras reiniciar el editor. `--listen <puerto>` fija un puerto exacto (si hay conflicto, MCP no arranca),
`--listen 0` desactiva MCP. Los clientes MCP existentes no cambian de ventana por sí solos.

## Compilar para las plataformas — aiversebuild

```bash
./build/mac/aiversebuild --project templates/skirmish --target macos     # Builds/macos/<Name>.app (ad-hoc signed)
./build/mac/aiversebuild --project templates/skirmish --target ios       # Builds/ios/xcode/AIVERSE.xcodeproj + simulator build
./build/mac/aiversebuild --project templates/skirmish --target android   # Builds/android/<Name>-release.apk when SDK/NDK and gradle are present
./build/mac/aiversebuild --project templates/skirmish --target web       # Builds/web (requires emsdk)
./build/mac/aiversebuild --project templates/skirmish --target windows   # .exe via mingw-w64 (brew install mingw-w64) or sources + build.bat
./build/mac/aiversebuild --project templates/skirmish --target all
```

El resultado va a `<proyecto>/Builds/<plataforma>/`: solo builds terminados y el informe de tamaño. Todo lo intermedio
(árboles de CMake, paquetes, audio recodificado, el proyecto Gradle, datos de xcodebuild, la estimación de tamaño del editor)
vive en la carpeta oculta `Builds/.temp/`, que se puede borrar en cualquier momento. Con `--generate-only` el proyecto Gradle
para Android Studio va a `Builds/android/<Name>`. El empaquetador elimina por sí mismo las antiguas carpetas visibles
`_build`, `_pack`, `_audio`, `_estimate`.

Lo mismo está en el editor: el menú **Build**. Los proyectos de iOS y Android generados son autónomos (llevan copiados las
fuentes del motor y el contenido) y se pueden entregar a otros. Para iOS en un dispositivo: abre el proyecto en Xcode y elige
un equipo en Signing (o `--team <TeamID>`, o `"appleTeamId"` en game.json).

Identificador, nombre, versión, orientación e icono (`icon.png`, 1024×1024; se genera si falta) están en `game.json`.

### Herramientas de compilación: comprobación e instalación automática

Antes de compilar, `aiversebuild` comprueba qué necesita la plataforma y lo busca más allá del `PATH` (un editor lanzado desde
el Finder solo ve `/usr/bin:/bin:/usr/sbin:/sbin`): Homebrew (`/opt/homebrew/bin`, `/usr/local/bin`), `~/.local/bin`,
`~/Library/Python/*/bin`, `~/emsdk`, `$EMSDK`, `ANDROID_HOME` / `ANDROID_SDK_ROOT`, `~/Library/Android/sdk`, `~/Android/Sdk`,
`%LOCALAPPDATA%\Android\Sdk`, `JAVA_HOME`, `/usr/libexec/java_home`, Visual Studio (vswhere) y su propia carpeta
`~/.aiverse/toolchains`. Las carpetas encontradas se añaden al `PATH` de los procesos hijos.

| Plataforma | Necesita | Se instala automáticamente |
|---|---|---|
| web | CMake, Python 3, Emscripten (emsdk), Ninja — opcional | emsdk (≈ 400 MB de descarga, ≈ 1,5 GB en disco), CMake + Ninja (≈ 50 MB) |
| android | JDK 17–23, Android SDK (platform 35, build-tools, platform-tools), NDK, CMake del SDK, Gradle | todo: Temurin JDK 17, command line tools + `sdkmanager` (licencias aceptadas), NDK ≈ 700 MB, Gradle 8.10.2 |
| macos | Xcode Command Line Tools, CMake | CLT — con el instalador de Apple (`xcode-select --install`), CMake |
| ios | Xcode (App Store), CMake | CMake |
| windows (en Windows) | Visual Studio 2022 / Build Tools (C++), CMake | CMake; VS — una indicación con una orden `winget` |
| linux | compilador de C++, CMake | CMake |

```bash
aiversebuild --check-toolchain web            # what is present and what is missing (--json for the editor and MCP)
aiversebuild --install-toolchain web          # install what is missing (or one id: emsdk, cmake, jdk, android-ndk, gradle, ...)
aiversebuild --project ... --target web --install-missing   # install and build right away
```

Sin `--install-missing`, si faltan herramientas la compilación se detiene con el código de salida 3 y la línea
`AIVERSE_MISSING_TOOLCHAIN web: emsdk` (el editor la reconoce). En el editor, la ventana **Build** muestra para la plataforma
elegida qué se encontró (rutas, versiones) y qué falta, con un botón **Instalar automáticamente** y una casilla «Instalar
automáticamente lo que falte»; el progreso aparece en la **Consola** (el texto se puede seleccionar y copiar; «Abrir carpeta de
registros» abre `<motor>/logs`). Los agentes hacen lo mismo por MCP: `toolchain_check`, `toolchain_install`,
`build_start {"installMissing": true}`.

Todo se instala en `~/.aiverse/toolchains` (Windows: `%USERPROFILE%\.aiverse\toolchains`; otra carpeta — la variable
`AIVERSE_TOOLCHAINS_DIR`) sin `sudo` ni cambios en el sistema: `emsdk/`, `py/` (un venv con cmake y ninja), `cmake/`, `ninja/`,
`jdk-17/`, `android-sdk/`, `gradle-8.10.2/`. Volver a ejecutar es seguro (lo que ya está listo se omite). Para quitarlo todo,
borra esa carpeta. Excepciones: las Command Line Tools las instala el instalador de Apple, y si se encuentra el Android SDK del
usuario, `sdkmanager` instala ahí los paquetes que faltan.

### Paquetes de recursos (.rpak) y tamaño del build

El contenido del juego (la carpeta del juego + `engine/assets`) entra en un build como un único archivo
`content/content.rpak`: cada archivo se comprime por separado con zstd, los archivos idénticos se guardan una vez y los formatos
ya comprimidos (png, ogg, mp3, …) tal cual. El motor monta `content.rpak` automáticamente; los archivos del paquete tienen
prioridad sobre los de la carpeta, así que sin paquete (durante el desarrollo) todo funciona como antes.
`platform::mountPack("patch.rpak")` monta un paquete encima (parche, DLC). Cada build imprime un informe de tamaño y lo escribe
en `dist/<plataforma>/size-report.txt`.

```bash
aiversebuild ... --no-pack          # ship folders as is (debugging)
aiversebuild ... --pack-level 9     # zstd level (default 19 for Release, 3 for Debug)

assetpipe pack -o content.rpak templates/skirmish engine/assets=engine   # build a pack by hand
assetpipe list content.rpak       # files, sizes, compression
assetpipe verify content.rpak     # unpack everything and check hashes
assetpipe extract content.rpak out/
assetpipe diff old.rpak new.rpak  # what changed between versions (for patches)
```

Lo que el build no necesita se lista en `.rpakignore` en la carpeta del juego (máscaras `*`, `?`; `carpeta/` — solo carpetas).
Los archivos ocultos (`.git`, `.DS_Store`), las repeticiones `*.rpl` y los `*.rpak` anidados se omiten siempre. La creación de
paquetes es reproducible: el mismo contenido da un archivo idéntico byte a byte; el «hash de contenido» del paquete identifica
el conjunto de datos.

### Builds divididos y descarga de contenido

Opcionalmente, un build se puede dividir: la aplicación solo lleva un paquete base con lo necesario para arrancar y el resto va
en partes (`content.001.rpkc`, …) de un tamaño dado que el juego descarga de cualquier origen. Todo lo decide el autor del
juego, en `game.json` (ajustes comunes y por plataforma) o con opciones de `aiversebuild`:

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

Las partes se colocan junto al build (`dist/<plataforma>/chunks/`) y se publican en la dirección de `sources` (`{hash}` es el
hash de contenido del build, así las versiones no se mezclan). En el juego, un archivo de una parte aún no descargada se
solicita automáticamente (en escritorio y móvil `readContentFile` lo espera; en la web devuelve false); el progreso es
`platform::contentProgress()` y una solicitud explícita `platform::requestContent(paths)`. Cada parte se comprueba con SHA-256
(una parte alterada de un espejo se rechaza y se toma del siguiente), se guarda en caché y no se vuelve a descargar en el
siguiente arranque. Protocolos: https/http (libcurl en Linux, WinHTTP, NSURLSession, HttpURLConnection en Android, fetch en la
web), `file://` y carpetas; un juego puede registrar su propio esquema (Steam, Play Asset Delivery, P2P).
`assetpipe pack --chunk-size 8MB ...` hace lo mismo a mano, y `assetpipe verify` también comprueba las partes.

### Importar archivos ajenos (desempaquetadores)

Los formatos que el motor no conoce (zip, tar, dmg, archivos de otros juegos) se añaden como plugins desempaquetadores sin
recompilar el motor: una carpeta con `unpacker.json` y un programa en cualquier lenguaje (protocolo:
[`tools/unpackers/PROTOCOL.md`](../../tools/unpackers/PROTOCOL.md)). Los desempaquetadores solo funcionan en las herramientas de
compilación: un archivo ajeno se convierte en archivos dentro de un `.rpak`, y el juego sigue leyendo solo `.rpak`.

Incluidos: zip (y `.oramap`), tar, dmg, archivos de Warcraft I/II (`.WAR/.SUD/.INS`), `.PAK` de Westwood (Dune II, Lands of
Lore, Kyrandia), Z de los Bitmap Brothers (`.PAC`, `.RSC`).

```bash
assetpipe formats                                   # which formats are known
assetpipe probe ~/Downloads/Warcraft2/war2/DATA/*   # what these files are
assetpipe list MAINDAT.WAR                          # what is inside
assetpipe import -o war2.rpak MAINDAT.WAR=imported/war2 SFXDAT.SUD=imported/war2/sfx
assetpipe unpackers new tools/unpackers myformat    # scaffold for a new unpacker
assetpipe unpackers check                           # check all plugins
```

Todas las órdenes tienen `--json`: así las llama el servidor MCP. `game.json` puede listar `"imports"`, y `aiversebuild` los
desempaqueta en el paquete durante la compilación (`"optional": true` — si el archivo no está en esta máquina, la compilación
sigue sin él). Un paquete con contenido de terceros importado se marca como «external»: esos builds son solo para uso
personal y no deben commitearse ni distribuirse.

### Importar juegos (importadores)

Un importador convierte tu copia de un juego clásico en una carpeta de juego para el motor. Ya está disponible Dune II:

```bash
python3 tools/importers/dune2/import.py --source "~/Downloads/Dune II" --out ~/AiverseGames/dune2
aiversebuild --project ~/AiverseGames/dune2 --target macos
```

El resultado es un juego 2D en el formato del motor (`docs/game-format.md`, «2D games»): cámara cenital, 66 misiones de
campaña con una capa de tiles (`tilesets/dune.json` a partir de ICON.ICN: arena, roca, dunas, montañas, especia con
autotiling), sprites de unidades a partir de SHP (5 direcciones con espejo, torretas de los tanques sobre el casco) y edificios
a partir de tiles ICN (construcción, ruinas, animación), color de la casa a partir de tonos magenta. Además `rules.json`
(nuestro equilibrio de Dune II con bloques del motor), imágenes CPS y animaciones WSA en PNG, sonidos VOC → WAV + `audio.json`,
nombres en inglés y ruso. El terreno es aproximado: no se usa el generador de paisaje original, las bases están sobre roca, la
especia viene de `Field=`, las cuotas de las misiones son disparadores. La infantería, el ornitóptero y el transporte aún se
dibujan como formas 3D. La carpeta está marcada con `"external": true`: no debe commitearse ni distribuirse.

## Archivo de proyecto .aiverse

Un proyecto es una carpeta en cualquier lugar del disco (`game.json`, `rules.json`, `maps/`, ...). En su raíz está el archivo
de proyecto **`<Nombre>.aiverse`**, que se abre con doble clic, como `.uproject` en Unreal o `project.godot`. `game.json` sigue
siendo el manifiesto del juego (lo leen el cliente, aiversebuild y los paquetes); `.aiverse` solo marca la carpeta como
proyecto y guarda datos del editor:

```json
{"format": 1, "name": "My Strategy", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
```

- Se crea con el proyecto (centro «Nuevo proyecto», MCP `create_project`); un proyecto antiguo sin él lo recibe al abrirse
  por primera vez en el editor. Las plantillas de `templates/*/` ya tienen uno (no se copia a un proyecto nuevo: este recibe
  el suyo).
- En todas partes donde el editor acepta una ruta de proyecto valen tanto `.aiverse` como la carpeta:
  `aiverse_editor "My Strategy.aiverse"`, `--project`, «Abrir…» en el centro (muestra `.aiverse` y carpetas de proyectos),
  recientes, MCP `open_game`, `aiversebuild --project x.aiverse`. `.aiverse` no entra en el build.
- Los proyectos creados antes del cambio de nombre a AIVERSE conservan su `<Nombre>.rtsproj`: se abre igual en todas partes,
  se reescribe en su sitio y nunca se renombra; si la carpeta tiene ambos, manda `.aiverse`.
- **Doble clic.** Al arrancar, el editor asocia `.aiverse` consigo mismo (por usuario, sin permisos de administrador) si no
  hay asociación o si apunta a otra instancia del editor; no sustituye otro programa que hayas elegido. A mano: «Proyecto →
  Hacer del editor la aplicación predeterminada para .aiverse»; el estado aparece abajo a la izquierda en el centro.
  Desactivar: `AIVERSE_FILE_ASSOC=off`.
  - **macOS:** el tipo `com.aiverse.project` está declarado en el Info.plist de `AiverseEditor.app`
    (`platform/macos/EditorInfo.plist.in`; el icono de documento `rtsproj.icns` lo dibuja `aiverse_projicon` a partir del logo
    del editor `apps/editor/branding/icon_1024.png`); el editor registra el bundle en LaunchServices (`lsregister -f`) y pasa a
    ser el gestor predeterminado. Un doble clic y `open -a AiverseEditor x.aiverse` abren el proyecto en el editor en marcha
    (si el mapa tiene cambios sin guardar, pregunta si guardarlo).
  - **Windows:** `HKCU\Software\Classes\.aiverse` (+ `.rtsproj`) → `AIVERSE.Project` (icono `%USERPROFILE%\.aiverse\icons\rtsproj.ico`,
    orden `"<aiverse_editor.exe>" "%1"`). Si Windows ya tiene otro programa elegido para `.aiverse`, el centro sugiere
    «Abrir con → Siempre».
  - **Linux:** el tipo `application/x-aiverse-project` (`~/.local/share/mime/packages/aiverse-project.xml`),
    `~/.local/share/applications/aiverse-editor.desktop` (`Exec=<editor> %f`), iconos en `~/.local/share/icons/hicolor`,
    aplicación predeterminada en `~/.config/mimeapps.list`; se llaman `update-mime-database`, `update-desktop-database` y
    `xdg-mime` si están instalados.

## Crear un juego (asistente)

El botón **Crear un juego** en la ventana de proyectos (y **Proyecto → Crear un juego…**): da una carpeta o un enlace con arte,
música y textos, describe la idea (o nada: el asistente elige solo) y obtén un proyecto `.aiverse` terminado: un juego sobre una
plantilla adecuada, un tutorial paso a paso (guiado) y consejos en combate (discretos), niveles con estrellas y recompensas, la
meta (menú, mejoras, tienda, recompensa diaria, perfil, ajustes), anuncios y compras si se desea, idiomas, icono y pantalla de
carga a partir del arte. El asistente juega él mismo el tutorial con un bot y comprueba los niveles; lo asignado y lo que hay
que revisar está en el `WIZARD.md` del proyecto. Para agentes: MCP `game_make_options` / `game_make_plan` / `game_make`.
Detalles: [game-wizard.md](game-wizard.md).

## Cómo se describe un juego

Un tipo de objeto es un conjunto de bloques opcionales; lo que se define, el objeto lo sabe hacer:

| Bloque | Significado |
|---|---|
| `health` | salud, armadura, tipo de armadura, regeneración |
| `move` | velocidad, capa (`ground`/`air`/`naval`), radio |
| `structure` | un edificio con tamaño `footprint` en la cuadrícula |
| `weapons` | alcance, daño, tipo de daño, enfriamiento, proyectil, daño en área, a qué capas alcanza |
| `cost` | recursos, tiempo, suministro |
| `produces` / `builds` | qué entrena / qué construye |
| `gather` / `resourceNode` / `dropoff` | recolección de recursos (Warcraft/StarCraft) |
| `income` / `storage` + recurso `"kind": "flow"` | economía de flujo (Total Annihilation) |
| `supply` | aporta suministro |
| `requires` | árbol tecnológico |
| `garrison` + `entersGarrison` | edificios-guarnición, envío de un porcentaje de tropas, captura (Mushroom Wars) |
| `view` | aspecto: `shape`, `scale`, `color` |

Global: `resources`, `damageTable` (tipo de daño × tipo de armadura), `supplyCap`, `victory`.
Mapa: tamaño, terreno (`rows`: `.` suelo, `~` agua, `#` roca, `,` sin construir, `=` vado; `heights`), jugadores y equipos,
recursos iniciales, objetos, caminos, `roadsOnly`.

## Localización

Un juego se traduce sin cambiar `rules.json`: los nombres en los datos son texto en el idioma de origen del juego, y las
traducciones están en `<proyecto>/lang/<idioma>.json` bajo claves como `type.footman`, `resource.gold`, `map.valley.title`…
Las cadenas del menú y del HUD están en `engine/assets/lang/` (ahora inglés y ruso); un juego puede sustituir cualquiera.

```jsonc
// game.json
"sourceLanguage": "en",
"languages": ["en", "ru"]
```

```bash
./build/mac/aiverse_i18n extract --project templates/skirmish --lang de --write   # lang/de.json skeleton, new strings marked // TODO
./build/mac/aiverse_i18n check   --project templates/skirmish                     # placeholders, plural forms, untranslated strings
```

El idioma se elige así: la elección del jugador en el menú (se recuerda) → el idioma del sistema → el idioma de origen del
juego. Para probar: `--lang ru`, `AIVERSE_LANG=ru`, el seudoidioma `--lang qps` (todas las cadenas deformadas y más largas: lo
no traducido y lo que no cabe salta a la vista). Las formas del plural siguen CLDR («1 segundo / 2 segundos / 5 segundos»).
Más: `engine/i18n/README.md`.

## Interfaz del juego (RmlUi, HTML/CSS)

El menú, el HUD, la tarjeta de órdenes, el minimapa y la pausa del cliente son documentos RmlUi (marcado `.rml`, estilos
`.rcss`), no código. El motor los dibuja con su RHI (GL/Metal); las fuentes pasan por stb_truetype (latín + cirílico).

- Documentos por defecto: `engine/assets/ui/` (`menu.rml`, `hud.rml`, `theme.rcss`).
- Un juego sustituye cualquier archivo poniendo `ui/<mismo nombre>` en su carpeta: por ejemplo, solo `ui/theme.rcss` —
  colores propios con el marcado por defecto. La fuente del juego es `fonts/ui.ttf`.
- Los datos vienen del modelo `game` (la lista de campos está en un comentario de `hud.rml`); las pulsaciones de botones se
  convierten en órdenes de la simulación (`apps/client/ClientUi.cpp`).
- Tamaños en `dp`: la interfaz se escala con la densidad de pantalla; en dispositivos táctiles la raíz tiene la clase `touch`
  (botones más grandes).
- F8 en el cliente abre el depurador de RmlUi (árbol de elementos, estilos, registro).
- El aspecto del juego es `ui/skin.rcss` (se carga después de `theme.rcss` en cada documento: colores, bordes, botones,
  disposición) y la fuente de títulos `fonts/title.ttf` (familia `title`). Cada prototipo de `templates/` tiene su aspecto. La
  disposición vertical (`@media (orientation: portrait)`) está en el tema del motor; en PC se comprueba con `--size 540x960`.
- Los textos de los documentos se escriben como claves `@menu.play`: la traducción se toma de `engine/assets/lang` (y del
  `lang/` del juego) al cargar el documento; cambiar de idioma en el menú recarga los documentos.
- Teclado en pantalla (`engine/ui/UiKeyboard.*`): hecho con RmlUi, se abre al enfocar un campo de entrada en dispositivos
  táctiles (en PC — `AIVERSE_OSK=1`). Las distribuciones (en, ru, símbolos; Mayús, Bloq Mayús con doble toque, cambio de
  idioma) están en `keyboard.json` y el aspecto en `keyboard.rml` / `keyboard.rcss`; un juego sustituye cualquiera de estos
  archivos en su `ui/`. Si el cursor está en la parte baja de la pantalla, el teclado se coloca arriba.
- Multijugador (`lobby.rml`, `apps/client/Lobby.cpp`): dirección del servidor y nombre, partida rápida por las colas del
  servidor, lista de salas (entrar / mirar), sala propia (plazas, bots, listo, inicio, chat). La partida la lleva `NetClient`:
  `attach(sim)`, `advance(dt)` en vez de `sim.advance(dt)`, las órdenes salen de la plaza `match().localSlot`
  (0 — espectador); los bots que el servidor asignó a este cliente también se calculan aquí.

## Determinismo

La simulación es idéntica bit a bit en todas las plataformas: solo punto fijo, generador aleatorio propio, órdenes en orden
canónico, sumas de verificación en las repeticiones. Verificado: Linux x86-64 (GCC, Clang, MinGW), Linux ARM64 (GCC),
macOS ARM64 (AppleClang): sumas de verificación de partidas idénticas.

## Gráficos y móviles

Una sola pasada forward: sol (Lambert + especular), un mapa de sombras ajustado a la parte visible del mapa y alineado a los
texels, PCF por hardware (1/4/9 muestras), luz ambiental hemisférica, niebla por distancia, ACES. La calidad de las sombras se
elige según la GPU (baja para Mali/Adreno/PowerVR antiguas) y se puede cambiar en el menú de pausa.

## Multijugador

`engine/net` es el multijugador de cualquier juego del motor, sin código de juego. Por la red solo viajan órdenes: la
simulación es determinista, así que todos tienen el mismo estado.

- **Transporte:** UDP con capa de fiabilidad propia (iOS, Android, PC) y WebSocket (HTML5; en nativo, alternativa si el UDP
  está bloqueado). Dirección: `host[:27015]`, `udp://…`, `ws://host:27016`.
- **Lockstep con rollback.** El servidor confirma las entradas de cada tick cuando llegan las órdenes de todos los jugadores o
  vence el plazo (`lagToleranceMs`); las órdenes tardías pasan al tick siguiente: un jugador con retraso no detiene a los
  demás. El cliente no espera la confirmación: juega por delante con sus propias órdenes y, si hay discrepancia, restaura una
  instantánea y recalcula (una instantánea tarda ~10 µs). `rollbackWindow: 0` es lockstep clásico para dispositivos débiles.
- **El `aiverse_server`** no necesita el contenido del juego: salas (plazas, bots, espectadores, listo, comprobación de la
  versión de los datos), emparejamiento automático por colas de `server.json` (clasificación Elo con ventana creciente, mapa
  común, equilibrio de equipos, relleno con bots), comparación de sumas de verificación (desincronización), repeticiones
  (`aiverse_headless --replay` las reproduce), reconexión, espectadores con retardo.
- **Salir = rendirse.** Un jugador que se fue (o no volvió en `reconnectGraceSec`) se rinde con una orden en el flujo de
  entradas común: el resultado lo decide la simulación, igual para todos, y se ve en la repetición. Se descuenta clasificación.
- **Veredicto del servidor.** Si nadie confirmó el resultado (todos se fueron, los resultados no coinciden), el servidor
  reproduce la partida en un hilo de fondo con sus propios datos de juego (`"games"` en `server.json`) y decide el ganador.
- **Reconexión:** tras un corte de red, el cliente se reconecta solo (`NetClient(address, …)` o `setReconnect`), la simulación
  sigue viva: el servidor solo envía las entradas posteriores al último tick confirmado. Si el juego se reinició, el cliente
  recibe todo el historial y recalcula la partida desde el principio.
- **En el juego:** `NetClient` (`engine/net/NetClient.h`) se encarga de la sala y la partida. El juego construye una
  `Simulation` a partir de `match()` y llama a `attach(sim)`, luego a `advance(dt)` en lugar de `sim.advance(dt)`; la interfaz
  sigue enviando órdenes con `sim.submit()`.
- **Textos:** el servidor no envía frases hechas, solo claves de localización con argumentos (`NetText`): los jugadores de una
  misma partida pueden usar idiomas distintos. El cliente las traduce con `net::translate(loc, event.text)` y
  `net::displayName(...)` de `engine/net/NetLocalize.h`; las cadenas están en `engine/assets/lang/*.json` bajo `net.*`. Los
  nombres de las colas en `server.json` también son claves (`"@net.queue.1v1"`).

```bash
./build/mac/aiverse_server --config apps/server/server.json            # UDP 27015, WebSocket 27016
./build/mac/aiverse_netbot --server 127.0.0.1 --project templates/mushrooms --queue 1v1   # a bot in the queue
./build/mac/aiverse_netbot --server ws://127.0.0.1:27016 --project templates/mushrooms --queue 1v1
./build/mac/aiverse_net_tests                                          # network tests
docker build -f apps/server/Dockerfile -t aiverse-server .             # server in a container
```

Para `wss://` (una página de juego servida por HTTPS), pon un proxy TLS (nginx, Caddy) delante del servidor.

## Audio

`engine/audio` es un mezclador, bancos de sonidos a partir de datos, síntesis procedural y música; la salida va por
[miniaudio](../../third_party/miniaudio) (CoreAudio, AAudio/OpenSL ES, Web Audio, WASAPI, ALSA/PulseAudio).
`modules/rts/audio` convierte eventos de la simulación en sonidos; no escribe nada en la simulación. Sin dispositivo, el juego
funciona en silencio. Sin `audio.json`, el juego simplemente no tiene sonido.

El sonido de un juego es el archivo `audio.json` junto a `game.json` (otro nombre: `"audio"` en game.json). El formato
completo está en `docs/game-format.md`, sección audio.json; en resumen:

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

| Qué | Significado |
|---|---|
| formatos | WAV, OGG Vorbis, MP3, FLAC; `"synth"` — un sonido a partir de parámetros (`sine/square/saw/triangle/noise`, barrido de frecuencia, envolvente, vibrato, filtro, capas) |
| buses | `sfx` y `ambient` son posicionales (3D: atenuación, paneo, altura); `voice`, `ui`, `music` no |
| distancia | `spatial`: modelo de atenuación, alcances, «aire» (los agudos se pierden con la distancia), la reverberación crece con la distancia, velocidad del sonido; un sonido puede tener sus alcances y `far` — una variante lejana con fundido cruzado |
| `maxInstances`, `cooldown`, `priority` | aclarado en grandes batallas: 40 arqueros no producen 40 disparos idénticos |
| `groups` | límites comunes para varios sonidos (todo el cuerpo a cuerpo, todas las explosiones, un bus entero): `max`, `cooldown`, cuál descartar — `farthest` (por defecto: el combate lejano no corta lo que está en pantalla), `oldest`, `quietest`, `none` |
| eventos (cues) | `attack hit miss died spawned trainStarted ready buildPlaced buildDone gathered captured lost playerDefeated victory defeat notEnough notEnoughSupply noRoom researched levelUp ability readyToPlace sold regionClaimed wave lowPower upgraded message underAttack baseUnderAttack select move attackOrder buildOrder click error` |
| búsqueda del sonido | tipo → arma/habilidad por id → `events` |
| música | listas `menu`, `match`, `battle`, `victory`, `defeat` (y `match.<mapa>`); fundido cruzado, intro + bucles sin cortes, capas según la intensidad del combate, transiciones en tiempos/compases, golpes musicales, atenuación bajo la voz |
| compresión al compilar | `aiversebuild` recodifica el audio por plataforma: ADPCM para efectos, Vorbis para música, menor frecuencia de muestreo, mono; perfiles `desktop`/`mobile`/`web` en `audio.json` → `build`; `--audio off` — sin recodificar |

Los sonidos de combate solo se oyen donde no hay niebla de guerra; «nos atacan» llega con una pausa y solo si la cámara mira a
otro sitio. El volumen (general y música) está en el menú de pausa y se guarda en `userDataDir/audio.cfg`. En el teléfono, el
sonido se pausa cuando la aplicación pasa a segundo plano.

Depuración sin tarjeta de sonido: `AIVERSE_AUDIO=null` (un hilo de audio real sin hardware), `AIVERSE_AUDIO=off`,
`AIVERSE_AUDIO_CAPTURE=/tmp/mix.wav` — grabar todo lo que sonó en un WAV.

## Modelos, materiales, animación

Un tipo se dibuja según su bloque `"view"`: una forma integrada (`"shape"`) o un archivo de modelo (`"model"`) —
**glTF 2.0** (`.glb`, `.gltf` + `.bin` + imágenes) y **OBJ + MTL**. Todo pasa por una misma cadena:

- **Materiales PBR** (metallic-roughness): color base, normal, metallic-roughness, oclusión, emisivo; transparencia opaque /
  mask / blend, doble cara, unlit, variantes de material (`KHR_materials_variants`), sustituciones en `"materials"`. Color del
  jugador: en todo el material, por textura de máscara, por alfa de la textura (como en Warcraft III) o por alfa de vértices.
- **Animación esquelética** en la GPU (4 influencias, cualquier número de huesos), clips por estado de la unidad (`idle`,
  `move`, `attack`, `gather`, `build`, `die`, `spawn`, `hit`, `cast`…), transiciones suaves, capas con máscara de huesos
  (atacar con la parte superior del cuerpo mientras camina), cadencia del paso según la velocidad real.
- **Animación procedural** en cualquier nodo del modelo o forma: balanceo, giro (molinos, hélices), ruedas según la distancia
  recorrida, torreta apuntando al objetivo, retroceso, inclinación en las curvas, caída y hundimiento al morir y más; se activa
  por estados y eventos de combate.
- **Niveles de calidad** de los materiales (`low` / `medium` / `high`) se eligen según la GPU, como las sombras;
  `AIVERSE_MATERIALS=low|medium|high` los fuerza para pruebas.
- **Multitudes**: los clips se hornean en una textura y las unidades iguales se dibujan con instanciación (300 unidades
  animadas: 61 llamadas de dibujo en vez de 1801); LOD de modelos según la distancia; la animación fuera de pantalla y a lo
  lejos se actualiza con menos frecuencia.
- **Animación avanzada**: blend space caminar/correr, clips aditivos (respiración, retroceso), IK de pies sobre el terreno,
  giro de la cabeza hacia el objetivo, vehículos inclinados en las pendientes, morph targets (blend shapes) en la GPU.
- **Sockets**: un arma en la mano, carga a la espalda (según el recurso que lleva), puntos de salida de proyectiles, puntos de
  efectos (humo, fuego, escape); eventos de clips y `RtsView::events()/effects()` para el sonido y las partículas.
- **Etapas**: andamios y fases de construcción, variante dañada según la salud, disolución al aparecer y al morir, contorno de
  selección, fantasma translúcido al colocar un edificio, retratos.
- **Formatos**: también **FBX** (ufbx) y texturas comprimidas **KTX2** (ETC2/BC/ASTC, siguen comprimidas en la GPU);
  **Collada**, **3DS**, **PLY**, **STL**; vóxeles **MagicaVoxel .vox** y **.vxl/.hva** (Tiberian Sun, Red Alert 2); modelos de
  RTS clásicos desde la copia del usuario: **Warcraft III .mdx/.mdl** (+ BLP), **Total Annihilation .3do**,
  **C&C Generals .w3d** (+ DDS). Todo lo demás que lee assimp (.blend, .lwo, .x, .ms3d, .md2/.md3, .smd, .b3d…) lo convierte a
  glTF la utilidad **modelconv**: el editor lo hace solo al abrir y guardar, y el empaquetador solo mete el resultado en el
  paquete.
- **Materiales a demanda**: una biblioteca común `materials.json` (herencia con `base`), asignar un material a un nodo del
  modelo (`"assign": {"Turret": "steel"}`), texturas procedurales sin costuras con mapas de normales y máscaras de color del
  jugador; la ventana **Materiales** con vista previa y las operaciones MCP `material_*` / `texture_generate` para agentes.

Un modelo roto no rompe el juego: el tipo se dibuja como forma y el motivo va a la consola del editor. Visualización:
`aiverse_modelview model.glb@Walk` o `aiverse_modelview --game templates/showcase` (todos los tipos de un juego tal como los
dibuja el juego); en el editor, la ventana **Models** (clips, nodos, materiales, retrato, recarga al guardar el archivo del
modelo). Ejemplo: `templates/showcase`; formato completo: [models.md](../models.md).

## Iluminación

La luz dinámica está pensada para Android antiguos (Mali-T, Adreno 3xx/4xx): todas las luces se dibujan en un pequeño mapa de
luz sobre el suelo visible, y el shader toma la luz con una sola muestra de textura, haya las luces que haya. Niveles: `off`,
`low` (mapa de 128², actualización cada dos fotogramas), `medium` (256²), `high` (+ 8 luces por píxel con especular). Se elige
según la GPU, como las sombras; a mano: `AIVERSE_LIGHTS=…`. Si durante la partida los fotogramas no llegan de forma
sostenida, el cliente baja luces y sombras por escalones (primero lo más caro y menos visible) y sube un escalón tras una
batalla intensa; elegir las sombras a mano en el menú lo desactiva (`AIVERSE_ADAPTIVE=off`).

- **Luz de tipos y combate** — `view.light` (hogueras, forjas, setas luminosas), destellos de disparos, impactos y explosiones,
  luz de proyectiles (`attackLight`, `projectileLight`, `hitLight`, `deathLight`), luz de habilidades (`abilityLights`; por
  defecto el color sigue el efecto: daño — naranja, curación — verde), halos `halo` en lugar de un bloom caro. La niebla de
  guerra oculta los destellos.
- **Día y noche, clima** — el bloque `lighting` en `game.json` o en un mapa: `time`, `dayLength`, `nightLevel`, `clouds`,
  `wind`, `storm` (sombras de nubes y rayos, idénticos en las repeticiones), `preset`: `desert`, `snow`, `volcanic`,
  `overcast`, `cave` (subterráneo: noche eterna, luz solo de las fuentes). De noche los materiales emisivos brillan más y las
  luces se reflejan en el agua.
- **Luces del mapa** — el array `lights` del mapa (farolas, hogueras, lava).
- **Sombras de contacto y AO** — manchas suaves bajo las unidades y alrededor de los edificios, en todos los niveles
  (`contactShadow`, `contactShape` en el tipo).
- **Editor** — la ventana «Lighting»: vista previa de la hora del día y la tormenta, nivel de calidad de la luz, lista de
  fuentes. Para agentes: `light_add`, `light_update`, `light_delete`, `light_list`, `lighting_set`, `view_lighting`.

El formato de los campos está en la cabecera de `modules/rts/view/RtsLights.h`. Variables de prueba: `AIVERSE_TIME_OF_DAY`,
`AIVERSE_NIGHT`, `AIVERSE_CLOUDS`, `AIVERSE_STORM`.

## Comprobar mecánicas por MCP

Un agente que crea un juego sin código comprueba las mecánicas de forma puntual, no solo con una partida de bots.
- **`scenario_run`** — un escenario sobre una copia del mapa abierto (no se guarda nada): `clear` (un mapa vacío sin objetos,
  oleadas ni unidades iniciales), `spawn` con nombres (`"id": "foot", "count": 3` → `foot#1..3`, `"foot"` — todos), órdenes
  por segundos (las mismas claves que `play_command`), `bots` para algunas plazas, `checks` con resultado superado/fallido
  (`alive`, `dead`, `count`, `hp`, `resource`, `event`, `trigger`, `variable`, `distance`, `status`, `result` + comparación),
  `watch` + `every` (estado de objetos en el tiempo), `events` (registro de eventos).
- **`play_units` / `play_command`** — órdenes en una prueba en vivo de la ventana Game (antes solo dentro del editor, ahora
  visibles para los clientes MCP). Referencias a objetos: ids de `play_units`, nombres `id` del mapa o un filtro
  `{type, owner, tags, kind, area, near + radius}`; `queue: true` — cola (Mayús), `ability` por id.
- **`match_run`** — además, un registro de los disparadores activados y `events` (registro de eventos por nombre).
- **`rules_lint`** — claves de `rules.json` y del mapa que el cargador no lee (erratas, bloque fuera de lugar), con sugerencia
  («did you mean 'armor'?»). Funciona sobre el cargador real (`JsonAccessLog` en `engine/data/Json.h`); `rules_set`,
  `rules_merge` y `map_validate` informan de esas claves al momento.
- **`rules_schema`** — `part: "keys"`: cada clave que el cargador pide en cada bloque (`types.*.weapons[]`), reunidas de todos
  los juegos, y las claves que faltan en `docs/game-format.md`; `part: "doc"` — el propio documento.
- El evento de simulación `kEvTriggerFired` (solo para herramientas y presentación, no entra en la suma de verificación).
- Rama `agent-mech-tools`.

## Posefectos

Los efectos a pantalla completa sobre la escena 3D se definen con datos: el bloque `"postfx"` en `game.json` (un mapa puede
sustituirlo). Hay bloom, corrección de color, LUT, viñeta, tilt-shift, rayos de sol, FXAA, grano, aberración, nitidez,
pixelado, posterización, tinte y bandas de cine, además de conjuntos listos (cine, vivo, noir, retro, miniatura, atardecer,
invierno, visión nocturna). Cada efecto tiene `minQuality`. El nivel se elige según la GPU: los Android débiles reciben «bajo»,
los teléfonos «medio», los PC «alto»; `AIVERSE_POSTFX` lo sustituye. Sin efectos, el posprocesado no cuesta nada. Los efectos
ligeros se agrupan en una pasada; los pesados (bloom, FXAA, rayos) añaden la suya.

Editor: la ventana **Posefectos** (vista previa de niveles, tabla del coste por fotograma según la clase de dispositivo).
MCP: `postfx_catalog/get/add/set/remove/preset/enable/estimate/lut_neutral`, `view_postfx`. Las respuestas a los cambios incluyen
una estimación en ms/fotograma y avisos sobre efectos pesados. Detalles: `docs/postfx.md`.

## Verificado / no verificado

- ✅ Linux: cliente, editor, pruebas; .exe de Windows (compilación cruzada con MinGW, ejecutado en Wine).
- ✅ macOS: compilación, pruebas, juego en Metal; aiversebuild → .app y un proyecto de iOS (+ build para el simulador).
- ✅ Web: compila (Emscripten 3.1.50), una partida se ejecuta en Chromium headless (WebGL2/SwiftShader).
- ⏳ Android: el proyecto Gradle se genera; el APK y la ejecución en el emulador están en la CI (job `android`), no se compiló en local.
- ⏳ Windows en hardware real e iOS en dispositivo — no ejecutados (en la CI: build con MSVC y build de iOS para el simulador).

## Plataformas y CI

`.github/workflows/ci.yml` en cada push a `master`/`agent-*` y en las pull requests:

| Job | Qué hace |
|---|---|
| `linux` | gcc y clang (x64) + gcc (ARM64), advertencias como errores, pruebas, partidas de bots, capturas con Xvfb |
| `windows-msvc` | Visual Studio 2022, pruebas, partidas, `aiversebuild --target windows` |
| `windows-mingw` | compilación cruzada con MinGW-w64, pruebas y partidas en Wine |
| `macos` | build universal de Xcode, pruebas, partidas, `aiversebuild` → .app y un proyecto de iOS con build para el simulador |
| `android` | `.so` con el NDK, APK con `aiversebuild`; en el emulador: inicio → toque → segundo plano → vuelta → «atrás» → reinicio, logcat revisado en busca de cierres |
| `web` | Emscripten, una partida en Chromium headless, comprobación de la consola y del fotograma |
| `determinism` | compara las sumas de verificación de partidas (ambos juegos × 3 seeds) de todas las plataformas: deben coincidir bit a bit |

Las capturas, el APK, el build web y la .app se guardan como artefactos de la ejecución.

En local:

```bash
# Windows .exe from Linux/macOS
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64.cmake
# bot matches on every game → a checksum file (compare between machines with diff)
.github/scripts/headless-checksums.sh build/linux-release/aiverse_headless sums.txt
# smoke test of the web build
node platform/web/smoke.mjs build/web
```

En el build web, los parámetros de línea de órdenes se pasan por la URL: `index.html?map=0&spectate` = `--map 0 --spectate`.

Teclado en pantalla: `platform::startTextInput(area)` / `stopTextInput()` lo abren y cierran en iOS, Android y navegadores
móviles (en escritorio no hacen nada). El texto llega como eventos `Char` en cualquier idioma, Retroceso/Intro como
`KeyDown`/`KeyUp`; `softKeyboardHeight()` y el evento `KeyboardChanged` indican cuánta pantalla queda tapada abajo, y
`TextInputEnded`, que el usuario cerró el teclado. La interfaz (`UiLayer`) lo llama sola al enfocar
`<input type="text|password|search|email|number">` y `<textarea>`.

Otras funciones para móviles y navegador en `Platform.h`:
- `safeAreaInsets()` — márgenes para el recorte de la cámara, las esquinas redondeadas, el indicador de inicio y las barras
  del sistema (iOS, Android, Safari/Chrome móviles; 0 en escritorio, en un MacBook con notch — en pantalla completa).
- `setFullscreen()` / `isFullscreen()`; atajos integrados — F11 (Windows, Linux, web), Alt+Intro (Windows). En el navegador,
  la pantalla completa también fija la orientación (`AppConfig::orientation`).
- Android: el botón Atrás llega al juego como `Key::Escape`; pantalla completa con barras que se ocultan; teclado y ratón
  físicos (Chromebook, DeX) dan eventos normales de teclas y ratón.
- iPad: Magic Keyboard / teclado Bluetooth, ratón y trackpad (clic derecho, hover, desplazamiento).
- El portapapeles funciona en todas partes; en el navegador Ctrl/Cmd+V llega al juego después de que el navegador entregue el texto.
- Web: `userDataDir()` se guarda en IndexedDB: los ajustes y las partidas guardadas sobreviven a recargar la página.

### Plugins nativos: pagos y anuncios

Cualquier SDK de plataforma se conecta como plugin en su lenguaje nativo —Java (Android), Objective-C (iOS/macOS), JavaScript
(web) o C++— mediante un único puente asíncrono, `engine/platform/Plugins.h`. Un juego usa las API comunes `aiverse::store`
(compras) y `aiverse::ads` (anuncios) de `engine/services`, y el proveedor se elige por plataforma en `game.json`. Plugins
listos en `plugins/`: **App Store**, **Google Play**, **RuStore**, **Yandex Games** (web: compras y anuncios), **Steam**
(compras mediante Inventory Service y DLC, Windows/Linux/macOS), **Yandex Mobile Ads**, **AdMob**, **AppLovin MAX**,
**ironSource (Unity LevelPlay)**. Un build solo lleva los plugins que elige el juego (`"plugins"` en game.json o `--plugins`):
sin SDK, dependencias, entradas de manifiesto ni claves de Info.plist ajenos; la CI lo comprueba. Para depurar en escritorio
hay simulaciones de tienda y anuncios (`mock-store`, `mock-ads`; `AIVERSE_MOCK_STORE`, `AIVERSE_MOCK_ADS`): todo el flujo de
compra funciona sin teléfono, y las simulaciones no se compilan en los builds de release. aiversebuild inscribe por sí mismo
los plugins en Gradle/el manifiesto, Info.plist/Podfile y el HTML; compila Google Play y RuStore por separado:
`--plugins googleplay,admob` y `--plugins rustore,yandex-ads`. Detalles: [plugins.md](../plugins.md).

### Versiones

`.github/workflows/release.yml`: una etiqueta `v1.2.3` (o un lanzamiento manual con la carpeta de juego que elijas) compila
todas las plataformas con `aiversebuild` y publica una GitHub Release:

| Plataforma | Resultado | Firma (secretos del repositorio) |
|---|---|---|
| Windows | instalador (Inno Setup, sin permisos de administrador) + .zip portable | `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` |
| Android | .apk y .aab para Google Play | `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` |
| macOS | .app en un .zip, notarizada | `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD` (Developer ID) + una clave de App Store Connect |
| iOS | proyecto de Xcode; con clave, un build para TestFlight | `APPLE_TEAM_ID`, `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_P8_BASE64` |
| Linux | .tar.gz | — |
| Web | .zip; con `DEPLOY_WEB_PAGES=true` (variable del repositorio), publicación en GitHub Pages | — |

Sin secretos la release sale igualmente: el APK se firma con una clave de depuración, la .app en ad-hoc y Windows sin firma.

### Motor precompilado (rama `builds`) y aiversemcp sin compilador

El editor y las herramientas precompilados se publican en la rama **`builds`**:
`<platform>/AIVERSE-<version>-<commit>-<platform>.zip` (ahora `windows` x86_64 y `macos` arm64; `linux` x86_64 cuando lo publique
el integrador). Dentro hay una carpeta `AIVERSE/` con las fuentes del motor en ese commit (plantillas `templates/`,
`engine/assets`, `docs/` — las lee el servidor MCP) y `AIVERSE/bin/` con los programas. Lo empaqueta
`tools/release/package_engine.py` y lo publica `tools/release/publish_builds.py` (AGENTS.md §8); el paquete de Linux puede venir
sin editor, solo `aiversemcp` y `aiverse_headless`.

El servidor MCP desde la carpeta descomprimida (las partidas de bots y `balance_*` se ejecutan dentro de aiversemcp, no hace
falta un `aiverse_headless` aparte; el editor es opcional, sin él usa `--no-editor`):

```bash
# ARCHIVE is the path to the AIVERSE archive downloaded from index.json.
unzip "$ARCHIVE" -d ~/aiverse      # unzip keeps the executable bit
~/aiverse/AIVERSE/bin/aiversemcp --no-editor --engine ~/aiverse/AIVERSE
```

`--engine` se puede omitir en builds de commits en los que aiversemcp busca el motor junto a sí mismo
(`engine/platform/EngineDir.h`, como el editor); en builds publicados antes es obligatorio. Con descompresores que no
restauran los permisos (Python `zipfile`), ejecuta `chmod +x` sobre los archivos de `bin/`. En macOS, un archivo descargado por
un navegador puede quedar en cuarentena: `xattr -dr com.apple.quarantine ~/aiverse/AIVERSE`.

Los programas que descargan el motor por su cuenta leen **`index.json`** en la raíz de la rama
(`https://raw.githubusercontent.com/chromealex/AIVERSE/builds/index.json`), no el README. Abajo hay un ejemplo de una entrada
histórica de RTSEngine; en los builds nuevos las rutas contienen AIVERSE y la clave `rtsmcp` se mantiene:

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

- `packages` va de más nuevo a más antiguo; la primera entrada con el `platform` + `arch` deseados es el build más reciente.
- `platform`: `windows` | `macos` | `linux`; `arch`: `x86_64` | `arm64` | `universal` (macOS fat) | `null` (no reconocido) —
  se determina por la cabecera de `aiversemcp` dentro del zip.
- `rtsmcp`, `editor`, `headless` son rutas dentro del archivo, o `null` si el programa no está; `--engine` es la carpeta
  `root`. En macOS, `editor` es un bundle `.app`.
- `url` apunta a una rama que se reescribe en cada publicación (se guardan 5 versiones por plataforma): las entradas antiguas
  desaparecen, así que descarga a partir de un `index.json` reciente y comprueba `sha256`.
- Pueden añadirse campos nuevos; un cambio de formato incompatible aumentará `schema`.

Comprobación: `python3 tools/release/test_release.py [build/linux-release/aiversemcp]` (CTest `release_tools`): el formato de
`index.json` y, con una ruta a aiversemcp, el empaquetado de un paquete de Linux, su descompresión en una carpeta temporal y
llamadas MCP desde allí sin `--engine` (`tools/list`, `create_project`, `map_generate`, `map_validate`, `map_ascii`).

El modo de captura (`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`) avanza exactamente 1/60 s por fotograma, así que el
fotograma N es el mismo en cualquier máquina.

## Próximos pasos

Modelos glTF y animación, audio, red (lockstep), habilidades y mejoras desde datos, flow fields y RVO para grandes ejércitos,
mejoras de la IA, scripting en C# (AOT para iOS).

## Módulos de juego en C++

Además de Lua, un proyecto puede contener `native/CMakeLists.txt` y módulos de C++20. Se añaden con
`aiverse_native_module(...)` al compilar y se activan con el array `nativeModules` de `rules.json`. Se admiten ticks, órdenes,
rollback y guardado del estado; Lua y C++ trabajan juntos. Tras cambiar el C++ hay que recompilar el juego y el editor.
[API, ejemplo e instrucciones de compilación](../native-modules.md).

## Biblioteca de meta reutilizable en C++

`aiverse::meta`: tienda/ofertas/fabricación configurables, monedas y objetos, misiones y pases, recompensas diarias, energía,
correo, clasificaciones, chats y segmentos. Disponible para el código nativo del proyecto sin Lua/GPU; las definiciones son
JSON, no hace falta volver a generar los algoritmos. Inicio: `python3 tools/meta/scaffold.py --preset mobile --out <project>`.
Documentación: [meta-game.md](../meta-game.md); para agentes, `skills/meta-game/SKILL.md`. Las políticas locales funcionan sin
servidor; la comunicación entre dispositivos y las clasificaciones globales requieren un backend real. Un servicio en la nube
listo no forma parte del paquete.

El preset `full --with-ui` añade amigos/clanes, una colección de instancias, equipamiento, fusión, suscripciones y ligas de
torneo. `aiverse::meta::Service` ejecuta las operaciones de forma atómica; `tools/meta/server.py` es un host HTTP/SQLite local
con comprobación de sesiones. Se incluyen pantallas de meta para navegador y un catálogo de recetas breves para agentes.
[Compilación, ejecución y límites](../meta-service.md).

`full` incluye también 15 grupos de LiveOps: cofres con garantía, álbumes/intercambios, colas de trabajos,
investigación/prestigio, misiones compuestas, eventos, guerras de clanes, PvP asíncrono, emparejamiento de grupos,
Elo/Glicko-2, referidos/códigos promocionales, anuncios con recompensa, cadenas de ofertas, notificaciones e ingresos sin
conexión. Todos los parámetros están en `extensions.liveops`; este preset no instala nuevas pantallas de interfaz separadas ni
SDK externos. `python3 tools/meta/catalog.py describe matchmaking` muestra solo el esquema y el ejemplo necesarios.
[API, políticas y límites](../meta-liveops.md).

### Física 3D opcional

Box3D se activa con la opción `AIVERSE_ENABLE_PHYSICS=ON`; los proyectos de RTS normales no la necesitan. Un ejemplo
separado, `aiverse_physics_demo`, muestra cuerpos, colisiones, un péndulo y el guardado del estado.
[Compilación, API y límites de integración](../physics.md).

### Tela XPBD visual

El módulo opcional `AIVERSE_ENABLE_CLOTH=ON`: su propio solucionador de tela XPBD en CPU o en Metal compute, lotes
compartidos, LOD temporal, reposo automático y dibujo de mallas densas con un único draw instanciado. En la GPU las posiciones
se quedan en la memoria de vídeo; los backends no admitidos usan la CPU. El ejemplo puede comparar la GPU con una referencia
en CPU y comprobar el reposo/despertar. [Ejemplo independiente, límites y mediciones](../cloth.md).
