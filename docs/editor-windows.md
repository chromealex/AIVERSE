# Adding a window to the editor

The editor (`apps/editor`) is a set of dockable Dear ImGui windows (docking branch). Every window
is a small class; the editor creates one instance of each registered class, lists it in the top
menu of its group (`group()`, see below), docks it into the default layout, saves the user's layout
(`<userDataDir>/editor_layout.ini`) and lets agents open/close it over MCP (`editor_windows`,
`editor_window`).

## 1. Create the file

Put a `.cpp` file in `apps/editor/windows/`. Every file there is compiled into the editor
automatically (CMake `GLOB ... CONFIGURE_DEPENDS`), nothing else to register.

```cpp
// apps/editor/windows/BalanceWindow.cpp
#include "../Editor.h"
#include "../EditorUtil.h"     // ui::inputString, ui::inputMultiline, ui::help

namespace rts::editor {

class BalanceWindow : public EditorWindow {
public:
    const char* id() const override { return "balance"; }        // stable latin id (MCP, layout)
    const char* title() const override { return "Баланс"; }      // tab title, Russian (must be unique)
    const char* group() const override { return "logic"; }       // top menu: scene | world | look | logic | publish | service
    const char* dockArea() const override { return "bottom"; }   // left | left-bottom | right (Inspector) | right-bottom (rules, world / look tools) | center | bottom
    int order() const override { return 5; }                     // tab order in that area (0 = shown first)
    bool openByDefault() const override { return false; }        // hidden until opened from the menu

    void draw(Editor& ed) override {                             // called between ImGui::Begin/End
        for (auto& t : ed.rules().types) {
            if (t.cost.empty()) continue;
            ImGui::Text("%s: %lld hp", t.name.c_str(), (long long)t.maxHp.toInt());
        }
    }
    void update(Editor& ed, double dt) override {}              // optional: every frame, even when closed
};

RTS_EDITOR_WINDOW(BalanceWindow);

} // namespace rts::editor
```

`title()` is the plain tab text (MCP `editor_windows` reports it). The ImGui name is
`imguiName()` = `"<icon>  <title>###<id>"`: the `###id` part keeps docking, saved layouts and focus
stable. Use `imguiName()` (or `ed.focusWindow("id")`) whenever ImGui needs the window's name. The tab
icon comes from `windowIconFor(id())` in `EditorIcons.h`, or override `icon()`.

Rebuild the editor; the window appears in the menu of its group. New windows join the tabs of their
`dockArea` the first time they open; **Вид → Сбросить раскладку** rebuilds the default layout.

### Menus and groups

| `group()` | Top menu | Windows | Workspace (Вид → Раскладки) |
|---|---|---|---|
| `scene` | **Сцена** | Сцена, Игра, Иерархия, Инспектор, Инструменты | Карта и объекты |
| `world` | **Мир** | Рельеф, Тайлы, Генерация, Материалы, Поля, Туман войны | Ландшафт |
| `look` | **Графика** | Освещение, Постэффекты, Эффекты, Модели, Оверлеи | Графика и эффекты |
| `logic` | **Логика** | Проект, Правила, Скрипты, Статистика (+ запуск теста) | Правила и тест |
| `publish` | **Сборка** | Сборка, Иконки и заставка (+ цели сборки) | Выпуск |
| `service` | **Вид** | Консоль, ИИ-агент | — |
| anything else | **Вид → Другие окна** | | |

A workspace (`Editor::openWorkspace(group)`) opens the default windows plus every window of the group,
rebuilds the layout and brings the group's tabs to the front. Clicking a window in a menu opens it and
brings it to the front; clicking it again while it is in front hides it.

Other menus: **Файл** (projects, maps), **Правка** (undo, symmetry, ramps, map check), **Вид** (service
windows, workspaces, what the Scene shows, theme). Shortcuts: Ctrl/Cmd+S save, Ctrl/Cmd+Z / Y undo/redo,
F5 test, Ctrl/Cmd+B build window; in the Scene 1…0 pick tools, F focus, [ ] brush size, Delete removes.

## 2. What a window can use (`Editor` public API, `apps/editor/Editor.h`)

| API | What for |
|---|---|
| `ed.ws()` | the `Workspace`: project (manifest, `rulesJson`, parsed `rules`), map document, activity log, and **`ws().call(op, params)`** — run any MCP operation (`rules_set`, `entity_place`, `match_run`, ...) exactly like an agent; prefer this over editing data directly, it validates and records history |
| `ed.doc()` | the open map (`MapDoc`): `data` (terrain layers, players, entities, roads, `extra` JSON blocks), `beginEdit()` before a change (undo snapshot), `touched()` after a change (views rebuild) |
| `ed.rules()` | parsed rules of the open game (read-only; change rules through `rules_set`) |
| `ed.selection`, `ed.select(...)`, `ed.selectEntity(i)` | what the Inspector shows (the primary item) |
| `ed.selected()`, `ed.selectToggle/selectAdd/selectMany`, `ed.isSelected(s)`, `ed.selectedEntities()`, `ed.removeSelectedEntities()` | multi-selection (Hierarchy, Scene, Inspector «Детали»); `Selection::Live` = a world object by `ref` |
| `ed.world()`, `ed.worldLive()`, `ed.worldSim()`, `ed.worldRow(s)`, `ed.worldSelection(row)`, `ed.refreshWorld()` | world tree: every object of the game world — edit mode: the map's setup run in a throwaway preview; play: the running test (docs/editor-world-tree.md) |
| `ed.hover`, `ed.setHover(s)`, `ed.pickObject(vp, cam, x, y)`, `ed.sceneHidden` / `ed.sceneLocked` | hover shared by Scene and Hierarchy, picking of any object, eye / lock in the Scene |
| `ed.tool`, `ed.brush`, `ed.placeType`, ... | Scene tool state |
| `ed.sceneCam`, `ed.focusScene(x, z)`, `ed.flash(x0, z0, x1, z1)` | camera and highlight in Scene |
| `ed.playing()`, `ed.sim()`, `ed.simModule()`, `ed.startPlay(slot)`, `ed.stopPlay()` | the play-test simulation (read-only from windows; issue player commands with `ed.issueCommand`) |
| `ed.playHistory`, `ed.frameHistory` | sampled statistics |
| `Viewport` + `ed.renderScene(vp, cam, overlays)` / `ed.renderGame(vp, cam)` | a 3D view of the map inside your window (see `SceneWindow.cpp`: `vp.ensure()`, render, `vp.image()`) |
| `ed.status` | text in the status bar |

Rules for windows:
- Never write into the simulation (`ed.sim()`): it is deterministic and replayable; use commands.
- Change rules and maps through `ws().call(...)` or `doc().beginEdit()` + edit + `doc().touched()`.
- Keep per-window state in members (one instance per window).
- Russian UI text, English code and comments (see AGENTS.md).

## 3. Look: theme, icons, toolbars

The editor style (dark / light, accent presets, menu **Вид → Тема**, MCP `editor_theme`) lives in
`apps/editor/EditorTheme.*`; windows don't set colours of their own. Prefer the theme's colours
(`ImGui::GetStyle().Colors`, `ui::accentColor()`, `ed.theme().palette()`) over hard-coded RGB, so
both themes stay readable.

Icons: Lucide font merged into the UI font, constants in `apps/editor/EditorIcons.h`
(`ImGui::Button(ICON_PLAY " Играть")`). Helpers in `EditorUtil.h`:

| Helper | What |
|---|---|
| `ui::toolButton(ICON_X, "Подсказка", active, enabled)` | square icon button: hover / active (accent) states, tooltip |
| `ui::beginSegment()` / `ui::endSegment()` | segmented group of tool buttons on one background (`SameLine()` between them) |
| `ui::toolbarDivider()` | thin vertical divider between groups |
| `ui::beginToolbar()` / `ui::endToolbar(y)` | toolbar strip at the top of a `zeroPadding()` window (Scene, Game) |
| `ui::button(ICON_X, "Текст", primary)` | icon + label button; `primary` = filled with the accent |
| `ui::emptyState(...)` / `ui::emptyStateHere(icon, title, hint)` | centred placeholder: big icon, title, hint |
| `ui::heading("Текст")` | section title in the semibold heading font |
| `ui::checkbox("Текст", &v)` | checkbox whose label is shortened with "…" to the column (full text in a tooltip); use instead of `ImGui::Checkbox` |
| `ui::button` / `ui::wideButton` with a width | a label that doesn't fit is shortened with "…", the whole label in a tooltip |
| `ui::collapsing("Секция")` | collapsible section in the neutral frame colour (ImGui's default looks selected) |
| `ui::inputHint("##id", "подсказка", str)` | `std::string` text field with a placeholder |
| `ui::pathText(path, maxW)` / `ui::prettyPath(path)` | path for display: home as `~`, shortened from the left, full path on hover |
| `ed.sceneVisible()` | the Scene tab drew this frame: check it before drawing gizmos or painting with `ed.sceneViewport` (a hidden tab leaves a stale viewport) |
| `ui::section(ICON_X, "Текст")` | section title with an icon and a hairline to the right edge (instead of `SeparatorText`) |
| `ui::beginProps("##id")` + `ui::prop("Подпись", "подсказка")` + widget with a `"##"` label + `ui::endProps()` | property grid: dim labels in a left column, full-width widgets on the right; `ui::propWide()` for a row of its own (checkbox, buttons) |
| `ui::searchField("##id", str, "Подсказка")` | search field with the hint inside and a clear button; `ui::contains(text, filter)` matches case-insensitively |
| `ui::segmented("##id", value, labels, n)` / `ui::choiceGrid(...)` | mode switch filling the width / wrapping tiles for 5+ options |
| `ui::callout(ui::Tone::Warn, "…")`, `ui::resultMessage(msg)` | tinted box with an icon for results, warnings, errors (`resultMessage` guesses the tone) |
| `ui::toneText(tone, fmt, ...)`, `ui::toneColor(tone)`, `ui::badge(text, color)` | theme colours for ok / warning / error text and small count badges — never hard-coded RGB |
| `ui::headerCard(icon, title, subtitle, color)` | card at the top of a panel (Inspector: what is selected) |
| `ui::wideButton(icon, text, index, count, primary)` | buttons that share one row evenly |
| `ui::hint("…")`, `ui::menuItem(icon, text, shortcut)`, `ui::viewportLabel(dl, pos, text)` | dim explanation line, menu item with an icon, readable label over a 3D view |

Style rules: Russian UI text; a window with nothing to show draws `ui::emptyStateHere` with what to do;
results of actions go to a callout in the window (and `ed.status` for the status bar); quote other
windows by their Russian title in «ёлочках» («Сцена», «Консоль»).

## 4. Exposing it to agents

If the window adds an action an agent should be able to do, register an operation (in the
window's constructor is too early; do it in `update()` once, or in `EditorAgent.cpp`):

```cpp
ed.ws().registerOp({"balance_report", "Damage per cost of every unit type.", Json::parse("{}"),
                    [&ed](const Json&) { Json r = Json::object(); /* ... */ return r; }, true});
```

Operations appear in MCP `tools/list` for agents connected through `rtsmcp` (the tool list is the
`Workspace` registry; GUI-only operations have `guiOnly = true` and fail politely without the
editor window).

## Game HUD

The Game play-test renders `ui/hud.rml` and `ui/howto.rml` through RmlUi into its
viewport after the scene. Missing documents, styles and images fall back to
`engine/assets/ui`; game fonts (`fonts/ui.ttf`, `fonts/title.ttf`) follow the client.
The editor selects the game language with the same saved-choice/system-language rules.
Restart the play-test after editing RML, RCSS or fonts to reload them.

`apps/client/UiModel.h` owns the shared `game` model schema and change-only updates.
`apps/editor/GameHud` adapts the editor simulation to that model: resource/status
panels, script HUD cards, support actions and region ratios. Buttons issue Commands;
pause/restart/stop operate on the editor play-test. Pointer coordinates are local to
the viewport; RmlUi layout and projection use its framebuffer size and device density.
World-space region markers remain editor overlays.

This is an embedded play-test HUD, not a second GameClient: client lobby/menu flow,
UI Lua `game` APIs, audio playback and the client's full standard RTS command card/minimap
are not hosted by the editor adapter. For those use **Play as in the client** below.

## Play as in the client

The Game window can also run the game exactly as RTSGame does: the same `GameClient`
(`apps/client/GameClient*.cpp`, `Client*.cpp`; RTSGame's `main.cpp` adds only the entry point)
hosted by `apps/editor/ClientHost` and rendered into the window's offscreen target, inside the
device simulator frame. Everything is the client's: the game's menus / meta screens
(`ui/main.lua`, `ui.screen` / `ui.popup`), launch parameters, battle -> result -> meta flow,
view scripts (draw, camera, HUD documents, `view.ui_call`), script UI, input (rules `mapClick`
tap on release, left-drag pan, wheel zoom, `map-drag` HUD elements, RmlUi clicks, keys), the
pause screen and audio. The editor's own play-test tools (bots vs bots, player pick, speed,
pause, stats) stay for the map play-test; the two never run at once.

- Start: the gamepad button of the Game toolbar, «Как в клиенте» under the empty Game window,
  the play popup of the top bar, menu Логика → Тест, MCP `play_start {"client": true}`; F5 and
  the top-bar Play button start it for games with their own screens (`ui/main.lua`). Stop /
  restart in the Game toolbar, F5, `play_stop`; the game's own Quit stops it too.
- «В отдельном окне клиента» (external-link button): the project in a separate RTSGame window
  (RTS_CONTENT = project), the one built next to this editor first (same C++ modules).
- The project's C++ modules (rules `nativeModules`) must be in the editor binary, as for the
  play-test (the banner offers the per-project editor build).
- Hosting: while the host calls into the client it sets `platform::setHostedView` (logical size of
  the device screen, framebuffer size, scale, safe area of the device preset, touch, content root =
  project, `engine/...` -> the engine assets; `requestQuit` / fullscreen are recorded, the window
  title is left alone) and restores the editor's ImGui context afterwards (every `ImGuiLayer` owns
  its context). Pointer events inside the device screen (or captured by a press inside) and keys
  while the Game window has focus are mapped into screen points and forwarded.
- Profiles / saves go to the editor's user data dir (`RTS_UI_SAVE_DIR` overrides, as in the client).
- Command line (automated checks): `rts_editor --project <dir> --play-client --device iphone-se:portrait --window game`.
