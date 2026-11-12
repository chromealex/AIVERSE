# Importar un proyecto de otro motor

La implementación está en `agent-engine-import`; la especificación de origen es
[engine-import-spec.md](../engine-import-spec.md). Las sumas de verificación de la simulación y de las plantillas no cambian.
Es una base de importación funcional, **no el cumplimiento de todas las etapas de la especificación**. La cobertura y el
trabajo pendiente de abajo forman parte del traspaso a MAIN.

## CLI y datos intermedios

Python 3.8+; no hacen falta paquetes de Python. El lanzador ejecutable es `tools/importers/port/aiverse_port` (o la forma
explícita con Python de abajo):

```sh
python3 tools/importers/port/aiverse_port.py scan /path/to/UnityProject
python3 tools/importers/port/aiverse_port.py extract /path/to/UnityProject --out /tmp/port.json
python3 tools/importers/port/aiverse_port.py emit /tmp/port.json --out /path/to/Imported
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported --scale 1 --mode auto
python3 tools/importers/port/aiverse_port.py report /path/to/Imported
python3 tools/importers/port/aiverse_port.py item /path/to/Imported --id ITEM_ID
python3 tools/importers/port/aiverse_port.py resolve /path/to/Imported --id ITEM_ID --how lua --reason 'Verified by scenario…'
```

Cada orden imprime JSON; si falla, sale con estado 2. `scan` no escribe nada. Las carpetas de entrada y salida no deben
solaparse. No ejecutes importaciones simultáneas al mismo destino. `extract` registra rutas de origen, componentes
serializados completos, campos, código fuente de los scripts y su uso. `emit` escribe un manifiesto del juego, reglas, mapas,
`.rtsproj`, recursos copiados, opcionalmente sprites/tiles/audio/efectos, `port.json` e informes JSON/Markdown. `sourceRoot`
debe seguir existiendo al escribir los recursos copiados. El `.rpakignore` generado excluye de los paquetes de ejecución el
índice intermedio de scripts y los informes. El manifiesto marca el contenido importado con `external: true`, y aiversebuild
lo propaga a los paquetes de recursos.

`--scale` son celdas por metro; `--mode auto|2d|3d` elige el plano de la escena y la cámara. Unity 3D invierte Z; Unity 2D
proyecta XY sobre el plano del suelo. El Vector2 de Godot se proyecta en XZ. Los orígenes se desplazan para que las posiciones
del mapa sean positivas. No se inventan propietarios: solo se traslada un campo `owner` explícito de Unity, con un aviso de
aproximación. Hay que comprobar la numeración de jugadores del origen.

`lib/port/mapping.json` define alias de campos. `--mapping FILE` añade o sustituye alias para los destinos admitidos.
`fireRate` se interpreta como disparos por segundo (inverso del enfriamiento) y se marca explícitamente como aproximación. Los
tiempos de enfriamiento y de construcción del JSON del motor están en **segundos**, no en ticks de simulación. Cada campo
deducido tiene procedencia, confianza y ruta de destino.

### Importaciones repetidas

`.port-state.json` guarda los hashes de los archivos/tipos generados. Los archivos editados o ya existentes se conservan,
incluso cuando un editor mantuvo el marcador `ported`. Las reglas se fusionan por tipo; los tipos editados pierden ese marcador.
Se pueden seguir añadiendo tipos nuevos. Los cambios del usuario en los ajustes de nivel de reglas se conservan. Los mapas y
los archivos binarios se protegen completos. Los archivos generados obsoletos no se borran. El manifiesto de estado pertenece
al importador; no lo edites.

Las anotaciones de resolución sobreviven a las reimportaciones solo mientras el elemento y la huella de la entidad/script de
origen correspondiente no cambien. Un origen modificado vuelve a abrir el elemento para revisión.

## Editor y MCP

Abre **Proyecto → Importar proyecto de motor** o `editor_window {id:"project_import", open:true}`. Analiza el origen, elige
destino/escala/2D/3D e importa. La interfaz ejecuta el importador fuera del hilo de renderizado y puede abrir el resultado.
**Copiar traspaso para IA** copia una tarea explícita y abre la ventana Agent; no afirma que se haya lanzado un agente.

La rama base actual no tiene el asistente «Crear un juego». La integración con la rama separada `agent-gamewizard` queda para
MAIN; no se fusionaron ramas ajenas.

Operaciones registradas (los ajustes de importación también pueden anidarse en `options`):

- `port_scan {path}`: detección del origen, comprobación de la serialización, listas de recursos/escenas/scripts.
- `port_import {path,out,scale?,mode?,mapping?}`: ejecutar la extracción y la escritura.
- `port_report {path?}`: leer el informe del destino (por defecto, el proyecto abierto).
- `port_item {path?,id}`: elemento de origen, componentes, scripts enlazados y tipos generados.
- `port_resolve {path?,id,how,reason}`: anotar trabajo verificado (`rules`, `lua`, `native`, `dropped`).

La importación por MCP es síncrona, como el importador VFX existente; los proyectos grandes pueden tardar. `AIVERSE_PYTHON`
elige el intérprete. Los metacaracteres del shell de Windows `%`, `!`, comillas y saltos de línea en los argumentos se rechazan,
no se interpolan.

Flujo del agente: revisar los elementos que faltan, consultar `rules_schema`, implementar las mecánicas adecuadas como datos,
luego Lua y luego un módulo nativo si hace falta. Usar `scenario_run` y partidas de bots para comparar DPS, tiempo de
producción y velocidad de movimiento. Comparar capturas si se aporta la imagen de origen. Resolver solo el trabajo verificado;
explicar el comportamiento descartado a propósito. C#/GDScript/Blueprint no se traducen mecánicamente ni se ejecutan.

## Cobertura y trabajo pendiente de la especificación

| Área | Implementado | Pendiente |
|---|---|---|
| Código común de Unity | YAML, búsqueda de GUID, prefabs anidados y variantes movidos a `lib/unity`; importaciones de compatibilidad mantienen VFX funcionando | Siguen vigentes los límites de anidamiento del analizador común |
| Escenas/tipos de Unity | Transformaciones de mundo, objetos de escena, sustituciones de prefabs, uso de scripts y campos serializados, candidatos ScriptableObject, orden de escenas de build | Incluir la escala de las transformaciones en los límites del modelo, agrupación semántica robusta de tipos, deducción de jugadores/inicios |
| Recursos/materiales | Modelos/texturas/audio y dependencias vecinas conservan rutas; factores PBR y primera sustitución de material; metadatos de escala del modelo; opcionalmente PSD/TIFF → PNG con el adaptador ImageMagick/ffmpeg existente | Correspondencia de ranuras de materiales, mapas empaquetados, ajustes de ejes al importar, corte de fotogramas de animación; se informa si falta el conversor de imágenes |
| Unity 2D | Recortes de sprites del atlas, celdas explícitas de Tilemap horneadas en el atlas/capa fija, cámara ortográfica | RuleTiles, tiles transformados/teñidos, disposiciones Grid arbitrarias, tiles animados, pivotes/orden de sprites |
| Audio/FX | Audio en bucle de cámara/gestor → lista de reproducción de la partida; el conversor VFX existente escribe mediante una zona de preparación protegida | Enrutado del mezclador y vinculación semántica de efectos a eventos |
| Terreno | Los datos horneados opacos y los componentes Terrain se informan | No está implementada la conversión de alturas/splat/árboles/detalles de TerrainData |
| Godot | Detección de escenas/recursos de texto, recursos externos, traslaciones simples, sustituciones numéricas exportadas, índice de scripts, orden de la escena principal | Herencia de escenas, composición completa de transformaciones, decodificación de Variant complejos multilínea, conversión de TileMap/GridMap, clips de animación |
| Unreal | Receptor JSON y exportador Python de editor para actores cargados, propiedades candidatas por reflexión, mallas/texturas/audio mediante AssetExportTask | El exportador **no se ha ejecutado en Unreal**; paisaje, enumeración completa de variables de Blueprint y exportación de grafos sin implementar |
| Editor | Ventana de importación independiente y localizada, entrada de menú, MCP y traspaso para IA copiable | Integración con el asistente, selectores de categorías de recursos, lanzamiento directo del agente |

Pueden existir binarios `.asset` nativos desconocidos de Unity incluso con Force Text (iluminación/NavMesh horneados). Se
informan uno por uno. La serialización binaria de escenas/prefabs/materiales se rechaza con la instrucción de activar Force
Text. `.uasset` nunca se analiza fuera de Unreal.

La herramienta para Unreal es `tools/importers/unreal_export/export.py`; llama a `export_project(destination)` dentro de Unreal
Editor con el plugin de Python activado y una carpeta de salida nueva. Referencias de su API:
[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6),
[EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6).
Exporta el nivel cargado en ese momento y los recursos de `/Game`; no recorre niveles no cargados.

## Verificación, 2026-11-03

Los proyectos sintéticos de `tests/data/port` son datos de prueba originales. El FBX de Unreal es el
`tests/data/models/cube.fbx` CC0 existente, escrito a mano. No se han commiteado recursos de juegos descargados.

- 34 pruebas existentes del importador VFX pasan tras mover el código común.
- 15 pruebas del importador de proyectos cubren el rechazo de serialización, sustituciones de prefabs, unidades, ediciones
  protegidas, idempotencia (incluida la reescritura de `port.json`), invalidación al cambiar el origen, seguridad de rutas,
  sprites/tiles y resolución por CLI. Con `AIVERSE_PORT_HEADLESS`, los cinco proyectos se cargan en el motor real y el proyecto
  de combate de Unity termina con la victoria del jugador 1.
- Suite de C++: 5857 comprobaciones, un fallo de un listener de localhost propio del sandbox; esa prueba pasó por separado
  fuera del sandbox de red (27 comprobaciones). Pasó una prueba adicional del puente MCP (5 comprobaciones).
- Advertencia previa del compilador ajena a este cambio: `fb` sin usar en `engine/fx/FxEffect.cpp:1058`.

Se descargaron dos repositorios públicos de RTS solo en `/tmp` y se importaron; **cada mapa generado se cargó por separado con
`aiverse_headless --max-seconds 0`**, no solo el primero/por defecto:

| Origen | Revisión | Resultado | Informe |
|---|---|---|---|
| [Unity FPS RTS](https://github.com/aydenspringer/Unity-FPS-RTS-System) | `df468f644e6aff145d2eb219dca87f99742ca81d` | 509 recursos copiados, 375 candidatos a tipo, 30 mapas; cargan 30/30 | 486 aproximados, 1913 ausentes; requieren revisión scripts, componentes de renderizado/física, datos horneados opacos y recursos no admitidos |
| [Godot Open RTS](https://github.com/lampe-games/godot-open-rts) | `a628ad3bb6a9a903587a181a7ee3300dbf735e81` | 388 recursos copiados, 552 candidatos a tipo, 84 mapas; cargan 84/84 | 638 aproximados, 2017 ausentes; requieren revisión la lógica de scripts, la herencia de escenas, los recursos incrustados y los sistemas de tiles/animación |

Son comprobaciones de importación/carga, **no ports jugables uno a uno**. El número de candidatos no debe presentarse como
mecánicas de juego reconstruidas con éxito. La ejecución en Unreal, Windows, Android y web sigue sin verificar.

El formulario de importación independiente se revisó en el editor nativo de macOS. La entrada automatizada por portapapeles/
pulsaciones rápidas no rellenaba siempre el texto de ImGui, por lo que no se afirma una importación completa a base de clics;
las mismas operaciones se verificaron mediante el puente MCP de C++ y la CLI. El editor de macOS/Metal compila.

Tras fusionar el master actual (`82c47120`), dos bucles `std::filesystem::path` de `NativeBuild.cpp` pasaron a `const auto&`
para compilar con AppleClang. Es un arreglo mínimo de compatibilidad en un archivo compartido. MAIN debe republicar los
paquetes del editor tras integrar y verificar esta rama; esta rama no publica builds.

Verificación final tras la fusión del master y el arreglo de compatibilidad:

- Editor de macOS (Metal), aiversemcp, aiverse_headless y aiverse_tests recompilados con éxito.
- Suite completa de C++ fuera del sandbox con sockets restringidos: **5930 comprobaciones, 0 fallos**.
- CTest: pasan los objetivos existentes de los importadores VFX y de proyectos (34 + 15 pruebas de Python).
- Puente MCP específico: 5 comprobaciones; utilidades de build del editor nativo: 42 comprobaciones, todas pasan.
- La importación de extremo a extremo con `aiversemcp --no-editor` acepta `options` anidadas, aplica scale=2 y maneja una ruta
  de salida con espacios y un apóstrofo.
- `skirmish --seed 3`: gana el azul a los 470 segundos de juego; `mushrooms --seed 3`: gana el rojo a los 92 segundos.
