# Script HUD bindings

`game.json` may provide a `scriptHud` array of panels. The client command card and
editor region-mode game preview render the same read-only bindings. Existing
projects without this block are unchanged. All strings are localization keys.

```json
{
  "scriptHudOptions": {
    "choices": [{"label": "upgrade.title", "description": "upgrade.description"}]
  },
  "scriptHud": [{
    "title": "upgrade.choose",
    "visibleVar": "offer_open_{player}",
    "items": [{
      "optionVar": "offer_slot_1_{player}", "options": "choices",
      "ability": "buy_slot_1", "enabledVar": "can_buy_{player}"
    }, {
      "label": "upgrade.price", "args": {"price": "offer_price_{player}"}
    }]
  }]
}
```

- Variables are `game.set_var` / `GameState::trig.vars`, already hashed and replayed.
- `{player}` expands to the viewing player's slot. Missing variables read as zero.
- `visibleVar` and `enabledVar` are true above zero; omitted means true.
- `optionVar` is a one-based index in `scriptHudOptions[options]`. Invalid or missing
  options hide the row. Options provide only label and description, never commands.
- `args` maps localized placeholders to integer values of simulation variables.
- Without `ability`, a row displays a value; with one, it sends the existing
  `kCmdPlayerAbility` command for that rules ability. These bindings currently support
  instant, untargeted actions and `target: point` actions. Point actions arm the
  cursor; the next map click emits the command with its world coordinates.
  The editor displays a localized target hint; right-click or Escape cancels.
  Other targeting modes retain the ordinary skill UI.
- Bound abilities are omitted from the client's ordinary skills toolbar to prevent
  duplicate buttons. Affordability and effect validation remain in the simulation.
- The evaluator never mutates state, consumes randomness, or advances time.

Implementation: `modules/rts/view/ScriptHud.h`, client command card and editor
`GameWindow`. Test: `script_hud_player_binding_and_invalid_option`.
macOS editor/client compiled; Android, Windows and web execution remain unverified.

## Card and support layouts

Optional panel `layout` selects `cards` (centered choice overlay), `status`
(compact top-left counters/actions), or `support` (bottom-right action).
Omitting it retains the existing command list. Card overlays intercept map clicks;
the simulation continues running. Closing an offer remains a scripted action.

Items/options accept a project-relative `image` and localized `badge`.
An action with `cooldownVar` and `durationVar` displays remaining whole seconds
on the button and a clamped ready fraction; zero duration is safe. Both fields
support `{player}`. The client exposes `scriptCards`, `scriptStatus`,
`scriptSupport`, `scriptTitle` to RmlUi; `UiAction.image` is relative to its UI
document for assets under `ui/`. Game documents opt into rendering these arrays.
The editor mirrors these layouts in `apps/editor/ScriptHudDraw.h`.

Verification: `script_hud_card_layout_and_cooldown` covers panel ordering,
images, timer text, zero-duration progress and presentation-only evaluation.

## Card frames

Items/options may supply `frameImage` (project-relative atlas), `frameColumns`
(horizontal equal-width cells, default 1), `frameIndex` (zero-based, clamped),
and `frameSprite` (sprite name declared in the game's RCSS `@spritesheet`).
The editor samples the atlas directly. The client exposes `UiAction.frame` as
an `image(sprite fill)` decorator for `data-style-decorator`. No frame is
required; existing unframed cards continue to work. Frame artwork should place
its title plaque around 54% and description panel below 61% of its height.

## Top status strip

Panel layout `top` binds read-only or action rows to `scriptTop` in the client and editor RmlUi HUD. The game document chooses its position and styling; labels, integer args and visibleVar use the existing evaluator. Existing layouts are unchanged.

## Resource progress

An item may bind `progressVar` to a script variable and optionally `progressMaxVar` to its denominator (default 1). Presentation clamps the ratio to 0..1, preserving fractional Fixed values. Missing/non-positive denominators produce zero. This binding overrides cooldown progress when both are present, without changing cooldown text. It reads simulation state only.
