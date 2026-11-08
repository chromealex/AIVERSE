# World tree: Hierarchy and Inspector «Детали»

The editor's **Иерархия** lists every object of the game world, whatever created it, and the
**Инспектор** shows all data of the selected object or objects (like Unreal's *Details*). It works for
any game on the engine: nothing in it knows a particular game; a game can add its own grouping and
row names through `game.json` (below).

Code: `apps/editor/core/WorldInspect.*` (GPU-free, in `rts_editor_core`, unit-tested), `apps/editor/EditorWorld.cpp`
(preview / live rows, multi-selection, Scene picking, MCP), `apps/editor/windows/HierarchyWindow.cpp`,
`apps/editor/DetailsPanel.*` (used by `InspectorWindow.cpp`), `apps/editor/EditorWorldEdit.h` (edits of
several map entities as one undo step).

## Where the objects come from

| Mode | Source | Editable |
|---|---|---|
| Edit (no test running) | the map document + a **preview**: the open map set up in a throwaway `Simulation` (seed 1) exactly as a match starts — map entities, faction start units, the game's Lua scripts, C++ modules (`nativeModules`), engine systems — then `previewTicks` ticks (default 2) | map entities: yes; what the game created: read-only (✦ marker) |
| Play (editor play-test or «Как в клиенте») | the running simulation, read 5 times a second | read-only, live values |

- The preview is rebuilt 0.6 s after the last change of the map, the rules (`reloadRules`) or any
  `scripts/**/*.lua` file of the project (checked once a second). A preview slower than 2.5 s switches
  automatic rebuilds off (the refresh button ↻ in the Hierarchy rebuilds on request). Errors (a loader
  error, the first script error) are shown under the search field; the editor never crashes on them and
  the document is never touched.
- Map entities are matched to the objects spawned for them right after `setup` (`linkMapEntities`: the
  module spawns them in document order after the faction start units; type, owner and, on flat maps, the
  position must agree). Objects present when a world is first seen are *setup* objects ("Создан игрой на
  старте"), later ones *runtime* ("Создан во время игры").
- Reading never changes the simulation. The only call into it is `GameModule::prepareSnapshot`
  (`ScriptEngine::flush`), the observation step every checksum / snapshot runs anyway, so the Lua entity
  tables (`game.data`) are current.
- Ids: a world object is identified by its **ref** = packed entity + 1 (`worldRef`, 0 = none). Seeds and
  setup are deterministic, so the same data gives the same refs and a selection survives a preview
  rebuild; in play mode the entity generation keeps refs unique.

## Hierarchy

- Root **Карта** / **Мир** (click: map properties), then the objects, then the other map nodes: player
  starts, areas, regions, waves, map triggers, roads, planet bodies and every other block of the map
  (`lights`, `gridFields`, ... — edited as JSON in the Inspector). Sections appear only when present.
- **Grouping** (list button): owner → type (default; every player is listed, even without objects),
  type, category → type, component (an object is listed under each of its components), nesting
  (passengers under their transport, projectiles under the shooter, ground effects under the caster,
  summons under the summoner), origin → type, plus the game's groupings.
- **Search**: words are AND-ed; `t:` type (id, name, label, category), `p:` owner, `c:` component (id or
  Russian title), `r:` region, `d:` script data keys / values, `#` entity number or map id. A plain word
  matches any of these.
- Long names end with "…" and the full text is in the tooltip; counts stay at the right edge. Lists of any
  size are clipped (`ImGuiListClipper`); collapsed groups are not even materialized.
- **Selection**: click selects, Ctrl / Cmd toggles, Shift selects the range from the last clicked row, a
  click on a group selects its objects (a player row opens the player's properties; its context menu
  selects all its objects). Ctrl / Cmd+A: every listed object.
- **Scene sync**: selecting in the Scene opens the groups and scrolls to the row; hovering a row
  highlights the object in the Scene and the object under the Scene cursor highlights its row.
- **Keyboard** (Hierarchy focused): ↑ ↓ move, ← → collapse / expand (← on an item goes to its group),
  Delete deletes (selected map entities as one undo step, or the selected wave / trigger / area / road),
  F focuses the Scene, F2 renames (map id of an entity, area, trigger id, player name), Ctrl / Cmd+D
  duplicates (entities, waves, triggers, areas).
- **Context menus**: object (show, rules of the type, select all of the type, change owner, replace type,
  duplicate, set id, delete); group / player (select all, show, player properties, rules of the type,
  change owner / replace type / duplicate / delete all, hide / lock); waves / triggers / areas (show,
  rename, duplicate, delete); regions (paint).
- **Drag** map objects (or the whole selection) onto a player / Нейтральные / Враждебные row to change
  the owner (one undo step).
- **Eye / lock** (on hover, or while on) per player, per type and per player + type: hidden objects are
  not drawn in the Scene, locked ones can't be picked or dragged there (`Editor::sceneHidden` /
  `sceneLocked`, keys `o<owner>`, `t<type>`, `o<owner>/t<type>`); the eye of Зоны / Регионы toggles their
  overlay. «Объекты игры в «Сцене»» (grouping menu) draws the preview's game-created objects in the
  edit Scene (read-only, pickable).

## Inspector «Детали»

Shown for map entities and world objects (`Selection::Entity` / `Selection::Live`), one or many:

1. Header card (type, owner, origin, entity number; several: count and the types), actions (show, rules
   of the type, delete).
2. **Карта (правка)** — map entities in edit mode: type, owner, position (several: the centre — dragging
   moves all), garrison, resource amount (• = set by the map; the tooltip names the type's default), id.
   With several objects a field shows the shared value or "—"; a change applies to all as one undo step.
3. **Характеристики** — base value of the type → effective value (techs, levels, buffs, statuses) for
   max hp, armor, speed, sight, damage, range.
4. One section per ECS component the object has (`worldComponents()`: all 63 pool types of the RTS
   module, Russian titles), fields by name; entity references are links that select the object.
5. **Данные скрипта** (`game.data(h)` decoded) and **Правила типа** (the type's block of rules.json).

Several objects: values are merged member by member, differing values show "—"; a section that only
some have says "(k из N)". Data is read for the first 300 objects. The filter field keeps the rows whose
name or value matches (a matching section title keeps the whole section). Right click on a row: copy the
value, its path or the JSON; on a section header: copy the section; the braces button copies everything.
Live / game-created objects are read-only.

The other selections (players, areas, regions, waves, triggers, map properties, rules items) keep their
Inspector pages; map blocks are edited as JSON (`map_set`), roads and planet bodies have small pages.

## Game hooks (`game.json` → `editor.hierarchy`)

```json
"editor": {"hierarchy": {
  "previewTicks": 2,
  "label": "name",
  "categories": ["infantry", "vehicle", "air"],
  "groupings": [
    {"title": "По фронтам", "by": ["data:front", "owner"]},
    {"title": "По тегу", "by": ["tag", "type"]}
  ]
}}
```

| Key | Meaning |
|---|---|
| `previewTicks` | ticks the edit-mode preview runs after setup (0…600, default 2): games whose scripts create the scene on the first tick(s) |
| `label` | a `game.data(h)` key whose value names the row (e.g. a script sets `game.data(h).name = "Army of the North"`) |
| `categories` | tags in priority order: an object's category is the first of them its type has (else its kind: units, structures, heroes, resources, projectiles, ground effects, other) |
| `groupings` | extra grouping modes: `title` + `by` keys from `owner`, `type`, `kind`, `category`, `origin`, `region`, `parent`, `tag`, `component`, `data:<key>` |

Unknown keys are reported in the editor console (`game.json editor.hierarchy...`). Lua scripts need no
API of their own: whatever they keep in `game.data(h)` is searchable (`d:`), groupable (`data:<key>`) and
shown in the details.

## MCP

| Operation | What |
|---|---|
| `world_entities` | the rows (`ref`, `doc`, name, type, owner, kind, origin, position, hp, parent, components); `group` keys → nested counts; `filter` = the Hierarchy search |
| `world_entity` | everything about one object by `ref` or `doc`: components, stats, rules block, row, map entry |
| `editor_select` | select map entities (`docs`) / world objects (`refs`), `focus` flies the Scene camera |

## Limits

- Planet maps: the preview's game-created objects are listed, but not drawn / picked in the edit Scene.
- Live Lua data is as of the last observation (5 Hz).
- Hosted client («Как в клиенте»): the editor sees the match only when the client has one (menus: the edit
  preview stays); objects count as *setup* only if the editor saw the match within its first 2 seconds, and
  map entities are not linked (the client may play another map than the open one).
- Native module state (`NativeStore`) is global, not per object; it is not shown per object.
