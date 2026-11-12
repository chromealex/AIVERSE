# Importing Unity UI (uGUI / UI.Windows) and Unity Localization

`tools/importers/unity_ui` converts the screens of a Unity project into the engine's game UI (RmlUi documents +
Lua, docs/ui-scripting.md) and its Unity Localization string tables into engine/i18n tables. Python 3.8+, no
packages; ImageMagick `convert` is used only for PSD/TIFF/GIF/EXR textures.

```sh
python3 tools/importers/unity_ui/import.py --source <UnityProject | Assets> --out <game folder> \
    [--screens <folder under Assets>]... [--prefab <uGUI prefab>]... [--only <screen>]... \
    [--no-l10n] [--no-screens] [--no-copy] [--key-prefix p] [--json]
python3 tools/importers/unity_ui/test_import.py        # synthetic fixture (fixture.py); ctest: unity_ui_importer
```

The output folder must not overlap the Unity project. Textures and fonts are copied from the user's own copy of the
game into `ui/img`, `ui/fonts`: they are the game's assets and are never committed to the engine repository.

## What is written

| Path | What |
|---|---|
| `ui/screens/<name>.rml` | one document per screen: the canvas, layout slots (`u-slot`), blocks, controls |
| `ui/ugui/ugui.rcss`, `sprites.rcss` | base styles (`div` block, canvas, default font); one `@spritesheet` per texture (sprite rects, 9-slice centres) |
| `ui/ugui/runtime.lua`, `config.lua` | CanvasScaler, safe area, fonts, click routing (regenerated on every import) |
| `ui/ugui/screens/<name>.json` | slots, blocks, controls, logic components and keys of a screen: the map for writing Lua |
| `ui/ugui/report.{json,md}` | per screen: unsupported / approximate items with examples, logic components, keys |
| `ui/ugui/l10n-keys.json` | Unity table / key / id -> engine key; languages |
| `ui/logic/<name>.lua` | logic stub (written once, never overwritten): `M.mount(document)`, `M.on[id] = fn(element, event)` |
| `lang/<tag>.json` | string tables; `game.json` gets missing `languages` and `sourceLanguage` (JSON without comments only) |

Report-only files are added to `.rpakignore`. Screen names: prefab name without `Screen`, lower case
(`FindMatchScreen` -> `findmatch`); a duplicate gets its window folder as prefix (`hud-inventory`).

## Screens

- **UI.Windows**: every prefab whose window component has `layouts.items` is a screen. Each layout prefab is
  converted; a `WindowLayoutElement` becomes a slot `<div u-slot="<name>">` and the component prefab assigned to
  its `tagId` (`components[].component`: guid, `directRef` or `address`) is converted inside it. Components mapped
  to a tag that has no slot are reported. `layoutPreferences.canvasScalerData` overrides the layout's CanvasScaler
  (as UI.Windows does at run time). `WindowLayoutSafeZone` -> `.u-safe` (runtime insets it by `game.safeArea()`).
  `ListComponent` sources (external prefab) are converted once as a hidden row template
  (`<div class="u-template" id="<list>-row">` inside `customRoot`; the list has `u-template`).
- **Plain uGUI**: `--prefab Path.prefab` converts a Canvas prefab as a whole screen.
- Every GameObject is a `<div id>` (ids are `<slot>-<object>`, unique per document; `u-src` = object name).
  Inactive objects: `display: none` with `u-display` (what `require('ugui.runtime').show(el, true)` restores).
- WindowComponent subclasses keep their class in `u-component`; game-specific ones are listed as *logic
  components* (behaviour to rewrite in `ui/logic/<screen>.lua`), modules in `u-modules`,
  `TextComponentFormatModule.format` in `u-format`.

## Mapping

| Unity | Engine |
|---|---|
| RectTransform anchors / pivot / offsets | absolute box: `left/right/top/bottom` in % of the parent + `margin-*` in dp; point anchors get `width/height`. Rotation (z) and scale -> `transform` around the pivot |
| Canvas + CanvasScaler (Scale With Screen Size) | `#ugui-canvas` with the reference size in dp; `runtime.lua` scales it like Unity (match width/height, expand, shrink) on load and on `resize` |
| Image Simple / Sliced / Tiled / Filled | `decorator: image(sprite fill|contain)` / `ninepatch(sprite, sprite-in, edges)` (edges = border x 100 / sprite PPU / pixelsPerUnitMultiplier) / `image(sprite repeat)` / whole sprite + `u-fill*` (`runtime.fill(el, v)` for horizontal / vertical) ; colour -> `image-color`; no sprite -> `background-color`; Unity built-in sprites -> `background-color` + `border-radius` |
| Sprite sheets / atlases | `@spritesheet` of the source texture with the sprite rect (Unity bottom-left -> top-left); multi-sprite textures by `internalID` / name table |
| RawImage | `image(../img/file fill)`; render textures -> `u-texture="runtime"` |
| TextMeshPro / Text | flex box (vertical alignment) + `.u-text` block: size, colour, alignment, bold/italic, upper/lower case, wrapping, overflow clip, line spacing, margins; TMP material outline / underlay and uGUI Outline / Shadow -> `font-effect`; rich text `<b> <i> <color> <size> <br> <uppercase>` -> spans |
| TMP font asset | its source TTF/OTF copied to `ui/fonts`, loaded by `runtime.lua` (`rmlui:LoadFontFace`), family = the font's name table (as engine/ui/UiFont.cpp reads it); a missing source font falls back to a project font of the same family |
| Localized text (`TextLocalizationComponentModule`, `LocalizeStringEvent`) | `@<Collection>.<Key>` text node (+ `u-key`) |
| Button / Toggle | `u-kind`, `onclick` -> `runtime.click` -> `ui/logic/<screen>.lua`; ColorTint pressed colour -> `:active` rule; toggle `u-on` + checkmark `u-check-id` |
| Slider / Scrollbar | `u-kind`, `u-min/max/value/direction`, fill id; the authored fill rect is kept |
| ScrollRect + Mask / RectMask2D | viewport `overflow-x/y: auto`, content in flow (`position: relative`); masks -> `overflow: hidden` |
| Horizontal / Vertical / Grid LayoutGroup, LayoutElement, ContentSizeFitter, AspectRatioFitter | flexbox (direction, padding, spacing, child alignment, control size / force expand, preferred / flexible sizes); grid -> wrap with cell size; fitters -> auto size / aspect |
| CanvasGroup | `opacity` |

Not converted, reported per screen: show/hide animation parameters (counted), Animator / Animation, mesh effects
(`BaseMeshEffect` subclasses: gradients, shiny, flip), particles in UI, radial fills, custom UI materials,
EventTrigger callbacks, dropdowns, Shadow / Outline on images, nested canvas sorting, 3D tilts, game scripts that
are not WindowComponents. Rich-text tags without an RML equivalent are removed and named.

## Localization

Shared table data (keys, ids), string tables per locale, Locale assets and Localization Settings
(project locale, `SpecificLocaleSelector`) are read. Keys become `<Collection>.<Key>` (`|`, `/`, spaces -> `.`;
other characters outside `[A-Za-z0-9._-]` -> `_`; `--key-prefix` prepends). Values:

- `{0}` -> `{arg0}` (Lua: `game.tr('Main.Level', {arg0 = 5})`); named placeholders stay; number formats
  (`{0:N0}`) are dropped and reported — format the value before passing it;
- Smart String `{n:plural:a|b}` / `{n:p:...}` -> an engine plural object; the forms map by count (2: one/other,
  3: one/few/many, ...); the form follows `count` or the first numeric argument (engine rule). `cond` / `choose`
  keep the first branch, other formatters keep the placeholder; both are reported;
- rich text -> RML spans (RmlUi parses translated text that contains markup);
- empty translations are left out, so the engine falls back to the source language.

The source language is the startup `SpecificLocaleSelector`, else the project locale.

## Verification (2026-11-12)

- `test_import.py`: 17 tests (rect maths, rich text, smart strings, font name, a full UI.Windows screen with
  slots / 9-slice / sprite sheet / list template / scroll / toggle, report, localization incl. Russian plurals,
  re-import keeps logic stubs, deterministic output). With `AIVERSE_UI_IMPORT_CLIENT` the screen is rendered by
  the real client (passes on Linux, Xvfb + Mesa llvmpipe).
- Dry run on Drone Fall (Ext-Client): 29 UI.Windows screens, 10 string collections (en, ru) into
  `build/.temp/ext-ui`; client screenshots in `build/.temp/ext-ui-shots` (main, find match, battle HUD).
  The previews use the same client flags as MCP `ui_preview` (`--ui-screen`, `--size`, `AIVERSE_UI_SAFE_AREA`).
- Not verified: macOS / iOS / Android / web clients (no engine code changed; RmlUi documents and Lua only).
