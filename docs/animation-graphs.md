# AIVERSE MotionWeave — программируемые анимации

**MotionWeave** — новое общее имя модульной системы анимации.
Старые классы `AnimationGraph` и `Animator`, JSON-форматы и namespace
`aiverse::lib::animation` остаются без изменений для совместимости.
Публичная точка входа: `<aiverselib/motionweave/MotionWeave.h>`,
`AIVERSE::MotionWeave`. [Архитектура](motionweave.md).

`engine/render/AnimationGraph.h` предоставляет граф поз и машину состояний поверх
существующих glTF/FBX/процедурных моделей, семплера клипов и двухкостного IK.
Расширения UAF: trait stacks с Pre/Post hooks, вложенные графы, Motion Matching
и программируемый forward rig. Общий граф неизменяем; состояние принадлежит instance.
Это система отображения: её числа с плавающей точкой, параметры и переходы не входят
в checksum и никогда не управляют перемещением, уроном или коллизией симуляции.

## Подключение к игре

Добавьте объект `animationGraph` в `view` типа, рядом с `model` и `animations`. Также `view.animationGraph` может быть строкой — относительным путём к внешнему JSON-графу (Stage 97): `"animationGraph": "animation/hero.json"`. Файл читается тем же project `FileReader`, проверяется `render::parseAnimationAsset` и компилируется с текущей моделью. Внешний файл позволяет редактировать граф независимо от `rules.json`. Начиная со Stage 98 редактор подготавливает новую версию `TypeLook` и скомпилированных графов, а затем заменяет только затронутые визуальные типы, сохраняя резидентные GPU-модели и часы анимаций остальных типов. При ошибке перекомпиляции предыдущие рабочие графы не заменяются. Несколько одновременных изменений (модель вместе с графом/текстурой) используют полную перезагрузку визуальных ресурсов как fallback. Проверка сборкой и реальным рендерингом ещё не выполнена.
Пример определения: `examples/animation/locomotion.json`. Имена клипов и костей
в нём иллюстративные: замените их именами из своей модели. Это не готовый персонаж.
Граф автоматически включает CPU palette skinning, включая при `skinning: "baked"`.
Обычные типы без графа сохраняют прежнюю анимацию и GPU baked skinning.
Ошибки компиляции попадают в предупреждения модели; тип использует прежнюю систему.

В редакторе и клиенте используется один RtsView. Внешние FPS/native-хосты подключают
C++ API самостоятельно; автоматическое подключение к FPS-персонажам не реализовано.

## Формат

`parameters` — значения по умолчанию: числа, boolean (0/1) или массивы `[x,y,z]`.
`nodes` — топологически отсортированный массив: входы должны быть объявлены раньше
потребителя. До 128 узлов, 64 состояний, 64 переходов на состояние и 32 условий
на переход. До 128 числовых и 128 векторных параметров. Циклы, неизвестные ссылки,
недостающие клипы, кости и параметры отклоняются при компиляции.

| kind | Поля | Результат |
|---|---|---|
| `stack` | `input`, `traits` | Стек native/Lua traits и rig-коррекций |
| `subgraph` | `graph`, `bindings` | Отдельный экземпляр именованного графа |
| `motionMatching` | `database` | Выбор клипа/времени по позе и будущей траектории |
| `rig` | `input`, `units` | Последовательная программа управления костями |
| `bind` | `id` | Bind pose модели |
| `clip` | `clip`, `loop` (true), `speed` (1), `parameter` | Клип; параметр задаёт скорость воспроизведения |
| `blend` | `a`, `b`, `weight` (1), `parameter`, `mask` | Смешивание полных поз A → B; маска — имя корня поддерева |
| `additive` | `a`, `b`, `weight`, `parameter`, `mask` | К A добавляется разница B относительно bind pose |
| `controller` | `controller` | Mecanim-style layered state machine imported from Unity ([unity-animation.md](unity-animation.md)) |
| `twoBoneIK` | `input`, `hip`, `knee`, `foot`, `target`, `pole`, `weight`, `parameter` | Двухкостный IK; target/pole — имена векторных параметров в пространстве модели |

У blend/additive/IK `parameter` переопределяет вес; он ограничивается 0..1.
Additive перемещение, scale и morph weights складываются; rotation добавляется как
взвешенная разность quaternion. Это bind-relative семантика, отличающаяся от
старого `sampleClipAdditive`, где reference — первый кадр клипа.

Без машины состояний укажите `output` — id выходного узла.
Иначе задайте `states: [{id, output, transitions: [...]}]` и `initial` (по умолчанию
первое состояние). Переход: `{to, fade: 0.2, after: 0, when: [{parameter, op, value}]}`.
Все условия должны выполняться; `op`: eq/ne/gt/ge/lt/le. `after` — минимальное время
в состоянии. Пустой `when: []` разрешает безусловный переход. Первый подходящий
переход имеет приоритет; максимум один за update. Самопереход перезапускает состояние.

Часы клипов интегрируют скорость без скачка фазы при изменении speed. При входе в
состояние они сбрасываются. Crossfade идёт от зафиксированной исходящей позы к
движущейся входящей; прерванный переход берёт текущую смешанную позу. Это не
inertialization и не одновременное воспроизведение двух машин состояний.
Вычисляются только узлы, нужные активному выходу. LOD может пропускать оценку позы,
но часы и условия продолжают обновляться. Один dt ограничен секундой после suspend.

## C++

```cpp
auto graph = aiverse::render::AnimationGraph::compile(model, definition);
aiverse::render::AnimationGraphInstance instance;
instance.reset(graph);
instance.parameters().set("moving", 1.0f);
instance.update(dt);
instance.evaluate(pose);
worldMatrices(model, pose, world);
```

`model` должен жить дольше графа и экземпляров; не меняйте модель после компиляции.
Граф неизменяемый и общий; каждый объект имеет свой instance, часы, параметры и позы.
`state()` и `stateTime()` доступны для диагностики. `reset()` сбрасывает всё состояние,
включая параметры. `set` отвергает нечисловые значения и превышение лимитов.
C++-хост может программно собрать Json и скомпилировать граф. Перекомпиляция и reset
— явные операции хоста, автоматического hot reload нет.

## Lua в viewScripts

```lua
view.animation(character, {
    attackWeight = 0.6,
    ikWeight = 1,
    footTarget = {0.2, 0.1, 0},
    kneePole = {0, 0, 1}
})
view.animation(character, nil) -- удалить все переопределения
```

`character` — обычный entity handle из game API. Изменение таблицы атомарно: при
некорректном поле не применяется ни одно поле. Пропущенные поля сохраняют значения.
Встроенные параметры RtsView: `speed`, `moving`, `dead`, `attacking`, `hasTarget`.
Объявляйте их в parameters при использовании. Lua-переопределения имеют приоритет;
удаление переопределений восстанавливает defaults и автоматические значения.
Записи удаляются через 10 секунд после исчезновения сущности, как entity_fx.
Lua может также создавать графы и реализовывать traits; API описан ниже.
`view.animation(entity, nil)` удаляет и параметры, и назначенный Lua-граф.

## Границы реализации

Это собственная система AIVERSE, ориентированная на принципы AnimGraph. В неё не входят
визуальный редактор Blueprint, совместимость с Unreal Control Rig VM,
полная автоматическая интеграция Humanoid/Avatar, Unreal Control Rig VM
или root-motion motor в RTS/FPS. Новый самостоятельный модуль
`aiverselib/animation` предоставляет семантический Humanoid Retargeter,
Montage, extract-only Motion Warping, фиксированный ECS Root Motion motor
с явной регистрацией и Ragdoll/Box3D bridge. Эти модули подключаются
проектом, а не включаются автоматически для каждого жанра.
`kind:"clip"` теперь поддерживает массив
`"notifies":[{"time":0.35,"name":"step"}]`; экземпляр графа
возвращает `notifies()` после update, но не дублирует legacy clip events
`RtsView`. Эти события предназначены для звука/VFX, не для авторитетного
урона или hit detection. Старые `RtsView` terrain foot IK/procedural
модификаторы применяются после графа и должны учитывать порядок IK.
LOD и construction stage используют старый mapping костей.

Проверки: `aiverse_animation_tests` (CPU pose, blending, transitions, IK, validation),
основной `aiverse_tests` и сборка клиента. Визуальная проверка должна использовать
реальный RtsView и скелет с подходящими именами, а не только JSON-пример.

## Запускаемый пример

```sh
python3 examples/animation/showcase.py /tmp/aiverse-animation-demo
AIVERSE_CONTENT=/tmp/aiverse-animation-demo AIVERSE_SPLASH=off AIVERSE_AUDIO=off \
  build/<config>/AiverseGame --map 0 --spectate
# Крупный план через существующий просмотрщик моделей:
build/<config>/aiverse_modelview --game /tmp/aiverse-animation-demo --types footman
```

Скрипт создаёт новый проект, копируя оригинальные ассеты `templates/showcase`.
Воин циклически переключается между idle и смешанными walk/run с additive-дыханием;
смерть прерывает цикл. Исходный showcase не изменяется.
Двухкостный IK рассчитан на цепочки без неравномерного масштабирования костей;
это геометрический решатель, не физический контроллер контакта с поверхностью.

## Native-операции и программа вычисления

При компиляции для каждого состояния строится упорядоченная `State::program` только
из необходимых узлов; общий вход DAG исполняется один раз за evaluate.
`update()` обновляет часы и память активных операций, `evaluate()` читает эту память
и вычисляет позу. Вызов evaluate без update не продвигает анимацию.

C++ может передать в `AnimationGraph::compile(model, json, operations)` реестр
`AnimationOperations`. В RtsView его задают через `setAnimationOperations()` **до init**.
Узел `{kind:"native", id:"custom", input:"base", operation:"myOperation", config:{...}}`
использует зарегистрированную операцию. Неизвестные операции отклоняются.

`AnimationOperation` хранит неизменяемую реализацию: `validate` вызывается при компиляции,
`update` получает отдельный `span<float>` состояния, `evaluate` — read-only состояние
и изменяемую входную Pose. До 256 float на узел. Память обнуляется на reset/входе в
состояние. Runtime callback должен быть noexcept, не менять число костей, не сохранять
ссылки на позу/параметры и не обращаться к симуляции. Не меняйте зарегистрированный
объект операции после компиляции. Передавайте его сразу как shared_ptr<const ...>.
Реестр принадлежит хосту, глобального singleton нет. Lua-traits вызываются один раз
на hook активного узла и получают всю позу; их стоимость учитывайте отдельно.

## Сопоставление с новым Unreal Animation Framework

Проверено по открытой документации Epic 11 ноября 2026, включая [UE 5.9 Preview,
release notes от 9 ноября](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-9-release-notes?lang=en-US).
Это изучение опубликованных API, а не сборка или аудит полного исходного кода Unreal.
В 5.9 расширены layering/notifies и интеграции StateTree, Chooser, Pose Search, Warping,
Mirroring и Control Rig; появился UAF AnimGen.

Архитектурные ориентиры:

- [FTrait](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/UAFAnimGraph/FTrait):
  реализация поведения без внутреннего изменяемого состояния; общие read-only данные
  отделены от данных экземпляра. В AIVERSE это immutable graph/operation + instance storage.
- [IUpdate](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/UAFAnimGraph/IUpdate)
  и [IEvaluate](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/UAFAnimGraph/IEvaluate):
  отдельные обходы обновления и подготовки вычисления. AIVERSE отделяет update от evaluate,
  а stack-узлы имеют собственные Pre/Post hooks с отдельной памятью traits.
- [FEvaluationProgram](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/UAFAnimGraph/FEvaluationProgram):
  неизменяемая последовательность задач для evaluation VM. У AIVERSE компактная программа
  индексов операций на состояние; отдельной универсальной VM нет.
- [FAnimNextGraphInstance](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/UAFAnimGraph/FAnimNextGraphInstance)
  отделяет граф от экземпляра и связывает его с модулем; AIVERSE пока имеет только граф/instance
  и хост RtsView; вложенные графы и назначение Lua-графа реализованы,
  полного аналога UAF System/Module нет.

## Trait stacks и вложенные графы

`traits` перечислены снизу вверх: `[base, overlay]`. PreUpdate/PreEvaluate идут
сверху вниз, PostUpdate/PostEvaluate — снизу вверх. Входной pose-узел уже вычислен
к моменту PreEvaluate стека. Это последовательная композиция, не копия Unreal dispatch.
Каждый trait имеет immutable `config` и до 256 float отдельной памяти на instance.
Повторный evaluate не продвигает часы. При входе в состояние память сбрасывается.
Native callbacks должны не бросать исключений; `false` отключает trait этого instance.
Изменения позы применяются только после проверки размера и конечности компонентов.
`healthy()` / `error()` сообщают сбой, остальной граф продолжает выдавать позу.

```cpp
using namespace aiverse::render;
auto t = std::make_shared<AnimationTrait>();
t->stateFloats = 1;
t->postUpdate = [](const Json&, AnimationParameters&, float dt, std::span<float> s) {
    s[0] += dt; return true;
};
t->postEvaluate = [](const Json&, const AnimationParameters& p,
                     std::span<const float> s, Pose& pose) {
    pose[0].t.y += std::sin(s[0]) * p.values.at("amplitude");
    return true;
};
AnimationTraits traits{{"bob", t}};
auto graph = AnimationGraph::compile(model, definition, {}, traits);
// definition: stack with traits: [{"trait":"bob", "config":{}}]
```

RtsView принимает C++ реестр через `setAnimationTraits()` до init. Не изменяйте
реализацию после регистрации и не сохраняйте переданные spans/ссылки.
Lua callbacks исполняются только на потоке владельца VM; worker scheduling не добавлен.

Именованные определения находятся в корневом `graphs` либо передаются C++ пятым
аргументом compile (`AnimationGraphs`). Узел `{kind:"subgraph",graph:"locomotion",
bindings:{childSpeed:"speed"}}` создаёт собственную машину состояний, часы и память.
Binding связывает имя параметра ребёнка с параметром родителя; типы должны совпадать.
Не связанные параметры сохраняют defaults и могут изменяться через C++ `child(id)`.
Рекурсия запрещена, глубина ≤8, ≤64 определений, ≤512 развёрнутых узлов,
≤16 traits на stack. Вход в состояние родителя сбрасывает дочерние instances.

## Motion Matching

```json
{"id":"motion", "kind":"motionMatching", "database":{
  "root":"root", "clips":["Idle","Walk","Run"], "bones":["foot.L","foot.R"],
  "trajectory":[{"time":0.3,"parameter":"future"}],
  "fps":30, "searchInterval":0.1, "blend":0.15, "continuationBias":0.05,
  "poseWeight":1, "velocityWeight":0.1, "trajectoryWeight":2,
  "maxCandidates":8192, "stripRoot":true
}}
```

`future` — объявленный vector-параметр: желаемое смещение root через 0.3 секунды
в текущем пространстве root, в единицах модели. Можно задать 1–4 горизонта до 2 секунд.
При компиляции клипы семплируются в базу: позиции/скорости до 8 костей относительно
root и будущие root-смещения. Runtime строит запрос из текущего семпла и управляющей
траектории, минимизирует взвешенную сумму квадратов отличий. `continuationBias`
предпочитает продолжение текущего клипа. Это поиск реальных кадров, не генерация движений.
Веса не нормализуют разный масштаб исходных данных автоматически.

По умолчанию просматривается вся база (≤8192 кадров, ≤64 клипов). Меньший
`maxCandidates` равномерно ограничивает кандидатов и превращает поиск в приближённый.
Всего ≤16384 кадров на развёрнутый граф. `fps` 1–120; поиск не чаще 0.016 секунды.
При смене сегмента выполняется crossfade от сохранённой позы, без inertialization.
`stripRoot` возвращает root translation/rotation к bind pose, не меняя объект симуляции.
Циклы экстраполируют суммарный перенос root; поворотные циклы требуют отдельной подготовки.
In-place клипы без root-траектории не содержат скорости перемещения для такого поиска:
используйте клипы с root motion. Пример ниже добавляет демонстрационные траектории.
`motion(id)` предоставляет C++ диагностику выбранного clip/time/cost.

## Программируемый rig

`AnimationRig` — компилируемая последовательность до 64 forward units с именованными
костями и контролами из vector/scalar parameters. Используется узлом `rig` или элементом
stack `{kind:"rig",units:[...]}` (исполняется в PostEvaluate).

| op | Поля | Семантика |
|---|---|---|
| `transform` | `bone`, `translation`, `rotation`, `scale`, `space`, `additive` | Полный TRS, local либо model space |
| `copy` | `bone`, `source`, `space` | Копирует local либо model transform |
| `aim` | `bone`, `axis`, `target` | Направляет локальную ось к model-space цели |
| `twoBoneIK` | `bone`, `knee`, `end`, `target`, `pole` | Двухкостная цепочка; цели в model space |
| `limit` | `bone`, `min`, `max` | Ограничивает **локальную translation**, не углы |

Для transform доступны `translationParameter`, `rotationParameter`, `scaleParameter`.
Rotation — Euler XYZ в радианах, композиция Rz·Ry·Rx. Полный transform заменяет TRS:
неуказанные компоненты равны identity; для поправки поверх клипа задайте `additive:true`.
Все units имеют `weight` и необязательный scalar `weightParameter` (0..1).
Операции выполняются по порядку с обновлением мировых матриц. Неравномерный/отрицательный
scale и shear не поддерживаются полноценно при разложении model-space TRS и IK.
Это программируемый forward rig; full-body solver и визуальный редактор описаны ниже.
Backward solve, ретаргетинг и Unreal RigVM bytecode не реализованы.

## Полное программирование из Lua

Для динамического назначения включите у типа `view.programmableAnimation: true`
(статический animationGraph включает его автоматически). Это резервирует palette skinning.
Доступны:

- `view.animation_trait(name, callbacks)` — регистрация реализации, ≤64 за жизнь VM.
- `view.animation_define(name, table)` — все узлы, states, nested graphs, motion и rig в Lua.
  Определение сохраняет snapshot зарегистрированных traits; ≤64 имён.
- `view.animation_use(entity, name)` — назначение; `nil` возвращает граф типа.
- `view.animation(entity, params)` — scalar/vector controls, как выше.
- `view.animation_status(entity)` — `{state,error}` последнего прохода отображения.

```lua
view.animation_trait("lift", {
  stateSize=1,
  post_update=function(params, state, dt, config)
    state[1]=state[1]+dt; return state
  end,
  post_evaluate=function(params, state, pose, config)
    pose[1].t[2]=pose[1].t[2]+math.sin(state[1])*params.height
    return pose
  end
})
view.animation_define("example", {
  parameters={height=0.1},
  nodes={{id="base",kind="bind"},
         {id="out",kind="stack",input="base",traits={{trait="lift"}}}},
  output="out"
})
view.animation_use(actor,"example")
```

Также доступны `pre_update` и `pre_evaluate` с теми же аргументами.
Update возвращает массив state, evaluate — массив pose. Pose: индекс модели +1,
элементы `{t={x,y,z}, r={x,y,z,w}, s={x,y,z}}`; morph weights сохраняются вне Lua API.
Callbacks получают копии, не указатели в C++. Некорректный результат/исключение/лимит
100000 инструкций отключает trait данного экземпляра с сообщением. Не больше 512 костей
для Lua evaluate. Нельзя менять назначения/определения анимации внутри trait callback.
Lua-таблицы графов ограничены глубиной 24, размером и типами; циклы отклоняются.

Компиляция происходит при первом использовании с конкретной моделью. Ошибка остаётся
в status; отображение использует исходную анимацию типа. Переопределение asset и новый
animation_use переключают граф, сбрасывая runtime. Hot reload отзывает callbacks старой
VM; экземпляры с ними безопасно сообщают ошибку. Lua-графы presentation-only, не сохранение
симуляции и не источник игровых событий/движения.

## Запускаемый UAF-пример

```sh
python3 examples/animation/uaf_showcase.py /tmp/aiverse-uaf-demo
AIVERSE_CONTENT=/tmp/aiverse-uaf-demo AIVERSE_AUDIO=off AIVERSE_SPLASH=off \
  build/<config>/AiverseGame --map 0 --spectate
```

`examples/animation/uaf.lua` создаёт весь граф: вложенные alive/dead states,
Motion Matching Idle/Walk/Run, rig для головы и Lua-trait дыхания. Скорость желаемой
траектории меняется каждые 2 секунды. Генератор копирует showcase и добавляет root tracks
к исходным in-place клипам; исходный проект не меняется. Для macOS executable находится
в `AiverseGame.app/Contents/MacOS/AiverseGame`.

Остаются отдельными задачами: blend spaces, типизированные handles
вместо map-поисков, события с обработкой loops, retargeting, warping, inertialization,
пакетный scheduler, Animation Budget Allocator и аналоги StateTree/Chooser/AnimGen.

## Full-body IK

`engine/render/FullBodyIK.h` — общий multi-effector решатель для произвольного дерева
скелета. В отличие от независимых двухкостных цепей, каждый общий сустав учитывает
все цели своего поддерева. Damped least-squares coordinate descent выполняет локальные
3×3 решения от конечностей к общим предкам; число итераций ограничено.

```json
{"op":"fullBodyIK", "bone":"hips", "iterations":32, "damping":0.05,
 "tolerance":0.001, "rootPull":0.2, "weightParameter":"strength",
 "effectors":[
   {"bone":"hand.L","target":"leftHand","weight":1},
   {"bone":"hand.R","target":"rightHand","rotation":"rightRotation","rotationWeight":0.5},
   {"bone":"foot.L","target":"leftFoot","weightParameter":"footWeight"}
 ],
 "joints":[
   {"bone":"spine","stiffness":0.2,"min":[-0.7,-0.5,-0.7],"max":[0.7,0.5,0.7]},
   {"bone":"knee.L","preferred":[0.1,0,0]}
 ]}
```

Все цели — объявленные vector parameters, позиции в пространстве модели, rotation —
Euler XYZ в радианах (Rz·Ry·Rx). Effector weight — 0..1; rotationWeight задаёт вклад
ориентации. До 16 effectors, 128 затронутых суставов, 1–64 итераций (по умолчанию 24).
`rootPull:0` фиксирует **translation** корня; 0..1 разрешает перенос корня к среднему
остатку целей. Его rotation можно зафиксировать `stiffness:1` в joints.

Длины костей и локальные translation сохраняются, кроме разрешённого переноса корня.
`stiffness:1` блокирует вращение сустава, промежуточные значения уменьшают шаг.
`min/max` ограничивают компоненты **rotation vector относительно bind rotation**,
не Euler-углы; диапазоны в радианах. `preferred` задаёт начальный локальный rotation
vector относительно входной позы — полезен для выбора сгиба выпрямленной цепочки.
Неуказанные ветви сохраняют локальные трансформации и следуют за изменёнными предками.
Общий rig weight смешивает итог с входной позой; при частичном весе цели и ограничения
могут быть выполнены лишь частично.

```cpp
using namespace aiverse::render;
auto solver = FullBodyIK::compile(model, unitJson, parameters);
auto result = solver->solve(model, parameters, pose, 1.0f);
// result.iterations / result.error / result.converged
```

`error` includes the final blended pose residual, including contact constraints.
Невыполнимые цели дают лучшую найденную итеративную позу с остатком, без растяжения
костей и бесконечного цикла. Сходимость к глобальному минимуму не гарантируется;
несовместимые цели, singular straight limbs, масштаб модели и веса требуют настройки.
Nonuniform/negative bone scale is unsupported. Optional contacts add collision and quasi-static balance.
This is an independent AIVERSE solver, not the Unreal PBIK binary. Reduce iterations and samples on mobile.
итераций/эффекторов и используйте animation LOD; бюджет задаётся данными, потоков нет.

Lua создаёт этот unit через `view.animation_define` и изменяет цели через
`view.animation(entity, {...})`. C++ может использовать solver напрямую или в
AnimationRig/AnimationGraph. `examples/animation/fullbody.json` и `fullbody.lua` —
пример для модели golem из showcase (в ней конечности упрощены).

## Визуальные редакторы

Меню **Графика → Граф анимации / Риг анимации**. Оба окна работают с одними файлами
`<project>/animations/<name>.json`: это обычный runtime graph плюс `model` для превью
и `position` для положения карточек. Эти поля не влияют на вычисление позы.

1. Выберите тип проекта либо задайте относительный путь модели. Добавьте clip/bind,
   blend/additive, stack, subgraph, Motion Matching или rig через «Добавить узел».
2. Перетащите выходной порт на входной. Редактор сортирует DAG для runtime и отклоняет
   цикл атомарно. Карточки перемещаются за заголовок; средняя кнопка сдвигает полотно,
   колесо меняет масштаб. Назначьте выход графа. Переименование обновляет ссылки;
   удалить используемый узел нельзя, пока не переназначены потребители/выходы.
3. В свойствах доступны типизированные scalar/vector/string/object/array поля,
   параметры, states/transitions/when, traits и bindings. Добавляйте поля по их именам
   из runtime-схемы. Вложенные графы выбираются через «Граф». Их preview исполняется
   отдельно с собственными controls; основной граф исполняет bindings целиком.
4. В rig выберите узел `rig`, добавьте units. Карточки показывают порядок выполнения;
   кнопка `^` меняет его. Щелчок карточки открывает её свойства под превью.
   Rig — последовательная программа, не произвольный DAG units.
5. Preview вычисляет настоящий runtime graph и отображает skinned mesh, скелет и
   vector controls. Пауза не мешает evaluate при изменении целей. Контролы можно
   перетаскивать в XY/XZ/YZ проекции и менять числами. «Сохранить значения контролов»
   записывает их в defaults; иначе изменения временные. Модель можно обновить с диска.
6. Undo/redo хранит до 64 состояний документа. При переключении проекта черновик
   остаётся в памяти окна. Черновики **не переживают закрытие приложения** — сохраняйте их.
   «Сохранить» пишет файл после проверки topology, включая ещё не настроенные units.
   «Применить к типу» дополнительно компилирует граф против модели типа и сохраняет
   snapshot в `view.animationGraph` через rules_set. Повторите применение после изменений.
   Сохранение проверяет прежнее содержимое файла: второе окно не затрёт чужие правки.

Неизвестные C++/Lua callbacks сохраняются в документе, но для исполнения требуют
реестр соответствующего хоста; встроенный preview не запускает произвольные скрипты.
Ошибка показывается вместо успешного preview. UI редактирует параметры/имена hooks,
исходный C++/Lua код остаётся в обычных исходных файлах.

```lua
-- animations/character.json: тот же файл, который сохранён редактором.
-- Сначала зарегистрируйте используемые Lua traits.
view.animation_load("character", "character")
view.animation_use(actor, "character")
view.animation(actor, {leftHand={0.4,1.4,0.2}, strength=1})
```

`animation_load` ограничивает имя ассета, использует platform::readContentFile
(в том числе resource packs) и лимит 256 KiB. На типе нужен programmableAnimation.
C++ загружает тот же Json своим content reader и передаёт AnimationGraph::compile.

Операции Workspace/MCP: `animation_graph_list/get/set/validate/apply`.
`set` принимает `expected` — ранее прочитанный граф для защиты существующего файла.
GUI: `animation_editor_open` / `rig_editor_open` с `{name,graph?}` и соответствующие
`animation_editor_status` / `rig_editor_status` с готовностью, ошибкой и текущей позой.

## Контакты, столкновения и баланс full-body IK

`fullBodyIK.contacts` добавляет к решателю удержание стоп, препятствия и квазистатический
баланс. Полный запускаемый asset для golem: `examples/animation/contacts.json`, Lua:
`examples/animation/contacts.lua`. Скопируйте их в `animations/contacts.json` и view script
проекта; назначьте типу golem и включите `view.programmableAnimation`.

В редакторе **Риг анимации** выберите fullBodyIK и нажмите **Контакты и баланс**.
Настройте кости стоп и их `offset` до подошвы, радиусы коллайдеров, массы и ограничения
суставов. Поля вложенных массивов редактируются в инспекторе; кнопка добавления создаёт
типизированную запись даже в пустом массиве. Для капсулы добавьте `end` с именем прямой
дочерней кости. Препятствия выбираются полем `kind`: `plane`, `box`, `sphere`.
Скелетное превью показывает препятствия, коллайдеры конечностей и площадки стоп.
**Высота опоры** изменяет плоский пол превью (это окружение предпросмотра, не изменение
terrain проекта). Сохранение/apply, undo/redo, C++ и Lua используют один JSON-формат.

| Поле contacts | Назначение / диапазон |
|---|---|
| `world` | Чтение запросов окружения; по умолчанию true |
| `up`, `maxSlope` | Направление вверх в model space; допустимый уклон 0..<90°, default 45° |
| `rootAdjust` | Доля коррекции корня по остаткам контактов, 0..1 |
| `maxCorrection` | Максимальный сдвиг корня за итерацию, 0..<2 model units (строго >0) |
| `collisionWeight` | Вес столкновений в общем решении, >0..1000 |
| `feet` | До 8 опор: `bone`, `offset`, `up`, `radius`, `probeUp`, `probeDown`, `releaseDistance`, `weight`, `align`, необязательный `plantParameter` |
| `capsules` | До 16 коллайдеров: `bone`, `offset`, `radius`, необязательный `end`, `samples` 1..8 |
| `obstacles` | До 32 model-space примитивов: `kind`, `point`, `normal`, `extent` (полуразмер box), `radius`; `positionParameter` связывает point с vector-параметром |
| `balance` | `strength` 0..1, `margin` и до 32 `masses`: `{bone,mass,offset}` |

Опоры ищутся лучом от подошвы +probeUp до подошвы −probeDown. Допустимая поверхность
фиксирует мировую точку стопы. `plantParameter<=0` отпускает стопу для шага; он должен
быть объявлен в parameters. Опора также отпускается при исчезновении поверхности;
смещение исходной стопы дальше releaseDistance переустанавливает контакт. `align`
выравнивает локальный up стопы по нормали поверхности. Касательный перенос движущейся
платформы автоматически не отслеживается. Без валидного контакта стопа не тянется к
несуществующему полу. Настройка шага/поиска новой опоры остаётся задачей графа или Lua.

Капсула вдоль кости покрывается перекрывающимися сферами с консервативно увеличенным
радиусом между отсчётами. Без end это сфера. Коллизии формируют дополнительные цели
точек на костях, решаемые вместе с контактами и обычными эффекторами. Цель обычного
эффектора стопы подавляется, пока стопа зафиксирована. Трансляции дочерних костей не
растягиваются. Пределы суставов и stiffness продолжают действовать.

Баланс вычисляет взвешенный центр масс, проецирует его вдоль up на выпуклую область
опоры и корректирует корень к ближайшей границе. Опоры — квадратные площадки стоп с
полуразмером `radius-margin`. Это **квазистатика**, без инерции, импульсов, трения,
ragdoll или автоматического падения. Для полноценной поддержки желательны таз, колени
и стопы; упрощённый golem демонстрирует формат и контакты без полноценного коленного rig.

```cpp
AnimationContactState state; // отдельный на персонажа; сохранять между кадрами
AnimationParameters parameters;
// Объявить все control parameters перед compile.
auto solver = FullBodyIK::compile(model, unitJson, parameters);
auto environment = std::make_shared<AnimationEnvironment>();
environment->modelToWorld = actorTransform;
environment->raycast = raycastWorld;        // bool(start,end,IKHit&)
environment->sphereContact = contactWorld;  // bool(center,radius,IKHit&)
parameters.environment = environment;
auto result = solver->solve(model, parameters, pose, 1.f, &state);
// result.contacts: supports, collisions, penetration, balanceError, supportError,
// supported, stable. state.reset() отпускает все контакты при телепортации.
```

Запросы read-only и в мировых координатах. IKHit содержит точку, нормаль наружу,
глубину проникновения и стабильный ID поверхности (младшие 63 бита). `sphereContact`
возвращает самое глубокое проникновение; отсутствие попадания — false. Владельцы C++
callback должны обеспечивать срок жизни источника данных на время вычисления позы.
AnimationRig принимает необязательный `AnimationRigState*`; AnimationGraphInstance
сам хранит раздельные состояния rig/traits/вложенных графов. Новый граф/переход состояния
сбрасывает фиксацию. `contactReport()` проверяет итоговую позу, включая смешивание
родительского графа; для вложенных графов диагностика отражает их собственный результат.

В RtsView плоских карт окружение подключено автоматически: terrain через heightAt,
соседние видимые объекты — AABB по bounds основного варианта модели (до 64 ближайших).
Это приблизительные presentation-коллайдеры, не точная геометрия анимированного mesh;
они не учитывают entity_fx, construction stages и нестандартные формы всех объектов.
Для точных запросов собственного физического мира используйте
`RtsView::setAnimationEnvironment(callback(Entity, AnimationEnvironment&))`: callback
заменяет raycast/sphereContact готового окружения с modelToWorld. Для планет автоматический
адаптер пока не подключён; C++ графы могут получать собственное окружение.

Графы с contacts управляют позой целиком: старые procedural modifiers и отдельный
terrain foot IK в RtsView отключаются, чтобы не испортить найденные контакты.
Контактный rig размещайте **после** blending/других модификаторов, у выхода графа.
Crossfade и частичный вес могут снова нарушить контакт; diagnostics показывают остаток.
Для сильного LOD/быстрого движения возможен пропуск препятствий между кадрами:
это дискретная коррекция, без continuous collision detection или self-collision.
Nonuniform/negative bone scale не поддерживается; actor transform рассчитан прежде
всего на положительный uniform scale. Система не пишет в simulation и не меняет
позицию gameplay-сущности.

```lua
view.animation_load("standing", "contacts")
view.animation_use(actor, "standing")
view.animation(actor, {plantLeft=1, plantRight=1, obstacle={0.7,1.2,0}})
local status = view.animation_status(actor) -- результат последнего pose update
-- status.stable / supported / supports / collisions
-- status.penetration / balanceError / supportError (model units)
```

`stable` требует хотя бы одной опоры и ошибок <0.005 model units. Невыполнимые
контакты (зажатая конечность, противоречивые лимиты, слишком далёкая опора) возвращают
ограниченное число итераций с остатком. Это сигнал для графа выбрать другую стойку
или отпустить опору, а не гарантия физически допустимой позы при любых входах.
На мобильных устройствах снижайте iterations, количество капсул и samples; работа
ограничена данными, без потоков. Автоматический адаптер обрабатывает только видимые
объекты, поэтому скрытые препятствия следует предоставлять собственным C++ запросом.

## Surface locomotion

The `surfaceLocomotion` rig unit adds multi-leg floor/wall/ceiling movement and adhesive
support. See [the controller guide](surface-locomotion.md) for C++/Lua/editor setup,
world query contracts, the generated spider demo and gameplay authority boundaries.

### Stage 108 — shared native graph and material authoring entrypoint

For any genre, `view::AppearanceAuthoringService::compileGraph`
from `engine/view/AppearanceAuthoringService.h` accepts a
`render::Model`, inline JSON or a path to an external
AnimationGraph asset, optional project FileReader and
compile-time operations/traits/graph libraries. It performs
the same bounded external JSON parsing and immutable
`render::AnimationGraph::compile` as the legacy RTS view,
but does not create a player or advance a pose.

`AppearanceAuthoringService::resolveMaterial` and
`applyMaterial` are likewise independent material-library
operations with project-provided texture callbacks. Native
projects may reuse these without introducing RTS/FPS types.
The compatibility RtsLook type graph and runtime
RtsAnimate control-authored graph paths use the service.
MotionWeave animation evaluation still happens in the
existing presentation World/graph instance, once per
source. Attachments, type selection, texture residency,
legacy material library hot reload and all runtime
gameplay-specific state decisions remain the host's job.

Validation: source code and deferred CMake fixtures only;
**NOT RUN** native compilation, GPU visual playback, CTest
or screenshot/animation parity checks.

## Stage 110 — one animation writer during appearance hot reload

The standalone FPS character host now publishes its project-authored
graph and material choices via `view::AppearanceAssetRegistry`. The
registry owns immutable compiled graph/material snapshots, not a
runtime `AnimationGraphInstance`. Its `sourceId=1` is the *asset
authoring key*, not an ECS actor identity. Actual remote actors keep
their own presentation ECS entities, per-actor clips and MotionWeave
updates. When a watched `fps.visual.graphFile` changes, the FPS host
invalidates that source, publishes a replacement, then updates the
existing `AnimationAssetComponent` and marks the existing player for
restart; failure leaves the current player untouched. Draw submission
uses already computed node matrices or baked samples through the
generic `View` and must not call a second `View::update`.

`AppearanceChoiceAuthoring` now shares RTS/FPS authored material,
node-assignment and model-variant rules, while per-genre simulation
data, attack state, IK contact sampling and attachment behavior remain
host adapters. A host that replaces `Model` at the same address must
increment `Request::modelRevision` or invalidate its `Model::path`
before republishing so a graph cannot be reused against stale model
node/clip storage. This is not protection against destruction of a
Model while a pinned snapshot or render packet still borrows it.

## Stage 111 — retaining RTS graph clocks during material updates

The RTS compatibility adapter now stores the parsed
`ModelChoice::graphSignature` (including contents of external
AnimationGraph files) and a pinned
`AppearanceAssetRegistry::Snapshot` for hot-reloaded model/LOD/stage
choices. A material-library-only rebuild may keep its previous graph
pointer and existing entity animation clocks when every corresponding
choice still uses the same imported Model, graph signature and runtime
shape. No new evaluator or timing source is allocated by
`AppearanceAssetRegistry::publishPrepared`; the original per-entity
MotionWeave / legacy animation state remains the only pose writer.
Changed graphs and incompatible choices retain the existing reset path.

`RtsView::shutdown` detaches generic View poses and clears model
borrowing animation state and appearance snapshots before the
ModelResourceCache GPU release barrier. Standalone RTS GameClient and
the Editor's embedded GameClient now use a host-owned GPU lifetime
generation, with the embedded client borrowing the Editor generation.

## Stage 112 — model texture rollback without resetting animation clocks

The RTS hot-appearance reload has a separate
`render::ModelImageUploadTransaction` for newly authored material
textures. Material indices refer to proposed append slots while
candidate looks are staged, and only become live with `commit()`
after all candidate models/LODs/stages and their immutable appearance
snapshots are prepared. A rejected texture/ShaderGraph/AnimationGraph
change leaves the last-good Model image count, live TypeLooks and
existing animation players untouched. GPU allocations created by the
rejected transaction go to a host-owned, fence-aware rollback queue;
no graph runtime or second animation ECS is added.

PostFx, terrain, ground effects and native particle rendering now
take an optional host `RenderDeviceLifetime::Lease` and check it at
their GPU entrypoints. Editor-embedded clients share the editor's
generation. These guards cannot prove a GPU fence has completed and
do not replace explicit resource retirement and shutdown ordering.

## Stage 113 — scratch model imports preserve last-good animation

`RtsView::reloadAppearanceTypes` stages new (previously
nonresident) models as independent scratch resource owners, as
well as staging new material images. On failed graph/material
authoring, it leaves the active TypeLooks and their graph/pose
writers untouched; scratch model GPU buffers and images retire
through normal GPU tokens without replacing live Model pointers.
A successful publish transfers the same Model* storage into the
real model cache after committing new image indices.

A speculative model may borrow a *resident* shared texture
lease, but cannot modify the shared GPU texture residency map
during authoring. The active MotionWeave or legacy per-entity
animation writer is unchanged; no second ECS or graph clock
is allocated by the scratch owner. `Renderer3D` now validates
the render host's existing GPU generation before scene work.
Validation is still deferred.

## Stage 114 — GPU shader readiness is an appearance publication condition

In the RTS/editor appearance reload path, authored CPU materials,
AnimationGraph programs, shared GPU image ownership and native GPU
shader/pipeline readiness must be prepared **before** the live TypeLook
and animation graph sources are replaced. The host can supply
`RtsView::setAppearancePipelineValidator`, normally forwarding to
its existing `Renderer3D::warmModelPipelinesChecked`.

- Ready: candidate may proceed through shared texture/Model commit.
- Pending: preserve last-good graph and material snapshots, leave
  the editor's watched source pending and retry after GPU compiler
  progress.
- Failed: reject the candidate, keep old animations and return an
  authoring error; do not replace the live TypeLook.

The validator handles GPU pipeline **preflight** but does not own a
second animation evaluator or make the Renderer3D shader cache fully
transactional. Speculative Model and material texture GPU handles
still require retirement using their own completion fences on abort.
The implementation is not compiled or GPU-verified.

## Stage 115 — asynchronous GPU compiler progress without repeated Model imports

`Renderer3D::warmModelPipelinesChecked` still distinguishes
`Ready`, `Pending`, and `Failed`. Stage 115 additionally
exposes `hasPendingModelPipelineJobs()` and
`pollPendingModelPipelineJobs()` so the renderer's already
submitted native shader/pipeline jobs can progress independently
of a repeated authored model import. An Editor hot appearance
update marked `gpuAppearancePending` does not reupload
speculative Models and textures while those jobs are still
in-flight. After they complete, the Editor retries the original
authoring transaction and publishes only the validated candidate.
This does not add a second ECS animation evaluator, reset
locomotion/attack clocks on material-only changes, or change
simulation ownership.

The optimization currently covers pending native shader/pipeline
jobs. A renderer whose `warmPipelineColor` variant remains
pending but exposes no native job may still trigger another
candidate staging attempt. Different file edits during a pending
job may also require another authoring attempt. Do not infer
successful GPU compilation until the real backend returns Ready.
The branch now includes NEREID main changes, but native and
GPU cross-system parity has not been tested.

## Stage 116 — HDR pipeline warmup without reimporting animation sources

The single `Renderer3D` shader cache now tracks asynchronous
`warmPipelineColor` requests in addition to ShaderJob and
PipelineJob callbacks. A variant that has not completed is recorded
as `pendingColorWarms_`. Later presentation-owner frames poll the
existing native handle; `hasPendingModelPipelineJobs()` remains
true while any shader, pipeline or HDR colour variant is pending.
This lets the editor keep an authored appearance transaction
unpublished and retain last-good MotionWeave player clocks without
reimporting Models solely to retry the identical GPU program.

Quality or instance-limit changes clear the outdated variant
polling list and retire old pipeline/shader handles behind actual
GPU fences. Native GPU API colour-warm readiness is currently
boolean, not a precise Failed/Pending status; persistent false
readiness is visible through `gpuWarmStats()` and requires backend
diagnostics for definitive failure classification.

NEREID water reflection rendering retains its own scene policy
while sharing the same generic GPU target retirement pool. This
stage changes no animation clock, ECS writer or gameplay state.
Source changes are committed to the feature branch but were not
compiled or GPU-tested.

## Stage 117 — transactional animation appearance with terminal GPU errors

During Model/Material/AnimationGraph hot reload the existing appearance
validator distinguishes an asynchronous HDR colour variant still Pending
from a Metal shader/pipeline that definitively Failed. It uses the same
`Renderer3D::warmModelPipelinesChecked` call from generic View, not a
separate graph evaluator. Failed variants remain cached as terminal
for that GPU pipeline generation, keeping last-good model materials
and MotionWeave presentation state. Quality/instance-limit shader
invalidation clears those failed-handle entries.

The generic gfx::Device status API is optional and backwards-compatible.
Legacy bool-only Vulkan/GL/third-party implementations report Ready/Pending
without inventing a Failed result. Real Metal and cross-backend validation,
CMake/CTest and visual continuity tests are **NOT RUN**.
See [Stage 117 handoff](agent-handoffs/animation-stage117-hdr-warm-status-2026-11-19.md).

## Stage 118 — GPU resource initialization for appearance publication

The AnimationGraph and MotionWeave presentation ECS remain unchanged.
Their genre-neutral View publication host can now reject and roll back
partial Renderer3D, LightSystem and trace GPU initialization without
mistaking a partially allocated pipeline or fallback texture for a valid
render owner. Rollback requires a real Device GPU-idle barrier; on
failure to prove idle, a native exception prevents unsafe publication
or destruction rather than silently falling back to old resource IDs.
Standalone LightSystem owners have explicit orphan/idle-failure counters.

This does not add a second AnimationGraph update, ECS simulation writer,
root motion authority or genre dependency. Native build/CTest, GPU
failure injection, visual hot reload and sanitizers were **NOT RUN**.
See [Stage 118 handoff](agent-handoffs/animation-stage118-gpu-init-rollback-2026-11-19.md).

## Stage 119 — compatible MotionWeave playback state across hot reload

`AnimationGraphInstance::snapshotPlayback()/restorePlayback()` now
capture an authored state name and normalized named clip node phases.
On the SAME source generation and compatible ordered bone hierarchy,
`MotionWeaveSourceBridge` attaches this one-shot snapshot to the
presentation ECS player while replacing its Model/Graph. After
resetting the new compiled graph, the evaluator restores matching
state/node/clip/loop identities with the new clip duration, and
re-evaluates a fresh pose without duplicating time-zero notifies.
New graph contacts, rig/native state, montage slots, pose matrices
and unwrapped notify cycles begin clean. Incompatible hierarchy or
respawn generation intentionally restarts from authored defaults.

The host must keep the old Model and Graph valid until bridge sync
and never run presentation ECS mutation during MotionWeave worker
dispatch. This is not a simulation snapshot or authority transfer.
**NOT RUN:** build, CTest, GPU editor, FPS/RTS visual parity, sanitizers.
See [Stage 119 handoff](agent-handoffs/animation-stage119-playback-continuity-2026-11-19.md).

## Stage 120 — canvas lifetime remains outside MotionWeave ECS

Stage 120 changes `RenderTexture` and the generic GPU resource
retirement owner used for native canvases and view paint layers.
Its GPU buffers, textures and shader programs retire via GPU
completion tokens at host frame boundaries. No AnimationGraph
parameters, source generations, pose evaluators, montage players,
root-motion authority or MotionWeave scheduler clocks were changed.

Compatible hot-reload playback introduced in Stage 119 remains
presentation-only. Canvas owner source changes are independent
from those graph/model generation semantics. **NOT COMPILED OR
TESTED** for this Stage 120.
See [Stage 120 handoff](agent-handoffs/animation-stage120-canvas-gpu-retirement-2026-11-19.md).

## Stage 121 — MotionWeave clocks independent from whole-model LOD

The new optional `view::Object::animationModel` identifies the
immutable graph rig separately from `draw.model` and any
`view::Object::lods` visual replacements. `View::update` sends
the source rig, stable ID, generation and named graph inputs to the
ONE MotionWeave presentation ECS player. `View::submitOne` selects
a camera-distance visual LOD without re-evaluating or resetting the
animation. Per-View hysteresis is source-ID/generation scoped and
no animation update occurs during draw submission.

Only a visual LOD whose skeleton, bind TRS and skin matrices match
the source consumes its evaluated pose. Distinct morph meshes need
explicit channel maps; incompatible whole-model LODs use bind pose
while the source rig and its socket attachments remain animated.
The host continues to own model/graph lifetime through the worker
join and synchronous draw. The RTS attachment pass now composes
socket draws through the generic View algorithm without migrating
RTS gameplay filters into the animation library.

**NOT RUN:** actual CMake build, animation/LOD CTests, GPU visual
parity, sanitizer or performance tests. Do not claim functional
validation from GitHub-only source commits.
[Stage 121 handoff](agent-handoffs/animation-stage121-universal-lod-attachments-2026-11-19.md).

## Stage 122 — animation clock continuity with LOD retargeting

`ModelPoseRetarget` maps one validated base Model's presentation
Pose into a different visual Model's bone hierarchy and optional
morph target channels. The source AnimationGraphInstance remains
owned by the same MotionWeave presentation ECS World and is never
reset because the camera chooses a lower-detail Model.
Strictly compatible LODs share source Pose without a copy;
explicit mappings produce target-local Pose and matrices; invalid
mappings use visual bind pose. Source sockets always use base-rig
matrices. No animation notifies are synthesized from a LOD switch.

FPS already evaluates its `characterPresentation` World; it now
calls `View::beginPreparedFrame` before sending precomputed draw
packets, rather than invoking `View::update`. RTS keeps its
game-specific authoring and state filters but reuses the common
retarget helper for LOD visuals whose node maps validate.

**NOT RUN:** native compiler/linker, animation graph or LOD CTests,
Metal/GL/Vulkan real GPU parity or sanitizers. See [Stage 122 handoff](agent-handoffs/animation-stage122-rig-retarget-2026-11-19.md).

## Stage 123 — retarget plans share assets, never graph clocks

`CompiledModelPoseRetarget` stores prevalidated static
source/visual bone and morph correspondence for a particular
pair of host-owned Model publication generations. Model LOD
selection never resets the MotionWeave source graph, advances
animation time twice, or transfers ECS/gameplay authority.
The CPU retarget step consumes the **already evaluated** source
Pose, computing a transient target-local Pose for one draw.

The source and visual pointers/versions must match before the
compiled steps run. On stale plan or incompatible source Pose,
the visual uses its own bind pose, while base rig sockets and
source AnimationGraph continue uninterrupted. When no compiled
plan exists the older validation path remains. Host assets must
outlive the synchronous animation worker join and Renderer3D
submission. `View::modelRetargetStats` is diagnostic only,
reset per presentation update / prepared frame.

**NOT RUN:** CMake/C++ compiler, CTest, GPU backend visual
comparison, sanitizers or crowd benchmarks. See [Stage 123 handoff](agent-handoffs/animation-stage123-compiled-retarget-cache-2026-11-19.md).

## Stage 124 — source graph unchanged; reusable target-local retarget scratch

The already-compiled source/visual skeleton mapping continues
to consume ONE MotionWeave source graph pose per frame.
`retargetCompiledModelPose` now writes the target-local
Pose and world matrices into reusable caller-owned vectors;
`aiverse::View::submitOne` retains scratch capacity per View.
A nested synchronous model submission uses isolated scratch,
preserving the outer draw's currently borrowed pose pointers.
Renderer3D copies draw-item matrices/morph weights during
submit and does not enqueue these Pose pointers on the GPU.

Retarget authoring diagnostics now distinguish invalid map
sizes/ancestry, duplicate/missing nodes, malformed morph slots,
bad bind transforms and obsolete asset generation. No
animation authority or new clock was introduced, nor was
baked/crowd clip state reset by model LOD changes.
An unsuccessful retarget call is a bind-pose fallback;
never rely on scratch remaining unchanged on failure.

**NOT RUN:** native C++ compile/link, CTest/CI,
Metal/Vulkan/GL visual, sanitizers or benchmarks.
[Stage 124 handoff](agent-handoffs/animation-stage124-pose-scratch-authoring-2026-11-19.md).

## Stage 125 — bounded Pose scratch independent from AnimationGraph

One MotionWeave graph continues to evaluate per source
entity/animation World. `View::submitOne` can reuse
precomputed Pose across visual LODs and retain at most
the configured CPU scratch capacity (512 KiB per View
by default) AFTER a completed synchronous submission.
One giant rig is never clipped by this retention budget.
Nested renderer capture uses isolated scratch; neither
the animation clock nor ECS schedule changes.

`ModelPublicationEpoch` is a globally monotonic,
CPU-only revision for replaced models. It is distinct
from entity `generation`, Graph generation and GPU
retirement tokens. `AppearanceAssetRegistry::Asset::packet`
propagates this revision to generic View. Compiled retarget
plans diagnose stale source/visual revisions, and rejected
plans safely produce visual bind poses while keeping the
same source animation time and socket attachments.
RTS optional legacy bind fallback is a host-only visual
choice and leaves authoritative game logic unchanged.

**NOT RUN:** native CMake/CTest/linker, Metal/Vulkan/GL,
performance, animator parity or sanitizers. See
[Stage 125 handoff](agent-handoffs/animation-stage125-scratch-budget-model-epochs-2026-11-19.md).

## Stage 126 — FPS graph hot reload continuity and independent model epochs

The FPS native presentation host now uses the same
`AnimationGraphInstance::snapshotPlayback` /
`AnimationPlayerComponent::pendingPlayback` contract as
MotionWeave source-bridge hot reload. When the character
Model remains unchanged and the graph file is recompiled,
compatible named state and normalized clip phases are
captured before the one-shot player restart and restored
by the existing evaluator. Deleted/renamed states or clip
nodes cannot be restored and restart at authored defaults.
Notifies, montage events, rig native/contact caches and
gameplay authority remain discarded.

The FPS prepared View packet forwards the published
`modelPublicationGeneration`, independent of player
network life or AnimationGraph generation. Material/graph
only appearance changes do not invalidate Model-asset
LOD retarget caches, while actual reimport/model source
changes require a new Model epoch and compiled plan.
Generic C++ `AppearanceSpec` and published
`Asset::packet` use the same epoch propagation API.
No second AnimationGraph update is introduced.

**NOT RUN:** CMake/C++ compiler, CTest, FPS animation
visual parity, GPU runtime validation or sanitizers.
See [Stage 126 handoff](agent-handoffs/animation-stage126-published-lod-hot-reload-2026-11-19.md).

## Stage 127 — reject stale graph/Model epoch pairs before ECS update

The universal `MotionWeaveSourceBridge` now stores
`modelPublicationGeneration` in its presentation
`AnimationSourceLinkComponent`. `View::update`
supplies the epoch from the host's `view::Object`,
and `View::submitOne` refuses to use a Pose produced
for a different asset epoch even when the logical actor
generation is unchanged.

A newly published nonzero Model epoch means a Model asset
may have changed at the same address. The bridge therefore
rejects a submitted frame that pairs the new Model epoch
with the SAME old Model and AnimationGraph pointers.
This is a fatal source contract violation, diagnosed
before World structural mutations: recompile the graph
against the new Model before publication. With a newly
compiled graph, the player resets instead of transferring
stale playback, IK, montage or notify state across Model
reimport. Graph-only reload with unchanged Model epoch
continues to support the compatible state/clip-phase
migration from Stage 119/126.

`AnimationBridgeStats::assetEpochRestarts` separately
counts new Model-epoch-triggered restarts. No second graph
update or simulation authority was added. Host-owned
published snapshots can be kept alive for a frame using
`PinnedAppearanceBatch`; the loaded Model itself still
requires separate lifetime ownership.

**NOT RUN:** native C++ compilation, animation graph
CTest, GPU FPS/RTS/editor parity or sanitizers.
See [Stage 127 handoff](agent-handoffs/animation-stage127-model-epoch-bridge-pinned-appearance-2026-11-19.md).

## Stage 128 — snapshot lifetime for native MotionWeave presentation

Native hosts can retain immutable appearance and AnimationGraph
snapshots in `PinnedAppearanceBatch` while staging the
`View::update` input and submitting visuals. New opt-in
`addOwned` and `pinRelatedModel` also retain genuinely
shared-owned CPU Models for the base rig and every referenced
visual LOD/attachment. `validatedSpan` rejects unpinned
Model references before update, or additionally checks a
host-provided GPU Device generation lease.

The source and visual Model publication epochs, MotionWeave
single-writer graph update, previous playback hot reload
contract and source rig attachments remain unchanged.
A shared CPU Model owner does NOT prevent a different host
from destroying that Model's GPU buffers without its real
fenced retirement. `ModelResourceCache` remains an explicitly
borrowed unique-owner host; do not fabricate shared ownership
for its models.

**NOT RUN:** native C++ compiler/link, CTest, Graph lifecycle
fault injection, GPU visual parity or sanitizers.
[Stage 128 handoff](agent-handoffs/animation-stage128-native-model-lifetime-pins-2026-11-19.md).

## Stage 129 — Model lifetime through one MotionWeave render transaction

The MotionWeave graph still runs once per logical presentation
source and only reads the borrowed source Model while its
host owner guarantees lifetime. Cache-owned native Models
now have a short-lived, owner-thread `ModelReadLease` (or
`ModelCacheReadScope` for multi-model legacy scenes) that
prevents `ModelResourceCache` from replacing/evicting CPU
Model data during the joined animation evaluation and
synchronous Renderer3D submission. A changed Model asset
epoch still invalidates old graph/skeleton caches as in
Stages 127/126; a Model read lease is NOT a substitute for
source asset publication revisions.

`PinnedAppearanceBatch` can now combine pinned appearance/
graph snapshots with cache-issued move-only Model read
leases from `addCacheOwned` / `pinCacheRelatedModel`, or
genuine shared Model owners from Stage 128. After
`validatedSpan`, complete View.update and View.submit
before releasing all guards. Legacy RTS takes a coarse
render-frame read scope after synchronous type loads.
GPU resources retain their original fence-based owner;
no animation update or gameplay writer was added.

**NOT RUN:** CMake/compiler/CTest, FPS/RTS/editor visual
validation, GPU Model fence failure-injection, sanitizers
or crowd benchmarks. [Stage 129 handoff](agent-handoffs/animation-stage129-model-cache-read-leases-2026-11-19.md).

## Stage 130 — Model read guard lifetime across MotionWeave dispatch

The source Model remains borrowed by the presentation
AnimationGraph ECS while MotionWeave evaluates; a host
that uses `ModelResourceCache` must hold a
`ModelReadLease` or `ModelCacheReadScope`
from before evaluation until the workers join and
Renderer3D finishes synchronous submission. Stage 130
keeps this owner-thread-only contract strict by
terminating on lease release from a foreign thread.
It does NOT add a second graph clock or turn the
CPU reader count into a GPU fence.

The Model cache's reader owner resets only AFTER a
successful zero-reader GPU-safe shutdown, enabling
a later Device reinitialization. The cache exposes
active/peak reader and denied-mutation counters for
host diagnostics; these are never written to gameplay
simulation or an authoritative replay checksum.
[Stage 130 handoff](agent-handoffs/animation-stage130-read-lease-lifecycle-diagnostics-2026-11-19.md).

**NOT RUN:** native CMake/CTest, animation/graphics parity,
thread-lifetime subprocess fixtures, sanitizers or benchmarks.

## Stage 131 — Model reader lifetime with streaming publication

`MotionWeave` remains single-writer for one native
presentation source. While its worker jobs and the
synchronous model submissions read borrowed Model rigs,
the render-owner-thread `ModelReadLease` /
`ModelCacheReadScope` prevents the host from evicting
already resident Models. Stage 131 moves the read thread
validation in `acquireModelRead(key)` BEFORE cache map
lookup, eliminating a lookup-before-validation hazard.

Normal new-asset streaming into distinct Model cache keys
remains allowed during a guard; old Model replacement,
trim, invalidation and shutdown remain forbidden.
The source frame's Model publication epoch continues
to guard against in-place reimports/reused pointers;
no animation clock, gameplay state or GPU-fence owner
was changed. The already-registered `render_device_epoch`
CPU test was expanded for owner-thread and streaming
invariants, but is **NOT RUN**.

[Stage 131 handoff](agent-handoffs/animation-stage131-streaming-cache-read-regressions-2026-11-19.md).


## Stage 132 — native Model streaming and owner-thread read lifetime

The presentation-asset owner can publish a **new** model-cache key during
a coarse read guard only on the established cache/render thread.
`ModelResourceCache::insertIfAbsent` now enforces that thread before it
touches the mutable `std::map`. It does not permit off-thread streaming
publication or cross-thread cache reads; pre-binding asset creation must
still be serialized by the native host. Existing model eviction, replacement,
trim, source invalidation and shutdown remain prohibited with active
read leases. CPU Model pins are not GPU retirement fences.

The `render_device_epoch` CPU regression now uses always-on `CHECK`
instead of Release-elidable assertions and contains staged-publish
transaction checks. On macOS/Linux it also spawns separate children to
test intentional fatal wrong-thread `ModelReadLease` /
`ModelCacheReadScope` release and destruction with live readers.

**NOT RUN:** native compiler/CMake/CTest/CI/GPU parity/sanitizers; these
are committed test sources, not verified passing tests. Details and exact
commits: [Stage 132 handoff](agent-handoffs/animation-stage132-thread-owned-streaming-release-tests-2026-11-19.md).


## Stage 133 — callback-safe native Model eviction

Render-owner-thread `ModelResourceCache` host detach/release callbacks may
read existing resident Model data but may no longer recursively mutate cache
membership or create new Model read guards while an outer replace, trim or
source invalidation retains borrowed pointers and map iterators. The new
exception-safe synchronous callback guard rejects those nested operations
before they can invalidate the outer transaction. Outer mutations recheck
active readers after returning from the host callback. Hosts must defer
any intended nested publication or eviction until the outer call completes.

This guard is NOT a concurrent map, GPU fence, or any change to MotionWeave's
presentation-only graph clock / deterministic simulation authority. No
whole-trim rollback is promised after an exception on a later entry.

**NOT RUN:** AIVERSE compiler/build/CTest/CI on this head, graphics
parity, sanitizer or performance checks. [Stage 133 details](agent-handoffs/animation-stage133-model-cache-callback-reentrancy-2026-11-19.md).


## Stage 134 — owner-thread Model residency enumeration

A native animation host must not enumerate `ModelResourceCache`'s
`std::map` from an animation worker or streaming job. In addition to
Stage 132's checked new-key publication, established-owner reads through
`find`, `contains`, `models`, `modelFiles`,
`affectedBySource`, `stats`, Model visitors and GPU residency
estimates now reject foreign-thread access before touching the map.
The returned `models()` reference is borrowed and cannot safely escape
the render-owner thread. Before binding the owner, initialization remains
host-serialized; there is no concurrent reader/writer support or GPU fence
implicit in these checks.

The existing `render_device_epoch` test source covers ten wrong-thread
reads and verifies owner-thread behavior afterward. macOS/Linux child
death fixtures now inherit the process environment for native runtime
library loading. Targeted RTS reload/trim callback inspection found no
direct nested Model cache mutation; if new hosts require one, defer it to
a later owner-thread transaction.

**NOT RUN:** native CMake/compiler/CTest, successful CI for Stage 134
HEAD, renderer/GPU visual parity, sanitizer or performance measurements.
Detailed [Stage 134 handoff](agent-handoffs/animation-stage134-owner-thread-read-guards-2026-11-19.md).


## Stage 135 — native Model ownership visitors and retirement-queue safety

A native presentation host may enumerate resident and GPU-retired Models using
`ModelResourceCache::forEachOwnedModel`, including the genre-neutral
`RenderResourceOwnerGroup` diagnostics. The visitor may read or recursively
enumerate cached Models, but may not mutate the cache, acquire fresh reader
leases, poll/collect GPU retirements or append unpublished Models while its
borrowed iterator is live. A nestable owner-thread callback guard enforces
this until all synchronous visits return, including exception unwinding.

`pendingRetirements`, `retirementStats`,
`estimatedRetiredGpuBytes` and `publicationBackpressured` now reject
off-thread reads of mutable retirement-queue state. Existing atomic active
reader/safety metrics remain cross-thread diagnostics. Calling noexcept
`retireUnpublished` inside a visitor falls back to an explicit GPU-idle
safety barrier and can stall, so defer it until the visit completes.

The existing `render_device_epoch` source has Stage 135 visitor and
retirement regressions, **not yet executed**. See
[Stage 135](agent-handoffs/animation-stage135-model-ownership-visitors-2026-11-19.md).
No claims of native compiler/CTest/CI or Metal/GL/Vulkan performance parity.


## Stage 136 — asynchronous Model GPU upload rollback and retirement observers

`ModelResourceCache::publishPrepared` consumes worker-prepared CPU Model
data on the render owner thread. It now checks the actual `uploadModel`
return value: incomplete geometry is never admitted as a resident Model.
Failed or exception-interrupted uploads retain a scratch Model until it can
be retired via the existing GPU completion queue; no ordinary
`unique_ptr<Model>` destructor may drop GPU-live handles. Expected duplicate
keys are rejected without GPU upload; `preparedUploadFailures()` counts
failed/throwing upload attempts. The legacy null failed-load tombstone
still works.

Resident Model removal now uses allocation-safe `enqueueFrom` to preserve
ownership if pending-retirement allocation fails. Synchronous diagnostic
callbacks emitted by retired-resource enqueue, collection and shutdown
drain run with the same nestable no-mutation guard as ownership visitors;
read-only diagnostics remain valid. The RTS async preparation host reports
failed uploads and advances rather than silently retrying a stale worker
result forever. This does not add automatic backoff or a new GPU fence.

The new `render_device_epoch` CPU test cases cover failed geometry
allocation, injected exception and diagnostic observer recursion, but
**have not run** on native Mac/Metal or a real GPU. Existing
`uploadModel` success criteria do not comprehensively detect shared
texture/morph failure; coordinate that with graphics/texture owners.
[Stage 136 handoff](agent-handoffs/animation-stage136-prepared-model-rollback-retirement-observers-2026-11-19.md).


## Stage 137 — feature closure before one final validation pass

Per the user-approved workflow, continue implementing all remaining
MotionWeave / genre-neutral Universal View functionality first. Keep the
existing single presentation ECS clock, GPU owner fences and simulation
isolation, and document each untested feature stage in its independent
handoff. No intermediate CTest is requested. The
[Stage 137 closure gate](agent-handoffs/animation-stage137-feature-freeze-validation-plan-2026-11-19.md)
lists the incomplete GPU image/morph upload detection, native host
adapters, source publication/LOD/retarget ownership, and FPS/RTS/editor
presentation boundaries that must be closed or explicitly excluded.

A future native macOS pass can be invoked after feature freeze with
`bash tools/animation/validate-final.sh`, which performs the full
build/CTest/benchmarks and separate sanitizer configurations in one
operation and leaves an explicit hardware visual checklist. The runner is
committed but **NOT RUN**. A single command cannot automatically establish
Metal/GL/Vulkan visual parity, GPU fence correctness or device-loss
behavior without real hardware checks; preserve that limitation.


## Stage 138 — GPU texture and morph failure handling in Model publication

Generic Model GPU upload now returns failure if a dimensioned image lacks a
valid uploaded texture, whether it used shared `ModelTextureCache` residency
or a standalone speculative texture. An RGBA32F morph-delta texture that
fails to allocate likewise rejects the Model; CPU morph deltas are not
discarded before a successful morph GPU upload.

Shared cache texture ownership nodes and lease allocations are prepared
before a device texture allocation, while the synchronous RTS import now
retires any partial Model GPU buffers/images through its existing real
GPU-completion barrier rather than a local Model destructor. The Stage 136
asynchronous prepared Model rollback is unchanged. `addImage` returns
`-1` when texture creation fails, and post-upload CPU pixel/morph/vertex/
index cleanup uses nonthrowing empty-vector swaps.

**Source-only stage:** all native CMake, CTest, GPU visual/fault injection,
sanitizers and benchmarks remain **NOT RUN** under the user-approved
feature-first workflow. [Stage 138 handoff](agent-handoffs/animation-stage138-image-morph-gpu-admission-2026-11-19.md).


## Stage 140 — presentation scene GPU lifetime beyond ModelResourceCache

A standalone native host may use the generic
`render::NativePresentationRetirement` to retain successful Model
and standalone SpriteMesh GPU buffer ownership until an actual
completion token proves that previous submissions are complete. RTS
road and shore adapters now do so on rebuild, editor ghost and weather
changes, while preserving the same MotionWeave animation writer and
presentation-only source/graph clocks.

Collection is currently called at top-level `RtsView::draw` entry,
before scene provider callbacks; **the host must validate that this is
a legitimate GPU frame/submission boundary**, not merely a second View
draw within one in-progress graphics frame. Failure to prove GPU idle
at explicit host shutdown blocks destruction of resident textures,
SpriteBatch resources and pending Model allocations.

This is an uncompiled feature stage under the user-approved
implementation-first workflow. Exact source revisions and remaining
boundary/owner review:
[Stage 140 handoff](agent-handoffs/animation-stage140-native-scene-gpu-retirement-2026-11-19.md).


## Stage 141 — unified Model, canvas and native scene retirement frame hook

Animation and presentation hosts must separate their View draw call from
their GPU completion lifetime. `RtsView::draw` no longer schedules GPU
retirement; it can execute more than once in a Device frame, including
editor and offscreen preview passes. Existing pre-render client/editor
`RtsView::collectRetiredModels` now polls the generic Model cache,
image rollback and native roads/shorelines retirement queues, including
standalone SpriteMesh buffers. `pendingModelRetirements` aggregates all
these Model + native scene resource queues, retaining the old API name.

Modelview Gallery and the generic native ModelAssetProvider are now
polled explicitly before `Device::beginFrame`, not from either portrait
or main-scene draw. Modelview and Gallery teardown enforce GPU idle
before freeing still-live preview textures and Models; `RtsView`
teardown enforces GPU completion before `GeometryScene::shutdown`.

These are source-level contracts, not evidence of correct hardware
Metal/Vulkan/GL backend fences. They do not alter MotionWeave's single
presentation ECS animation writer, graph time or gameplay simulation.
[Stage 141 handoff](agent-handoffs/animation-stage141-explicit-frame-boundary-preview-lifetime-2026-11-19.md).
**NOT RUN** native compiler/CMake/CTest/CI, GPU fault injection, FPS/RTS/
editor visual parity, sanitizers or performance benchmarks.


## Stage 142 — editor-owned preview Model lifetime and reusable Portrait

`Editor::retirePreviewModel` now transfers any direct
`ModelingWindow`, `AnimationGraphWindow` or `AnimationRigWindow`
GPU Model to one editor-owned `NativePresentationRetirement` queue.
The editor polls that queue only at its Device-owner frame hook,
before UI windows and offscreen renderer submissions, and requires
GPU idle proof at final shutdown before window destructors release
native render targets. No Model GPU release in an ordinary authoring
window may rely on the duration of a UI frame.

`ModelingWindow` stages a replacement Model, graph and bind pose
before swapping active content; a bad authoring/graph compile preserves
the previous GPU Model. Partly failed uploads and replaced assets
retire safely. Both Modeling and Animation/Rig preview windows also
release active Models on closed tabs without destroying their
registration/agent-facing editor objects.

The AnimationEditor Graph/Rig preview retains an offscreen Portrait
target between source/graph reloads instead of recreating it and
blocking GPU idle every edit. The Model remains under the Editor's GPU
retirement ownership; only the final window destruction shuts down
the Portrait. Failed graph compilation does not commit the new
program key, allowing retry. No animation ECS authority, root motion
protocol or second MotionWeave clock is introduced.

**NOT RUN:** native compiler, CMake/CTest, CI green for new head,
Metal/GL/Vulkan offscreen visual checks, GPU failure injection,
sanitizers or benchmarks. See
[Stage 142](agent-handoffs/animation-stage142-editor-preview-model-ownership-2026-11-19.md).


## Stage 143 — static and streamed geometry GPU retirement across View hosts

MotionWeave animated Model sources and static/HLOD scene geometry
share the same GPU-completion constraint, but **not the same CPU
ownership**. `render::GeometryScene` uniquely owns Models/HLOD proxies:
a rules reload now calls `clear(device)` to detach its borrowed scene
pointers and enqueue GPU assets behind actual completion tokens. Its
pre-frame `collectRetired` is polled by RtsView and FPS native hosts;
final `shutdown` requires a full GPU idle barrier, not a frame timer.
A malformed new GeometryScene still may leave the scene empty; a
last-good whole-scene transaction is not promised.

`world::WorldScene` streams GPU Models also referenced by other chunks
or asynchronous CPU workers via `shared_ptr`. An evicted cell transfers
models into one deferred owner map per unique Model. Only after all other
shared CPU references have dropped may the pre-frame hook atomically
remove the matching weak-cache entry (under its worker mutex) and enqueue
a final GPU retirement token. GameClient/FPS call this owner hook before
new rendering, not from the streaming worker, animation ECS tick or
renderer draw.

No extra animation writer, genre-dependent logic in MotionWeave core,
root-motion authority or GPU fence relaxation was introduced. **NOT
RUN:** CMake/compiler/CTest, real Metal/Vulkan/GL, FPS/RTS/editor visual
parity, TSAN or device-token failure injection.
[Stage 143 handoff](agent-handoffs/animation-stage143-geometry-world-streamed-model-retirement-2026-11-19.md).


## Stage 144 — compatible Graph/Model source hot reload without restarting animation

The genre-independent `render::AnimationGraphInstance` now exposes
`rebindPlayback(std::shared_ptr<const AnimationGraph>)`, using the
existing named state/clip `snapshotPlayback` and `restorePlayback`
contract to preserve compatible phase across graph replacements. Its
result indicates successful clock recovery; an incompatible state or
clip starts cleanly. Previous Model pointers, poses, rig/IK contacts,
native operation state, root motion and notify queues NEVER transfer.

Legacy `RtsView` source and appearance reload adapters previously
erased affected EntityAnim instances (correct GPU/Model lifetime, but
lost compatible playback). They now capture one short-lived, Model-free
value snapshot per full logical entity ID before model/graph/LOD
replacement. Records are bounded at 8192 and expire after 120 View
frames. On the next matching entity presentation, scalar visual
events/death/phase inputs can be restored, including a named Graph
state and clip phase. For clip-only legacy actors, one unblended
base Animator clip may be restored by *new* Model clip name,
semantic kind, loop compatibility and normalized phase; no prior
clip indices/crossfades/additive/notify state transfer.

An entity's authored `program` can be overridden at runtime; the
snapshot must wait until that final programmable/Lua graph is chosen,
not restore against an intermediate default TypeLook graph.
The independent generic MotionWeave ECS writer is never seeded
from this legacy state. ModelingWindow and AnimationEditor panels
also attempt compatible preview clock retention on new authored
graph/model loads. Invalid mappings restart and optional memory
failures cannot abort accepted GPU Model reloads.

**NOT RUN:** native compiler, CMake/CTest, real GPU, editor/FPS/RTS
visual parity, source reimport/LOD/retarget replay injection,
sanitizers or benchmarking. This is a source-only implementation
under the user-requested feature-first workflow. See
[Stage 144 handoff](agent-handoffs/animation-stage144-named-playback-source-reload-2026-11-19.md).


## Stage 145 — independent MotionWeave ECS asset epoch playback rebind

The genre-neutral `MotionWeaveSourceBridge::sync` now records a copied
`RigHierarchy` (exact node names and parent indices, no old borrowed
Model pointers) for every presentation source. Changed/new sources
share one immutable hierarchy snapshot per distinct Model publication
within a sync. The copied rig metadata is removed with absent sources,
`forget` and `clear`.

A graph/Model publication change may transfer its named state and
clip phases through `AnimationPlayerComponent::pendingPlayback` only
when the logical source generation is unchanged, the old/new copied
rigs match exactly and the previous graph player is valid. Across a
Model publication-epoch change, **both epochs must be nonzero** and
the new Model must be a DIFFERENT C++ object. Same-pointer in-place
reimport cannot safely snapshot the old graph's potentially mutated
clip data; it restarts, and changing epoch without recompiling the
Graph is still rejected.

A new ECS graph player is initialized from the new Model/Graph, then
attempts optional portable playback restoration. Incompatible phase,
recovery failure or exception leaves authored default clocks and
discards the one-shot snapshot. Native rig/IK/contact state, old
poses, montage events, gameplay root motion or other GPU handles
never transfer; the independent presentation ECS still has exactly
one writer, and the bridge itself does not evaluate animation.

**Host CPU lifetime requirement:** the old Model/Graph must remain
CPU-readable until `sync()` completes because old
`AnimationGraphInstance::snapshotPlayback` reads the previous
compiled Graph's clip names/durations. An asset already detached via
`forget` cannot resume in this stage; a separate opt-in pre-retire
snapshot API is planned for Stage 146.

[Stage 145 handoff](agent-handoffs/animation-stage145-generic-ecs-source-epoch-playback-2026-11-19.md).
**NOT RUN:** native compiler/CMake/CTest, current passing CI, GPU/driver
Metal/Vulkan/GL, worker race/fence failure, visual parity, sanitizers
or performance benchmarks. All tests remain deferred until feature freeze.



## Pre-retirement source playback handoff (Stage 146, 2026-11-19)

Generic presentation hosts using `<aiverselib/animation/MotionWeaveSourceBridge.h>` can opt into `bridge.forgetForReimport(world, sourceIds)` (or `view.forgetSourcesForReimport(sourceIds)`) before retiring the OLD CPU Model and Graph. Unlike ordinary `forget`, this captures the old healthy graph player's **named** playback state and clip phases before the ECS source and any borrowed Model/Graph references are destroyed. Call on the presentation thread outside worker update; the caller still owns CPU Model read leases, GPU retirement tokens and graphics fences.

When a NEW Model/compiled Graph is next synchronized under the same source identity and logical generation, the bridge restores playback only if copied rig names/parents match exactly, both publication epochs are nonzero and different, and the Model address is different. The old address is compared only as an integer (never dereferenced); allocator reuse and in-place reimport restart conservatively. Incompatible authored clips/states also restart. The one-shot snapshot is removed on first attempted recovery, expires after 120 source `sync` calls and is capped at 8192 records. The bridge never transfers old physical pose, contacts, native solver/IK, root motion, montage/notifies, gameplay authority or GPU handles. `forget` and `clear` cancel pending continuity.

This feature was committed without native validation per the agreed feature-first workflow. Exact commits and upcoming tests are in [Stage 146 handoff](agent-handoffs/animation-stage146-pre-retirement-playback-rebind-2026-11-19.md). **NOT RUN:** compiler, CTest, real Metal/GL/Vulkan, hot-reimport and sanitizer tests.



## Stage 147 correction — RTS epochs and graph-only rebind (2026-11-19)

The RTS adapter now supplies `ModelChoice::modelGeneration` to generic `View::Object::animationModelGeneration`, rather than silently passing zero. It only opts into the generic player with a valid packed ECS ID and a nonzero published Model epoch; ambiguous sources keep the legacy path. For `forgetForReimport` restoration the Stage 146 rule is **extended**: a newly compiled Graph referencing the SAME immutable Model at the SAME nonzero epoch can recover compatible named playback after pre-detach capture, in addition to a genuinely new Model object with a DIFFERENT nonzero epoch. Never mix changed Model address with unchanged epoch or in-place changed Model data with unchanged epoch. Identity/rig/graph-state mismatches still restart. These paths are source-implemented but **NOT RUN** on native Mac; details: [Stage 147](agent-handoffs/animation-stage147-rts-publication-epoch-and-graph-rebind-2026-11-19.md).



## Stage 148 — common MotionWeave ECS graph-only hot reload

The reusable presentation ECS now exposes `rebindAnimationPresentationGraph(world, entity, immutableModel, nextGraph)`. Any native project or editor ECS owner can replace a compiled Graph bound to the SAME immutable Model, with optional name-based state/clip continuity and no genre-specific logic. A valid replacement clears old montage and graph-notify streams, invalidates old pose before another draw, and leaves EXACTLY one existing ECS player to restart on the normal MotionWeave update. If an old snapshot is unhealthy/unsupported, playback restarts from authored defaults. The API reports whether the compiled graph was replaced and whether portable playback was captured, not whether later restore succeeded.

The pre-retirement source bridge separately requires a different compiled Graph identity when reimporting only the Graph under unchanged Model address and publication epoch. Cached previous graph addresses are held as numerical comparisons only, never dereferenced after CPU asset retirement. A Model source replacement still requires exact copied skeleton compatibility, the same logical source generation and genuinely different nonzero publication epoch/new Model address. Details: [Stage 148 handoff](agent-handoffs/animation-stage148-genre-neutral-graph-rebind-native-owners-2026-11-19.md). **NOT RUN:** compiler, CMake/CTest, GPU/backend visual checks, stress/fault tests, sanitizers and benchmark. All runtime validation remains for the user-requested post-feature-freeze pass.
