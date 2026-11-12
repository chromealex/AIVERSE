# Программируемые анимации AIVERSE

`engine/render/AnimationGraph.h` предоставляет граф поз и машину состояний поверх
существующих glTF/FBX/процедурных моделей, семплера клипов и двухкостного IK.
Расширения UAF: trait stacks с Pre/Post hooks, вложенные графы, Motion Matching
и программируемый forward rig. Общий граф неизменяем; состояние принадлежит instance.
Это система отображения: её числа с плавающей точкой, параметры и переходы не входят
в checksum и никогда не управляют перемещением, уроном или коллизией симуляции.

## Подключение к игре

Добавьте объект `animationGraph` в `view` типа, рядом с `model` и `animations`.
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
ретаргетинг, montage slots/notifies, root-motion authority или motion warping.
Существующий terrain foot IK и procedural modifiers применяются после графа;
при их одновременном использовании учитывайте порядок и не дублируйте коррекцию ног.
Граф не генерирует старые clip events: звуки/VFX запускайте из подтверждённых событий
игры. Модельные mesh LOD и construction stages используют существующий mapping костей.

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
