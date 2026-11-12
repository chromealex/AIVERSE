# “制作游戏”向导

一个窗口（项目中心 → **制作游戏**，或 **项目 → 制作游戏…**）或一次 MCP 调用（`game_make`）即可生成一个带 `.rtsproj` 的完整游戏项目：
按想法选出的模板游戏、用户的美术 / 模型 / 音乐 / 音效、强制新手教程地图、对局中的轻提示、生成的关卡、元游戏（菜单、升级、商店、
每日奖励、个人资料、设置、结算）、按需添加的内购和广告、多语言、品牌形象，以及用机器人进行的检查。它写出的所有内容都是普通的游戏数据，
之后可以随意修改。

代码：`apps/editor/core/GameWizard*.cpp`（无 UI）、`apps/editor/core/WizardOps.cpp`（MCP）、`apps/editor/EditorGameWizard.cpp`
（项目中心页面）。复制到每个游戏中的运行时套件：`templates/_wizard/`（Lua + RmlUi，见其 README）。

## 回答（`WizardSpec`，全部可选）

| 分组 | 键 |
|---|---|
| 内容 | `artDir`（文件夹：png/jpg/tga、glb/gltf/obj/fbx、wav/ogg/mp3/flac、ttf/otf、md/txt），`artUrl`（单个文件或 .zip / .tar.gz；用系统 curl 下载到 `~/.aiverse/wizard/<时间>`） |
| 想法 | `idea`（任意语言；文件夹里的 .md/.txt 也会被读取），`genre`（模板 id 或 `auto`），`features`（economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait scifi modern） |
| 项目 | `title`、`location`、`studio`（bundle id `com.<studio>.<slug>`）、`version`、`orientation`、`languages`、`platforms`、`seed` |
| 教程 | `tutorial`（`both` / `hard` / `soft` / `none`）、`tutorialForced`、`tutorialSkippable`、`tutorialDim`、`hints` |
| 关卡 | `levels`（0..40）、`mapPreset`（`auto`，`none` = 复制基础地图，或生成器预设）、`mapSize`、`bossEvery`、`difficultyCurve`、`startResourcesPercent`、`keepTemplateMaps`、`verifyLevels` |
| 元游戏 | `metaIdea`（自由文本：元游戏应包含什么；会写入智能体的任务）、`currencies`（soft、hard）、`upgrades`、`shop`、`daily`、`profileScreen`、`xpPerLevel` |
| 变现 | `ads`、`interstitialEvery`、`purchases`、`stores` |
| 外观 | `brandingFromArt`、`paletteFromArt` |

`game_make_options` 列出可选项，`game_make_plan` 显示将会生成什么（不写文件）。相同的回答 + seed = 相同的游戏。

## 它如何决定

- **模板**：将想法中的词与每个模板的关键词（词干，ru / en）比对，期望的特性与其机制比对，屏幕方向与其画面比对；打平时由 seed 决定。
  什么都没给：从均衡的模板中选一个。
- **内容**：模型和 2D 精灵分配给文件名中含有其 id / 名称（ru / en）的类型；其余文件被复制并列在 `WIZARD.md` 中。主视觉图（按名称和尺寸）
  成为启动画面、菜单背景和界面强调色；最接近正方形的图片（或 “icon” / “logo”）成为 1024 图标。音乐按名称进入 `menu` / `match` /
  `battle` / `victory` / `defeat`，短音效绑定到事件（`click`、`select`、`move`、`died`、`underAttack` 等）。第一个 TrueType 字体成为
  `fonts/title.ttf`。
- **强制教程**（`maps/wiz_tutorial.json` + `scripts/wizard/tutorial_steps.lua`），根据规则自动编写：
  - *经济*（工人、侧边栏船坞、可展开的 MCV、TA 建造者）：展开、采集、按需建造跃迁场 / 补给、兵营类生产建筑的前置链、训练两个单位、
    摧毁一座不会还击的敌方建筑；
  - *驻军*（Mushroom Wars、State.io）：派兵进入中立建筑，再占两座，再占一座敌方建筑；
  - *自动建造*（兵线对战）：按两次“建造”，升级，坚守；
  - *通用*：移动军队，进攻。
  只放行当前步骤的指令（`before_command`），敌人会等待（但其单位仍会防守），面板和箭头由视图脚本绘制，步骤所需资源会被补足，避免卡住。
  不适用于 Knights and Merchants 式的道路经济（那里使用通用形式）。
- **轻提示**：来自 `scripts/wizard/hints_config.lua` 的一次性提示（镜头、空闲工人、资源不足、人口上限、军队、驻军、遭到攻击）；看过的提示
  会记在个人资料中，可在设置中关闭。
- **关卡**：`maps/wiz_level_N.json` = 基础地图 + `generate`（模板的预设、每关一个 seed、以基础地图为样板）；难度按曲线在 0..2 之间，
  每 N 关一个 Boss；奖励、首胜奖励和星级时间写在 `ui/wizard.json` 中。
- **元游戏**：`ui/wizard.json`（配置）、`ui/profile.json`、`ui/shop.json`、`ui/screens.json`（`start: home`、`afterMatch: result`），
  界面来自套件。升级以 `initialVars` `wiz_up_<id>` 的形式进入对局，由 `scripts/wizard/meta_bonus.lua` 转换为增益 / 初始资源。
- **变现**：`game.json` 中按平台写入 `plugins` / `store` / `ads`（Yandex 演示广告位和桌面模拟：请替换 id）。

## 智能体步骤：一比一还原描述的游戏

模板只是起点，不是最终的游戏。开启 `agent`（默认开启）时，向导会在项目中写入 `WIZARD_TASK.md` —— 原文照录的描述 / GDD，以及如何让向导
生成的各部分保持一致（教程步骤、提示、元游戏配置、语言文件）—— 并交给已连接的 AI 智能体。智能体通过编辑器的 MCP 工具（在编辑器中实时）
重建项目：类型、属性、阵营、机制（规则或 Lua）、地图、胜利条件、文本；随后用 `rules_lint`、`map_validate`、`match_run` 检查，并在
WIZARD.md 中写入 “## Агент: что сделано”。

- 连接：向导显示与 Agent 窗口相同的“连接 AI 智能体”面板（`EditorMcpConnect.*`、`core/McpInstall.h`）：点一下就把 aiversemcp 写入
  客户端配置，同一个连接两边共用。
- 从编辑器启动（`core/WizardAgent.*`，通过用户的登录 shell 运行以便找到 CLI）：Claude Code
  `claude -p --output-format stream-json --allowedTools mcp__aiverse` 和 Codex CLI `codex exec --full-auto`，任务通过 stdin 传入，
  工作目录为项目文件夹。CLI 会在 PATH、常见安装目录、用户的 shell 以及桌面应用内部查找（Claude Code 位于
  `~/Library/Application Support/Claude/claude-code/<version>/…/claude.app`，Codex 位于 `ChatGPT.app/Contents/Resources/codex-cli`）。
  进度显示在活动动态（Agent 窗口）、状态栏和向导页面（停止按钮）；完整日志在 `logs/wizard-agent-<时间>.log`。
- Cursor、Windsurf、Claude Desktop：“复制智能体任务”会把任务放到剪贴板，粘贴到客户端即可。
- 编辑器中不需要 API 密钥：智能体在用户自己的客户端和账号下运行。
- 听写：想法输入框（“想法”页）和元游戏想法输入框（“元游戏”页）下方的“语音输入” —— macOS 语音识别（`apps/editor/EditorSpeech_mac.mm`：
  Speech + AVFoundation，以游戏的第一语言作为区域设置；系统会询问一次麦克风和语音识别权限）。文本会追加到输入框中。其他平台上该按钮不可用。
- 规格中的 `ai`（按语言的文本：标语、简介、类型、资源、货币、升级、关卡）让自行调用 `game_make` 的智能体可以传入游戏的名称和文本。

编辑器 Game 视图中的教程：编辑器没有显示步骤面板的游戏 UI，因此视图脚本发送 `wiz_tut_off`，教程地图在那里按普通对局运行。面板、箭头和
指令过滤在游戏客户端（AiverseGame、构建版本）中生效。编辑器打开的是第一关，而不是教程。

## 检查

开启 `verifyLevels` 时，向导会自己通关教程（`initialVars {wiz_tutorial_auto: 1}`：每一步的 `auto` 指令与玩家一样经过同一过滤器），并用
机器人抽样跑若干关卡，报告加载错误、脚本错误和未完成的教程。已在所有模板上跑过：除 ambersands（只能在岩石上建造）、planets 和 underlord
（在特殊地形上移动 / 挖掘）外全部能完成 —— 向导会在警告中说明。

测试：`game_wizard_plan_picks_a_template_by_the_idea`、`game_wizard_makes_a_playable_project`、
`script_command_card_false_hides_the_button`。

## 引擎改动

脚本指令支持 `"card": false`（没有按钮 / 快捷键；由视图脚本、HUD 文档、MCP 发送）—— 用于教程的“下一步 / 跳过”
（`modules/rts/RtsScriptCommands.cpp`，客户端和编辑器的指令卡）。
