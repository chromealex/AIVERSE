# PRISMA — handoff этапа 01: новое имя и совместимость

**Ветка:** `feature/prisma-postfx`
**База:** `feature/universal-entity-inspector-fps` @ `1c3399e779bca3423033b458202c7826a4344112`.
**Последний коммит до этого документа:** `29998386a1e17d770da676cf0fa5cce4a136b725`. Commit документа — отдельный последующий коммит; проверь `git log -1`.

## Что сделано
- Главный C++ класс `aiverse::render::PostFx` переименован в `Prisma`, member definitions в `PostFx.cpp` обновлены; `using PostFx = Prisma` оставлен для исходной совместимости.
- Добавлен `engine/render/Prisma.h` как основная точка входа и aliases `PrismaSettings/Effect/Quality/Stats/Type`.
- `ScreenLighting`, редактор, клиент, граф-превью и benchmark обновлены до нового имени.
- Формат JSON `postfx`, MCP команды `postfx_*`, классы `PostFxData` и файлы реализации **намеренно не переименованы**, чтобы избежать ломания проектов и сборок.

## Изменённые файлы
`engine/render/{PostFx.h,PostFx.cpp,Prisma.h,ScreenLighting.h,ScreenLighting.cpp,Renderer3D.h}`,
`apps/client/{GameClient.h,GameClient.cpp}`, `apps/editor/{Editor.h,Editor.cpp}`,
`apps/editor/windows/{PostFxWindow.cpp,ShaderGraphPreview.h}`,
`apps/renderbench/Bench.h`, `docs/postfx.md`, `docs/prisma.md`, `AGENTS.md`.

## Проверки
- **Фактически:** GitHub API успешно записал файлы; проверено наличие исходных определений `PostFx::` перед заменой и целевых `PostFx` типов в основных интеграциях.
- **Не запускались:** CMake, компиляция C++, CTest, Metal/OpenGL GPU тесты; нет права утверждать, что они прошли.
- **Риск:** дополнительный legacy-код может ссылаться на старое имя, но должен работать через alias; проверить сборкой, включая optional targets.

## Следующий шаг
Создать headless-ядро `PrismaVolume/PrismaProfile` с приоритетами, слоями, сферой/боксом, blend distance и детерминированными тестами. Сохранить обратную совместимость, затем подключить динамические профили камер к runtime/редактору и разработать render graph.
