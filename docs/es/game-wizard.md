# Asistente «Crear un juego»

Una ventana (centro de proyectos → **Crear un juego**, o **Proyecto → Crear un juego…**) o una llamada MCP (`game_make`) crea un
proyecto de juego completo con su `.rtsproj`: el juego de una plantilla elegida según la idea, el arte / modelos / música /
sonidos del usuario, un mapa de tutorial guiado, consejos discretos en las partidas, niveles generados, el metajuego (menús,
mejoras, tienda, recompensa diaria, perfil, ajustes, resultado), compras y anuncios si se piden, idiomas, identidad visual y
una comprobación con bots. Todo lo que escribe son datos de juego normales que luego se pueden editar.

Código: `apps/editor/core/GameWizard*.cpp` (sin UI), `apps/editor/core/WizardOps.cpp` (MCP), `apps/editor/EditorGameWizard.cpp`
(la página del centro de proyectos). Kit de ejecución que se copia a cada juego: `templates/_wizard/` (Lua + RmlUi, ver su README).

## Respuestas (`WizardSpec`, todas opcionales)

| Grupo | Claves |
|---|---|
| Contenido | `artDir` (carpeta: png/jpg/tga, glb/gltf/obj/fbx, wav/ogg/mp3/flac, ttf/otf, md/txt), `artUrl` (un archivo o un .zip / .tar.gz; se descarga con el curl del sistema a `~/.aiverse/wizard/<hora>`) |
| Idea | `idea` (cualquier idioma; también se leen los .md/.txt de la carpeta), `genre` (id de plantilla o `auto`), `features` (economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait scifi modern) |
| Proyecto | `title`, `location`, `studio` (bundle id `com.<studio>.<slug>`), `version`, `orientation`, `languages`, `platforms`, `seed` |
| Tutorial | `tutorial` (`both` / `hard` / `soft` / `none`), `tutorialForced`, `tutorialSkippable`, `tutorialDim`, `hints` |
| Niveles | `levels` (0..40), `mapPreset` (`auto`, `none` = copias del mapa base, o un preset del generador), `mapSize`, `bossEvery`, `difficultyCurve`, `startResourcesPercent`, `keepTemplateMaps`, `verifyLevels` |
| Meta | `metaIdea` (texto libre: qué debe tener la meta; va a la tarea del agente), `currencies` (soft, hard), `upgrades`, `shop`, `daily`, `profileScreen`, `xpPerLevel` |
| Dinero | `ads`, `interstitialEvery`, `purchases`, `stores` |
| Aspecto | `brandingFromArt`, `paletteFromArt` |

`game_make_options` lista las opciones, `game_make_plan` muestra qué se crearía (sin archivos). Mismas respuestas + seed =
mismo juego.

## Cómo decide

- **Plantilla**: las palabras de la idea se comparan con las palabras clave de cada plantilla (raíces, ru / en), las
  funciones deseadas con sus mecánicas, la orientación con su pantalla; en caso de empate decide el seed. Sin datos: una de
  las plantillas equilibradas.
- **Contenido**: los modelos y sprites 2D van a los tipos cuyo id / nombre (ru / en) aparece en el nombre del archivo; el
  resto se copia y se lista en `WIZARD.md`. El arte principal (por nombre y tamaño) se convierte en la pantalla de carga, el
  fondo del menú y el color de acento de la interfaz; la imagen más cuadrada (o «icon» / «logo») en el icono de 1024. La
  música, por su nombre, va a `menu` / `match` / `battle` / `victory` / `defeat`; los sonidos cortos, a eventos (`click`,
  `select`, `move`, `died`, `underAttack`, ...). La primera fuente TrueType se convierte en `fonts/title.ttf`.
- **Tutorial guiado** (`maps/wiz_tutorial.json` + `scripts/wizard/tutorial_steps.lua`), escrito a partir de las reglas:
  - *economía* (obreros, astilleros de barra lateral, MCV desplegables, constructores de TA): desplegar, recolectar, campo
    warp / suministro si hace falta, la cadena de requisitos de un productor tipo cuartel, entrenar dos unidades, arrasar un
    edificio enemigo que no dispara;
  - *guarnición* (Mushroom Wars, State.io): enviar a un edificio neutral, tomar dos más, tomar uno enemigo;
  - *construcción automática* (lane battlers): pulsar Construir dos veces, mejorar, resistir;
  - *genérico*: mover el ejército, atacar.
  Solo pasan las órdenes del paso (`before_command`), el enemigo espera (sus unidades se defienden igualmente), el panel y la
  flecha los dibuja el script de vista, los recursos del paso se reponen para que no haya atascos. No es para economías de
  caminos al estilo de Knights and Merchants (allí, la forma genérica).
- **Tutorial discreto**: consejos de una sola vez (cámara, obreros inactivos, recursos insuficientes, límite de suministro,
  ejército, guarniciones, ataque) desde `scripts/wizard/hints_config.lua`; los consejos vistos se recuerdan en el perfil y
  los ajustes los desactivan.
- **Niveles**: `maps/wiz_level_N.json` = el mapa base + `generate` (el preset de la plantilla, un seed por nivel, el mapa base
  como plantilla); dificultad 0..2 según la curva, cada N niveles un jefe; recompensas, recompensas por la primera victoria y
  tiempos de estrellas en `ui/wizard.json`.
- **Meta**: `ui/wizard.json` (configuración), `ui/profile.json`, `ui/shop.json`, `ui/screens.json` (`start: home`,
  `afterMatch: result`), pantallas del kit. Las mejoras llegan a la partida como `initialVars` `wiz_up_<id>` y
  `scripts/wizard/meta_bonus.lua` las convierte en bonificaciones / recursos iniciales.
- **Dinero**: `plugins` / `store` / `ads` en `game.json` por plataforma (bloques de anuncios de demostración de Yandex y
  simulaciones de escritorio: sustituye los ids).

## Paso del agente: el juego descrito, uno a uno

La plantilla es un punto de partida, no el juego. Con `agent` activado (por defecto), el asistente escribe `WIZARD_TASK.md` en
el proyecto —la descripción / GDD literal más cómo mantener coherentes las partes del asistente (pasos del tutorial, consejos,
configuración de la meta, archivos de idioma)— y se lo entrega a un agente de IA conectado, que reconstruye el proyecto con
las herramientas MCP del editor (en vivo en el editor): tipos, estadísticas, facciones, mecánicas (reglas o Lua), mapas,
condiciones de victoria, textos; después comprueba con `rules_lint`, `map_validate`, `match_run` y escribe
«## Агент: что сделано» en WIZARD.md.

- Conexión: el asistente muestra el mismo panel «conectar un agente de IA» que la ventana Agent (`EditorMcpConnect.*`,
  `core/McpInstall.h`): un clic escribe aiversemcp en la configuración del cliente, y esa única conexión sirve para ambos.
- Lanzamiento desde el editor (`core/WizardAgent.*`, mediante el shell de inicio de sesión del usuario para que se encuentre
  la CLI): Claude Code `claude -p --output-format stream-json --allowedTools mcp__aiverse` y Codex CLI
  `codex exec --full-auto`, la tarea por stdin, la carpeta del proyecto como directorio de trabajo. La CLI se busca en el PATH,
  en las carpetas de instalación habituales, en el shell del usuario y dentro de las aplicaciones de escritorio (Claude Code en
  `~/Library/Application Support/Claude/claude-code/<version>/…/claude.app`, Codex en `ChatGPT.app/Contents/Resources/codex-cli`).
  El progreso aparece en el registro de actividad (ventana Agent), la barra de estado y la página del asistente (botón
  Detener); el registro completo en `logs/wizard-agent-<hora>.log`.
- Cursor, Windsurf, Claude Desktop: «Copiar la tarea para el agente» pone la tarea en el portapapeles para pegarla en el cliente.
- Sin clave de API en el editor: el agente funciona en el cliente y con la cuenta del propio usuario.
- Dictado: «Dictar» bajo el campo de la idea (página Idea) y el de ideas de meta (página Meta) — reconocimiento de voz de macOS
  (`apps/editor/EditorSpeech_mac.mm`: Speech + AVFoundation, el primer idioma del juego como configuración regional; el
  sistema pide una vez acceso al micrófono y al reconocimiento). El texto se añade al campo. Otras plataformas: botón desactivado.
- `ai` en la especificación (textos por idioma: eslogan, introducción, tipos, recursos, monedas, mejoras, niveles) permite a
  un agente que llama él mismo a `game_make` pasar los nombres y textos del juego.

El tutorial en la vista Game del editor: el editor no tiene interfaz de juego para mostrar el panel de pasos, así que el script
de vista envía `wiz_tut_off` y allí el mapa del tutorial se juega como una partida normal. El panel, la flecha y el filtro
funcionan en el cliente del juego (AiverseGame, builds). El editor abre el primer nivel, no el tutorial.

## Comprobación

Con `verifyLevels` el asistente juega solo el tutorial (`initialVars {wiz_tutorial_auto: 1}`: la orden `auto` de cada paso
pasa por el mismo filtro que la del jugador) y una muestra de niveles con bots, e informa de errores de carga, errores de
scripts y de un tutorial que no terminó. Probado con todas las plantillas: todo termina salvo ambersands (construcción solo
sobre roca), planets y underlord (movimiento / excavación en terreno especial); el asistente lo indica en sus advertencias.

Pruebas: `game_wizard_plan_picks_a_template_by_the_idea`, `game_wizard_makes_a_playable_project`,
`script_command_card_false_hides_the_button`.

## Cambio en el motor

Las órdenes de script aceptan `"card": false` (sin botón / atajo; las envían scripts de vista, documentos del HUD, MCP): el
Siguiente / Saltar del tutorial (`modules/rts/RtsScriptCommands.cpp`, tarjetas de órdenes del cliente y del editor).
