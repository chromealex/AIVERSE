# Prompts (choice menus from scripts)

`"prompts": true` in rules.json. Code: `modules/rts/RtsPrompt.{h,cpp}`, test `prompts_show_pick_and_interact`.

A simulation script shows one player a small menu: a talk with a passer-by, an offer, a question.

- `prompt.show(player, id, title, {lines}, {options})` opens it (up to 8 lines, 1..9 options, the id without `:`).
  It replaces the one that was open. `prompt.close(player)`, `prompt.get(player)` (also in view scripts).
- The client shows the title, the lines and the numbered options; a number key sends `kCmdPrompt` with the option
  (Lua: `game.command(p, {type = "prompt", param = index - 1})`). The prompt closes and the game's global
  `on_prompt(player, "<id>:<option>")` runs (option from 1); it may open the next prompt.
- The interact key (**E**) with no prompt open and no door menu sends `kCmdPrompt` with `-1`: the global
  `on_interact(player, "")` runs and decides what is in front of the hero.

Open prompts are part of the checksum and of saves. Touch buttons for the options are not done.
