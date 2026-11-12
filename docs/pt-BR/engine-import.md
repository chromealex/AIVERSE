# Importar um projeto de outra engine

A implementação está em `agent-engine-import`; a especificação de origem é [engine-import-spec.md](../engine-import-spec.md).
Os checksums da simulação e dos templates não mudaram. Esta é uma base de importação funcional, **não a conclusão de todas as
etapas da especificação**. A cobertura e o trabalho pendente abaixo fazem parte da passagem para o MAIN.

## CLI e dados intermediários

Python 3.8+; nenhum pacote Python é necessário. O lançador executável é `tools/importers/port/aiverse_port` (ou a forma
explícita com Python abaixo):

```sh
python3 tools/importers/port/aiverse_port.py scan /path/to/UnityProject
python3 tools/importers/port/aiverse_port.py extract /path/to/UnityProject --out /tmp/port.json
python3 tools/importers/port/aiverse_port.py emit /tmp/port.json --out /path/to/Imported
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported --scale 1 --mode auto
python3 tools/importers/port/aiverse_port.py report /path/to/Imported
python3 tools/importers/port/aiverse_port.py item /path/to/Imported --id ITEM_ID
python3 tools/importers/port/aiverse_port.py resolve /path/to/Imported --id ITEM_ID --how lua --reason 'Verified by scenario…'
```

Cada comando imprime JSON; em caso de falha, sai com status 2. `scan` não escreve nada. As pastas de entrada e saída não podem
se sobrepor. Não rode importações simultâneas para o mesmo destino. `extract` registra caminhos de origem, componentes
serializados completos, campos, código-fonte dos scripts e seu uso. `emit` escreve um manifesto do jogo, regras, mapas,
`.aiverse`, assets copiados, opcionalmente sprites/tiles/áudio/efeitos, `port.json` e relatórios JSON/Markdown. `sourceRoot`
ainda precisa existir ao escrever os assets copiados. O `.rpakignore` gerado exclui dos pacotes de execução o índice
intermediário de scripts e os relatórios. O manifesto marca o conteúdo importado com `external: true`, e o aiversebuild
propaga isso para os pacotes de recursos.

`--scale` é células por metro; `--mode auto|2d|3d` escolhe o plano da cena e a câmera. Unity 3D inverte o Z; Unity 2D projeta
XY no plano do chão. O Vector2 do Godot vai para XZ. As origens são deslocadas para manter as posições do mapa positivas. Donos
não são inventados: só um campo `owner` explícito do Unity é mapeado, com aviso de aproximação. A numeração dos jogadores da
origem precisa ser conferida.

`lib/port/mapping.json` define apelidos de campos. `--mapping FILE` adiciona ou substitui apelidos para os alvos suportados.
`fireRate` é interpretado como tiros por segundo (inverso do cooldown) e marcado explicitamente como aproximação. Os tempos de
recarga e de construção no JSON da engine são em **segundos**, não em ticks de simulação. Cada campo inferido tem origem,
confiança e caminho de destino.

### Importações repetidas

`.port-state.json` guarda os hashes dos arquivos/tipos gerados. Arquivos editados ou já existentes são preservados, inclusive
quando um editor manteve o marcador `ported`. As regras são mescladas por tipo; tipos editados perdem esse marcador. Novos
tipos ainda podem ser adicionados. Alterações do usuário nas configurações de nível de regras são preservadas. Mapas e
arquivos binários são protegidos por inteiro. Arquivos gerados obsoletos não são apagados. O manifesto de estado pertence ao
importador; não o edite.

As anotações de resolução sobrevivem a novas importações só enquanto o item e a impressão digital da entidade/script de origem
correspondente não mudarem. Uma origem alterada reabre o item para revisão.

## Editor e MCP

Abra **Projeto → Importar projeto de engine** ou `editor_window {id:"project_import", open:true}`. Escaneie a origem, escolha
destino/escala/2D/3D e importe. A interface roda o importador fora da thread de renderização e pode abrir o resultado.
**Copiar passagem para IA** copia uma tarefa explícita e abre a janela Agent; não afirma que um agente foi iniciado.

A branch base atual não tem o assistente «Criar um jogo». A integração com a branch separada `agent-gamewizard` fica com o
MAIN; nenhuma branch alheia foi mesclada.

Operações registradas (as configurações de importação também podem ficar aninhadas em `options`):

- `port_scan {path}`: detecção da origem, verificação da serialização, listas de assets/cenas/scripts.
- `port_import {path,out,scale?,mode?,mapping?}`: executar a extração e a escrita.
- `port_report {path?}`: ler o relatório do destino (por padrão, o projeto aberto).
- `port_item {path?,id}`: item de origem, componentes, scripts vinculados e tipos gerados.
- `port_resolve {path?,id,how,reason}`: anotar trabalho verificado (`rules`, `lua`, `native`, `dropped`).

A importação via MCP é síncrona, como o importador VFX existente; projetos grandes podem demorar. `AIVERSE_PYTHON` escolhe o
interpretador. Metacaracteres do shell do Windows `%`, `!`, aspas e quebras de linha nos argumentos são rejeitados, não
interpolados.

Fluxo do agente: examinar os itens ausentes, consultar `rules_schema`, implementar as mecânicas adequadas como dados, depois
Lua, depois um módulo nativo se necessário. Usar `scenario_run` e partidas de bots para comparar DPS, tempo de produção e
velocidade de movimento. Comparar capturas quando a imagem de origem for fornecida. Resolver só o trabalho verificado; explicar
o comportamento descartado de propósito. C#/GDScript/Blueprint não são traduzidos mecanicamente nem executados.

## Cobertura e trabalho pendente da especificação

| Área | Implementado | Pendente |
|---|---|---|
| Código comum do Unity | YAML, busca de GUID, prefabs aninhados e variantes movidos para `lib/unity`; importações de compatibilidade mantêm o VFX funcionando | Os limites de aninhamento do parser comum continuam valendo |
| Cenas/tipos do Unity | Transformações de mundo, objetos de cena, sobrescritas de prefabs, uso de scripts e campos serializados, candidatos a ScriptableObject, ordem das cenas de build | Considerar a escala das transformações nos limites do modelo, agrupamento semântico robusto de tipos, dedução de jogadores/inícios |
| Assets/materiais | Modelos/texturas/áudio e dependências vizinhas mantêm os caminhos; fatores PBR e primeira sobrescrita de material; metadados de escala do modelo; opcionalmente PSD/TIFF → PNG pelo adaptador ImageMagick/ffmpeg existente | Correspondência de slots de material, mapas empacotados, configurações de eixos na importação, recorte de quadros de animação; a falta de conversor de imagens é relatada |
| Unity 2D | Recortes de sprites do atlas, células explícitas de Tilemap assadas no atlas/camada fixa, câmera ortográfica | RuleTiles, tiles transformados/tingidos, layouts arbitrários de Grid, tiles animados, pivôs/ordenação de sprites |
| Áudio/FX | Áudio em loop da câmera/gerenciador → playlist da partida; o conversor VFX existente escreve por uma área de preparação protegida | Roteamento do mixer e ligação semântica de efeitos a eventos |
| Terreno | Dados assados opacos e componentes Terrain são relatados | A conversão de alturas/splat/árvores/detalhes do TerrainData não está implementada |
| Godot | Detecção de cenas/recursos de texto, assets externos, translações simples, sobrescritas numéricas exportadas, índice de scripts, ordem da cena principal | Herança de cenas, composição completa de transformações, decodificação de Variants complexos multilinha, conversão de TileMap/GridMap, clipes de animação |
| Unreal | Receptor JSON e exportador Python do editor para atores carregados, propriedades candidatas por reflexão, malhas/texturas/áudio via AssetExportTask | O exportador **não foi executado no Unreal**; paisagem, enumeração completa de variáveis de Blueprint e exportação de grafos não implementadas |
| Editor | Janela de importação independente e localizada, item de menu, MCP e passagem para IA copiável | Integração com o assistente, seletores de categorias de assets, início direto do agente |

Binários `.asset` nativos desconhecidos do Unity podem existir mesmo com Force Text (iluminação/NavMesh assados). Eles são
relatados um a um. A serialização binária de cenas/prefabs/materiais é rejeitada com a instrução de ativar Force Text.
`.uasset` nunca é analisado fora do Unreal.

O utilitário para o Unreal é `tools/importers/unreal_export/export.py`; chame `export_project(destination)` dentro do Unreal
Editor com o plugin de Python ativado e uma pasta de saída nova. Referências da API:
[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6),
[EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6).
Ele exporta o nível carregado no momento e os assets de `/Game`; não percorre níveis não carregados.

## Verificação, 2026-11-03

Os projetos sintéticos em `tests/data/port` são dados de teste originais. O FBX do Unreal é o `tests/data/models/cube.fbx`
CC0 já existente, escrito à mão. Nenhum asset de jogo baixado foi commitado.

- 34 testes existentes do importador VFX passam após a mudança do código comum.
- 15 testes do importador de projetos cobrem a rejeição de serialização, sobrescritas de prefabs, unidades, edições
  protegidas, idempotência (inclusive a reescrita de `port.json`), invalidação ao mudar a origem, segurança de caminhos,
  sprites/tiles e resolução pela CLI. Com `AIVERSE_PORT_HEADLESS`, os cinco projetos carregam na engine real e o projeto de
  combate do Unity termina com a vitória do jogador 1.
- Suíte C++: 5857 verificações, uma falha de listener de localhost própria do sandbox; esse teste passou separadamente fora
  do sandbox de rede (27 verificações). Um teste adicional da ponte MCP passou (5 verificações).
- Aviso de compilador preexistente, fora desta mudança: `fb` não usado em `engine/fx/FxEffect.cpp:1058`.

Dois repositórios públicos de RTS foram baixados apenas em `/tmp` e importados; **cada mapa gerado foi carregado
individualmente com `aiverse_headless --max-seconds 0`**, não só o primeiro/padrão:

| Origem | Revisão | Resultado | Relatório |
|---|---|---|---|
| [Unity FPS RTS](https://github.com/aydenspringer/Unity-FPS-RTS-System) | `df468f644e6aff145d2eb219dca87f99742ca81d` | 509 assets copiados, 375 candidatos a tipo, 30 mapas; carregam 30/30 | 486 aproximados, 1913 ausentes; scripts, componentes de renderização/física, dados assados opacos e assets não suportados precisam de revisão |
| [Godot Open RTS](https://github.com/lampe-games/godot-open-rts) | `a628ad3bb6a9a903587a181a7ee3300dbf735e81` | 388 assets copiados, 552 candidatos a tipo, 84 mapas; carregam 84/84 | 638 aproximados, 2017 ausentes; lógica de scripts, herança de cenas, recursos embutidos e sistemas de tiles/animação precisam de revisão |

São verificações de importação/carregamento, **não ports jogáveis um a um**. O número de candidatos não deve ser apresentado
como mecânicas de jogo reconstruídas com sucesso. A execução no Unreal, no Windows, no Android e na web segue sem verificação.

O formulário de importação independente foi inspecionado no editor nativo do macOS. A entrada automatizada por área de
transferência/teclas rápidas nem sempre preenchia o texto do ImGui, então não se afirma uma importação completa por cliques;
as mesmas operações foram verificadas pela ponte MCP em C++ e pela CLI. O editor do macOS/Metal compila.

Depois de mesclar o master atual (`82c47120`), dois laços `std::filesystem::path` em `NativeBuild.cpp` passaram a
`const auto&` para compilar com o AppleClang. É uma correção mínima de compatibilidade num arquivo compartilhado. O MAIN deve
republicar os pacotes do editor após integrar e verificar esta branch; esta branch não publica builds.

Verificação final após o merge do master e a correção de compatibilidade:

- Editor do macOS (Metal), aiversemcp, aiverse_headless e aiverse_tests recompilados com sucesso.
- Suíte C++ completa fora do sandbox com sockets restritos: **5930 verificações, 0 falhas**.
- CTest: os alvos existentes dos importadores VFX e de projetos passam (34 + 15 testes Python).
- Ponte MCP específica: 5 verificações; utilitários de build do editor nativo: 42 verificações, todas passam.
- A importação de ponta a ponta via `aiversemcp --no-editor` aceita `options` aninhadas, aplica scale=2 e trata um caminho de
  saída com espaços e apóstrofo.
- `skirmish --seed 3`: o azul vence aos 470 segundos de jogo; `mushrooms --seed 3`: o vermelho vence aos 92 segundos.
