# Assistente «Criar um jogo»

Uma janela (hub de projetos → **Criar um jogo**, ou **Projeto → Criar um jogo…**) ou uma chamada MCP (`game_make`) cria um
projeto de jogo inteiro com seu `.aiverse`: o jogo de um template escolhido pela ideia, a arte / modelos / música / sons do
usuário, um mapa de tutorial guiado, dicas discretas nas partidas, fases geradas, o meta-jogo (menus, melhorias, loja,
recompensa diária, perfil, configurações, resultado), compras e anúncios quando pedidos, idiomas, identidade visual e uma
verificação com bots. Tudo o que ele escreve são dados de jogo comuns, que podem ser editados depois.

Código: `apps/editor/core/GameWizard*.cpp` (sem UI), `apps/editor/core/WizardOps.cpp` (MCP), `apps/editor/EditorGameWizard.cpp`
(a página do hub). Kit de execução copiado para cada jogo: `templates/_wizard/` (Lua + RmlUi, veja o README dele).

## Respostas (`WizardSpec`, todas opcionais)

| Grupo | Chaves |
|---|---|
| Conteúdo | `artDir` (pasta: png/jpg/tga, glb/gltf/obj/fbx, wav/ogg/mp3/flac, ttf/otf, md/txt), `artUrl` (um arquivo ou um .zip / .tar.gz; baixado com o curl do sistema para `~/.aiverse/wizard/<hora>`) |
| Ideia | `idea` (qualquer idioma; os .md/.txt da pasta também são lidos), `genre` (id do template ou `auto`), `features` (economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait scifi modern) |
| Projeto | `title`, `location`, `studio` (bundle id `com.<studio>.<slug>`), `version`, `orientation`, `languages`, `platforms`, `seed` |
| Tutorial | `tutorial` (`both` / `hard` / `soft` / `none`), `tutorialForced`, `tutorialSkippable`, `tutorialDim`, `hints` |
| Fases | `levels` (0..40), `mapPreset` (`auto`, `none` = cópias do mapa base, ou um preset do gerador), `mapSize`, `bossEvery`, `difficultyCurve`, `startResourcesPercent`, `keepTemplateMaps`, `verifyLevels` |
| Meta | `metaIdea` (texto livre: o que a meta deve ter; vai para a tarefa do agente), `currencies` (soft, hard), `upgrades`, `shop`, `daily`, `profileScreen`, `xpPerLevel` |
| Dinheiro | `ads`, `interstitialEvery`, `purchases`, `stores` |
| Visual | `brandingFromArt`, `paletteFromArt` |

`game_make_options` lista as opções, `game_make_plan` mostra o que seria criado (sem arquivos). Mesmas respostas + seed =
mesmo jogo.

## Como ele decide

- **Template**: as palavras da ideia são comparadas com as palavras-chave de cada template (radicais, ru / en), os recursos
  desejados com suas mecânicas, a orientação com sua tela; em caso de empate, o seed decide. Nada informado: um dos templates
  equilibrados.
- **Conteúdo**: modelos e sprites 2D vão para os tipos cujo id / nome (ru / en) aparece no nome do arquivo; o resto é copiado e
  listado em `WIZARD.md`. A arte principal (por nome e tamanho) vira a tela de abertura, o fundo do menu e a cor de destaque da
  interface; a imagem mais quadrada (ou «icon» / «logo») vira o ícone de 1024. A música, pelo nome, vai para `menu` / `match` /
  `battle` / `victory` / `defeat`; sons curtos, para eventos (`click`, `select`, `move`, `died`, `underAttack`, ...). A primeira
  fonte TrueType vira `fonts/title.ttf`.
- **Tutorial guiado** (`maps/wiz_tutorial.json` + `scripts/wizard/tutorial_steps.lua`), escrito a partir das regras:
  - *economia* (trabalhadores, estaleiros da barra lateral, MCVs implantáveis, construtores do TA): implantar, coletar, campo
    warp / suprimento quando necessário, a cadeia de requisitos de um produtor tipo quartel, treinar duas unidades, destruir
    uma construção inimiga que não atira de volta;
  - *guarnição* (Mushroom Wars, State.io): enviar para uma construção neutra, tomar mais duas, tomar uma inimiga;
  - *construção automática* (lane battlers): apertar Construir duas vezes, melhorar, segurar;
  - *genérico*: mover o exército, atacar.
  Só passam os comandos da etapa (`before_command`), o inimigo espera (suas unidades ainda se defendem), o painel e a seta são
  desenhados pelo script de visão, os recursos da etapa são repostos para não travar. Não serve para economias de estradas no
  estilo Knights and Merchants (lá, a forma genérica).
- **Tutorial discreto**: dicas de uma vez só (câmera, trabalhadores ociosos, recursos insuficientes, limite de suprimento,
  exército, guarnições, ataque) de `scripts/wizard/hints_config.lua`; as dicas vistas ficam salvas no perfil e as
  configurações as desligam.
- **Fases**: `maps/wiz_level_N.json` = o mapa base + `generate` (o preset do template, um seed por fase, o mapa base como
  modelo); dificuldade 0..2 pela curva, a cada N fases um chefe; recompensas, recompensas da primeira vitória e tempos de
  estrelas em `ui/wizard.json`.
- **Meta**: `ui/wizard.json` (configuração), `ui/profile.json`, `ui/shop.json`, `ui/screens.json` (`start: home`,
  `afterMatch: result`), telas do kit. As melhorias chegam à partida como `initialVars` `wiz_up_<id>`, e
  `scripts/wizard/meta_bonus.lua` as transforma em bônus / recursos iniciais.
- **Dinheiro**: `plugins` / `store` / `ads` no `game.json` por plataforma (blocos de anúncio de demonstração do Yandex e
  simulações de desktop: troque os ids).

## Etapa do agente: o jogo descrito, um a um

O template é um ponto de partida, não o jogo. Com `agent` ligado (padrão), o assistente escreve `WIZARD_TASK.md` no projeto — a
descrição / GDD literal, mais como manter coerentes as partes do assistente (etapas do tutorial, dicas, configuração da meta,
arquivos de idioma) — e entrega a um agente de IA conectado, que reconstrói o projeto pelas ferramentas MCP do editor (ao vivo
no editor): tipos, atributos, facções, mecânicas (regras ou Lua), mapas, condições de vitória, textos; depois verifica com
`rules_lint`, `map_validate`, `match_run` e escreve «## Агент: что сделано» no WIZARD.md.

- Conexão: o assistente mostra o mesmo painel «conectar um agente de IA» que a janela Agent (`EditorMcpConnect.*`,
  `core/McpInstall.h`): um clique grava o aiversemcp na configuração do cliente, e essa única conexão serve aos dois.
- Execução pelo editor (`core/WizardAgent.*`, pelo shell de login do usuário para que a CLI seja encontrada):
  Claude Code `claude -p --output-format stream-json --allowedTools mcp__aiverse` e Codex CLI `codex exec --full-auto`,
  a tarefa pelo stdin, a pasta do projeto como diretório de trabalho. A CLI é procurada no PATH, nas pastas de instalação
  comuns, no shell do usuário e dentro dos apps de desktop (Claude Code em
  `~/Library/Application Support/Claude/claude-code/<version>/…/claude.app`, Codex em `ChatGPT.app/Contents/Resources/codex-cli`).
  O progresso aparece no feed de atividades (janela Agent), na barra de status e na página do assistente (botão Parar); o log
  completo em `logs/wizard-agent-<hora>.log`.
- Cursor, Windsurf, Claude Desktop: «Copiar a tarefa para o agente» coloca a tarefa na área de transferência para colar no cliente.
- Sem chave de API no editor: o agente roda no cliente e na conta do próprio usuário.
- Ditado: «Ditar» abaixo do campo da ideia (página Ideia) e do campo de ideias de meta (página Meta) — reconhecimento de fala do
  macOS (`apps/editor/EditorSpeech_mac.mm`: Speech + AVFoundation, o primeiro idioma do jogo como localidade; o sistema pede uma
  vez acesso ao microfone e ao reconhecimento). O texto é acrescentado ao campo. Outras plataformas: botão desativado.
- `ai` na especificação (textos por idioma: slogan, introdução, tipos, recursos, moedas, melhorias, fases) permite que um
  agente que chama `game_make` por conta própria passe os nomes e textos do jogo.

O tutorial na visão Game do editor: o editor não tem interface de jogo para mostrar o painel de etapas, então o script de
visão envia `wiz_tut_off` e o mapa do tutorial é jogado lá como uma partida comum. Painel, seta e filtro funcionam no cliente
do jogo (AiverseGame, builds). O editor abre a primeira fase, não o tutorial.

## Verificação

Com `verifyLevels` o assistente joga o tutorial sozinho (`initialVars {wiz_tutorial_auto: 1}`: a ordem `auto` de cada etapa
passa pelo mesmo filtro do jogador) e uma amostra de fases com bots, e relata erros de carregamento, erros de script e um
tutorial que não terminou. Testado em todos os templates: tudo termina, exceto ambersands (construção só na rocha), planets e
underlord (movimento / escavação em terreno especial) — o assistente avisa isso.

Testes: `game_wizard_plan_picks_a_template_by_the_idea`, `game_wizard_makes_a_playable_project`,
`script_command_card_false_hides_the_button`.

## Mudança na engine

Comandos de script aceitam `"card": false` (sem botão / atalho; enviados por scripts de visão, documentos do HUD, MCP) — o
Próximo / Pular do tutorial (`modules/rts/RtsScriptCommands.cpp`, cartões de comando do cliente e do editor).
