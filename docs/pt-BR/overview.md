# AIVERSE — visão completa

Uma referência de cada parte da engine: estrutura, build, formatos, subsistemas, plataformas e status de verificação.
A introdução curta — para que serve a engine e quais problemas ela resolve — está no [README](../../README.pt-BR.md).
As regras para agentes que alteram a engine estão no [AGENTS.md](../../AGENTS.md).

## Estrutura

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

## Início rápido (macOS)

```bash
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release      # requires Xcode and CMake
cmake --build build/mac -j 8
open build/mac/AiverseEditor.app                              # editor (project templates/skirmish)
open build/mac/AiverseGame.app                                # game
./build/mac/aiverse_tests
```

Outro jogo no editor: `AiverseEditor.app/Contents/MacOS/AiverseEditor --project templates/mushrooms`.
Para o jogo iniciar com outro conjunto de dados, passe `-DAIVERSE_CONTENT_DIR=.../templates/mushrooms`.

### Vários editores ao mesmo tempo

Cada processo do editor pode abrir seu próprio projeto com `--project <pasta>`. Sem `--listen`, a primeira instância ocupa a
porta MCP `47321`; se estiver ocupada, o sistema atribui uma livre. A porta real aparece na barra de status; a janela **Agent**
a adiciona à configuração de conexão (`aiversemcp --port <porta>`). Uma porta atribuída automaticamente pode mudar após
reiniciar o editor. `--listen <porta>` fixa uma porta exata (em caso de conflito, o MCP não inicia), `--listen 0` desativa o
MCP. Clientes MCP já conectados não trocam de janela sozinhos.

## Build para as plataformas — aiversebuild

```bash
./build/mac/aiversebuild --project templates/skirmish --target macos     # Builds/macos/<Name>.app (ad-hoc signed)
./build/mac/aiversebuild --project templates/skirmish --target ios       # Builds/ios/xcode/AIVERSE.xcodeproj + simulator build
./build/mac/aiversebuild --project templates/skirmish --target android   # Builds/android/<Name>-release.apk when SDK/NDK and gradle are present
./build/mac/aiversebuild --project templates/skirmish --target web       # Builds/web (requires emsdk)
./build/mac/aiversebuild --project templates/skirmish --target windows   # .exe via mingw-w64 (brew install mingw-w64) or sources + build.bat
./build/mac/aiversebuild --project templates/skirmish --target all
```

O resultado vai para `<projeto>/Builds/<plataforma>/` — só builds prontos e o relatório de tamanho. Tudo o que é intermediário
(árvores do CMake, pacotes, áudio recodificado, o projeto Gradle, dados do xcodebuild, a estimativa de tamanho do editor) fica
na pasta oculta `Builds/.temp/`, que pode ser apagada a qualquer momento. Com `--generate-only`, o projeto Gradle para o Android
Studio vai para `Builds/android/<Name>`. O empacotador remove sozinho as antigas pastas visíveis `_build`, `_pack`, `_audio`,
`_estimate`.

O mesmo existe no editor: o menu **Build**. Os projetos de iOS e Android gerados são autônomos (as fontes da engine e o
conteúdo são copiados para dentro) e podem ser repassados. Para iOS num aparelho: abra o projeto no Xcode e escolha um time em
Signing (ou `--team <TeamID>`, ou `"appleTeamId"` no game.json).

Identificador, nome, versão, orientação e ícone (`icon.png`, 1024×1024; gerado se faltar) ficam no `game.json`.

### Ferramentas de build: verificação e instalação automática

Antes do build, o `aiversebuild` verifica o que a plataforma precisa e procura além do `PATH` (um editor aberto pelo Finder só
enxerga `/usr/bin:/bin:/usr/sbin:/sbin`): Homebrew (`/opt/homebrew/bin`, `/usr/local/bin`), `~/.local/bin`,
`~/Library/Python/*/bin`, `~/emsdk`, `$EMSDK`, `ANDROID_HOME` / `ANDROID_SDK_ROOT`, `~/Library/Android/sdk`, `~/Android/Sdk`,
`%LOCALAPPDATA%\Android\Sdk`, `JAVA_HOME`, `/usr/libexec/java_home`, Visual Studio (vswhere) e a própria pasta
`~/.aiverse/toolchains`. As pastas encontradas são adicionadas ao `PATH` dos processos filhos.

| Plataforma | Precisa de | Instalado automaticamente |
|---|---|---|
| web | CMake, Python 3, Emscripten (emsdk), Ninja — opcional | emsdk (≈ 400 MB de download, ≈ 1,5 GB em disco), CMake + Ninja (≈ 50 MB) |
| android | JDK 17–23, Android SDK (platform 35, build-tools, platform-tools), NDK, CMake do SDK, Gradle | tudo: Temurin JDK 17, command line tools + `sdkmanager` (licenças aceitas), NDK ≈ 700 MB, Gradle 8.10.2 |
| macos | Xcode Command Line Tools, CMake | CLT — pelo instalador da Apple (`xcode-select --install`), CMake |
| ios | Xcode (App Store), CMake | CMake |
| windows (no Windows) | Visual Studio 2022 / Build Tools (C++), CMake | CMake; VS — uma dica com um comando `winget` |
| linux | compilador C++, CMake | CMake |

```bash
aiversebuild --check-toolchain web            # what is present and what is missing (--json for the editor and MCP)
aiversebuild --install-toolchain web          # install what is missing (or one id: emsdk, cmake, jdk, android-ndk, gradle, ...)
aiversebuild --project ... --target web --install-missing   # install and build right away
```

Sem `--install-missing`, a falta de ferramentas interrompe o build com código de saída 3 e a linha
`AIVERSE_MISSING_TOOLCHAIN web: emsdk` (o editor a reconhece). No editor, a janela **Build** mostra para a plataforma escolhida
o que foi encontrado (caminhos, versões) e o que falta, com um botão **Instalar automaticamente** e uma caixa «Instalar
automaticamente o que faltar»; o progresso aparece no **Console** (o texto pode ser selecionado e copiado; «Abrir pasta de
logs» abre `<engine>/logs`). Agentes fazem o mesmo via MCP: `toolchain_check`, `toolchain_install`,
`build_start {"installMissing": true}`.

Tudo é instalado em `~/.aiverse/toolchains` (Windows: `%USERPROFILE%\.aiverse\toolchains`; outra pasta — a variável
`AIVERSE_TOOLCHAINS_DIR`), sem `sudo` e sem mudanças no sistema: `emsdk/`, `py/` (um venv com cmake e ninja), `cmake/`,
`ninja/`, `jdk-17/`, `android-sdk/`, `gradle-8.10.2/`. Rodar de novo é seguro (o que já está pronto é pulado). Para remover tudo,
apague essa pasta. Exceções: as Command Line Tools são instaladas pelo instalador da Apple, e se o Android SDK do usuário for
encontrado, o `sdkmanager` instala nele os pacotes que faltam.

### Pacotes de recursos (.rpak) e tamanho do build

O conteúdo do jogo (a pasta do jogo + `engine/assets`) entra no build como um único arquivo `content/content.rpak`: cada
arquivo é comprimido separadamente com zstd, arquivos idênticos são guardados uma vez, formatos já comprimidos (png, ogg, mp3,
…) entram como estão. A engine monta o `content.rpak` automaticamente; os arquivos do pacote têm prioridade sobre os da pasta,
então sem pacote (no desenvolvimento) tudo funciona como antes. `platform::mountPack("patch.rpak")` monta um pacote por cima
(patch, DLC). Cada build imprime um relatório de tamanho e o grava em `dist/<plataforma>/size-report.txt`.

```bash
aiversebuild ... --no-pack          # ship folders as is (debugging)
aiversebuild ... --pack-level 9     # zstd level (default 19 for Release, 3 for Debug)

assetpipe pack -o content.rpak templates/skirmish engine/assets=engine   # build a pack by hand
assetpipe list content.rpak       # files, sizes, compression
assetpipe verify content.rpak     # unpack everything and check hashes
assetpipe extract content.rpak out/
assetpipe diff old.rpak new.rpak  # what changed between versions (for patches)
```

O que o build não precisa é listado em `.rpakignore` na pasta do jogo (máscaras `*`, `?`; `pasta/` — só pastas). Arquivos
ocultos (`.git`, `.DS_Store`), replays `*.rpl` e `*.rpak` aninhados são sempre ignorados. A geração do pacote é reprodutível:
o mesmo conteúdo gera um arquivo idêntico byte a byte; o «hash de conteúdo» do pacote identifica o conjunto de dados.

### Builds divididos e download de conteúdo

Opcionalmente, um build pode ser dividido: o app leva só um pacote base com o necessário para iniciar, e o resto vai em partes
(`content.001.rpkc`, …) de tamanho definido que o jogo baixa de qualquer origem. Quem decide tudo é o autor do jogo, no
`game.json` (configurações comuns e por plataforma) ou com flags do `aiversebuild`:

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

As partes ficam ao lado do build (`dist/<plataforma>/chunks/`) e são publicadas no endereço de `sources` (`{hash}` é o hash de
conteúdo do build, então as versões não se misturam). No jogo, um arquivo de uma parte ainda não baixada é pedido
automaticamente (no desktop e no celular, `readContentFile` espera por ele; na web, retorna false); o progresso é
`platform::contentProgress()` e um pedido explícito `platform::requestContent(paths)`. Cada parte é verificada por SHA-256
(uma parte adulterada de um espelho é rejeitada e buscada no próximo), fica em cache e não é baixada de novo na próxima
inicialização. Protocolos: https/http (libcurl no Linux, WinHTTP, NSURLSession, HttpURLConnection no Android, fetch na web),
`file://` e pastas; um jogo pode registrar seu próprio esquema (Steam, Play Asset Delivery, P2P).
`assetpipe pack --chunk-size 8MB ...` faz o mesmo à mão, e `assetpipe verify` também verifica as partes.

### Importar arquivos de terceiros (descompactadores)

Formatos que a engine não conhece (zip, tar, dmg, arquivos de outros jogos) entram como plugins descompactadores sem recompilar
a engine: uma pasta com `unpacker.json` e um programa em qualquer linguagem (protocolo:
[`tools/unpackers/PROTOCOL.md`](../../tools/unpackers/PROTOCOL.md)). Os descompactadores só rodam nas ferramentas de build: um
arquivo de terceiros vira arquivos dentro de um `.rpak`, e o jogo continua lendo só `.rpak`.

Incluídos: zip (e `.oramap`), tar, dmg, arquivos do Warcraft I/II (`.WAR/.SUD/.INS`), `.PAK` da Westwood (Dune II, Lands of
Lore, Kyrandia), Z dos Bitmap Brothers (`.PAC`, `.RSC`).

```bash
assetpipe formats                                   # which formats are known
assetpipe probe ~/Downloads/Warcraft2/war2/DATA/*   # what these files are
assetpipe list MAINDAT.WAR                          # what is inside
assetpipe import -o war2.rpak MAINDAT.WAR=imported/war2 SFXDAT.SUD=imported/war2/sfx
assetpipe unpackers new tools/unpackers myformat    # scaffold for a new unpacker
assetpipe unpackers check                           # check all plugins
```

Todos os comandos têm `--json` — é assim que o servidor MCP os chama. O `game.json` pode listar `"imports"`, e o
`aiversebuild` os descompacta no pacote durante o build (`"optional": true` — se o arquivo não existir nesta máquina, o build
segue sem ele). Um pacote com conteúdo de terceiros importado é marcado como «external»: esses builds são só para uso pessoal
e não podem ser commitados nem distribuídos.

### Importar jogos (importadores)

Um importador transforma sua cópia de um jogo clássico numa pasta de jogo para a engine. Hoje há o Dune II:

```bash
python3 tools/importers/dune2/import.py --source "~/Downloads/Dune II" --out ~/AiverseGames/dune2
aiversebuild --project ~/AiverseGames/dune2 --target macos
```

O resultado é um jogo 2D no formato da engine (`docs/game-format.md`, «2D games»): câmera de cima, 66 missões de campanha com
camada de tiles (`tilesets/dune.json` a partir do ICON.ICN: areia, rocha, dunas, montanhas, especiaria com autotile), sprites
de unidades a partir de SHP (5 direções com espelhamento, torres dos tanques sobre o chassi) e construções a partir de tiles
ICN (obra, ruínas, animação), cor da casa a partir de tons de magenta. Mais `rules.json` (nosso balanceamento de Dune II com os
blocos da engine), imagens CPS e animações WSA em PNG, sons VOC → WAV + `audio.json`, nomes em inglês e russo. O terreno é
aproximado: o gerador de paisagem original não é usado, as bases ficam na rocha, a especiaria vem de `Field=`, as metas das
missões são gatilhos. Infantaria, ornitóptero e transportador ainda são desenhados como formas 3D. A pasta é marcada com
`"external": true` — não pode ser commitada nem distribuída.

## Arquivo de projeto .aiverse

Um projeto é uma pasta em qualquer lugar do disco (`game.json`, `rules.json`, `maps/`, ...). Na raiz fica o arquivo de projeto
**`<Nome>.aiverse`** — aberto com duplo clique, como o `.uproject` no Unreal ou o `project.godot`. O `game.json` continua sendo
o manifesto do jogo (lido pelo cliente, pelo aiversebuild e pelos pacotes); o `.aiverse` só marca a pasta como projeto e guarda
informações do editor:

```json
{"format": 1, "name": "My Strategy", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
```

- É criado junto com o projeto (hub «Novo projeto», MCP `create_project`); um projeto antigo sem ele recebe um na primeira
  abertura no editor. Os templates em `templates/*/` já têm um (ele não é copiado para um projeto novo: este recebe o seu).
- Em todo lugar em que o editor aceita um caminho de projeto, servem tanto o `.aiverse` quanto a pasta:
  `aiverse_editor "My Strategy.aiverse"`, `--project`, «Abrir…» no hub (mostra `.aiverse` e pastas de projeto), recentes,
  MCP `open_game`, `aiversebuild --project x.aiverse`. O `.aiverse` não vai para o build.
- Projetos criados antes da renomeação para AIVERSE mantêm seu `<Nome>.rtsproj`: ele abre do mesmo jeito em todo lugar,
  é regravado no lugar e nunca renomeado; se a pasta tiver os dois, vale o `.aiverse`.
- **Duplo clique.** Ao iniciar, o editor associa o `.aiverse` a si mesmo (por usuário, sem direitos de administrador) se não
  houver associação ou se ela apontar para outra instância do editor; ele não substitui outro programa que você escolheu.
  Manualmente: «Projeto → Tornar o editor o app padrão para .aiverse»; o estado aparece no canto inferior esquerdo do hub.
  Desligar: `AIVERSE_FILE_ASSOC=off`.
  - **macOS:** o tipo `com.aiverse.project` está declarado no Info.plist do `AiverseEditor.app`
    (`platform/macos/EditorInfo.plist.in`; o ícone de documento `rtsproj.icns` é desenhado pelo `aiverse_projicon` a partir do
    logo do editor `apps/editor/branding/icon_1024.png`); o editor registra o bundle no LaunchServices (`lsregister -f`) e
    vira o manipulador padrão. Duplo clique e `open -a AiverseEditor x.aiverse` abrem o projeto no editor em execução (se o
    mapa tiver alterações não salvas, ele pergunta se deve salvar).
  - **Windows:** `HKCU\Software\Classes\.aiverse` (+ `.rtsproj`) → `AIVERSE.Project` (ícone `%USERPROFILE%\.aiverse\icons\rtsproj.ico`,
    comando `"<aiverse_editor.exe>" "%1"`). Se o Windows já tiver outro programa escolhido para `.aiverse`, o hub sugere
    «Abrir com → Sempre».
  - **Linux:** o tipo `application/x-aiverse-project` (`~/.local/share/mime/packages/aiverse-project.xml`),
    `~/.local/share/applications/aiverse-editor.desktop` (`Exec=<editor> %f`), ícones em `~/.local/share/icons/hicolor`, app
    padrão em `~/.config/mimeapps.list`; `update-mime-database`, `update-desktop-database` e `xdg-mime` são chamados se
    estiverem instalados.

## Criar um jogo (assistente)

O botão **Criar um jogo** na janela de projetos (e **Projeto → Criar um jogo…**): informe uma pasta ou um link com arte, música e
textos, descreva a ideia (ou nada — o assistente escolhe sozinho) e receba um projeto `.aiverse` pronto: um jogo num template
adequado, um tutorial passo a passo (guiado) e dicas em combate (discretas), fases com estrelas e recompensas, a meta (menu,
melhorias, loja, recompensa diária, perfil, configurações), anúncios e compras se quiser, idiomas, ícone e tela de abertura a
partir da arte. O assistente joga o tutorial sozinho com um bot e verifica as fases; o que foi atribuído e o que conferir está
no `WIZARD.md` do projeto. Para agentes: MCP `game_make_options` / `game_make_plan` / `game_make`. Detalhes:
[game-wizard.md](game-wizard.md).

## Como um jogo é descrito

Um tipo de objeto é um conjunto de blocos opcionais; o que estiver definido, o objeto sabe fazer:

| Bloco | Significado |
|---|---|
| `health` | vida, armadura, tipo de armadura, regeneração |
| `move` | velocidade, camada (`ground`/`air`/`naval`), raio |
| `structure` | uma construção com tamanho `footprint` na grade |
| `weapons` | alcance, dano, tipo de dano, recarga, projétil, dano em área, quais camadas atinge |
| `cost` | recursos, tempo, suprimento |
| `produces` / `builds` | o que treina / o que constrói |
| `gather` / `resourceNode` / `dropoff` | coleta de recursos (Warcraft/StarCraft) |
| `income` / `storage` + recurso `"kind": "flow"` | economia de fluxo (Total Annihilation) |
| `supply` | fornece suprimento |
| `requires` | árvore tecnológica |
| `garrison` + `entersGarrison` | construções de guarnição, envio de uma porcentagem das tropas, captura (Mushroom Wars) |
| `view` | aparência: `shape`, `scale`, `color` |

Global: `resources`, `damageTable` (tipo de dano × tipo de armadura), `supplyCap`, `victory`.
Mapa: tamanho, terreno (`rows`: `.` chão, `~` água, `#` rocha, `,` sem construção, `=` vau; `heights`), jogadores e equipes,
recursos iniciais, objetos, estradas, `roadsOnly`.

## Localização

Um jogo é traduzido sem mudar o `rules.json`: os nomes nos dados são texto no idioma de origem do jogo, e as traduções ficam em
`<projeto>/lang/<idioma>.json` sob chaves como `type.footman`, `resource.gold`, `map.valley.title`… Os textos do menu e do HUD
ficam em `engine/assets/lang/` (hoje inglês e russo); um jogo pode substituir qualquer um.

```jsonc
// game.json
"sourceLanguage": "en",
"languages": ["en", "ru"]
```

```bash
./build/mac/aiverse_i18n extract --project templates/skirmish --lang de --write   # lang/de.json skeleton, new strings marked // TODO
./build/mac/aiverse_i18n check   --project templates/skirmish                     # placeholders, plural forms, untranslated strings
```

O idioma é escolhido assim: a escolha do jogador no menu (memorizada) → o idioma do sistema → o idioma de origem do jogo. Para
testar: `--lang ru`, `AIVERSE_LANG=ru`, o pseudoidioma `--lang qps` (todos os textos distorcidos e mais longos — o que não foi
traduzido e o que não cabe aparece na hora). As formas de plural seguem o CLDR («1 segundo / 2 segundos / 5 segundos»). Mais:
`engine/i18n/README.md`.

## Interface do jogo (RmlUi, HTML/CSS)

O menu, o HUD, o cartão de comandos, o minimapa e a pausa do cliente são documentos RmlUi (marcação `.rml`, estilos `.rcss`),
não código. A engine os desenha pelo seu RHI (GL/Metal); as fontes passam pelo stb_truetype (latino + cirílico).

- Documentos padrão: `engine/assets/ui/` (`menu.rml`, `hud.rml`, `theme.rcss`).
- Um jogo substitui qualquer arquivo colocando `ui/<mesmo nome>` na sua pasta: por exemplo, só `ui/theme.rcss` — cores próprias
  com a marcação padrão. A fonte do jogo é `fonts/ui.ttf`.
- Os dados vêm do modelo `game` (a lista de campos está num comentário em `hud.rml`); os cliques nos botões viram comandos da
  simulação (`apps/client/ClientUi.cpp`).
- Tamanhos em `dp` — a interface acompanha a densidade da tela; em dispositivos de toque a raiz tem a classe `touch` (botões
  maiores).
- F8 no cliente abre o depurador do RmlUi (árvore de elementos, estilos, log).
- A skin do jogo é `ui/skin.rcss` (carregada depois de `theme.rcss` em todos os documentos: cores, bordas, botões, layout) e a
  fonte de títulos `fonts/title.ttf` (família `title`). Cada protótipo em `templates/` tem sua skin. O layout retrato
  (`@media (orientation: portrait)`) está no tema da engine; no PC, confira com `--size 540x960`.
- Os textos dos documentos são escritos como chaves `@menu.play`: a tradução vem de `engine/assets/lang` (e do `lang/` do jogo)
  ao carregar o documento; trocar o idioma no menu recarrega os documentos.
- Teclado na tela (`engine/ui/UiKeyboard.*`): feito em RmlUi, abre quando um campo de entrada recebe foco em dispositivos de
  toque (no PC — `AIVERSE_OSK=1`). Os layouts (en, ru, símbolos; Shift, Caps com toque duplo, troca de idioma) ficam em
  `keyboard.json`, e a aparência em `keyboard.rml` / `keyboard.rcss`; um jogo substitui qualquer um desses arquivos no seu
  `ui/`. Se o cursor estiver na parte de baixo da tela, o teclado vai para cima.
- Multijogador (`lobby.rml`, `apps/client/Lobby.cpp`): endereço do servidor e nome, partida rápida pelas filas do servidor,
  lista de salas (entrar / assistir), sala própria (vagas, bots, pronto, início, chat). A partida é conduzida pelo `NetClient`:
  `attach(sim)`, `advance(dt)` em vez de `sim.advance(dt)`, os comandos saem da vaga `match().localSlot` (0 — espectador); os
  bots que o servidor atribuiu a este cliente também são calculados aqui.

## Determinismo

A simulação é idêntica bit a bit em todas as plataformas: só ponto fixo, gerador aleatório próprio, comandos em ordem
canônica, checksums nos replays. Verificado: Linux x86-64 (GCC, Clang, MinGW), Linux ARM64 (GCC), macOS ARM64 (AppleClang) —
checksums de partidas idênticos.

## Gráficos e mobile

Uma única passada forward: sol (Lambert + especular), um shadow map ajustado à parte visível do mapa e alinhado aos texels, PCF
por hardware (1/4/9 amostras), luz ambiente hemisférica, neblina por distância, ACES. A qualidade das sombras é escolhida pela
GPU (baixa para Mali/Adreno/PowerVR antigas) e pode ser trocada no menu de pausa.

## Multijogador

`engine/net` é o multijogador de qualquer jogo da engine, sem código de jogo. Pela rede só trafegam comandos: a simulação é
determinística, então todos têm o mesmo estado.

- **Transporte:** UDP com camada de confiabilidade própria (iOS, Android, PC) e WebSocket (HTML5; no nativo, alternativa se o
  UDP estiver bloqueado). Endereço: `host[:27015]`, `udp://…`, `ws://host:27016`.
- **Lockstep com rollback.** O servidor confirma as entradas de cada tick quando chegam os comandos de todos os jogadores ou
  quando o prazo (`lagToleranceMs`) vence; comandos atrasados passam para o tick seguinte — um jogador com lag não trava os
  outros. O cliente não espera a confirmação: joga à frente com seus próprios comandos e, se houver divergência, restaura um
  snapshot e recalcula (um snapshot leva ~10 µs). `rollbackWindow: 0` é o lockstep clássico para aparelhos fracos.
- **O `aiverse_server`** não precisa do conteúdo do jogo: salas (vagas, bots, espectadores, pronto, verificação da versão dos
  dados), matchmaking automático pelas filas do `server.json` (ranking Elo com janela que se amplia, mapa comum, equilíbrio de
  times, preenchimento com bots), comparação de checksums (dessincronia), replays (`aiverse_headless --replay` os reproduz),
  reconexão, espectadores com atraso.
- **Sair = desistir.** Um jogador que saiu (ou não voltou dentro de `reconnectGraceSec`) desiste com um comando no fluxo de
  entradas comum: o resultado é decidido pela simulação, igual para todos, e aparece no replay. O ranking é descontado.
- **Veredito do servidor.** Se ninguém confirmou o resultado (todos saíram, os resultados divergem), o servidor reproduz a
  partida numa thread de fundo com seus próprios dados de jogo (`"games"` no `server.json`) e define o vencedor.
- **Reconexão:** após uma queda de rede o cliente se reconecta sozinho (`NetClient(address, …)` ou `setReconnect`), a
  simulação continua — o servidor envia só as entradas posteriores ao último tick confirmado. Se o jogo foi reiniciado, o
  cliente recebe todo o histórico e recalcula a partida desde o início.
- **No jogo:** o `NetClient` (`engine/net/NetClient.h`) cuida da sala e da partida. O jogo monta uma `Simulation` a partir de
  `match()` e chama `attach(sim)`, depois `advance(dt)` em vez de `sim.advance(dt)`; a interface continua enviando comandos por
  `sim.submit()`.
- **Textos:** o servidor não envia frases prontas — só chaves de localização com argumentos (`NetText`): jogadores da mesma
  partida podem usar idiomas diferentes. O cliente as traduz com `net::translate(loc, event.text)` e `net::displayName(...)` de
  `engine/net/NetLocalize.h`; os textos ficam em `engine/assets/lang/*.json` sob `net.*`. Os nomes das filas no `server.json`
  também são chaves (`"@net.queue.1v1"`).

```bash
./build/mac/aiverse_server --config apps/server/server.json            # UDP 27015, WebSocket 27016
./build/mac/aiverse_netbot --server 127.0.0.1 --project templates/mushrooms --queue 1v1   # a bot in the queue
./build/mac/aiverse_netbot --server ws://127.0.0.1:27016 --project templates/mushrooms --queue 1v1
./build/mac/aiverse_net_tests                                          # network tests
docker build -f apps/server/Dockerfile -t aiverse-server .             # server in a container
```

Para `wss://` (uma página do jogo servida por HTTPS), coloque um proxy TLS (nginx, Caddy) na frente do servidor.

## Áudio

`engine/audio` é um mixer, bancos de sons a partir de dados, síntese procedural e música; a saída passa pelo
[miniaudio](../../third_party/miniaudio) (CoreAudio, AAudio/OpenSL ES, Web Audio, WASAPI, ALSA/PulseAudio).
`modules/rts/audio` transforma eventos da simulação em sons; não escreve nada na simulação. Sem dispositivo, o jogo roda em
silêncio. Sem `audio.json`, o jogo simplesmente não tem som.

O som de um jogo é o arquivo `audio.json` ao lado do `game.json` (outro nome: `"audio"` no game.json). O formato completo está
em `docs/game-format.md`, seção audio.json; em resumo:

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

| O quê | Significado |
|---|---|
| formatos | WAV, OGG Vorbis, MP3, FLAC; `"synth"` — um som a partir de parâmetros (`sine/square/saw/triangle/noise`, varredura de frequência, envelope, vibrato, filtro, camadas) |
| barramentos | `sfx` e `ambient` são posicionais (3D: atenuação, pan, altura); `voice`, `ui`, `music` não |
| distância | `spatial`: modelo de atenuação, alcances, «ar» (os agudos somem com a distância), a reverberação cresce com a distância, velocidade do som; um som pode ter alcances próprios e `far` — uma variante distante com crossfade |
| `maxInstances`, `cooldown`, `priority` | redução em grandes batalhas: 40 arqueiros não geram 40 disparos iguais |
| `groups` | limites comuns para vários sons (todo o corpo a corpo, todas as explosões, um barramento inteiro): `max`, `cooldown`, qual descartar — `farthest` (padrão: combate distante não corta o que está na tela), `oldest`, `quietest`, `none` |
| eventos (cues) | `attack hit miss died spawned trainStarted ready buildPlaced buildDone gathered captured lost playerDefeated victory defeat notEnough notEnoughSupply noRoom researched levelUp ability readyToPlace sold regionClaimed wave lowPower upgraded message underAttack baseUnderAttack select move attackOrder buildOrder click error` |
| busca do som | tipo → arma/habilidade por id → `events` |
| música | playlists `menu`, `match`, `battle`, `victory`, `defeat` (e `match.<mapa>`); crossfade, intro + loops sem emenda, camadas pela intensidade do combate, transições em tempos/compassos, vinhetas, abaixar sob a voz |
| compressão no build | o `aiversebuild` recodifica o áudio por plataforma: ADPCM para efeitos, Vorbis para música, taxa de amostragem menor, mono; perfis `desktop`/`mobile`/`web` em `audio.json` → `build`; `--audio off` — sem recodificar |

Os sons de combate só são ouvidos onde não há névoa de guerra; «estamos sob ataque» vem com uma pausa e só se a câmera estiver
olhando para outro lugar. O volume (geral e música) fica no menu de pausa e é salvo em `userDataDir/audio.cfg`. No celular, o
som pausa quando o app vai para o segundo plano.

Depuração sem placa de som: `AIVERSE_AUDIO=null` (uma thread de áudio real sem hardware), `AIVERSE_AUDIO=off`,
`AIVERSE_AUDIO_CAPTURE=/tmp/mix.wav` — gravar tudo o que tocou num WAV.

## Modelos, materiais, animação

Um tipo é desenhado pelo seu bloco `"view"`: uma forma embutida (`"shape"`) ou um arquivo de modelo (`"model"`) —
**glTF 2.0** (`.glb`, `.gltf` + `.bin` + imagens) e **OBJ + MTL**. Tudo passa por um único pipeline:

- **Materiais PBR** (metallic-roughness): cor base, normal, metallic-roughness, oclusão, emissivo; transparência opaque / mask /
  blend, dupla face, unlit, variantes de material (`KHR_materials_variants`), sobrescritas em `"materials"`. Cor do jogador: no
  material inteiro, por textura de máscara, pelo alfa da textura (como no Warcraft III) ou pelo alfa dos vértices.
- **Animação esquelética** na GPU (4 influências, qualquer número de ossos), clipes por estado da unidade (`idle`, `move`,
  `attack`, `gather`, `build`, `die`, `spawn`, `hit`, `cast`…), transições suaves, camadas com máscara de ossos (atacar com a
  parte de cima do corpo andando), cadência do passo pela velocidade real.
- **Animação procedural** em qualquer nó do modelo ou forma: balanço, rotação (moinhos, hélices), rodas pela distância
  percorrida, torre mirando o alvo, recuo, inclinação nas curvas, queda e afundamento ao morrer e mais; acionada por estados e
  eventos de combate.
- **Níveis de qualidade** dos materiais (`low` / `medium` / `high`) escolhidos pela GPU, como as sombras;
  `AIVERSE_MATERIALS=low|medium|high` força para testes.
- **Multidões**: os clipes são assados numa textura, unidades iguais são desenhadas com instancing (300 unidades animadas —
  61 draw calls em vez de 1801); LOD de modelos pela distância; a animação fora da tela e ao longe é atualizada com menos
  frequência.
- **Animação avançada**: blend space andar/correr, clipes aditivos (respiração, recuo), IK dos pés no terreno, cabeça virando
  para o alvo, veículos inclinando nas encostas, morph targets (blend shapes) na GPU.
- **Sockets**: arma na mão, carga nas costas (pelo recurso carregado), pontos de saída de projéteis, pontos de efeitos (fumaça,
  fogo, escapamento); eventos de clipes e `RtsView::events()/effects()` para som e partículas.
- **Etapas**: andaimes e fases de construção, variante danificada pela vida, dissolver ao surgir e ao morrer, contorno de
  seleção, fantasma translúcido ao posicionar uma construção, retratos.
- **Formatos**: também **FBX** (ufbx) e texturas comprimidas **KTX2** (ETC2/BC/ASTC, continuam comprimidas na GPU);
  **Collada**, **3DS**, **PLY**, **STL**; voxels **MagicaVoxel .vox** e **.vxl/.hva** (Tiberian Sun, Red Alert 2); modelos de
  RTS clássicos da cópia do usuário: **Warcraft III .mdx/.mdl** (+ BLP), **Total Annihilation .3do**, **C&C Generals .w3d**
  (+ DDS). Todo o resto que o assimp lê (.blend, .lwo, .x, .ms3d, .md2/.md3, .smd, .b3d…) é convertido para glTF pelo utilitário
  **modelconv**: o editor faz isso sozinho ao abrir e salvar, e o empacotador coloca no pacote só o resultado.
- **Materiais sob demanda**: uma biblioteca comum `materials.json` (herança via `base`), atribuição de material a um nó do
  modelo (`"assign": {"Turret": "steel"}`), texturas procedurais sem emenda com mapas de normal e máscaras de cor do jogador; a
  janela **Materiais** com pré-visualização e as operações MCP `material_*` / `texture_generate` para agentes.

Um modelo quebrado não quebra o jogo: o tipo é desenhado como forma e o motivo vai para o console do editor. Visualização:
`aiverse_modelview model.glb@Walk` ou `aiverse_modelview --game templates/showcase` (todos os tipos de um jogo como o jogo os
desenha); no editor — a janela **Models** (clipes, nós, materiais, retrato, recarga ao salvar o arquivo do modelo). Exemplo:
`templates/showcase`; formato completo: [models.md](../models.md).

## Iluminação

A luz dinâmica foi pensada para Androids antigos (Mali-T, Adreno 3xx/4xx): todas as luzes são desenhadas num pequeno mapa de luz
acima do chão visível, e o shader lê a luz com uma única amostra de textura, não importa quantas luzes existam. Níveis: `off`,
`low` (mapa 128², atualização a cada dois quadros), `medium` (256²), `high` (+ 8 luzes por pixel com especular). Escolhido pela
GPU, como as sombras; manualmente: `AIVERSE_LIGHTS=…`. Se durante a partida os quadros estourarem o orçamento de forma
contínua, o cliente reduz luzes e sombras em degraus (primeiro o mais caro e menos perceptível) e volta um degrau depois de uma
batalha pesada; escolher sombras manualmente no menu desliga isso (`AIVERSE_ADAPTIVE=off`).

- **Luz dos tipos e do combate** — `view.light` (fogueiras, forjas, cogumelos brilhantes), clarões de tiros, impactos e
  explosões, luz de projéteis (`attackLight`, `projectileLight`, `hitLight`, `deathLight`), luz de habilidades
  (`abilityLights`; por padrão a cor segue o efeito: dano — laranja, cura — verde), halos `halo` no lugar de um bloom caro. A
  névoa de guerra esconde os clarões.
- **Dia e noite, clima** — o bloco `lighting` no `game.json` ou num mapa: `time`, `dayLength`, `nightLevel`, `clouds`, `wind`,
  `storm` (sombras de nuvens e raios, idênticos nos replays), `preset`: `desert`, `snow`, `volcanic`, `overcast`, `cave`
  (subterrâneo: noite eterna, luz só das fontes). À noite os materiais emissivos brilham mais e as luzes refletem na água.
- **Luzes do mapa** — o array `lights` no mapa (lampiões, fogueiras, lava).
- **Sombras de contato e AO** — manchas suaves sob as unidades e ao redor das construções, em todos os níveis
  (`contactShadow`, `contactShape` no tipo).
- **Editor** — a janela «Lighting»: pré-visualização da hora do dia e da tempestade, nível de qualidade da luz, lista de fontes.
  Para agentes: `light_add`, `light_update`, `light_delete`, `light_list`, `lighting_set`, `view_lighting`.

O formato dos campos está no cabeçalho de `modules/rts/view/RtsLights.h`. Variáveis de teste: `AIVERSE_TIME_OF_DAY`,
`AIVERSE_NIGHT`, `AIVERSE_CLOUDS`, `AIVERSE_STORM`.

## Verificar mecânicas via MCP

Um agente que faz um jogo sem código verifica as mecânicas de forma pontual, não só com uma partida de bots.
- **`scenario_run`** — um cenário numa cópia do mapa aberto (nada é salvo): `clear` (um mapa vazio, sem objetos, ondas nem
  unidades iniciais), `spawn` com nomes (`"id": "foot", "count": 3` → `foot#1..3`, `"foot"` — todos), comandos por segundo
  (as mesmas chaves de `play_command`), `bots` para algumas vagas, `checks` com resultado aprovado/reprovado (`alive`, `dead`,
  `count`, `hp`, `resource`, `event`, `trigger`, `variable`, `distance`, `status`, `result` + comparação), `watch` + `every`
  (estado dos objetos ao longo do tempo), `events` (log de eventos).
- **`play_units` / `play_command`** — comandos num teste ao vivo na janela Game (antes só dentro do editor, agora visíveis para
  clientes MCP). Referências a objetos: ids de `play_units`, nomes `id` do mapa ou um filtro
  `{type, owner, tags, kind, area, near + radius}`; `queue: true` — fila (Shift), `ability` por id.
- **`match_run`** — mais um log dos gatilhos disparados e `events` (log de eventos por nome).
- **`rules_lint`** — chaves do `rules.json` e do mapa que o carregador não lê (erros de digitação, bloco no lugar errado), com
  sugestão («did you mean 'armor'?»). Roda no carregador real (`JsonAccessLog` em `engine/data/Json.h`); `rules_set`,
  `rules_merge` e `map_validate` avisam sobre essas chaves na hora.
- **`rules_schema`** — `part: "keys"`: cada chave que o carregador pede em cada bloco (`types.*.weapons[]`), reunidas de todos os
  jogos, e as chaves ausentes de `docs/game-format.md`; `part: "doc"` — o próprio documento.
- O evento de simulação `kEvTriggerFired` (só para ferramentas e apresentação, fora do checksum).
- Branch `agent-mech-tools`.

## Pós-efeitos

Os efeitos de tela cheia sobre a cena 3D são definidos por dados: o bloco `"postfx"` no `game.json` (um mapa pode
sobrescrevê-lo). Há bloom, correção de cor, LUT, vinheta, tilt-shift, raios de sol, FXAA, granulação, aberração, nitidez,
pixelização, posterização, tonalidade e faixas de cinema, além de conjuntos prontos (cinema, vívido, noir, retrô, miniatura,
pôr do sol, inverno, visão noturna). Cada efeito tem `minQuality`. O nível é escolhido pela GPU: Androids fracos recebem
«baixo», celulares «médio», PCs «alto»; `AIVERSE_POSTFX` sobrescreve. Sem efeitos, o pós-processamento não custa nada. Os
efeitos leves são agrupados numa passada; os pesados (bloom, FXAA, raios) adicionam a sua.

Editor: a janela **Pós-efeitos** (pré-visualização dos níveis, tabela do custo por quadro por classe de aparelho).
MCP: `postfx_catalog/get/add/set/remove/preset/enable/estimate/lut_neutral`, `view_postfx`. As respostas às alterações trazem
uma estimativa em ms/quadro e avisos sobre efeitos pesados. Detalhes: `docs/postfx.md`.

## Verificado / não verificado

- ✅ Linux: cliente, editor, testes; .exe do Windows (compilação cruzada com MinGW, executado no Wine).
- ✅ macOS: build, testes, jogo no Metal; aiversebuild → .app e um projeto de iOS (+ build para o simulador).
- ✅ Web: compila (Emscripten 3.1.50), uma partida roda no Chromium headless (WebGL2/SwiftShader).
- ⏳ Android: o projeto Gradle é gerado; o APK e a execução no emulador estão na CI (job `android`), não foi compilado localmente.
- ⏳ Windows em hardware real e iOS em aparelho — não executados (na CI: build com MSVC e build de iOS para o simulador).

## Plataformas e CI

`.github/workflows/ci.yml` a cada push em `master`/`agent-*` e em pull requests:

| Job | O que faz |
|---|---|
| `linux` | gcc e clang (x64) + gcc (ARM64), avisos como erros, testes, partidas de bots, capturas com Xvfb |
| `windows-msvc` | Visual Studio 2022, testes, partidas, `aiversebuild --target windows` |
| `windows-mingw` | compilação cruzada com MinGW-w64, testes e partidas no Wine |
| `macos` | build universal do Xcode, testes, partidas, `aiversebuild` → .app e um projeto de iOS com build para o simulador |
| `android` | `.so` pelo NDK, APK pelo `aiversebuild`; no emulador: iniciar → tocar → segundo plano → voltar → «voltar» → reiniciar, logcat verificado contra falhas |
| `web` | Emscripten, uma partida no Chromium headless, verificação do console e do quadro |
| `determinism` | compara os checksums das partidas (os dois jogos × 3 seeds) de todas as plataformas — precisam coincidir bit a bit |

As capturas, o APK, o build web e o .app são guardados como artefatos da execução.

Localmente:

```bash
# Windows .exe from Linux/macOS
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64.cmake
# bot matches on every game → a checksum file (compare between machines with diff)
.github/scripts/headless-checksums.sh build/linux-release/aiverse_headless sums.txt
# smoke test of the web build
node platform/web/smoke.mjs build/web
```

No build web, os parâmetros de linha de comando são passados pela URL: `index.html?map=0&spectate` = `--map 0 --spectate`.

Teclado na tela: `platform::startTextInput(area)` / `stopTextInput()` o abrem e fecham no iOS, no Android e em navegadores
móveis (no desktop não fazem nada). O texto chega como eventos `Char` em qualquer idioma, Backspace/Enter como
`KeyDown`/`KeyUp`; `softKeyboardHeight()` e o evento `KeyboardChanged` informam quanto da tela está coberto embaixo, e
`TextInputEnded`, que o usuário fechou o teclado. A interface (`UiLayer`) chama isso sozinha ao focar
`<input type="text|password|search|email|number">` e `<textarea>`.

Outros recursos para mobile e navegador em `Platform.h`:
- `safeAreaInsets()` — margens para o recorte da câmera, cantos arredondados, indicador de início e barras do sistema (iOS,
  Android, Safari/Chrome móveis; 0 no desktop, num MacBook com notch — em tela cheia).
- `setFullscreen()` / `isFullscreen()`; atalhos embutidos — F11 (Windows, Linux, web), Alt+Enter (Windows). No navegador, a tela
  cheia também trava a orientação (`AppConfig::orientation`).
- Android: o botão Voltar chega ao jogo como `Key::Escape`; tela cheia com barras que se escondem; teclado e mouse físicos
  (Chromebook, DeX) geram eventos comuns de teclas e mouse.
- iPad: Magic Keyboard / teclado Bluetooth, mouse e trackpad (clique direito, hover, rolagem).
- A área de transferência funciona em todo lugar; no navegador, Ctrl/Cmd+V chega ao jogo depois que o navegador entrega o texto.
- Web: o `userDataDir()` fica no IndexedDB — configurações e saves sobrevivem a recarregar a página.

### Plugins nativos: pagamentos e anúncios

Qualquer SDK de plataforma entra como plugin na sua linguagem nativa — Java (Android), Objective-C (iOS/macOS), JavaScript
(web) ou C++ — por uma única ponte assíncrona, `engine/platform/Plugins.h`. Um jogo usa as APIs comuns `aiverse::store`
(compras) e `aiverse::ads` (anúncios) de `engine/services`, e o provedor é escolhido por plataforma no `game.json`. Plugins
prontos em `plugins/`: **App Store**, **Google Play**, **RuStore**, **Yandex Games** (web: compras e anúncios), **Steam**
(compras pelo Inventory Service e DLC, Windows/Linux/macOS), **Yandex Mobile Ads**, **AdMob**, **AppLovin MAX**,
**ironSource (Unity LevelPlay)**. Um build leva só os plugins escolhidos pelo jogo (`"plugins"` no game.json ou `--plugins`):
nenhum SDK, dependência, entrada de manifesto ou chave de Info.plist alheios — a CI verifica isso. Para depurar no desktop há
simulações de loja e anúncios (`mock-store`, `mock-ads`; `AIVERSE_MOCK_STORE`, `AIVERSE_MOCK_ADS`): todo o fluxo de compra roda
sem celular, e as simulações não são compiladas nos builds de release. O aiversebuild grava os plugins sozinho no Gradle/manifesto,
Info.plist/Podfile e HTML; faça builds separados para Google Play e RuStore: `--plugins googleplay,admob` e
`--plugins rustore,yandex-ads`. Detalhes: [plugins.md](../plugins.md).

### Versões

`.github/workflows/release.yml`: uma tag `v1.2.3` (ou uma execução manual com a pasta de jogo que você escolher) compila todas as
plataformas com o `aiversebuild` e publica um GitHub Release:

| Plataforma | Resultado | Assinatura (segredos do repositório) |
|---|---|---|
| Windows | instalador (Inno Setup, sem direitos de administrador) + .zip portátil | `WINDOWS_CERT_PFX_BASE64`, `WINDOWS_CERT_PASSWORD` |
| Android | .apk e .aab para o Google Play | `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` |
| macOS | .app num .zip, notarizado | `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD` (Developer ID) + uma chave do App Store Connect |
| iOS | projeto do Xcode; com chave — um build para o TestFlight | `APPLE_TEAM_ID`, `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_P8_BASE64` |
| Linux | .tar.gz | — |
| Web | .zip; com `DEPLOY_WEB_PAGES=true` (variável do repositório) — publicação no GitHub Pages | — |

Sem segredos o release sai mesmo assim: o APK é assinado com uma chave de depuração, o .app em ad-hoc e o Windows sem assinatura.

### Engine pré-compilada (branch `builds`) e aiversemcp sem compilador

O editor e as ferramentas pré-compilados são publicados na branch **`builds`**:
`<platform>/AIVERSE-<version>-<commit>-<platform>.zip` (hoje `windows` x86_64 e `macos` arm64; `linux` x86_64 quando o
integrador publicar). Dentro há uma pasta `AIVERSE/` com as fontes da engine naquele commit (templates `templates/`,
`engine/assets`, `docs/` — o servidor MCP os lê) e `AIVERSE/bin/` com os programas. Empacotado por
`tools/release/package_engine.py` e publicado por `tools/release/publish_builds.py` (AGENTS.md §8); o pacote do Linux pode vir sem
o editor — só `aiversemcp` e `aiverse_headless`.

O servidor MCP a partir da pasta descompactada (partidas de bots e `balance_*` rodam dentro do aiversemcp, que não precisa de um
`aiverse_headless` separado; o editor é opcional, sem ele use `--no-editor`):

```bash
# ARCHIVE is the path to the AIVERSE archive downloaded from index.json.
unzip "$ARCHIVE" -d ~/aiverse      # unzip keeps the executable bit
~/aiverse/AIVERSE/bin/aiversemcp --no-editor --engine ~/aiverse/AIVERSE
```

`--engine` pode ser omitido em builds de commits nos quais o aiversemcp procura a engine ao seu lado
(`engine/platform/EngineDir.h`, como o editor); nos builds publicados antes ele é obrigatório. Com descompactadores que não
restauram permissões (Python `zipfile`), rode `chmod +x` nos arquivos de `bin/`. No macOS, um arquivo baixado pelo navegador
pode ficar em quarentena: `xattr -dr com.apple.quarantine ~/aiverse/AIVERSE`.

Programas que baixam a engine sozinhos leem o **`index.json`** na raiz da branch
(`https://raw.githubusercontent.com/chromealex/AIVERSE/builds/index.json`), não o README. Abaixo, um exemplo de entrada histórica
do RTSEngine; nos builds novos os caminhos contêm AIVERSE e a chave `rtsmcp` é mantida:

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

- `packages` vai do mais novo ao mais antigo; a primeira entrada com o `platform` + `arch` desejados é o build mais recente.
- `platform`: `windows` | `macos` | `linux`; `arch`: `x86_64` | `arm64` | `universal` (macOS fat) | `null` (não reconhecido) —
  definido pelo cabeçalho do `aiversemcp` dentro do zip.
- `rtsmcp`, `editor`, `headless` são caminhos dentro do arquivo, ou `null` se o programa não estiver no pacote; `--engine` é a
  pasta `root`. No macOS, `editor` é um bundle `.app`.
- `url` aponta para uma branch reescrita a cada publicação (ficam 5 versões por plataforma): entradas antigas somem, então baixe a
  partir de um `index.json` recente e confira o `sha256`.
- Novos campos podem ser adicionados; uma mudança de formato incompatível aumentará `schema`.

Verificação: `python3 tools/release/test_release.py [build/linux-release/aiversemcp]` (CTest `release_tools`) — o formato do
`index.json` e, com um caminho para o aiversemcp, o empacotamento de um pacote Linux, a descompactação numa pasta temporária e
chamadas MCP a partir dela sem `--engine` (`tools/list`, `create_project`, `map_generate`, `map_validate`, `map_ascii`).

O modo de captura (`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`) avança exatamente 1/60 s por quadro, então o quadro N é o
mesmo em qualquer máquina.

## Próximos passos

Modelos glTF e animação, áudio, rede (lockstep), habilidades e buffs a partir de dados, flow fields e RVO para grandes exércitos,
melhorias da IA, scripting em C# (AOT para iOS).

## Módulos de jogo em C++

Além de Lua, um projeto pode conter `native/CMakeLists.txt` e módulos C++20. Eles são incluídos com `aiverse_native_module(...)`
no build e ativados pelo array `nativeModules` no `rules.json`. Ticks, comandos, rollback e salvamento de estado são
suportados; Lua e C++ funcionam juntos. Depois de mudar o C++, é preciso recompilar o jogo e o editor.
[API, exemplo e instruções de build](../native-modules.md).

## Biblioteca de meta reutilizável em C++

`aiverse::meta` — loja/ofertas/criação configuráveis, moedas e itens, missões e passes, recompensas diárias, energia, correio,
rankings, chats e segmentos. Disponível para o código nativo do projeto sem Lua/GPU; as definições são JSON, os algoritmos não
precisam ser gerados de novo. Início: `python3 tools/meta/scaffold.py --preset mobile --out <project>`. Documentação:
[meta-game.md](../meta-game.md); para agentes — `skills/meta-game/SKILL.md`. As políticas locais funcionam sem servidor; a
comunicação entre aparelhos e rankings globais exigem um backend real. Um serviço de nuvem pronto não faz parte do pacote.

O preset `full --with-ui` adiciona amigos/clãs, uma coleção de instâncias, equipamento, fusão, assinaturas e ligas de torneio.
`aiverse::meta::Service` executa as operações de forma atômica; `tools/meta/server.py` é um host HTTP/SQLite local com
verificação de sessões. Inclui telas de meta para navegador e um catálogo de receitas curtas para agentes.
[Build, execução e limites](../meta-service.md).

`full` inclui também 15 grupos de LiveOps: baús com garantia, álbuns/trocas, filas de tarefas, pesquisa/prestígio, missões
compostas, eventos, guerras de clãs, PvP assíncrono, matchmaking de grupos, Elo/Glicko-2, indicações/códigos promocionais,
anúncios recompensados, cadeias de ofertas, notificações e renda offline. Todos os parâmetros ficam em `extensions.liveops`;
este preset não instala novas telas de interface separadas nem SDKs externos.
`python3 tools/meta/catalog.py describe matchmaking` mostra só o esquema e o exemplo necessários.
[API, políticas e limites](../meta-liveops.md).

### Física 3D opcional

O Box3D é ativado pela flag `AIVERSE_ENABLE_PHYSICS=ON`; projetos de RTS comuns não precisam dele. Um exemplo separado,
`aiverse_physics_demo`, mostra corpos, colisões, um pêndulo e salvamento de estado.
[Build, API e limites de integração](../physics.md).

### Tecido XPBD visual

O módulo opcional `AIVERSE_ENABLE_CLOTH=ON`: um solver de tecido XPBD próprio na CPU ou em Metal compute, lotes compartilhados,
LOD temporal, repouso automático e desenho de malhas densas com um único draw instanciado. Na GPU, as posições ficam na memória
de vídeo; backends sem suporte usam a CPU. O exemplo pode comparar a GPU com uma referência na CPU e verificar repouso/despertar.
[Exemplo independente, limites e medições](../cloth.md).
