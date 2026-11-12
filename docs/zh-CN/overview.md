# AIVERSE — 完整概览

引擎各部分的参考：结构、构建、格式、子系统、平台和验证状态。
简短介绍（引擎为何而生、解决什么问题）见 [README](../../README.zh-CN.md)。修改引擎本身的智能体须遵守的规则见 [AGENTS.md](../../AGENTS.md)。

## 结构

```
engine/
  core/      Fixed (Q47.16), Vec3, Random (PCG32), Hash, ECS
  data/      JSON without floats (numbers go straight to fixed-point)
  sim/       deterministic simulation, commands, replays
  net/authority/ authoritative network sessions for FPS
  world/     spatial structures and chunked world streaming
  gfx/       RHI + Metal and GL (ES3/WebGL2/GL3.3) backends, render to texture
  render/    3D rendering: lighting, shadows (shadow map + PCF), fog, tone mapping; Dear ImGui over the RHI;
             glTF/OBJ models, PBR materials, skeletal and procedural animation
  platform/  window/input/loop: macos, ios, android, web, windows, linux
  assets/    shared engine assets (Noto Sans font with Cyrillic, OFL)
modules/rts/ generic RTS layer: types from data, orders, A*, combat, economy, construction,
             production, garrisons (Mushroom Wars), roads, fog of war, AI
             view/ — draws any type from the "view" block in rules.json
modules/fps/ FPS module (in development): movement, combat and match state
apps/
  fps/       standalone FPS client and server
  client/    the game (menu, match with bots, HUD, minimap, mouse and touch)
  editor/    map editor (terrain, objects, roads, players, validation, bot test, Build menu)
  headless/  bot matches and replay checks without graphics
  modelview/ viewer for models, clips, materials and every type of a game
tools/mcp/      MCP server through which AI agents work with the editor
tools/aiversebuild/  packager: exe / app / Xcode project / Gradle project / web
templates/
  skirmish/  "Iron and Gold" — a Warcraft/Red Alert-style sample
  mushrooms/ "Mushroom Clash" — a Mushroom Wars-style sample
  showcase/  Iron and Gold rules with imported models (glTF, OBJ, animations)
platform/    packaging templates (Info.plist, Gradle project, HTML shell)
```

## 快速开始（macOS）

```bash
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release      # requires Xcode and CMake
cmake --build build/mac -j 8
open build/mac/AiverseEditor.app                              # editor (project templates/skirmish)
open build/mac/AiverseGame.app                                # game
./build/mac/aiverse_tests
```

在编辑器中打开另一个游戏：`AiverseEditor.app/Contents/MacOS/AiverseEditor --project templates/mushrooms`。
要让游戏以另一套数据启动，请传入 `-DAIVERSE_CONTENT_DIR=.../templates/mushrooms`。

### 同时运行多个编辑器

每个编辑器进程都可以用 `--project <文件夹>` 打开自己的项目。不带 `--listen` 时，第一个实例占用 MCP 端口 `47321`；端口被占用时，
系统会分配一个空闲端口。实际端口显示在状态栏中；**Agent** 窗口会把它写入连接配置（`aiversemcp --port <端口>`）。自动分配的端口在编辑器
重启后可能变化。`--listen <端口>` 固定端口（冲突时 MCP 不启动），`--listen 0` 关闭 MCP。已连接的 MCP 客户端不会自行在窗口之间切换。

## 多平台构建 —— aiversebuild

```bash
./build/mac/aiversebuild --project templates/skirmish --target macos     # Builds/macos/<Name>.app (ad-hoc signed)
./build/mac/aiversebuild --project templates/skirmish --target ios       # Builds/ios/xcode/AIVERSE.xcodeproj + simulator build
./build/mac/aiversebuild --project templates/skirmish --target android   # Builds/android/<Name>-release.apk when SDK/NDK and gradle are present
./build/mac/aiversebuild --project templates/skirmish --target web       # Builds/web (requires emsdk)
./build/mac/aiversebuild --project templates/skirmish --target windows   # .exe via mingw-w64 (brew install mingw-w64) or sources + build.bat
./build/mac/aiversebuild --project templates/skirmish --target all
```

输出位于 `<项目>/Builds/<平台>/` —— 只有完成的构建和体积报告。所有中间产物（CMake 树、资源包、重新编码的音频、Gradle 项目、xcodebuild
数据、编辑器的体积估算）都放在隐藏的 `Builds/.temp/` 中，随时可以删除。使用 `--generate-only` 时，供 Android Studio 使用的 Gradle 项目
放在 `Builds/android/<Name>`。旧的可见目录 `_build`、`_pack`、`_audio`、`_estimate` 会由打包工具自行清理。

编辑器中也有同样的功能：**构建**菜单。生成的 iOS 和 Android 项目是自包含的（其中复制了引擎源码和内容），可以直接交给他人。在 iOS 真机上运行：
用 Xcode 打开项目并在 Signing 中选择团队（或使用 `--team <TeamID>`，或在 game.json 中写 `"appleTeamId"`）。

标识符、名称、版本、屏幕方向和图标（`icon.png`，1024×1024；缺失时自动生成）都在 `game.json` 中。

### 构建工具：检查与自动安装

构建前，`aiversebuild` 会检查平台所需的工具，并且不只在 `PATH` 中查找（从 Finder 启动的编辑器只能看到 `/usr/bin:/bin:/usr/sbin:/sbin`）：
Homebrew（`/opt/homebrew/bin`、`/usr/local/bin`）、`~/.local/bin`、`~/Library/Python/*/bin`、`~/emsdk`、`$EMSDK`、`ANDROID_HOME` /
`ANDROID_SDK_ROOT`、`~/Library/Android/sdk`、`~/Android/Sdk`、`%LOCALAPPDATA%\Android\Sdk`、`JAVA_HOME`、`/usr/libexec/java_home`、
Visual Studio（vswhere）以及它自己的 `~/.aiverse/toolchains` 文件夹。找到的目录会加入子进程的 `PATH`。

| 平台 | 需要 | 自动安装 |
|---|---|---|
| web | CMake、Python 3、Emscripten（emsdk），Ninja 可选 | emsdk（下载约 400 MB，占用磁盘约 1.5 GB），CMake + Ninja（约 50 MB） |
| android | JDK 17–23、Android SDK（platform 35、build-tools、platform-tools）、NDK、SDK 自带的 CMake、Gradle | 全部：Temurin JDK 17、command line tools + `sdkmanager`（自动接受许可）、NDK 约 700 MB、Gradle 8.10.2 |
| macos | Xcode Command Line Tools、CMake | CLT —— 通过 Apple 安装程序（`xcode-select --install`），CMake |
| ios | Xcode（App Store）、CMake | CMake |
| windows（在 Windows 上） | Visual Studio 2022 / Build Tools（C++）、CMake | CMake；VS —— 给出 `winget` 命令提示 |
| linux | C++ 编译器、CMake | CMake |

```bash
aiversebuild --check-toolchain web            # what is present and what is missing (--json for the editor and MCP)
aiversebuild --install-toolchain web          # install what is missing (or one id: emsdk, cmake, jdk, android-ndk, gradle, ...)
aiversebuild --project ... --target web --install-missing   # install and build right away
```

不带 `--install-missing` 时，缺少工具会使构建以退出码 3 中止，并输出一行 `AIVERSE_MISSING_TOOLCHAIN web: emsdk`（编辑器能识别）。
在编辑器中，**构建**窗口会为所选平台显示找到了什么（路径、版本）、缺少什么，并提供 **自动安装** 按钮和“自动安装缺少的工具”复选框；进度显示在
**控制台**中（文本可以选择和复制；“打开日志文件夹”会打开 `<引擎>/logs`）。智能体通过 MCP 做同样的事：`toolchain_check`、
`toolchain_install`、`build_start {"installMissing": true}`。

所有内容都安装到 `~/.aiverse/toolchains`（Windows：`%USERPROFILE%\.aiverse\toolchains`；其他位置用变量 `AIVERSE_TOOLCHAINS_DIR` 指定），
不需要 `sudo`，也不修改系统：`emsdk/`、`py/`（含 cmake 和 ninja 的 venv）、`cmake/`、`ninja/`、`jdk-17/`、`android-sdk/`、
`gradle-8.10.2/`。重复运行是安全的（已就绪的部分会跳过）。删除该文件夹即可移除所有安装内容。例外：Command Line Tools 由 Apple 安装程序
安装；如果找到了用户自己的 Android SDK，`sdkmanager` 会把缺少的包装进去。

### 资源包（.rpak）与构建体积

游戏内容（游戏文件夹 + `engine/assets`）以单个文件 `content/content.rpak` 进入构建：每个文件单独用 zstd 压缩，相同文件只存一份，已压缩的
格式（png、ogg、mp3 等）原样存放。引擎会自动挂载 `content.rpak`；资源包中的文件优先于文件夹中的文件，因此没有资源包时（开发阶段）一切照旧。
`platform::mountPack("patch.rpak")` 可在上面再挂载一个包（补丁、DLC）。每次构建都会打印体积报告并写入 `dist/<平台>/size-report.txt`。

```bash
aiversebuild ... --no-pack          # ship folders as is (debugging)
aiversebuild ... --pack-level 9     # zstd level (default 19 for Release, 3 for Debug)

assetpipe pack -o content.rpak templates/skirmish engine/assets=engine   # build a pack by hand
assetpipe list content.rpak       # files, sizes, compression
assetpipe verify content.rpak     # unpack everything and check hashes
assetpipe extract content.rpak out/
assetpipe diff old.rpak new.rpak  # what changed between versions (for patches)
```

构建不需要的内容列在游戏文件夹的 `.rpakignore` 中（通配符 `*`、`?`；`文件夹/` 仅匹配目录）。隐藏文件（`.git`、`.DS_Store`）、
`*.rpl` 录像和嵌套的 `*.rpak` 总是会被跳过。资源包构建是可复现的：相同内容会生成逐字节相同的文件；资源包的“内容哈希”标识这套数据。

### 分包构建与内容下载

可以选择对构建进行分包：应用只携带启动所需的基础包，其余内容放进指定大小的分片（`content.001.rpkc`……），由游戏从任意来源下载。
一切由游戏作者决定，可写在 `game.json` 中（通用设置和各平台设置），也可用 `aiversebuild` 参数指定：

```json
"packaging": {
  "split": true,                 // whether to split
  "chunkSize": "8MB",            // size of one part
  "baseSize": "40MB",            // limit for what ships inside the app
  "totalSize": "1GB",            // limit for all content
  "fillBase": false,             // true: fill the base up to baseSize (less to download)
  "base": ["maps/intro.json"],   // what must be in the base (besides the default: game.json, rules, lang, ui, atlases, first map)
  "order": ["maps/scena00*"],    // download order
  "sources": ["https://cdn.example.com/mygame/{hash}/", "file:///Volumes/DLC/"],   // mirrors in order
  "autoDownload": true,          // download everything in the background right after start
  "targets": {"web": {"baseSize": "15MB"}, "windows": {"split": false}}
}
```

```bash
aiversebuild --project templates/x --target web --split --chunk-size 4MB --base-size 15MB   # flags override game.json
```

分片放在构建旁边（`dist/<平台>/chunks/`），发布到 `sources` 中的地址（`{hash}` 是构建的内容哈希，不同版本不会混淆）。游戏中，来自尚未下载
分片的文件会被自动请求（桌面和移动端的 `readContentFile` 会等待它，Web 上返回 false）；进度通过 `platform::contentProgress()` 获取，
显式请求用 `platform::requestContent(paths)`。每个分片都用 SHA-256 校验（被篡改的镜像分片会被拒绝并改从下一个镜像获取），缓存后下次启动
不再下载。协议：https/http（Linux 用 libcurl、WinHTTP、NSURLSession、Android 用 HttpURLConnection、Web 用 fetch）、`file://` 和文件夹；
游戏还可以注册自己的协议（Steam、Play Asset Delivery、P2P）。`assetpipe pack --chunk-size 8MB ...` 可手动完成同样的操作，
`assetpipe verify` 也会校验分片。

### 导入外部归档（解包器）

引擎不认识的格式（zip、tar、dmg、其他游戏的归档）以解包器插件的形式加入，无需重新编译引擎：一个包含 `unpacker.json` 和任意语言程序的文件夹
（协议见 [`tools/unpackers/PROTOCOL.md`](../../tools/unpackers/PROTOCOL.md)）。解包器只在构建工具中运行：外部归档会变成 `.rpak` 中的文件，
游戏仍然只读取 `.rpak`。

内置支持：zip（以及 `.oramap`）、tar、dmg、Warcraft I/II 归档（`.WAR/.SUD/.INS`）、Westwood `.PAK`（Dune II、Lands of Lore、
Kyrandia）、Bitmap Brothers 的 Z（`.PAC`、`.RSC`）。

```bash
assetpipe formats                                   # which formats are known
assetpipe probe ~/Downloads/Warcraft2/war2/DATA/*   # what these files are
assetpipe list MAINDAT.WAR                          # what is inside
assetpipe import -o war2.rpak MAINDAT.WAR=imported/war2 SFXDAT.SUD=imported/war2/sfx
assetpipe unpackers new tools/unpackers myformat    # scaffold for a new unpacker
assetpipe unpackers check                           # check all plugins
```

所有命令都支持 `--json` —— MCP 服务器就是这样调用它们的。`game.json` 可以列出 `"imports"`，`aiversebuild` 会在构建时把它们解包进资源包
（`"optional": true` —— 本机没有该文件时构建照常继续）。含有导入的第三方内容的资源包会被标记为 “external”：这类构建仅供个人使用，不得提交或分发。

### 导入游戏（导入器）

导入器把你手中的经典游戏副本转换为引擎可用的游戏文件夹。目前支持 Dune II：

```bash
python3 tools/importers/dune2/import.py --source "~/Downloads/Dune II" --out ~/AiverseGames/dune2
aiversebuild --project ~/AiverseGames/dune2 --target macos
```

结果是引擎格式的 2D 游戏（`docs/game-format.md`，“2D games”）：俯视相机，66 个战役任务，带瓦片层（`tilesets/dune.json` 来自 ICON.ICN：
沙地、岩石、沙丘、山脉、香料，支持自动拼接），单位精灵来自 SHP（5 个方向加镜像，坦克炮塔叠在车体上），建筑来自 ICN 瓦片（建造、废墟、动画），
家族颜色取自品红色调。另有 `rules.json`（我们用引擎模块实现的 Dune II 平衡）、CPS 图片和 WSA 动画转为 PNG、VOC 音效 → WAV + `audio.json`，
名称有英文和俄文。地形是近似的：没有使用原版地貌生成器，基地位于岩石上，香料来自 `Field=`，任务目标用触发器实现。步兵、扑翼机和运输机仍以
3D 形状绘制。该文件夹标记为 `"external": true` —— 不得提交或分发。

## 项目文件 .aiverse

项目是磁盘上任意位置的一个文件夹（`game.json`、`rules.json`、`maps/` 等）。其根目录下有项目文件 **`<名称>.aiverse`** —— 双击即可打开，
就像 Unreal 的 `.uproject` 或 `project.godot`。`game.json` 仍是游戏清单（由客户端、aiversebuild 和资源包读取）；`.aiverse` 只是把文件夹
标记为项目并保存编辑器信息：

```json
{"format": 1, "name": "My Strategy", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
```

- 它随项目一起创建（项目中心“新建项目”、MCP `create_project`）；没有它的旧项目会在编辑器中首次打开时获得一个。`templates/*/` 中的模板已经
  自带（不会被复制到新项目，新项目有自己的）。
- 凡是编辑器接受项目路径的地方，`.aiverse` 和文件夹都可以：`aiverse_editor "My Strategy.aiverse"`、`--project`、项目中心的“打开…”
  （显示 `.aiverse` 和项目文件夹）、最近项目、MCP `open_game`、`aiversebuild --project x.aiverse`。`.aiverse` 不会进入构建。
- 在改名为 AIVERSE 之前创建的项目保留其 `<名称>.rtsproj`：在所有地方都能同样打开，原地重写，不会被改名；
  如果文件夹中两者都有，以 `.aiverse` 为准。
- **双击。** 编辑器启动时，如果 `.aiverse` 没有关联程序或关联的是另一个编辑器实例，就会把它关联到自己（按用户，无需管理员权限）；你选择的其他
  程序不会被覆盖。手动操作：“项目 → 将编辑器设为 .aiverse 的默认程序”；状态显示在项目中心左下角。关闭：`AIVERSE_FILE_ASSOC=off`。
  - **macOS：**类型 `com.aiverse.project` 声明在 `AiverseEditor.app` 的 Info.plist 中（`platform/macos/EditorInfo.plist.in`；文档图标
    `rtsproj.icns` 由 `aiverse_projicon` 根据编辑器标志 `apps/editor/branding/icon_1024.png` 绘制）；编辑器在 LaunchServices 中注册
    bundle（`lsregister -f`）并成为默认处理程序。双击或 `open -a AiverseEditor x.aiverse` 会在已运行的编辑器中打开项目（地图未保存时会询问
    是否保存）。
  - **Windows：**`HKCU\Software\Classes\.aiverse` (+ `.rtsproj`) → `AIVERSE.Project`（图标 `%USERPROFILE%\.aiverse\icons\rtsproj.ico`，命令
    `"<aiverse_editor.exe>" "%1"`）。如果 Windows 已为 `.aiverse` 选择了其他程序，项目中心会提示“打开方式 → 始终”。
  - **Linux：**类型 `application/x-aiverse-project`（`~/.local/share/mime/packages/aiverse-project.xml`）、
    `~/.local/share/applications/aiverse-editor.desktop`（`Exec=<编辑器> %f`），图标在 `~/.local/share/icons/hicolor`，默认程序写在
    `~/.config/mimeapps.list`；如已安装，会调用 `update-mime-database`、`update-desktop-database`、`xdg-mime`。

## 制作游戏（向导）

项目窗口中的 **制作游戏** 按钮（以及 **项目 → 制作游戏…**）：提供包含美术、音乐和文本的文件夹或链接，描述想法（也可以不写 —— 向导会自己选），
就能得到完整的 `.aiverse` 项目：基于合适模板的游戏、分步教程（强制）和战斗提示（轻量）、带星级和奖励的关卡、元游戏（菜单、升级、商店、
每日奖励、个人资料、设置）、按需的广告和内购、多语言、由美术生成的图标和启动画面。向导会用机器人自己通关教程并检查关卡；分配了什么、需要检查
什么，都写在项目的 `WIZARD.md` 中。智能体可使用 MCP `game_make_options` / `game_make_plan` / `game_make`。详情：
[game-wizard.md](game-wizard.md)。

## 如何描述一个游戏

对象类型是一组可选的块；设置了哪些块，对象就具备哪些能力：

| 块 | 含义 |
|---|---|
| `health` | 生命值、护甲、护甲类型、回复 |
| `move` | 速度、层（`ground`/`air`/`naval`）、半径 |
| `structure` | 在网格上占 `footprint` 大小的建筑 |
| `weapons` | 射程、伤害、伤害类型、冷却、弹体、溅射、可攻击的层 |
| `cost` | 资源、时间、人口 |
| `produces` / `builds` | 训练什么 / 建造什么 |
| `gather` / `resourceNode` / `dropoff` | 资源采集（Warcraft/StarCraft） |
| `income` / `storage` + 资源 `"kind": "flow"` | 流式经济（Total Annihilation） |
| `supply` | 提供人口 |
| `requires` | 科技树 |
| `garrison` + `entersGarrison` | 驻军建筑、按比例派兵、占领（Mushroom Wars） |
| `view` | 外观：`shape`、`scale`、`color` |

全局：`resources`、`damageTable`（伤害类型 × 护甲类型）、`supplyCap`、`victory`。
地图：尺寸、地形（`rows`：`.` 地面，`~` 水，`#` 岩石，`,` 不可建造，`=` 浅滩；`heights`）、玩家和队伍、初始资源、对象、道路、`roadsOnly`。

## 本地化

翻译游戏不需要修改 `rules.json`：数据中的名称是游戏源语言的文本，翻译放在 `<项目>/lang/<语言>.json` 中，键名如 `type.footman`、
`resource.gold`、`map.valley.title`…… 菜单和 HUD 的字符串在 `engine/assets/lang/`（目前有英文和俄文），游戏可以覆盖其中任意一条。

```jsonc
// game.json
"sourceLanguage": "en",
"languages": ["en", "ru"]
```

```bash
./build/mac/aiverse_i18n extract --project templates/skirmish --lang de --write   # lang/de.json skeleton, new strings marked // TODO
./build/mac/aiverse_i18n check   --project templates/skirmish                     # placeholders, plural forms, untranslated strings
```

语言选择顺序：玩家在菜单中的选择（会被记住）→ 系统语言 → 游戏源语言。测试用：`--lang ru`、`AIVERSE_LANG=ru`，伪语言 `--lang qps`
（所有字符串都被变形并加长 —— 未翻译的内容和放不下的文本一目了然）。复数形式遵循 CLDR（“1 second / 2 seconds / 5 seconds”）。
更多：`engine/i18n/README.md`。

## 游戏界面（RmlUi，HTML/CSS）

客户端的菜单、HUD、指令卡、小地图和暂停界面都是 RmlUi 文档（`.rml` 标记、`.rcss` 样式），而不是代码。引擎通过自己的 RHI（GL/Metal）绘制它们；
字体通过 stb_truetype 渲染（拉丁字母 + 西里尔字母）。

- 默认文档：`engine/assets/ui/`（`menu.rml`、`hud.rml`、`theme.rcss`）。
- 游戏只需在自己的文件夹中放入 `ui/<同名文件>` 即可覆盖任意文件：例如只放 `ui/theme.rcss` —— 用默认标记、换自己的配色。游戏字体是
  `fonts/ui.ttf`。
- 数据来自 `game` 模型（字段列表见 `hud.rml` 中的注释）；按钮点击会变成模拟指令（`apps/client/ClientUi.cpp`）。
- 尺寸使用 `dp` —— 界面按屏幕密度缩放；在触屏设备上根元素带有 `touch` 类（按钮更大）。
- 在客户端按 F8 打开 RmlUi 调试器（元素树、样式、日志）。
- 游戏皮肤是 `ui/skin.rcss`（在每个文档中于 `theme.rcss` 之后加载：颜色、边框、按钮、布局）以及标题字体 `fonts/title.ttf`（字体族
  `title`）。`templates/` 中的每个原型都有自己的皮肤。竖屏布局（`@media (orientation: portrait)`）在引擎主题中；在 PC 上用
  `--size 540x960` 检查。
- 文档中的文本写成键 `@menu.play`：加载文档时从 `engine/assets/lang`（以及游戏的 `lang/`）取翻译；在菜单中切换语言会重新加载文档。
- 屏幕键盘（`engine/ui/UiKeyboard.*`）：基于 RmlUi 实现，在触屏设备上输入框获得焦点时弹出（PC 上用 `AIVERSE_OSK=1`）。布局（en、ru、
  符号；Shift、双击开启大写锁定、切换语言）在 `keyboard.json` 中，外观在 `keyboard.rml` / `keyboard.rcss` 中；游戏可以在自己的 `ui/` 中
  替换其中任意文件。如果光标在屏幕底部，键盘会移到上方。
- 多人游戏（`lobby.rml`、`apps/client/Lobby.cpp`）：服务器地址和名称、通过服务器队列快速匹配、房间列表（加入 / 观战）、自建房间（位置、
  机器人、准备、开始、聊天）。对局由 `NetClient` 驱动：`attach(sim)`，用 `advance(dt)` 代替 `sim.advance(dt)`，指令来自位置
  `match().localSlot`（0 —— 观战者）；服务器分配给该客户端的机器人也在这里计算。

## 确定性

模拟在所有平台上逐位一致：只用定点数、自有随机数生成器、按规范顺序处理指令、录像中带校验和。已验证：Linux x86-64（GCC、Clang、MinGW）、
Linux ARM64（GCC）、macOS ARM64（AppleClang）—— 对局校验和完全一致。

## 图形与移动端

单个前向渲染通道：太阳光（Lambert + 高光）、贴合地图可见区域并按纹素对齐的阴影贴图、硬件 PCF（1/4/9 次采样）、半球环境光、距离雾、ACES。
阴影质量根据 GPU 自动选择（老旧的 Mali/Adreno/PowerVR 使用低档），也可在暂停菜单中更改。

## 多人游戏

`engine/net` 为引擎上的任何游戏提供多人功能，无需编写游戏代码。网络上只传输指令：模拟是确定性的，所以所有人的状态都一样。

- **传输：**带自有可靠层的 UDP（iOS、Android、PC）和 WebSocket（HTML5；原生平台在 UDP 被封锁时作为后备）。地址：`host[:27015]`、
  `udp://…`、`ws://host:27016`。
- **带回滚的锁步。**服务器确认每个 tick 的输入：所有玩家的指令都到达，或超过截止时间（`lagToleranceMs`）；迟到的指令顺延到下一个 tick ——
  一个卡顿的玩家不会拖住其他人。客户端不等待确认：先用自己的指令向前推进，出现分歧时恢复快照并重算（一次快照约 10 µs）。
  `rollbackWindow: 0` 即经典锁步，适合性能较弱的设备。
- **`aiverse_server`** 不需要游戏内容：房间（位置、机器人、观战者、准备、数据版本校验）、按 `server.json` 中的队列自动匹配（窗口逐步放宽的
  Elo 评分、共同地图、队伍平衡、用机器人补位）、校验和比对（不同步检测）、录像（`aiverse_headless --replay` 可回放）、断线重连、延迟观战。
- **离开 = 认输。**离开的玩家（或在 `reconnectGraceSec` 内未返回的玩家）会通过公共输入流中的一条指令认输：结果由模拟决定，所有人一致，
  并可在录像中看到。同时扣除评分。
- **服务器裁决。**如果没有人确认结果（所有人都离开、结果不一致），服务器会在后台线程中用自己的游戏数据（`server.json` 中的 `"games"`）
  回放这局，并自行判定胜者。
- **重连：**网络中断后客户端会自动重连（`NetClient(address, …)` 或 `setReconnect`），模拟继续运行 —— 服务器只补发最后一个已确认 tick 之后
  的输入。如果游戏被重启，客户端会收到全部历史并从头重算整局。
- **在游戏中：**`NetClient`（`engine/net/NetClient.h`）负责房间和对局。游戏根据 `match()` 构建 `Simulation` 并调用 `attach(sim)`，
  然后用 `advance(dt)` 代替 `sim.advance(dt)`；界面仍通过 `sim.submit()` 提交指令。
- **文本：**服务器不发送现成的句子，只发送带参数的本地化键（`NetText`）：同一局的玩家可以使用不同语言。客户端用 `engine/net/NetLocalize.h`
  中的 `net::translate(loc, event.text)` 和 `net::displayName(...)` 翻译；字符串在 `engine/assets/lang/*.json` 的 `net.*` 下。
  `server.json` 中的队列名也是键（`"@net.queue.1v1"`）。

```bash
./build/mac/aiverse_server --config apps/server/server.json            # UDP 27015, WebSocket 27016
./build/mac/aiverse_netbot --server 127.0.0.1 --project templates/mushrooms --queue 1v1   # a bot in the queue
./build/mac/aiverse_netbot --server ws://127.0.0.1:27016 --project templates/mushrooms --queue 1v1
./build/mac/aiverse_net_tests                                          # network tests
docker build -f apps/server/Dockerfile -t aiverse-server .             # server in a container
```

如需 `wss://`（通过 HTTPS 提供的游戏页面），请在服务器前加一层 TLS 代理（nginx、Caddy）。

## 音频

`engine/audio` 包括混音器、由数据定义的音效库、程序化合成和音乐；输出通过 [miniaudio](../../third_party/miniaudio)（CoreAudio、
AAudio/OpenSL ES、Web Audio、WASAPI、ALSA/PulseAudio）。`modules/rts/audio` 把模拟事件转换为声音；它不向模拟写入任何内容。没有音频设备时
游戏静音运行；没有 `audio.json` 时游戏就是没有声音。

游戏的音频就是 `game.json` 旁边的 `audio.json` 文件（也可在 game.json 中用 `"audio"` 指定其他名称）。完整格式见 `docs/game-format.md` 的
audio.json 一节；简要示例：

```jsonc
{
  "volume": { "sfx": 0.9, "music": 0.6, "voice": 1, "ui": 0.7, "ambient": 0.7 },
  "sounds": {
    "sword": { "files": ["sfx/sword1.ogg", "sfx/sword2.ogg"], "pitch": [0.9, 1.1], "maxInstances": 3, "cooldown": 0.06 },
    "alarm": { "file": "voice/under_attack.ogg", "bus": "voice", "priority": 10 },
    "blip":  { "synth": { "wave": "square", "freq": [880, 440], "duration": 0.08 }, "bus": "ui" }
  },
  "music":     { "menu": "music/menu.ogg", "match": { "files": ["music/a.ogg", "music/b.ogg"], "shuffle": true } },
  "events":    { "died": "death", "underAttack": "alarm", "notEnough.gold": "need_gold" },
  "weapons":   { "bow": { "attack": "bow_release", "hit": "arrow_hit" } },
  "abilities": { "heal": "heal_cast" },
  "types":     { "footman": { "select": "footman_what", "move": "footman_yes", "died": "death_human" } }
}
```

| 项 | 含义 |
|---|---|
| 格式 | WAV、OGG Vorbis、MP3、FLAC；`"synth"` —— 由参数生成的声音（`sine/square/saw/triangle/noise`、扫频、包络、颤音、滤波、多层） |
| 总线 | `sfx` 和 `ambient` 是空间化的（3D：衰减、声像、高度）；`voice`、`ui`、`music` 不是 |
| 距离 | `spatial`：衰减模型、范围、“空气”（高频随距离衰减）、混响随距离增强、声速；单个声音可有自己的范围以及 `far` —— 带交叉淡化的远距离版本 |
| `maxInstances`、`cooldown`、`priority` | 大规模战斗中的精简：40 名弓箭手不会发出 40 个相同的射击声 |
| `groups` | 多个声音共享的限制（所有近战、所有爆炸、整条总线）：`max`、`cooldown`、丢弃哪个 —— `farthest`（默认：远处的战斗不会打断屏幕上的声音）、`oldest`、`quietest`、`none` |
| 事件（cues） | `attack hit miss died spawned trainStarted ready buildPlaced buildDone gathered captured lost playerDefeated victory defeat notEnough notEnoughSupply noRoom researched levelUp ability readyToPlace sold regionClaimed wave lowPower upgraded message underAttack baseUnderAttack select move attackOrder buildOrder click error` |
| 声音查找 | 类型 → 按 id 查武器/技能 → `events` |
| 音乐 | 播放列表 `menu`、`match`、`battle`、`victory`、`defeat`（以及 `match.<地图>`）；交叉淡化、前奏 + 无缝循环、按战斗强度分层、按拍/小节过渡、点缀音效、人声时自动压低音乐 |
| 构建时压缩 | `aiversebuild` 按平台重新编码音频：音效用 ADPCM，音乐用 Vorbis，降低采样率，单声道；`audio.json` → `build` 中的 `desktop`/`mobile`/`web` 配置；`--audio off` —— 不重新编码 |

只有在没有战争迷雾的地方才能听到战斗声音；“我们遭到攻击”会有间隔，并且只在镜头看向别处时播放。音量（总音量和音乐）在暂停菜单中，保存在
`userDataDir/audio.cfg`。在手机上，应用切到后台时声音会暂停。

无声卡调试：`AIVERSE_AUDIO=null`（没有硬件的真实音频线程）、`AIVERSE_AUDIO=off`、`AIVERSE_AUDIO_CAPTURE=/tmp/mix.wav` —— 把播放过的
所有声音录到 WAV 中。

## 模型、材质、动画

类型按其 `"view"` 块绘制：内置形状（`"shape"`）或模型文件（`"model"`）—— **glTF 2.0**（`.glb`、`.gltf` + `.bin` + 图片）和
**OBJ + MTL**。所有内容走同一条管线：

- **PBR 材质**（metallic-roughness）：基础色、法线、金属度-粗糙度、遮蔽、自发光；opaque / mask / blend 透明模式、双面、unlit、材质变体
  （`KHR_materials_variants`），在 `"materials"` 中覆盖。玩家颜色：作用于整个材质、通过遮罩纹理、通过纹理 alpha（同 Warcraft III）或顶点 alpha。
- **骨骼动画**在 GPU 上计算（4 个影响骨骼，骨骼数量不限），按单位状态播放片段（`idle`、`move`、`attack`、`gather`、`build`、`die`、
  `spawn`、`hit`、`cast`……），平滑过渡，带骨骼遮罩的分层（边走边用上半身攻击），步频随实际速度变化。
- **程序化动画**可作用于任意模型节点或形状：摆动、旋转（风车、螺旋桨）、按行驶距离转动的车轮、瞄准目标的炮塔、后坐、转弯倾斜、死亡时倒下
  和下沉等；由状态和战斗事件触发。
- 材质的**质量档位**（`low` / `medium` / `high`）像阴影一样按 GPU 选择；测试时可用 `AIVERSE_MATERIALS=low|medium|high` 强制指定。
- **人群**：动画片段烘焙进纹理，相同单位用实例化绘制（300 个带动画的单位 —— 61 次绘制调用，而不是 1801 次）；模型按距离使用 LOD；屏幕外和
  远处的动画更新频率更低。
- **高级动画**：走/跑混合空间、叠加片段（呼吸、后坐）、地形上的脚部 IK、头部转向目标、载具随坡度倾斜、GPU 上的变形目标（blend shapes）。
- **挂点**：手中的武器、背上的货物（按携带的资源）、弹体发射点、特效点（烟、火、尾气）；片段事件和 `RtsView::events()/effects()` 用于音效和粒子。
- **阶段**：脚手架和建造阶段、按生命值显示的损坏版本、出现和死亡时的溶解效果、选中描边、放置建筑时的半透明虚影、头像。
- **格式**：还支持 **FBX**（ufbx）和压缩纹理 **KTX2**（ETC2/BC/ASTC，在 GPU 上保持压缩）；**Collada**、**3DS**、**PLY**、**STL**；
  体素 **MagicaVoxel .vox** 和 **.vxl/.hva**（Tiberian Sun、Red Alert 2）；来自用户游戏副本的经典 RTS 模型：**Warcraft III .mdx/.mdl**
  （+ BLP）、**Total Annihilation .3do**、**C&C Generals .w3d**（+ DDS）。assimp 能读取的其他格式（.blend、.lwo、.x、.ms3d、
  .md2/.md3、.smd、.b3d……）由 **modelconv** 工具转换为 glTF：编辑器在打开和保存时自动完成，打包工具只把结果放进包里。
- **按需材质**：共享材质库 `materials.json`（通过 `base` 继承），把材质指定给模型节点（`"assign": {"Turret": "steel"}`），带法线贴图和玩家
  颜色遮罩的程序化无缝纹理；带预览的 **材质** 窗口，以及供智能体使用的 MCP 操作 `material_*` / `texture_generate`。

损坏的模型不会让游戏崩溃：该类型会用形状绘制，原因写入编辑器控制台。查看：`aiverse_modelview model.glb@Walk` 或
`aiverse_modelview --game templates/showcase`（按游戏的绘制方式显示游戏中的所有类型）；在编辑器中 —— **Models** 窗口（片段、节点、材质、
头像，保存模型文件时自动重新加载）。示例：`templates/showcase`；完整格式：[models.md](../models.md)。

## 光照

动态光照专为老旧 Android 设备（Mali-T、Adreno 3xx/4xx）设计：所有光源都绘制到可见地面上方的一张小光照贴图中，着色器只需一次纹理采样即可
取得光照，无论光源有多少。档位：`off`、`low`（128² 贴图，隔帧更新）、`medium`（256²）、`high`（另加 8 个带高光的逐像素光源）。像阴影一样
按 GPU 选择；手动指定：`AIVERSE_LIGHTS=…`。如果对局中帧率持续不达标，客户端会逐级降低光照和阴影（先降最贵、最不明显的部分），并在激烈战斗
结束后恢复一级；在菜单中手动选择阴影会关闭此机制（`AIVERSE_ADAPTIVE=off`）。

- **类型和战斗的光** —— `view.light`（篝火、锻炉、发光蘑菇），射击、命中和爆炸的闪光，弹体光（`attackLight`、`projectileLight`、
  `hitLight`、`deathLight`），技能光（`abilityLights`；默认颜色随效果而定：伤害 —— 橙色，治疗 —— 绿色），用 `halo` 光晕代替昂贵的 bloom。
  战争迷雾会遮住闪光。
- **昼夜与天气** —— `game.json` 或地图中的 `lighting` 块：`time`、`dayLength`、`nightLevel`、`clouds`、`wind`、`storm`（云影和闪电，
  录像中保持一致），`preset`：`desert`、`snow`、`volcanic`、`overcast`、`cave`（地下：永夜，只有光源发光）。夜晚自发光材质更亮，灯光会在
  水面上反射。
- **地图光源** —— 地图中的 `lights` 数组（灯笼、篝火、熔岩）。
- **接触阴影与 AO** —— 单位下方和建筑周围的柔和阴影，在所有档位都有（类型上的 `contactShadow`、`contactShape`）。
- **编辑器** —— “Lighting”窗口：预览一天中的时间和雷暴、光照质量档位、光源列表。智能体可使用：`light_add`、`light_update`、
  `light_delete`、`light_list`、`lighting_set`、`view_lighting`。

字段格式见 `modules/rts/view/RtsLights.h` 的文件头。测试变量：`AIVERSE_TIME_OF_DAY`、`AIVERSE_NIGHT`、`AIVERSE_CLOUDS`、`AIVERSE_STORM`。

## 通过 MCP 检查机制

不写代码制作游戏的智能体会精确地检查机制，而不只是跑一局机器人对战。
- **`scenario_run`** —— 在当前打开地图的副本上运行场景（不保存任何内容）：`clear`（没有对象、波次和初始单位的空地图），带名称的 `spawn`
  （`"id": "foot", "count": 3` → `foot#1..3`，`"foot"` —— 全部），按秒发出的指令（与 `play_command` 相同的键），部分位置用 `bots`，
  带通过/失败结果的 `checks`（`alive`、`dead`、`count`、`hp`、`resource`、`event`、`trigger`、`variable`、`distance`、`status`、
  `result` + 比较），`watch` + `every`（对象状态随时间变化），`events`（事件日志）。
- **`play_units` / `play_command`** —— 在 Game 窗口的实时测试中下达指令（以前只能在编辑器内部使用，现在 MCP 客户端也能看到）。对象引用：
  `play_units` 返回的 id、地图中的 `id` 名称，或过滤器 `{type, owner, tags, kind, area, near + radius}`；`queue: true` —— 排队（Shift），
  `ability` 按 id 指定。
- **`match_run`** —— 另外提供已触发的触发器日志和 `events`（按名称的事件日志）。
- **`rules_lint`** —— `rules.json` 和地图中加载器不会读取的键（拼写错误、块放错位置），并给出提示（“did you mean 'armor'?”）。它运行在真实
  的加载器上（`engine/data/Json.h` 中的 `JsonAccessLog`）；`rules_set`、`rules_merge` 和 `map_validate` 会立即报告这类键。
- **`rules_schema`** —— `part: "keys"`：加载器在每个块中读取的所有键（`types.*.weapons[]`），从所有游戏汇总，以及 `docs/game-format.md`
  中缺失的键；`part: "doc"` —— 文档本身。
- 模拟事件 `kEvTriggerFired`（仅用于工具和表现层，不计入校验和）。
- 分支 `agent-mech-tools`。

## 后处理效果

3D 场景之上的全屏效果用数据设置：`game.json` 中的 `"postfx"` 块（地图可以覆盖）。包括 bloom、调色、LUT、暗角、移轴、太阳光束、FXAA、
颗粒、色差、锐化、像素化、色调分离、着色和电影黑边，以及现成的组合（电影、鲜艳、黑色电影、复古、微缩、日落、冬季、夜视）。每个效果都有
`minQuality`。档位按 GPU 选择：弱 Android 设备为“低”，手机为“中”，PC 为“高”；`AIVERSE_POSTFX` 可覆盖。不启用效果时后处理没有任何开销。
轻量效果合并为一个通道；重的效果（bloom、FXAA、光束）各自增加通道。

编辑器：**后处理**窗口（各档位预览、按设备等级列出每帧开销的表格）。
MCP：`postfx_catalog/get/add/set/remove/preset/enable/estimate/lut_neutral`、`view_postfx`。修改后的响应包含每帧毫秒数估算和重效果警告。
详情：`docs/postfx.md`。

## 已验证 / 未验证

- ✅ Linux：客户端、编辑器、测试；Windows .exe（MinGW 交叉编译，在 Wine 下运行）。
- ✅ macOS：构建、测试、在 Metal 上运行游戏；aiversebuild → .app 和 iOS 项目（+ 模拟器构建）。
- ✅ Web：可构建（Emscripten 3.1.50），对局可在无头 Chromium（WebGL2/SwiftShader）中运行。
- ⏳ Android：会生成 Gradle 项目；APK 和模拟器运行在 CI 中（job `android`），未在本地构建。
- ⏳ Windows 实体硬件和 iOS 真机 —— 尚未运行（CI 中有 MSVC 构建和 iOS 模拟器构建）。

## 平台与 CI

`.github/workflows/ci.yml` 在每次推送到 `master`/`agent-*` 以及 pull request 时运行：

| Job | 内容 |
|---|---|
| `linux` | gcc 和 clang（x64）+ gcc（ARM64），警告视为错误，测试，机器人对战，Xvfb 下截图 |
| `windows-msvc` | Visual Studio 2022，测试，对战，`aiversebuild --target windows` |
| `windows-mingw` | MinGW-w64 交叉编译，在 Wine 下运行测试和对战 |
| `macos` | 通用 Xcode 构建，测试，对战，`aiversebuild` → .app 和带模拟器构建的 iOS 项目 |
| `android` | 用 NDK 构建 `.so`，用 `aiversebuild` 生成 APK；在模拟器中：启动 → 点击 → 切后台 → 返回 → “返回键” → 重新启动，检查 logcat 中的崩溃 |
| `web` | Emscripten，在无头 Chromium 中运行对局，检查控制台和画面 |
| `determinism` | 比较所有平台的对局校验和（两个游戏 × 3 个 seed）—— 必须逐位一致 |

截图、APK、Web 构建和 .app 会作为运行产物保存。

本地运行：

```bash
# Windows .exe from Linux/macOS
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64.cmake
# bot matches on every game → a checksum file (compare between machines with diff)
.github/scripts/headless-checksums.sh build/linux-release/aiverse_headless sums.txt
# smoke test of the web build
node platform/web/smoke.mjs build/web
```

Web 构建中的命令行参数通过 URL 传入：`index.html?map=0&spectate` = `--map 0 --spectate`。

屏幕键盘：`platform::startTextInput(area)` / `stopTextInput()` 在 iOS、Android 和移动浏览器上打开和关闭键盘（在桌面上不做任何事）。
文本以任意语言的 `Char` 事件到达，退格/回车以 `KeyDown`/`KeyUp` 到达；`softKeyboardHeight()` 和 `KeyboardChanged` 事件告知屏幕底部被
遮挡了多少，`TextInputEnded` 表示用户自己关闭了键盘。界面（`UiLayer`）在 `<input type="text|password|search|email|number">` 和
`<textarea>` 获得焦点时会自动调用这些接口。

`Platform.h` 中面向移动端和浏览器的其他功能：
- `safeAreaInsets()` —— 为摄像头刘海、圆角、主屏幕指示条和系统栏留出的边距（iOS、Android、移动版 Safari/Chrome；桌面为 0，在带刘海的
  MacBook 上为全屏时的值）。
- `setFullscreen()` / `isFullscreen()`；内置快捷键 —— F11（Windows、Linux、Web）、Alt+Enter（Windows）。在浏览器中，全屏还会锁定屏幕方向
  （`AppConfig::orientation`）。
- Android：返回键以 `Key::Escape` 传给游戏；全屏并自动隐藏系统栏；物理键盘和鼠标（Chromebook、DeX）产生普通的按键和鼠标事件。
- iPad：妙控键盘 / 蓝牙键盘、鼠标和触控板（右键、悬停、滚动）。
- 剪贴板在所有平台可用；在浏览器中，浏览器交出文本后 Ctrl/Cmd+V 才会到达游戏。
- Web：`userDataDir()` 存储在 IndexedDB 中 —— 设置和存档在刷新页面后仍然保留。

### 原生插件：支付与广告

任何平台 SDK 都以其原生语言的插件接入 —— Java（Android）、Objective-C（iOS/macOS）、JavaScript（Web）或 C++ —— 通过同一个异步桥
`engine/platform/Plugins.h`。游戏使用 `engine/services` 中的通用 API `aiverse::store`（内购）和 `aiverse::ads`（广告），并在 `game.json`
中按平台选择提供方。`plugins/` 中现成的插件：**App Store**、**Google Play**、**RuStore**、**Yandex Games**（Web：内购和广告）、
**Steam**（通过 Inventory Service 和 DLC 内购，Windows/Linux/macOS）、**Yandex Mobile Ads**、**AdMob**、**AppLovin MAX**、
**ironSource (Unity LevelPlay)**。构建中只包含游戏选择的插件（game.json 中的 `"plugins"` 或 `--plugins`）：不会混入其他 SDK、依赖、清单条目
或 Info.plist 键 —— CI 会检查这一点。桌面调试有商店和广告的模拟实现（`mock-store`、`mock-ads`；`AIVERSE_MOCK_STORE`、`AIVERSE_MOCK_ADS`）：
无需手机即可走完整个购买流程，正式构建不会编译这些模拟实现。aiversebuild 会自动把插件写入 Gradle/清单、Info.plist/Podfile 和 HTML；Google Play
和 RuStore 请分别构建：`--plugins googleplay,admob` 和 `--plugins rustore,yandex-ads`。详情：[plugins.md](../plugins.md)。

### 发布

`.github/workflows/release.yml`：打标签 `v1.2.3`（或手动运行并选择游戏文件夹）会用 `aiversebuild` 构建所有平台并发布 GitHub Release：

| 平台 | 产物 | 签名（仓库 secrets） |
|---|---|---|
| Windows | 安装程序（Inno Setup，无需管理员权限）+ 便携 .zip | `WINDOWS_CERT_PFX_BASE64`、`WINDOWS_CERT_PASSWORD` |
| Android | 面向 Google Play 的 .apk 和 .aab | `ANDROID_KEYSTORE_BASE64`、`ANDROID_KEYSTORE_PASSWORD`、`ANDROID_KEY_ALIAS`、`ANDROID_KEY_PASSWORD` |
| macOS | .zip 中的 .app，已公证 | `MACOS_CERT_P12_BASE64`、`MACOS_CERT_PASSWORD`（Developer ID）+ App Store Connect 密钥 |
| iOS | Xcode 项目；有密钥时生成 TestFlight 构建 | `APPLE_TEAM_ID`、`ASC_KEY_ID`、`ASC_ISSUER_ID`、`ASC_KEY_P8_BASE64` |
| Linux | .tar.gz | — |
| Web | .zip；设置 `DEPLOY_WEB_PAGES=true`（仓库变量）时发布到 GitHub Pages | — |

没有 secrets 也能发布：APK 用调试密钥签名，.app 使用 ad-hoc 签名，Windows 不签名。

### 预构建引擎（`builds` 分支）与无需编译器的 aiversemcp

预构建的编辑器和工具发布在 **`builds`** 分支：`<platform>/AIVERSE-<version>-<commit>-<platform>.zip`（目前有 `windows` x86_64 和
`macos` arm64；`linux` x86_64 待集成者发布）。其中包含 `AIVERSE/` 文件夹（该提交的引擎源码，含模板 `templates/`、`engine/assets`、
`docs/` —— MCP 服务器会读取它们）以及装有程序的 `AIVERSE/bin/`。由 `tools/release/package_engine.py` 打包、`tools/release/publish_builds.py`
发布（AGENTS.md §8）；Linux 包可以不含编辑器 —— 只有 `aiversemcp` 和 `aiverse_headless`。

从解压后的文件夹运行 MCP 服务器（机器人对战和 `balance_*` 在 aiversemcp 内部运行，不需要单独的 `aiverse_headless`；编辑器可选，不用时加
`--no-editor`）：

```bash
# ARCHIVE is the path to the AIVERSE archive downloaded from index.json.
unzip "$ARCHIVE" -d ~/aiverse      # unzip keeps the executable bit
~/aiverse/AIVERSE/bin/aiversemcp --no-editor --engine ~/aiverse/AIVERSE
```

在 aiversemcp 会在自身旁边查找引擎的提交（`engine/platform/EngineDir.h`，与编辑器相同）之后的构建中，可以省略 `--engine`；更早发布的构建
必须提供。若解压工具不保留权限（Python `zipfile`），请对 `bin/` 中的文件执行 `chmod +x`。在 macOS 上，浏览器下载的文件可能被隔离：
`xattr -dr com.apple.quarantine ~/aiverse/AIVERSE`。

自行下载引擎的程序读取分支根目录的 **`index.json`**（`https://raw.githubusercontent.com/chromealex/AIVERSE/builds/index.json`），而不是
README。下面是一条历史 RTSEngine 记录的示例；新构建的路径包含 AIVERSE，`rtsmcp` 键保持不变：

```json
{
  "schema": 1,
  "repository": "chromealex/AIVERSE",
  "branch": "builds",
  "packages": [
    {
      "platform": "macos",
      "arch": "arm64",
      "version": "0.2.0",
      "commit": "77126fa9",
      "file": "macos/RTSEngine-0.2.0-77126fa9-macos.zip",
      "url": "https://raw.githubusercontent.com/chromealex/AIVERSE/builds/macos/RTSEngine-0.2.0-77126fa9-macos.zip",
      "size": 43055202,
      "sha256": "c08044fd5a13648016f4d0f2184edb6054d338a814c2b2de2caab09b4cf4513d",
      "root": "RTSEngine",
      "rtsmcp": "RTSEngine/bin/rtsmcp",
      "editor": "RTSEngine/bin/RTSEditor.app",
      "headless": "RTSEngine/bin/rts_headless"
    }
  ]
}
```

- `packages` 按从新到旧排列；第一条匹配 `platform` + `arch` 的记录就是最新构建。
- `platform`：`windows` | `macos` | `linux`；`arch`：`x86_64` | `arm64` | `universal`（macOS 通用二进制）| `null`（无法识别）——
  根据 zip 中 `aiversemcp` 的文件头确定。
- `rtsmcp`、`editor`、`headless` 是归档内的路径，若包中没有该程序则为 `null`；`--engine` 指向 `root` 文件夹。在 macOS 上 `editor` 是
  `.app` bundle。
- `url` 指向每次发布都会被重写的分支（每个平台保留 5 个版本）：旧记录会消失，因此请根据最新的 `index.json` 下载并核对 `sha256`。
- 可能增加新字段；不兼容的格式变更会提升 `schema`。

检查：`python3 tools/release/test_release.py [build/linux-release/aiversemcp]`（CTest `release_tools`）—— 检查 `index.json` 的格式；
提供 aiversemcp 路径时还会打包 Linux 包、解压到临时文件夹，并在不带 `--engine` 的情况下从中调用 MCP（`tools/list`、`create_project`、
`map_generate`、`map_validate`、`map_ascii`）。

截图模式（`AIVERSE_SCREENSHOT`、`AIVERSE_EXIT_AFTER_FRAMES`）每帧精确推进 1/60 秒，因此第 N 帧在任何机器上都相同。

## 下一步

glTF 模型和动画、音频、网络（锁步）、数据驱动的技能和增益、面向大规模军队的流场和 RVO、AI 改进、C# 脚本（iOS 上使用 AOT）。

## C++ 游戏模块

除了 Lua，项目还可以包含 `native/CMakeLists.txt` 和 C++20 模块。构建时通过 `aiverse_native_module(...)` 加入，并通过 `rules.json` 中的
`nativeModules` 数组启用。支持 tick、指令、回滚和状态保存；Lua 和 C++ 可以协同工作。修改 C++ 后需要重新构建游戏和编辑器。
[API、示例和构建说明](../native-modules.md)。

## 可复用的 C++ 元游戏库

`aiverse::meta` —— 可配置的商店/礼包/合成、货币和道具、任务和通行证、每日奖励、体力、邮件、排行榜、聊天和用户分群。项目的原生代码无需
Lua/GPU 即可使用；定义写成 JSON，无需重新生成算法。开始：`python3 tools/meta/scaffold.py --preset mobile --out <project>`。文档：
[meta-game.md](../meta-game.md)；智能体请看 `skills/meta-game/SKILL.md`。本地策略无需服务器即可运行；设备间通信和全局排行榜需要真实的后端。
包中不含现成的云服务。

`full --with-ui` 预设增加了好友/公会、实例收藏、装备、合成升级、订阅和锦标赛联赛。`aiverse::meta::Service` 以原子方式执行操作；
`tools/meta/server.py` 是带会话校验的本地 HTTP/SQLite 主机。附带浏览器版元游戏界面和面向智能体的简短配方目录。
[构建、运行和限制](../meta-service.md)。

`full` 还包含 15 组 LiveOps：带保底的宝箱、图鉴/交换、任务队列、研究/声望、组合任务、活动、公会战、异步 PvP、组队匹配、Elo/Glicko-2、
邀请/兑换码、激励广告、礼包链、通知和离线收益。所有参数都在 `extensions.liveops` 中；该预设不会安装新的独立 UI 界面或外部 SDK。
`python3 tools/meta/catalog.py describe matchmaking` 只输出所需的 schema 和示例。[API、策略和限制](../meta-liveops.md)。

### 可选的 3D 物理

通过 `AIVERSE_ENABLE_PHYSICS=ON` 启用 Box3D；普通 RTS 项目不需要它。独立示例 `aiverse_physics_demo` 展示刚体、碰撞、单摆和状态保存。
[构建、API 和集成限制](../physics.md)。

### 可视化 XPBD 布料

可选模块 `AIVERSE_ENABLE_CLOTH=ON`：自研的 XPBD 布料求解器，可运行在 CPU 或 Metal compute 上，支持共享批次、时间 LOD、自动休眠，并用一次
实例化绘制渲染高密度网格。在 GPU 上，顶点位置始终留在显存中；不支持的后端使用 CPU。示例可以将 GPU 结果与 CPU 参考结果对比，并检查休眠/唤醒。
[独立示例、限制和测量结果](../cloth.md)。
