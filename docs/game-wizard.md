# Game wizard («Сделать игру»)

One window (project hub → **Сделать игру**, or **Проект → Сделать игру…**) or one MCP call (`game_make`) makes a whole game
project with its `.rtsproj`: a template's game picked by the idea, the user's art / models / music / sounds, a hard
tutorial map, soft hints in matches, generated levels, the meta game (menus, upgrades, shop, daily reward, profile,
settings, result), purchases and ads when asked, languages, branding, and a check with bots. Everything it writes is
ordinary game data that the game can edit afterwards.

Code: `apps/editor/core/GameWizard*.cpp` (no UI), `apps/editor/core/WizardOps.cpp` (MCP), `apps/editor/EditorGameWizard.cpp`
(the hub page). Runtime kit copied into every game: `templates/_wizard/` (Lua + RmlUi, see its README).

## Answers (`WizardSpec`, all optional)

| Group | Keys |
|---|---|
| Content | `artDir` (folder: png/jpg/tga, glb/gltf/obj/fbx, wav/ogg/mp3/flac, ttf/otf, md/txt), `artUrl` (a file or a .zip / .tar.gz; downloaded with the system curl into `~/.rtsengine/wizard/<time>`) |
| Idea | `idea` (any language; .md/.txt in the folder are read too), `genre` (template id or `auto`), `features` (economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait scifi modern) |
| Project | `title`, `location`, `studio` (bundle id `com.<studio>.<slug>`), `version`, `orientation`, `languages`, `platforms`, `seed` |
| Tutorial | `tutorial` (`both` / `hard` / `soft` / `none`), `tutorialForced`, `tutorialSkippable`, `tutorialDim`, `hints` |
| Levels | `levels` (0..40), `mapPreset` (`auto`, `none` = copies of the base map, or a generator preset), `mapSize`, `bossEvery`, `difficultyCurve`, `startResourcesPercent`, `keepTemplateMaps`, `verifyLevels` |
| Meta | `currencies` (soft, hard), `upgrades`, `shop`, `daily`, `profileScreen`, `xpPerLevel` |
| Money | `ads`, `interstitialEvery`, `purchases`, `stores` |
| Look | `brandingFromArt`, `paletteFromArt` |

`game_make_options` lists the choices, `game_make_plan` shows what would be made (no files). Same answers + seed = same game.

## How it decides

- **Template**: words of the idea are matched against each template's keywords (stems, ru / en), wanted features against
  its mechanics, the orientation against its screen; ties go by the seed. Nothing given: one of the well-rounded templates.
- **Content**: models and 2D sprites go on the types whose id / name (ru / en) their file name carries; the rest is copied
  and listed in `WIZARD.md`. The key art (by name and size) becomes the splash, the menu backdrop and the UI accent colour;
  the most square image (or "icon" / "logo") the 1024 icon. Music by name into `menu` / `match` / `battle` / `victory` /
  `defeat`, short sounds onto events (`click`, `select`, `move`, `died`, `underAttack`, ...). The first TrueType font becomes
  `fonts/title.ttf`.
- **Hard tutorial** (`maps/wiz_tutorial.json` + `scripts/wizard/tutorial_steps.lua`), written from the rules:
  - *economy* (workers, sidebar yards, deployable MCVs, TA builders): deploy, gather, warp field / supply when needed, the
    requirement chain of a barracks-like producer, train two units, raze an enemy building that doesn't shoot back;
  - *garrison* (Mushroom Wars, State.io): send to a neutral building, take two more, take an enemy one;
  - *autobuild* (lane battlers): press Build twice, upgrade, hold;
  - *generic*: move the army, attack.
  Only the step's commands pass (`before_command`), the enemy waits (its units still defend), the panel and the arrow
  are drawn by the view script, the step's resources are topped up so it can't get stuck. Not for Knights and Merchants
  style road economies (generic shape there).
- **Soft tutorial**: one-time hints (camera, idle workers, not enough resources, supply cap, army, garrisons, under
  attack) from `scripts/wizard/hints_config.lua`; seen hints are remembered in the profile, the settings turn them off.
- **Levels**: `maps/wiz_level_N.json` = the base map + `generate` (the template's preset, a seed per level, the base map
  as the template); difficulty 0..2 by the curve, every N-th a boss; rewards, first-win rewards and star times in
  `ui/wizard.json`.
- **Meta**: `ui/wizard.json` (config), `ui/profile.json`, `ui/shop.json`, `ui/screens.json` (`start: home`,
  `afterMatch: result`), screens from the kit. Upgrades reach the match as `initialVars` `wiz_up_<id>` and
  `scripts/wizard/meta_bonus.lua` turns them into buffs / start resources.
- **Money**: `game.json` `plugins` / `store` / `ads` by platform (Yandex demo ad units and desktop mocks: replace the ids).

## Agent step: the described game, 1 to 1

The template is a starting point, not the game. With `agent` on (default) the wizard writes `WIZARD_TASK.md` into the
project — the description / GDD verbatim plus how to keep the wizard's parts consistent (tutorial steps, hints, meta config,
language files) — and hands it to a connected AI agent, which rebuilds the project through the editor's MCP tools (live in
the editor): types, stats, factions, mechanics (rules or Lua), maps, victory, texts; then checks with `rules_lint`,
`map_validate`, `match_run` and writes «## Агент: что сделано» into WIZARD.md.

- Connection: the wizard shows the same "connect an AI agent" panel as the Agent window (`EditorMcpConnect.*`,
  `core/McpInstall.h`): one click writes rtsmcp into the client's config, and that one connection serves both.
- Launched from the editor (`core/WizardAgent.*`, through the user's login shell so the CLI is found):
  Claude Code `claude -p --output-format stream-json --allowedTools mcp__rtsengine` and Codex CLI `codex exec --full-auto`,
  the task on stdin, the project folder as the working directory. The CLI is found in PATH, the usual install folders, the user's shell, and inside the desktop apps
  (Claude Code in `~/Library/Application Support/Claude/claude-code/<version>/…/claude.app`, Codex in `ChatGPT.app/Contents/Resources/codex-cli`). Progress goes to the activity feed (Agent window), the
  status bar and the wizard page (Stop button); the full log to `logs/wizard-agent-<time>.log`.
- Cursor, Windsurf, Claude Desktop: «Скопировать задание агенту» puts the task on the clipboard to paste into the client.
- No API key in the editor: the agent runs under the user's own client and account.
- `ai` in the spec (texts per language: tagline, intro, types, resources, currencies, upgrades, levels) lets an agent that
  calls `game_make` itself pass the game's names and texts.

The tutorial in the editor's Game view: the editor has no game UI to show the step panel, so the view script sends
`wiz_tut_off` and the tutorial map plays as an ordinary match there. The panel, the arrow and the gate work in the game
client (RTSGame, builds). The editor opens the first level, not the tutorial.

## Check

With `verifyLevels` the wizard plays the tutorial by itself (`initialVars {wiz_tutorial_auto: 1}`: each step's `auto` order
goes through the same gate as a player) and a sample of levels with bots, and reports load errors, script errors and a
tutorial that didn't finish. Swept over every template: all finish except ambersands (rock-only placement), planets and
underlord (moves / digging on special terrain) — the wizard says so in its warnings.

Tests: `game_wizard_plan_picks_a_template_by_the_idea`, `game_wizard_makes_a_playable_project`,
`script_command_card_false_hides_the_button`.

## Engine change

Script commands take `"card": false` (no button / hotkey; sent by view scripts, HUD documents, MCP) — the tutorial's
Next / Skip (`modules/rts/RtsScriptCommands.cpp`, client and editor command cards).
