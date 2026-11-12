# 导入其他引擎的项目

实现位于 `agent-engine-import`；原始规格见 [engine-import-spec.md](../engine-import-spec.md)。模拟和模板的校验和没有变化。
这是一个可用的导入基础，**并非完成了规格的全部阶段**。下面的覆盖范围和剩余工作是交接给 MAIN 的一部分。

## CLI 与中间数据

Python 3.8+；不需要任何 Python 包。可执行启动器是 `tools/importers/port/aiverse_port`（或使用下面显式的 Python 形式）：

```sh
python3 tools/importers/port/aiverse_port.py scan /path/to/UnityProject
python3 tools/importers/port/aiverse_port.py extract /path/to/UnityProject --out /tmp/port.json
python3 tools/importers/port/aiverse_port.py emit /tmp/port.json --out /path/to/Imported
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported --scale 1 --mode auto
python3 tools/importers/port/aiverse_port.py report /path/to/Imported
python3 tools/importers/port/aiverse_port.py item /path/to/Imported --id ITEM_ID
python3 tools/importers/port/aiverse_port.py resolve /path/to/Imported --id ITEM_ID --how lua --reason 'Verified by scenario…'
```

每个命令都输出 JSON；失败时以状态码 2 退出。`scan` 不写任何文件。输入和输出目录不能重叠。不要同时向同一目标运行多个导入。
`extract` 记录源路径、完整的序列化组件、字段、脚本源码及其使用情况。`emit` 写出游戏清单、规则、地图、`.aiverse`、复制的资源、
可选的精灵/瓦片/音频/特效、`port.json` 以及 JSON/Markdown 报告。写出复制的资源时 `sourceRoot` 必须仍然存在。生成的
`.rpakignore` 会把脚本中间索引和报告排除在运行时资源包之外。清单将导入的内容标记为 `external: true`，aiversebuild 会把它传递到
资源包中。

`--scale` 表示每米多少格；`--mode auto|2d|3d` 选择场景平面和相机。Unity 3D 翻转 Z；Unity 2D 把 XY 投影到地面。Godot 的
Vector2 映射到 XZ。原点会平移，使地图坐标保持为正。不会凭空推断所有者：只有 Unity 中显式的 `owner` 字段会被映射，并附带近似警告。
源项目的玩家编号需要人工核对。

`lib/port/mapping.json` 定义字段别名。`--mapping FILE` 为支持的目标添加或覆盖别名。`fireRate` 被解释为每秒射击次数（冷却时间的倒数），
并显式标为近似。引擎 JSON 中的冷却和建造时间单位是**秒**，不是模拟 tick。每个推断出的字段都带有来源、置信度和目标路径。

### 重复导入

`.port-state.json` 保存生成文件/类型的哈希。编辑过的文件或原本就存在的文件会被保留，即使编辑器保留了 `ported` 标记也一样。规则按类型
合并；被编辑过的类型会失去该标记。仍可添加新类型。用户对规则级设置的修改会被保留。地图和二进制文件按整个文件保护。过时的生成文件不会被删除。
状态清单归导入器所有，请勿手动编辑。

只有当条目及其相关源实体/脚本的指纹未变时，解决标注才会在重复导入后保留。源发生变化会重新打开该条目以供检查。

## 编辑器与 MCP

打开 **项目 → 导入引擎项目**，或调用 `editor_window {id:"project_import", open:true}`。扫描源项目，选择目标/比例/2D/3D，然后导入。
界面在渲染线程之外运行导入器，并可以打开结果。**复制 AI 交接任务**会复制一份明确的任务并打开 Agent 窗口；这并不表示已经启动了智能体。

当前的基础分支没有“制作游戏”向导。与独立分支 `agent-gamewizard` 的集成留给 MAIN；没有合并其他无关分支。

已注册的操作（导入设置也可以嵌套在 `options` 中）：

- `port_scan {path}`：识别源项目、检查序列化方式、列出资源/场景/脚本。
- `port_import {path,out,scale?,mode?,mapping?}`：执行提取和写出。
- `port_report {path?}`：读取目标中的报告（默认为当前打开的项目）。
- `port_item {path?,id}`：源条目、组件、关联脚本和生成的类型。
- `port_resolve {path?,id,how,reason}`：标注已验证的工作（`rules`、`lua`、`native`、`dropped`）。

通过 MCP 的导入是同步的，与现有的 VFX 导入器一样；大项目可能需要一些时间。`AIVERSE_PYTHON` 选择解释器。命令参数中的 Windows shell
元字符 `%`、`!`、引号和换行会被拒绝，而不是被展开。

智能体的工作流程：查看缺失的条目，查阅 `rules_schema`，先用数据实现合适的机制，再用 Lua，必要时再用原生模块。用 `scenario_run` 和
机器人对战比较 DPS、生产时间和移动速度。若提供了源画面，就对比截图。只把已验证的工作标为已解决；对有意舍弃的行为给出说明。
C#/GDScript/Blueprint 不会被机械地翻译或执行。

## 覆盖范围与规格的剩余工作

| 领域 | 已实现 | 待完成 |
|---|---|---|
| Unity 公共代码 | YAML、GUID 查找、嵌套预制体和变体已移入 `lib/unity`；兼容性导入保证 VFX 正常工作 | 公共解析器现有的嵌套限制仍然适用 |
| Unity 场景/类型 | 世界变换、场景对象、预制体覆盖、脚本使用情况和序列化字段、ScriptableObject 候选、构建场景顺序 | 将变换缩放计入模型包围盒、稳健的语义类型分组、推断玩家/出生点 |
| 资源/材质 | 模型/纹理/音频及相邻依赖保留路径；PBR 系数和第一个材质覆盖；模型缩放元数据；可选通过现有 ImageMagick/ffmpeg 适配器把 PSD/TIFF 转为 PNG | 材质槽匹配、打包贴图、导入坐标轴设置、动画帧切分；缺少图像转换器时会在报告中说明 |
| Unity 2D | 从图集裁切精灵，显式的 Tilemap 单元烘焙进图集/固定层，正交相机 | RuleTile、变换/着色的瓦片、任意 Grid 布局、动画瓦片、精灵轴心/排序 |
| 音频/特效 | 相机/管理器上的循环音频 → 对局播放列表；现有 VFX 转换器通过受保护的暂存区写出 | 混音器路由和特效与事件的语义绑定 |
| 地形 | 不透明的烘焙数据和 Terrain 组件会写入报告 | 尚未实现 TerrainData 高度场/splat/树木/细节的转换 |
| Godot | 识别文本场景/资源、外部资源、简单平移、导出的数值覆盖、脚本索引、主场景顺序 | 场景继承、完整的变换组合、多行复杂 Variant 解码、TileMap/GridMap 转换、动画片段 |
| Unreal | JSON 接收端和编辑器内的 Python 导出器：已加载的 Actor、反射得到的候选属性、通过 AssetExportTask 导出网格/纹理/音频 | 导出器**尚未在 Unreal 中运行过**；地形、Blueprint 变量的完整枚举和图导出尚未实现 |
| 编辑器 | 独立的本地化导入窗口、菜单项、MCP 和可复制的 AI 交接任务 | 与向导集成、资源类别选择、直接启动智能体 |

即使启用 Force Text，也可能存在未知的 Unity 原生二进制 `.asset`（烘焙光照/NavMesh）。它们会被逐个写入报告。二进制序列化的场景/预制体/
材质会被拒绝，并提示启用 Force Text。`.uasset` 永远不会在 Unreal 之外解析。

Unreal 辅助脚本是 `tools/importers/unreal_export/export.py`；请在启用 Python 插件的 Unreal Editor 中、用一个新的输出文件夹调用
`export_project(destination)`。其 API 参考：
[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6)、
[EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6)。
它导出当前已加载的关卡和 `/Game` 资源；不会遍历未加载的关卡。

## 验证，2026-11-03

`tests/data/port` 下的合成项目是原创测试数据。Unreal 的 FBX 是现有的手写 CC0 文件 `tests/data/models/cube.fbx`。没有提交任何下载的游戏资源。

- 移动公共代码后，现有的 34 个 VFX 导入器测试全部通过。
- 15 个项目导入器测试覆盖：拒绝不合适的序列化、预制体覆盖、单位、受保护的编辑、幂等性（包括重新写出 `port.json`）、源变化导致失效、
  路径安全、精灵/瓦片以及通过 CLI 解决。设置 `AIVERSE_PORT_HEADLESS` 后，五个项目都能在真实引擎中加载，Unity 战斗项目以玩家 1 获胜结束。
- C++ 测试套件：5857 项检查，一个仅在沙箱中出现的 localhost 监听失败；该测试在网络沙箱外单独运行时通过（27 项检查）。额外的 MCP 桥接测试
  通过（5 项检查）。
- 与本次改动无关的既有编译警告：`engine/fx/FxEffect.cpp:1058` 中未使用的 `fb`。

两个公开的 RTS 仓库只下载到 `/tmp` 并完成导入；**每一张生成的地图都用 `aiverse_headless --max-seconds 0` 单独加载过**，而不只是第一张/默认地图：

| 来源 | 修订版本 | 结果 | 报告 |
|---|---|---|---|
| [Unity FPS RTS](https://github.com/aydenspringer/Unity-FPS-RTS-System) | `df468f644e6aff145d2eb219dca87f99742ca81d` | 复制 509 个资源，375 个类型候选，30 张地图；30/30 可加载 | 486 项近似，1913 项缺失；脚本、渲染/物理组件、不透明烘焙数据和不支持的资源需要检查 |
| [Godot Open RTS](https://github.com/lampe-games/godot-open-rts) | `a628ad3bb6a9a903587a181a7ee3300dbf735e81` | 复制 388 个资源，552 个类型候选，84 张地图；84/84 可加载 | 638 项近似，2017 项缺失；脚本逻辑、场景继承、内嵌资源以及瓦片/动画系统需要检查 |

这些是导入/加载检查，**不是可玩的一比一移植**。不得把候选数量当作成功重建的游戏机制来宣传。在 Unreal 中运行，以及在 Windows、Android 和
Web 上的运行仍未验证。

已在 macOS 原生编辑器中检查过独立的导入表单。通过剪贴板/快速按键的自动输入无法稳定填入 ImGui 文本，因此不宣称完成了完整的点击式导入；
同样的操作已通过 C++ MCP 桥接和 CLI 验证。macOS 编辑器/Metal 可以编译。

合并当前 master（`82c47120`）后，`NativeBuild.cpp` 中的两处 `std::filesystem::path` 循环改为 `const auto&`，以便 AppleClang 编译。
这是共享文件中的最小兼容性修复。MAIN 在集成并验证此分支后应重新发布编辑器包；此分支不发布构建。

合并 master 并完成兼容性修复后的最终验证：

- macOS 编辑器（Metal）、aiversemcp、aiverse_headless 和 aiverse_tests 均重新构建成功。
- 在不限制套接字的沙箱外运行完整 C++ 测试套件：**5930 项检查，0 失败**。
- CTest：现有的 VFX 和项目导入器目标通过（34 + 15 个 Python 测试）。
- 定向 MCP 桥接：5 项检查；原生编辑器构建辅助：42 项检查，全部通过。
- 通过 `aiversemcp --no-editor` 的端到端导入接受嵌套的 `options`，应用 scale=2，并能处理包含空格和撇号的输出路径。
- `skirmish --seed 3`：蓝方在 470 游戏秒时获胜；`mushrooms --seed 3`：红方在 92 秒时获胜。
