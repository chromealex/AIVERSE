#!/usr/bin/env python3
"""Find repository recipes and produce explicit, side-aware library selections (stdlib only)."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT/'skills/catalog.json'
# Category folders: skills/<category>/<skill>/SKILL.md (AGENTS.md 'Adding and updating repository skills').
CATEGORIES = ('core','gameplay','ai','world','ui','visuals','network','verification','meta','workflow','editor')

def load_meta():
    spec = importlib.util.spec_from_file_location('meta_catalog', ROOT/'tools/meta/catalog.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

# Search vocabulary is routing metadata, not another copy of skill instructions.
ALIASES = {
 'aiverse-character-locomotion':'wallrun wall-run wall slide walljump dash air dash grapple hook glide glider one-way platform drop through parkour паркур бег по стене рывок крюк кошка планирование односторонн платформ locomotion movement jump coyote slope ledge ladder swim climb mantle vault движение прыжок склон уступ лестница плавание лазание',
 'aiverse-ecs-shared-static':'ecs shared static immutable configuration prefab interned dedup memory snapshot rollback staticref sharedref scene read-only component component storage game editor общие статические компоненты префаб память конфиг дедупликация снапшот откат',
 'aiverse-voxel-world':'voxel voxels block blocks chunk chunks minecraft dig mine place raycast region save воксел блок чанк майнкрафт копат ставит регион',
 'aiverse-voxel-light':'voxel light skylight blocklight torch shadow flood свет освещен факел тень воксел',
 'aiverse-voxel-mesh':'voxel mesh meshing greedy ambient occlusion chunk render меш воксел затенен рендер чанк',
 'aiverse-voxel-gen':'voxel worldgen generator terrain biome cave ore tree seed infinite генерац биом пещер руда дерев бесконечн мир',
 'aiverse-voxel-ticks':'voxel fluid water lava flow sand gravity growth tick жидкост вода лава песок рост тик',
 'aiverse-voxel-nav':'voxel mob pathfinding spawn monster path моб путь спавн монстр воксел',
 'aiverse-voxel-replication':'voxel network multiplayer stream chunk server client replication сеть мультиплеер стрим чанк сервер реплик',
 'aiverse-world-calendar':'calendar clock day week shift schedule routine needs errands календар часы день недел смен расписан распорядок потребност поручен',
 'aiverse-weather-timeline':'weather rain wind gust wet puddles sky погода дождь ветер лужи мокр небо',
 'aiverse-vehicle-fx':'headlight brake indicator hazard beacon siren lamp skid tyre marks фары стоп поворотник аварийк мигалк сирен следы шин',
 'aiverse-swept-shapes':'swept sweep crossing pedestrian collision slide wall broadphase переход пешеход столкновен скольжен стен',
 'aiverse-derived-keys':'cache keys trigger variables lookup performance кэш ключ переменн поиск производительн',
 'aiverse-city-traffic':'traffic cars lanes signals lights pedestrians bus police ambulance fire tow gangs population трафик машин полос светофор пешеход автобус полици скор пожар эвакуатор банд жител',
 'aiverse-rules-files':'rules include split rules_io правила разбить include',
 'aiverse-nereid-water':'water ocean lake river swim nereid вода река океан',
 'aiverse-nereid-buoyancy':'buoyancy float drag swimming water плавучесть сила вода',
 'aiverse-nereid-interactions':'water splash ripple wake entry exit брызги волны след',
 'aiverse-nereid-cascades':'cascade waterfall river flow join водопад каскад соединение',
 'aiverse-nereid-currents':'current river flow stream shear течени река скорость',
 'aiverse-nereid-vortices':'vortex water eddy whirlpool swirl вихрь водоворот течение',
 'aiverse-physics':'box3d physics rigid collider collision hinge weld sensor raycast физик коллайдер столкнов шарнир датчик character controller kinematic capsule prediction platform push персонаж предсказание',
 'meta-economy':'shop store offer currency wallet магазин оффер валют кошелек покупк',
 'meta-social':'leaderboard ranking chat лидeрборд лидерборд рейтинг таблица чат',
 'meta-progression':'daily energy battlepass achievement ежеднев энерг достижен боевой пропуск',
 'meta-clans':'clan guild клан гильди', 'meta-items':'equipment instance экипиров предмет',
 'meta-tournaments':'tournament league турнир лиг', 'meta-loot':'chest loot сундук лут',
 'meta-albums':'album collection trade альбом коллекц обмен', 'meta-jobs':'queue crafting очередь крафт',
 'meta-research':'research prestige исследован престиж', 'meta-quests':'quest квест задан',
 'meta-events':'event событ ивент', 'meta-wars':'clanwar войн',
 'meta-async-pvp':'raid revenge рейд месть асинхрон', 'meta-matchmaking':'matchmaking матчмейкинг подбор',
 'meta-ratings':'elo glicko эло глико', 'meta-referrals':'referral promo реферал промокод',
 'meta-ads':'rewarded ads реклам', 'meta-offer-chains':'chain цепочк',
 'meta-notifications':'notification badge уведомлен', 'meta-offline':'offline income оффлайн доход',
 'meta-database':'database sqlite migration бд баз данных миграц',
 'meta-backend':'entitlement subscription подписк прав',
 'meta-client':'client outbox retry клиент повтор', 'meta-server':'server сервер сервак',
 'meta-ui':'screen interface экран интерфейс',
 'aiverse-card-runtime':'cards deck карточ колод', 'aiverse-turn-scheduler':'turn пошаг ход',
 'aiverse-grid-puzzles':'sokoban deadlock сокобан',
 'aiverse-puzzle-solvers':'puzzle match3 match-3 cascade shuffle hint flood pipes connectivity sliding 15-puzzle klotski packing polyomino sudoku nonogram constraint csp solver головолом три-в-ряд пятнашки судоку японский кроссворд', 'aiverse-rhythm-timing':'rhythm ритм',
 'aiverse-narrative-graphs':'story narrative dialogue relationship knowledge bark visual novel backlog consequence сюжет диалог отношения реплик визуальная новелла', 'aiverse-bots-commander':'rts bot ртс бот',
 'aiverse-bots-combatant':'fps bot фпс бот', 'aiverse-camera':'camera камер',
 'aiverse-orchestrator':'skill catalog orchestration скилл каталог оркестратор',
 'aiverse-combat-actions':'hitbox hurtbox dodge parry stagger poise lockon хитбокс уклон парир стаггер захват',
 'aiverse-stealth-senses':'stealth sight hearing noise footprint scent suspicion alert disguise hiding стелс зрение слух шум след подозрен тревог маскиров укрыт',
 'aiverse-settlement-production':'colony settlement city factory production recipe workstation haul logistics job schedule needs mood power grid electricity fluid water brownout disaster колони поселен город фабрик производств рецепт верстак перенос работ расписан потребност настроен электросет энерг вода авари бедстви',
 'aiverse-survival-ecology':'survival temperature cold hypothermia wet hunger thirst stamina shelter clothing weather ecosystem population predator prey animal deer wolf regrowth renewable выживан температур холод мокр голод жажд выносливост укрыти одежд погод экосистем популяц хищник жертв животн олен волк восстановлен ресурс',
 'aiverse-cinematic-gameplay':'cinematic cutscene timeline qte slowmo hitstop timescale sync staging checkpoint катсцен ролик таймлайн замедлен синхрон постановк чекпоинт',
 'aiverse-card-games':'card deck hand mulligan stack priority counter ccg deckbuilder board game hidden reveal карт колод рука муллиган стек приоритет контр настольн скрыт',
 'aiverse-tactics-grid':'tactics tactical turn hex grid cover flank overwatch initiative action points wego line of fire тактик пошагов гекс сетк укрыт фланг дозор инициатив очки действ',
 'aiverse-cards-turns-bots':'card bot turn planner tactics ai opponent evaluator lookahead бот карт планировщ ход тактик противник',
 'aiverse-action-bots':'bot opponent squad role formation objective melee shooter бот противник отряд роль формаци цел',
 'aiverse-vehicle-controllers':'vehicle car kart truck tank hover plane flight drive gear drift traction seat race lap checkpoint racing машин авто танк самолет гонк круг чекпоинт сиден дрифт',
 'aiverse-sports-rules':'sport football soccer hockey basketball ball goal pass shot tackle foul penalty formation спорт футбол хоккей мяч гол пас удар отбор фол пенальти расстановк',
 'aiverse-match-progression':'perk modifier xp experience level levelup offer reroll summon ally deck loadout pickup magnet перк модификатор опыт уровень прокачк призыв союзник колод лут магнит',
 'aiverse-physical-interactions':'carry pickup throw push pull rope joint door lock key platform elevator breakable debris destructible переноск подня брос толка тянут верев канат двер замок ключ платформ лифт разруш обломк',
 'aiverse-roads':'road roads spline street avenue junction crossing bridge tunnel parking bay highway rural дорог сплайн улиц проспект перекрест переход мост тоннел парковк карман шоссе грунтовк',
 'aiverse-building-gen':'building house procedural interior floors stairs stairwell door window room enterable здание дом процедурн этаж лестниц дверь окно комнат интерьер',
 'aiverse-gear-tiers':'gear armor helmet vest backpack shield tier level durability wear absorb capacity broken броня шлем жилет рюкзак щит уровень прочность износ вместимость экипировк',
 'aiverse-snapshot-delta':'snapshot delta compression baseline ack bandwidth netcode replication diff дельта снапшот сжатие трафик канал репликац',
 'aiverse-battle-royale':'battle royale last standing shrinking zone circle storm safe zone phase red zone bombardment airdrop supply drop plane jump skydive parachute knockdown knock revive placement killfeed lobby countdown королевская битва зона сужающ круг фаз красн бомбардир аирдроп сброс самолет прыжок парашют нокдаун нокнут поднять место киллфид лобби отсчет',
 'aiverse-boss-encounters':'boss telegraph windup cone slam phase enrage adds rotation status stun burn slow mob wander hunt flee swarm босс телеграф замах конус фаз ярост призыв ротаци статус оглушен горен замедлен моб блужд охот бегств рой',
}

# MCP and repository workflows (no runtime library): id, summary, source of truth.
WORKFLOWS=[
 ('aiverse-mcp-coverage','Audit real MCP tools/list, GUI-only restrictions and verified capability calls','docs/agents/mcp-coverage.md'),
 ('aiverse-warcraft3-import','Import licensed Warcraft III map archives and assets using the bounded MPQ/Studio pipeline','docs/warcraft3-import.md'),
 ('aiverse-gpu-batching','Profile renderer instance/material batching, Metal GPU counters and A/B capture parity','docs/geometry-performance.md'),
 ('aiverse-lens','LENS camera scene authoring, MCP validation and native Studio PNG capture','apps/editor/core/LensOps.cpp'),
 ('aiverse-lens-native','LENS native multi-camera scheduling, prepared-frame replay and GPU capture ownership','docs/camera-system.md'),

 ('aiverse-scene-entities','Native scene entity and component MCP authoring with stable light identity','docs/studio-light-entities.md'),
 ('aiverse-packages','Optional content from the AIVERSE-PACKAGES catalog: search, install into projects, lock, verify, publish by PR','docs/packages.md'),
 ('aiverse-lighting','Light entity authoring, probe GI and Luma through native MCP and shared runtime','docs/dynamic-gi.md'),
 ('studio-ui','HTML5 Studio editor windows with shared UI v1 components, typed native bridge and visual QA','docs/studio-design-system.md'),
 ('aiverse-human-creator','VIVID digital humans: DNA skin/eyes/hair/proportions, 52 rest channels, routes, rig and skin edits via human_* MCP or Studio Human Creator','docs/vivid.md'),
 ('aiverse-bots-configure','Computer player setup: rules.ai knobs, play styles, scripted director, strategic bots','docs/ai-controllers.md'),
 ('aiverse-bots-tune','Bot strength tuning with bot batches and a sweep over every map','engine/game/ComputerPlayer.cpp'),
 ('aiverse-bots-debug','Diagnose bots that do not play and teach ComputerPlayer a new mechanic','engine/game/MechAI.cpp'),
 ('aiverse-map-build','Build a playable map: generation or painting, starts, symmetry, validation, bot check','tools/mcp/main.cpp'),
 ('aiverse-unit-balance','Unit and faction balance: stats, duels, counter matrix, rules edits, bot batches','apps/editor/core/BalanceOps.cpp'),
 ('aiverse-lua-gameplay','Simulation Lua gameplay with determinism and per-tick budget rules','docs/scripting.md'),
 ('aiverse-campaign','Campaign mission chain, objectives, dialogs, camera, carry-over, campaign_run','engine/game/Campaign.h'),
 ('aiverse-new-mechanic','End-to-end engine mechanic: branch, data, code, tests, AI, docs, prototype, checksums','docs/game-format.md'),
 ('aiverse-look','Lighting, post effects with phone cost, materials, model view blocks, screenshots','docs/postfx.md'),
 ('aiverse-fx','Visual effects from templates or Unity prefabs, tuning, event binding, preview','docs/particles.md'),
 ('aiverse-niagara-vfx','Legacy Niagara-style discovery alias; use the official SpectraFX workflow','docs/spectrafx.md'),
 ('aiverse-spectrafx','SpectraFX universal graphs, particle assets and ECS presentation for native games','docs/spectrafx.md'),
 ('aiverse-prisma','PRISMA universal HDR post-processing, screen effects, volumes, camera profiles and GPU quality','docs/prisma.md'),
 ('aiverse-animation-ecs','Legacy C++ presentation ECS animation API and fixed-point root-motion handoff','engine/animation/EcsAnimationSystems.h'),
 ('aiverse-view','Genre-independent scene View: appearance, layers, visibility, packets and rendering adapters','docs/view.md'),
 ('aiverse-game-screens','Game screens and HUD with RmlUi and Lua, validation, phone previews','docs/ui-scripting.md'),
 ('aiverse-release-build','Platform packaging: branding, toolchains, size estimate, build and status','docs/overview.md'),
 ('aiverse-port-game','Port Unity or Godot projects: scan, import, resolve report items, verify','docs/engine-import.md'),
 ('aiverse-agent-branch','Agent branch and worktree workflow: merge main, area, builds, commits','AGENTS.md'),
 ('aiverse-main-merge','MAIN merge of agent branches with full test and checksum gates','AGENTS.md'),
 ('aiverse-game-wizard','Whole game from art and an idea with game_make, then bot, screen and phone checks','docs/game-wizard.md'),
 ('aiverse-tiles-2d','2D tile games: tilesets, tile layers, painting, sprites, atlases','tools/mcp/main.cpp'),
 ('aiverse-planets','Planet and orbit maps: bodies, generation presets, layouts, orbit fleets','docs/planets.md'),
 ('aiverse-strategic-campaign','Strategic layer: regions, armies, turn orders, bot factions, spawned battles','docs/game-format.md'),
 ('aiverse-special-modes','Top-down heroes, extraction raids and city traffic: data, samples, bots, checks','docs/topdown.md'),
 ('aiverse-assets-import','Models, textures, sounds and animations: import, view blocks, Spine, Unity animation','docs/models.md'),
 ('aiverse-shader-graph','Shader Graph materials, post effects, particles and baked lighting with phone cost','docs/shader-graph.md'),
 ('aiverse-localization','Lang keys, translations, consistency checks, fonts and long strings on phones','engine/i18n/README.md'),
 ('aiverse-audio','audio.json sounds, buses, music and event bindings, verified by recording the mix','docs/game-format.md'),
 ('aiverse-visual-qa','Screenshot checks: editor, headless Xvfb, before/after, phone profiles, quality levels','AGENTS.md'),
 ('aiverse-mobile-perf','Phone budgets: frame and tick, post effects, LOD, lighting, Lua and simulation cost','docs/lua-coverage.md'),
 ('aiverse-online-battles','Online PvP and PvE: rollback, tick rate, simulated mobile links, reconnect','docs/battle-net.md'),
 ('aiverse-determinism-triage','Checksum mismatches: first differing tick and field, causes, fix and proof','docs/native-toolkit.md'),
 ('aiverse-devlog','Website development log: devlog entries with real engine screenshots, system covers and the main-page showcase','tools/site/build.py'),
 ('aiverse-site-publish','Publish to the website: roadmap item before starting, which site data a change touches, screenshots for visual features, build.py publish','tools/site/build.py'),
 ('aiverse-docs-upkeep','README and translations, translated docs, format sections, readme check','tools/readme/check.py'),
 ('aiverse-shared-capability','Make a module-only feature usable by every project: move to engine/libraries, data blocks instead of genre modes, no compatibility headers, thin host adapters, checksum A/B','AGENTS.md'),
 ('aiverse-capability-audit','Feasibility and gap estimates from the genre status file, catalogs and module docs, not from memory','docs/capability-status.json'),
 ('aiverse-capability-request','Prepare and submit consented, minimal missing-capability requests to AIVERSE GitHub Issues','tools/feedback/capability_request.py'),
 ('aiverse-terrain','Terrain levels, heightfield, sub-cell detail, materials and heightmaps through MCP','docs/terrain.md'),
 ('aiverse-terra-first','TerraWeave-first world capability selection for every new game; custom only for verified gaps','skills/workflow/aiverse-terra-first/SKILL.md'),
 ('aiverse-terra-vegetation','Native Arbor trees, dense grass patches, rocks, PBR and LOD authoring','skills/world/aiverse-terra-vegetation/SKILL.md'),
 ('aiverse-terraweave','Procedural world terrain, biomes, Arbor/Flora, Horizon HLOD, authored seams and native C++ scene integration','docs/terrain/TERRAWEAVE.md'),
 ('aiverse-water','Spline straits, rivers and open sea with shore types, bridges and the water window','docs/water.md'),
 ('aiverse-physis-authoring','PHYSIS map configuration through revision-checked MCP operations and Undo/Redo','apps/editor/core/PhysisOps.cpp'),
 ('aiverse-physis','Genre-neutral nature, climate, wind, gravity, storms, ECS and Box3D adapter with editor diagnostics','docs/physis.md'),
 ('aiverse-city-blocks','City block paving, curb elevations, planting beds and street furniture','skills/world/aiverse-city-blocks/SKILL.md'),
 ('aiverse-city-planner','Procedural city maps with the city / metropolis generator preset','docs/city-planner.md'),
 ('aiverse-city-economy','City-game commerce, careers and the top-down hero arsenal','docs/commerce.md'),
 ('aiverse-projectile-programs','Ordered projectile flight programs: pierce, split, ricochet, arcs for RTS and FPS','docs/projectile-programs.md'),
 ('aiverse-cloth','Optional XPBD visual cloth: CPU/GPU solver, render, pins, colliders, wind, LOD','docs/cloth.md'),
 ('aiverse-wind-masks','Hierarchical wind masks: shared mask library, per-class profiles, model sidecars, Arbor tree rigs, one GPU graph','docs/wind-masks.md'),
 ('aiverse-cables','Procedural cable geometry: power lines, ropes, wire bundles with sag','docs/cables.md'),
 ('aiverse-paint-layers','View Lua render-texture canvases and ground paint layers','docs/render-texture.md'),
 ('aiverse-script-hud','Data-only HUD bindings: scriptHud panels, world counters, entity actions','docs/script-hud.md'),
 ('aiverse-controls','Client gamepad and region swipe controls in game.json','docs/gamepad.md'),
 ('aiverse-editor-extension','New editor windows, MCP window control, Hierarchy and inspector grouping','docs/editor-windows.md'),
 ('aiverse-modeling','In-engine model, rig and animation authoring through MCP and editor windows','docs/modeling.md'),
 ('aiverse-bunkerwars-skills','Convert Bunker Wars energy skills into rules player abilities','docs/bunkerwars-skills.md'),
 ('aiverse-flow-economy-warfare','Flow-economy wars: radar, jammers, reclaim, stockpiles and interceptors, wind, attack runs, TA import','docs/game-format.md'),
 ('aiverse-voxel-game','Minecraft-like game from the voxel packages and existing libraries: loop order, data, checks, gaps','docs/voxel.md'),
 ('aiverse-strata','STRATA virtual geometry CPU authoring, MCP analysis, native routing and GPU verification','docs/strata.md'),
 ('aiverse-texture-rhi','Native texture dimensions, virtual pages, mip streaming, backend sampling and GPU acceptance','docs/texture-rhi.md'),
 ('aiverse-skill-authoring','Add or change a repository skill with its code, registration, catalog and audit','tools/skills/audit.py'),
 ('aiverse-publish-builds','Editor packages for Windows and macOS on the builds branch (MAIN only)','tools/release/publish_builds.py'),
 ('aiverse-driving','Top-down hero at the wheel: rules driving, handling table, steering wheels, decks, autopilot','docs/driving.md'),
 ('aiverse-racing','Races in any project: rules and map "race" (grid, countdown, ordered checkpoints, laps, standings, wrong way, respawn, race bots, shared HUD and chase camera), racegen tracks','docs/races.md'),
 ('aiverse-city-server','Persistent city server: near-hero snapshots, tickets, seats, accounts, client mirror','docs/city-server.md'),
 ('aiverse-replica','Generic server-authoritative replication: schema, delta snapshots, interest, budget, prediction, clock','docs/replica.md'),
 ('aiverse-place-server','Server-authoritative place hosting: tickets, encrypted UDP, WebSocket gate, reconnect, bots','docs/replica.md'),
 ('aiverse-autolod','Static model LOD generation with meshoptimizer and LOD transitions in the renderer','docs/autolod.md'),
]
WORKFLOW_ALIASES={
 'aiverse-mcp-coverage':'mcp protocol server tools registry capabilities coverage audit tools/list tools/call introspection registration agents проверка скиллы инструменты мсп',
 'aiverse-warcraft3-import':'warcraft warcraft3 wc3 wc3x mpq w3x w3m mdx mdl blp map importer stormlib reforged варкрафт импорт карта архив',
 'aiverse-gpu-batching':'gpu batching material batches instancing metal icb draw calls performance profiling counters renderer батчинг батчи инстансинг производительность отрисовка',
 'aiverse-packages':'package packages catalog plugins download install asset store marketplace pbr textures materials template пакет пакеты каталог плагины скачать установить магазин ассетов материалы текстуры шаблон',
 'aiverse-lens':'lens camera cameras capture offscreen minimap камер захват линз',
 'aiverse-lens-native':'lens native camera replay snapshot gpu capture натив камер захват',

 'aiverse-strata':'strata страта virtual geometry meshlet streaming lod batching батчинг геометрия',
 'aiverse-physis-authoring':'physis authoring mcp map config validate weather wind gravity настройка погода карта',
 'aiverse-physis':'physis nature weather wind gust storms tornado hurricane gravity force physical fields ecs climate environment box3d diagnostics terra water природ природные явления ветер смерч ураган гравитац климат',
 'aiverse-racing':'racing race racer lap laps track circuit kart grid countdown checkpoint finish standings chase гонк гонщик трасс круг финиш старт заезд кольцев',
 'aiverse-driving':'drive driving car vehicle wheel steer autopilot deck tunnel bridge crash вожден машин руль колес автопилот туннел мост авари',
 'aiverse-city-server':'city server persistent world mmo seats tickets accounts mirror сервер город постоянн мир игроков билет счет',
 'aiverse-replica':'replica replication server authoritative snapshot delta interest budget prediction interpolation schema репликация сервер авторитетный снапшот предсказание',
 'aiverse-place-server':'place server host hosting tickets secure udp websocket wss reconnect bots replica сервер хостинг билет переподключение',
 'aiverse-autolod':'lod autolod meshoptimizer simplify levels detail упрощен уровни детализац',
 'aiverse-game-wizard':'wizard game_make whole game idea мастер сделать игру идея',
 'aiverse-tiles-2d':'2d tile tiles tileset sprite тайл тайлы тайлсет спрайт',
 'aiverse-planets':'planet planets orbit moon space планет орбит луна космос',
 'aiverse-strategic-campaign':'strategic campaign total war regions turns глобальн стратегическ карта регион ход',
 'aiverse-special-modes':'topdown twin-stick hero raid extraction traffic cars pedestrians герой рейд эвакуац трафик машин пешеход',
 'aiverse-assets-import':'asset model models texture import animation spine gltf ассет модел текстур импорт анимац',
 'aiverse-shader-graph':'shader graph lightmap bake probe gi шейдер граф запекан лайтмап',
 'aiverse-localization':'localization translation language lang i18n локализац перевод язык',
 'aiverse-audio':'audio sound music sfx voice звук музык озвучк',
 'aiverse-visual-qa':'screenshot visual qa xvfb compare скриншот снимок визуальн проверк',
 'aiverse-mobile-perf':'performance perf fps phone mobile budget lod производительн фпс телефон мобильн бюджет',
 'aiverse-online-battles':'online network pvp pve rollback netcode multiplayer онлайн сеть мультиплеер откат',
 'aiverse-determinism-triage':'determinism desync checksum mismatch детерминизм рассинхрон контрольн сумм',
 'aiverse-site-publish':'publish site website gh-pages pages roadmap deploy upload screenshots system page docs releases выложить залить сайт опубликовать роадмап дорожная карта скрины',
 'aiverse-devlog':'devlog website site screenshots showcase cover changelog progress gh-pages pages ход разработки сайт скриншот скрин кадр обложка витрина',
 'aiverse-docs-upkeep':'docs readme documentation translation документац ридми перевод',
 'aiverse-shared-capability':'genre split shared capability any project module only extract move engine library adapter host compatibility header no genre mode жанр разделение общий любой проект вынести перенести модуль адаптер',
 'aiverse-capability-audit':'mechanics status what exists code audit stale механики пробелы что есть статус устарел capability audit gap gaps missing feasibility estimate limits can engine genre minecraft voxel sandbox чего не хватает оценк можно ли сделать возможност ограничен пробел жанр движок умеет',
 'aiverse-capability-request':'feature request missing capability feedback report engine gap github issue запрос фичи не хватает движок отправить пожелание',
 'aiverse-terrain':'terrain relief heightfield heightmap sculpt cliff detail splat рельеф высот скульпт обрыв ландшафт',
 'aiverse-terra-first':'new game create game world environment terra terraweave новый проект игра создать мир окружение терра',
 'aiverse-terra-vegetation':'terra trees grass rocks vegetation foliage arbor density lod pbr терра деревья трава камни валуны растительность лес',
 'aiverse-terraweave':'terraweave terra weave terrain foliage forest arbor flora horizon hlod canopy biome tree generation seam near far генерация лес деревья биомы ландшафт',
 'aiverse-water':'water river strait sea shore beach cliff embankment вода река пролив море берег пляж набережн',
 'aiverse-city-blocks':'city blocks plaza courtyard curb sidewalk planter flowerbed paving zfight квартал площадь двор бордюр тротуар клумб мощение',
 'aiverse-city-planner':'city town streets blocks district generator gta город улиц квартал генератор',
 'aiverse-city-economy':'commerce business bank loan career rank perk arsenal weapon ammo money бизнес банк кредит карьер ранг оружие патрон деньг',
 'aiverse-projectile-programs':'projectile pierce split fork ricochet chain arc homing снаряд пробит рикошет разделен дуга самонавод',
 'aiverse-cloth':'cloth flag cape curtain sail xpbd ткань флаг плащ занавес парус',
 'aiverse-wind-masks':'wind sway foliage tree branch mask vegetation ветер маска колыхание дерево ветка листва',
 'aiverse-cables':'cable cables wire power line rope трос провод кабель лэп',
 'aiverse-paint-layers':'render texture canvas paint trail mask decal холст рисован след маск',
 'aiverse-script-hud':'hud panel counter label binding entity action худ панел счётчик подпис кнопк',
 'aiverse-controls':'gamepad controller joystick swipe region controls геймпад джойстик свайп управлен',
 'aiverse-editor-extension':'editor window imgui hierarchy inspector details редактор окно иерарх инспектор',
 'aiverse-modeling':'modeling mesh model rig weights animation lod моделирован меш модел риг анимац',
 'aiverse-bunkerwars-skills':'bunker wars mushroom energy skills converter бункер скилл энерг конвертер',
 'aiverse-flow-economy-warfare':'total annihilation ta flow radar sonar jammer stealth submarine reclaim repair assist patrol nuke anti-nuke stockpile intercept wind tidal geothermal bomber self-destruct hpi радар сонар глушилк стелс подлодк разбор ремонт помощь патрул ядерк противоракет накоплен перехват ветер прилив геотерм бомбардировщ самоуничтож',
 'aiverse-voxel-game':'minecraft voxel block sandbox survival craft mining майнкрафт воксел блок песочниц выживан крафт шахт',
 'aiverse-skill-authoring':'skill skills author add catalog audit скилл скиллы добав каталог аудит',
 'aiverse-publish-builds':'publish builds branch release editor zip публикац сборк редактор',
 'aiverse-bots-configure':'bot bots ai computer opponent rules.ai difficulty style personality scripted director бот боты ии компьютер противник сложност стиль характер',
 'aiverse-bots-tune':'bot bots tune strength winrate sweep balance_batch бот боты сила винрейт прогон карт',
 'aiverse-bots-debug':'bot bots debug stuck idle commands mechanic teach бот боты отладк застрял команд механик',
 'aiverse-map-build':'map level generate terrain start symmetry карта карты уровень генерац рельеф старт симметр',
 'aiverse-unit-balance':'balance unit units faction counter duel matrix баланс юнит юниты фракци контр дуэл матриц',
 'aiverse-lua-gameplay':'lua script scripting mechanic луа скрипт механик',
 'aiverse-campaign':'campaign mission briefing objective dialog cutscene кампани мисси брифинг цел диалог',
 'aiverse-new-mechanic':'mechanic feature engine branch механик фича движок',
 'aiverse-human-creator':'human character digital human face skin eyes iris hair groom dna creator vivid metahuman avatar персонаж человек лицо кожа глаза волосы причёска цифровой',
 'aiverse-look':'look lighting light postfx bloom material texture visual свет освещен постэффект материал текстур вид',
 'aiverse-fx':'fx effect particles explosion эффект частиц взрыв',
 'aiverse-niagara-vfx':'niagara unreal vfx legacy compatibility ниягара анриал старая совместимость',
 'aiverse-spectrafx':'spectrafx spectra vfx particles particle graph effects visual ecs game genre gpu cpu spectrum спектрафх эффекты частицы граф универсальн',
 'aiverse-scene-entities':'scene entity component inspector hierarchy transform mcp сущность компоненты лампа свет',
 'aiverse-lighting':'lighting light luma gi probes mcp освещение свет лампа пробы',
 'aiverse-prisma':'prisma postfx postprocess post-processing bloom hdr exposure tonemap lut grade depth of field motion blur taa ssr volume screen-space постобработка постэффект призма камера',
 'aiverse-animation-ecs':'animation ecs clip montage retarget budget fixed root motion animation system анимация бюджет корневая',
 'aiverse-motionweave':'motionweave animation rig retarget pose workers scheduler shared universal анимация риг потоки montage notify root motion warp warping ragdoll inertialization crowd facial viseme монтаж ретаргет рэгдолл варпинг лицо',
 'aiverse-view':'view scene appearance packet visibility frustum layers universal genre independent отображение сцена видимость',
 'aiverse-game-screens':'ui screen menu hud shop rml интерфейс экран меню худ магазин',
 'aiverse-release-build':'build release platform android ios web windows macos linux package сборк релиз платформ',
 'aiverse-port-game':'port import unity godot перенос импорт порт',
 'aiverse-agent-branch':'agent branch worktree commit merge агент ветк коммит',
 'aiverse-main-merge':'main merge integrate checksums мердж слияни интеграц',
}

PACKAGE_REF=re.compile(r'^[a-z0-9][a-z0-9-]{1,62}(@[\^~>=]*[0-9][0-9.]*)?$')

def skill_packages(path):
    """Content packages a skill needs, from its frontmatter line `packages: a, b@^1.2` (AIVERSE-PACKAGES
    catalog ids; agents install them with tools/pkg/aiverse_pkg.py skill <id> --project P)."""
    text=(ROOT/path).read_text(encoding='utf-8')
    if not text.startswith('---\n'):return []
    head=text[4:text.find('\n---',4)]
    for line in head.splitlines():
        if line.startswith('packages:'):
            refs=[x.strip() for x in line[len('packages:'):].replace('[','').replace(']','').split(',') if x.strip()]
            for r in refs:
                if not PACKAGE_REF.match(r):raise ValueError(f'{path}: bad package reference {r!r}')
            return refs
    return []

def skill_path(skill):
    hits=sorted((ROOT/'skills').glob(f'*/{skill}/SKILL.md'))
    if len(hits)!=1:raise ValueError('skill folder not found exactly once: '+skill)
    return hits[0].relative_to(ROOT).as_posix()

def generate():
    entries=[]
    packages=json.loads((ROOT/'libraries/catalog.json').read_text(encoding='utf-8'))['packages']
    ids={p['id']:Path(p['skill']).parent.name for p in packages}
    for p in packages:
        entries.append(dict(id=ids[p['id']],package=p['id'],path=p['skill'],domain='gameplay',
            summary=p['summary'],side=p['layer'],runtime=p['runtime'],target=p.get('cmakeTarget'),
            dependencies=[ids[x] for x in p.get('packageDependencies',[])],
            backendDependencies=p.get('linkDependencies',[]),requiresOwner=p.get('requiresOwner'),
            source=p['source']))
    modules={p['id']:p for p in json.loads((ROOT/'engine/meta/modules.json').read_text(encoding='utf-8'))['modules']}
    for key,summary,skill,source in load_meta().PACKAGES:
        definition=modules.get(key)
        deps=['meta-'+d for d in definition['dependencies']] if definition else {'database':['meta-server'],'ui':['meta-client'],'server':['meta-economy']}.get(key,[])
        target=definition['target'] if definition else {'client':'AIVERSE::meta_client','server':'AIVERSE::meta_server'}.get(key)
        entries.append(dict(id=skill,package=key,path=f'skills/meta/{skill}/SKILL.md',domain='meta',summary=summary,
            side='client' if key in ('client','ui') else 'database' if key=='database' else 'server',
            runtime='static' if target else 'assets' if key=='ui' else 'none',target=target,
            dependencies=deps,backendDependencies=[],source=source))
    for name,description in [('meta-game','Choose meta-game recipes'),('meta-compose','Compose a selected meta project'),('aiverse-orchestrator','Find skills and resolve minimal library dependencies')]:
        entries.append(dict(id=name,package=None,path=skill_path(name),domain='workflow',summary=description,
            side='tools',runtime='none',target=None,dependencies=[],backendDependencies=[],source='tools/skills/catalog.py'))
    entries.append(dict(id='aiverse-roads',package=None,path='skills/world/aiverse-roads/SKILL.md',domain='gameplay',
        summary='Spline roads: road types, map roadNetwork, editor tool and road_* ops, tiles, checks',
        side='tools',runtime='none',target=None,dependencies=[],backendDependencies=[],source='engine/roads/RoadNet.cpp'))
    # Native ECS opt-in engine feature, not a separately installed library.
    # Register it as a gameplay recipe (runtime=none) so skill routing reads
    # its native headers without inventing a duplicate CMake library target.
    entries.append(dict(id='aiverse-ecs-shared-static',package=None,
        path='skills/gameplay/aiverse-ecs-shared-static/SKILL.md',
        domain='gameplay',
        summary='Immutable SharedRef and StaticRef ECS configs, prefab dedup, snapshots and multi-world scene integration',
        side='tools',runtime='none',target=None,dependencies=[],backendDependencies=[],
        source='engine/sim/SharedStaticComponents.h'))
    for name,summary,source in WORKFLOWS:
        entries.append(dict(id=name,package=None,path=skill_path(name),domain='workflow',summary=summary,
            side='tools',runtime='none',target=None,dependencies=[],backendDependencies=[],source=source))
    # Search words of the policy packages live next to their registration script (tools/skills/policy_packages.py).
    policy_aliases=json.loads((ROOT/'tools/skills/policy_aliases.json').read_text(encoding='utf-8')) if (ROOT/'tools/skills/policy_aliases.json').is_file() else {}
    for entry in entries:
        entry['keywords']=(ALIASES.get(entry['id'],'')+' '+WORKFLOW_ALIASES.get(entry['id'],'')+' '+policy_aliases.get(entry['id'],'')).split()
        entry['contentPackages']=skill_packages(entry['path'])
    return dict(schema=1,skills=sorted(entries,key=lambda e:e['id']))

def validate(data):
    entries=data['skills']; by_id={e['id']:e for e in entries}
    if len(by_id)!=len(entries):raise ValueError('duplicate skill id')
    actual={p.relative_to(ROOT).as_posix() for p in (ROOT/'skills').glob('*/*/SKILL.md')}
    registered={e['path'] for e in entries}
    if actual!=registered:raise ValueError('unregistered or missing skills: '+str(sorted(actual^registered)))
    targets=set()
    for e in entries:
        path=ROOT/e['path']
        if path.resolve().parent.parent.parent!=(ROOT/'skills').resolve() or path.parent.parent.name not in CATEGORIES:raise ValueError('skill path escapes catalog or unknown category: '+e['path'])
        if f"\nname: {e['id']}\n" not in path.read_text(encoding='utf-8'):raise ValueError('frontmatter id: '+e['id'])
        if not (ROOT/e['source']).is_file():raise ValueError('missing source: '+e['source'])
        if e['runtime'] in ('static','header'):
            if not e['target'] or e['target'] in targets:raise ValueError('missing or shared library target: '+e['id'])
            targets.add(e['target'])
            if e['target'] not in path.read_text(encoding='utf-8'):raise ValueError('skill missing library contract: '+e['id'])
            if any(d in ('aiverse::meta','AIVERSE::libraries','AIVERSE::mechanics','AIVERSE::genre') for d in e['backendDependencies']):raise ValueError('aggregate dependency: '+e['id'])
        elif e['target'] is not None:raise ValueError('non-code skill has C++ target')
        for dep in e['dependencies']:
            if dep not in by_id:raise ValueError('unknown dependency: '+dep)
        if e['side']=='client' and any(by_id[d]['side'] in ('server','database') for d in e['dependencies']):raise ValueError('client depends on server')
    closure(by_id,list(by_id))
    return by_id

def closure(entries,roots):
    done=set(); active=set(); order=[]
    def visit(key):
        if key not in entries:raise ValueError('unknown skill: '+key)
        if key in active:raise ValueError('dependency cycle: '+key)
        if key in done:return
        active.add(key)
        for dep in sorted(entries[key]['dependencies']):visit(dep)
        active.remove(key);done.add(key);order.append(key)
    for root in sorted(set(roots)):visit(root)
    return order

def search(entries,query,domain='all'):
    words=re.findall(r'[\w-]+',query.lower())
    results=[]
    for e in entries.values():
        if domain!='all' and e['domain']!=domain:continue
        aliases=e['keywords']; tokens=re.findall(r'[\w-]+',(e['id']+' '+e['summary']).lower())
        score=sum(4 if any(w==a or (len(a)>=4 and w.startswith(a)) for a in aliases) else 1 if w in tokens else 0 for w in words)
        if query in (e['id'],e['package']):score+=20
        if score:results.append((score,e))
    return sorted(results,key=lambda row:(-row[0],row[1]['id']))

def plan(entries,roots,side):
    original=sorted(set(roots)); selected=list(original)
    if side=='client':
        # Policies are server capabilities; a client calls their protocol instead of linking them.
        selected=[key for key in selected if entries[key]['side'] not in ('server','database')]
        if any(entries[key]['domain']=='meta' for key in original):selected.append('meta-client')
    elif side=='server':
        selected=[key for key in selected if entries[key]['side']!='client']
    elif side=='both' and any(entries[key]['domain']=='meta' and entries[key]['side'] in ('client','server') for key in selected):
        selected.append('meta-client')
    if any(entries[key]['domain']=='meta' and entries[key]['side']=='server' for key in selected):selected.append('meta-server')
    order=closure(entries,selected)
    choices=[entries[key] for key in order]
    policies=[e['package'] for e in choices if e['domain']=='meta' and e['runtime']=='static' and e['package'] not in ('server','client')]
    gameplay=[e['package'] for e in choices if e['domain']=='gameplay' and e['runtime']=='header']
    commands=[]
    if policies:commands.append('aiverse_add_meta_server(game_meta MODULES '+' '.join(policies)+')')
    if 'meta-client' in order:commands.append('target_link_libraries(game_client PRIVATE AIVERSE::meta_client)')
    if gameplay:commands.append('aiverse_native_use_libraries(aiverse_native_GAME '+' '.join(gameplay)+')')
    content=sorted({p for e in choices for p in e.get('contentPackages',[])})
    return dict(requested=original,side=side,read=[e['path'] for e in choices],
        contentPackages=content,
        installPackages=[f'python3 tools/pkg/aiverse_pkg.py add {p} --project <project>' for p in content],
        selection=[{k:e.get(k) for k in ('id','side','runtime','target','dependencies','requiresOwner')} for e in choices],
        cmake=commands,nativeCmake=(["aiverse_native_use_meta(aiverse_native_GAME game_meta)"] if policies else [])+(["aiverse_native_use_meta(aiverse_native_GAME CLIENT)"] if 'meta-client' in order else []),serverModules=policies,clientFeatures=[entries[key]['package'] for key in original if entries[key]['domain']=='meta'] if side in ('client','both') else [])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['generate','check','list','search','plan'])
    parser.add_argument('query',nargs='?',default='')
    parser.add_argument('--skills',help='comma-separated exact skill IDs')
    parser.add_argument('--domain',choices=['all','meta','gameplay','workflow'],default='all')
    parser.add_argument('--side',choices=['both','client','server'],default='both')
    args=parser.parse_args()
    try:
        expected=generate()
        if args.command=='generate':
            validate(expected);CATALOG.write_text(json.dumps(expected,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Generated skill catalog');return
        data=json.loads(CATALOG.read_text(encoding='utf-8'));entries=validate(data)
        if args.command=='check':
            if data!=expected:raise ValueError('stale skill catalog; run generate')
            print(f'Validated {len(entries)} skills and isolated library mappings');return
        if args.command=='list':result=[{k:e[k] for k in ('id','summary','side','runtime','target')} for e in entries.values() if args.domain=='all' or e['domain']==args.domain]
        elif args.command=='search':result=[dict(score=score,**{k:e.get(k) for k in ('id','summary','side','path','target','contentPackages')}) for score,e in search(entries,args.query,args.domain)][:12]
        else:
            if args.skills:roots=[x.strip() for x in args.skills.split(',') if x.strip()]
            else:
                hits=search(entries,args.query,args.domain)
                # Explicit aliases select all mentioned capabilities; weak full-text hits are suggestions only.
                roots=[e['id'] for score,e in hits if score>=4]
                if not roots:raise ValueError('no unambiguous match; use search then --skills with exact IDs')
            for key in roots:
                if key not in entries:raise ValueError('unknown skill: '+key)
            result=plan(entries,roots,args.side)
        print(json.dumps(result,ensure_ascii=False,indent=2))
    except (ValueError,OSError,KeyError) as error:parser.error(str(error))
if __name__=='__main__':main()
