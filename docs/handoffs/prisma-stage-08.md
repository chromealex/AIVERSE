# PRISMA — handoff этапа 08: camera volumes в клиенте и редакторе

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **Начало:** `docs/handoffs/prisma-stage-07.md`. **Последний кодовый SHA этапа:** `655b42391b587274563df083c33ef74b76158a31`; docs `fc72f8af618480edc028a29c17a67d8d8aacb07f`, `70a22b3567973d9be3e22f1e0b101b20de45c1e5`.

## Реализовано
1. `engine/render/PrismaVolumes.h`: `PrismaVolume.id`, `overrideHdr`, `overrideEnabled`. Неуказанные в volume profile `hdr`/enabled больше не перетирают базовые настройки; новый ColorGrade плавно интерполируется от нейтрального профиля, а не включается скачком. Дискретные эффекты включаются при весе >=0.5. Существующий `Prisma::applyCameraVolumes`/clear использован без изменений GPU API.
2. `engine/render/PrismaVolumeData.{h,cpp}`: GPU-free `parsePrismaVolumes(game["prismaVolumes"],map["prismaVolumes"],&issues)`. JSON schema: `{volumes:[{id,shape,center,radius,halfExtents,blendDistance,weight,priority,layerMask,enabled,profile:{effects,...}}]}`. Game+map merge по `id`, `map.replace=true` удаляет все game-volume definitions. Ошибки в individual volumes сообщаются через issues, некорректные volume записи пропускаются без мутации authored JSON. Полное значение 32-битной маски `4294967295` читается из `Json::numberLiteral`, без переполнения `Json::integer` через simulation Fixed.
3. `CMakeLists.txt`: `PrismaVolumeData.cpp` входит в GPU-free `aiverse_postfx_data` (legacy target, alias `AIVERSE::prisma_data`), новый обычный CTest `prisma_volume`, executable `aiverse_prisma_volume_tests` включён в CMake ALL при AIVERSE_BUILD_TESTS.
4. `apps/client/GameClient.{h,cpp}`: `PrismaVolumeStack prismaVolumes_`; stack загружается при настройке карты и накладывается на `cam_.eye()` непосредственно перед сценой; сбрасывается к базовому профилю после `resolvePrisma()`, UI/симуляция не затрагиваются.
5. `apps/editor/Editor.{h,cpp}`: кэш JSON-сигнатуры game/map volumes, пересборка только при изменении; сцена и Game-view получают собственный camera eye, stack сбрасывается после каждого PRISMA resolve.
6. `tests/render/prisma_volume.cpp`: CPU проверки parsing, map override/replace, HDR preserve/explicit override, sphere blend, layer masks, максимальная 32-bit маска, invalid radius and duplicate IDs.
7. `docs/prisma.md`, `docs/game-format.md`, `skills/visuals/aiverse-prisma/SKILL.md`: JSON пример, cross-genre ownership, ограничения.

## Ограничения и совместимость
- Всё ещё `game.json["postfx"]` и `map["postfx"]` определяют исходный стандартный profile; `prismaVolumes` — новая необязательная конфигурация. Старые игры и MCP операции не мигрируют и не меняют формат.
- Box — axis-aligned (нет поворота/scale), нет редакторских gizmos или UI для размещения объёмов; объёмы пока создаются в JSON / native API.
- Внутри эффекта пока **полные** numeric settings, не per-parameter override masks. Применение GPU-free volume resolver раз в кадр может создавать временные CPU allocations; оптимизация и spatial index не выполнены.
- Camera layer mask при штатных вызовах клиента/редактора по умолчанию включает все биты; разные камеры могут передать собственную маску из native API.
- Эффекты TAA/SSR выполняются прежними engine passes; это не подтверждает GPU RenderGraph executor или физический DoF/auto exposure/motion vectors. Сохранено разделение: SpectraFX — particles/VFX, PRISMA — экранная постобработка.

## Фактическая верификация
- **Выполнено:** GitHub connector записал API, интеграции и тестовые исходники; статический аудит подтвердил (8/8) наличие parser+map-merge, вызовов client/editor, очистку profile после view и CMake test registration.
- GitHub workflows на SHA `655b4239` показывали ECS/Studio/GPU regression **queued**, CI/SpectraFX **pending**, не PASS; combined status был пустым.
- **NOT RUN:** компиляция C++, `./run-tests.sh`, `ctest -R 'prisma_graph|prisma_volume|hdr_policy'`, editor/Metal/GL/GLES, визуальное сравнение кадров. В данной среде Github connector не предоставляет checkout, исходный git host из контейнера недоступен. Не утверждать, что код компилируется или тесты проходят.

## Следующие действия другому агенту
1. На нативном checkout ветки выполнить: `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON`; `cmake --build build/prisma --target aiverse_prisma_graph_tests aiverse_prisma_volume_tests aiverse_hdr_policy aiverse_editor -j4`.
2. `ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|prisma_volume|hdr_policy'`; затем `./run-tests.sh --quick` и `python3 tools/skills/catalog.py check`. Исправить реальные ошибки и сохранить результаты в **новом** handoff.
3. Реальные Metal/GL screenshots: camera crosses sphere/box boundary, game/map override, Scene/Game view side-by-side, HDR and TAA compatibility; assert no UI processing and no persistent state leak.
4. После проверки: per-parameter numeric mask и oriented boxes / editor gizmos, разделение rendering graph pass scheduling from executor, optional ECS read-only presentation bridge. Не затрагивать authoritative simulation.
5. На момент начала реализации ветка PRISMA разошлась с быстро развивающейся `feature/universal-entity-inspector-fps`; перед merge посмотреть актуальные upstream commits, НЕ force push/переписывать SpectraFX/AGENTS изменения. PR #26 остаётся draft до подтверждения native CI/GPU.
