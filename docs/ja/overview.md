# AIVERSE — 詳細な概要

エンジンのあらゆる部分のリファレンスです：構成、ビルド、フォーマット、サブシステム、プラットフォーム、検証状況。
短い紹介（エンジンの目的と解決する課題）は [README](../../README.ja.md) にあります。エンジン自体を変更するエージェント向けのルールは
[AGENTS.md](../../AGENTS.md) にあります。

## 構成

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

## クイックスタート（macOS）

```bash
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release      # requires Xcode and CMake
cmake --build build/mac -j 8
open build/mac/AiverseEditor.app                              # editor (project templates/skirmish)
open build/mac/AiverseGame.app                                # game
./build/mac/aiverse_tests
```

エディターで別のゲームを開く：`AiverseEditor.app/Contents/MacOS/AiverseEditor --project templates/mushrooms`。
ゲームを別のデータセットで起動するには `-DAIVERSE_CONTENT_DIR=.../templates/mushrooms` を指定します。

### 複数のエディターを同時に使う

各エディタープロセスは `--project <フォルダ>` で別々のプロジェクトを開けます。`--listen` なしでは最初のインスタンスが MCP ポート `47321`
を使い、使用中なら OS が空きポートを割り当てます。実際のポートはステータスバーに表示され、**Agent** ウィンドウが接続設定
（`aiversemcp --port <ポート>`）に書き込みます。自動で割り当てられたポートはエディターの再起動後に変わることがあります。
`--listen <ポート>` でポートを固定し（競合すると MCP は起動しません）、`--listen 0` で MCP を無効にします。接続済みの MCP クライアントが
自動でウィンドウを切り替えることはありません。

## 各プラットフォーム向けビルド — aiversebuild

```bash
./build/mac/aiversebuild --project templates/skirmish --target macos     # Builds/macos/<Name>.app (ad-hoc signed)
./build/mac/aiversebuild --project templates/skirmish --target ios       # Builds/ios/xcode/AIVERSE.xcodeproj + simulator build
./build/mac/aiversebuild --project templates/skirmish --target android   # Builds/android/<Name>-release.apk when SDK/NDK and gradle are present
./build/mac/aiversebuild --project templates/skirmish --target web       # Builds/web (requires emsdk)
./build/mac/aiversebuild --project templates/skirmish --target windows   # .exe via mingw-w64 (brew install mingw-w64) or sources + build.bat
./build/mac/aiversebuild --project templates/skirmish --target all
```

出力は `<プロジェクト>/Builds/<プラットフォーム>/` に置かれます。完成したビルドとサイズレポートだけです。中間生成物（CMake のツリー、パック、
再エンコードした音声、Gradle プロジェクト、xcodebuild のデータ、エディターのサイズ見積もり）は隠しフォルダ `Builds/.temp/` にあり、いつでも
削除できます。`--generate-only` を付けると Android Studio 用の Gradle プロジェクトが `Builds/android/<Name>` に置かれます。古い表示フォルダ
`_build`、`_pack`、`_audio`、`_estimate` はパッケージャーが自動で片付けます。

エディターにも同じ機能があります：**ビルド** メニュー。生成された iOS / Android プロジェクトは自己完結しており（エンジンのソースとコンテンツが
コピーされている）、そのまま他の人に渡せます。iOS 実機で動かすには、Xcode でプロジェクトを開いて Signing でチームを選びます
（または `--team <TeamID>`、もしくは game.json の `"appleTeamId"`）。

識別子、名前、バージョン、画面の向き、アイコン（`icon.png`、1024×1024。なければ生成）は `game.json` にあります。

### ビルドツール：確認と自動インストール

ビルド前に `aiversebuild` はプラットフォームに必要なものを確認し、`PATH` 以外も探します（Finder から起動したエディターには
`/usr/bin:/bin:/usr/sbin:/sbin` しか見えないため）：Homebrew（`/opt/homebrew/bin`、`/usr/local/bin`）、`~/.local/bin`、
`~/Library/Python/*/bin`、`~/emsdk`、`$EMSDK`、`ANDROID_HOME` / `ANDROID_SDK_ROOT`、`~/Library/Android/sdk`、`~/Android/Sdk`、
`%LOCALAPPDATA%\Android\Sdk`、`JAVA_HOME`、`/usr/libexec/java_home`、Visual Studio（vswhere）、そして専用フォルダ `~/.aiverse/toolchains`。
見つかったフォルダは子プロセスの `PATH` に追加されます。

| プラットフォーム | 必要なもの | 自動インストールされるもの |
|---|---|---|
| web | CMake、Python 3、Emscripten（emsdk）、Ninja は任意 | emsdk（ダウンロード約 400 MB、ディスク約 1.5 GB）、CMake + Ninja（約 50 MB） |
| android | JDK 17–23、Android SDK（platform 35、build-tools、platform-tools）、NDK、SDK の CMake、Gradle | すべて：Temurin JDK 17、command line tools + `sdkmanager`（ライセンスは同意済みにする）、NDK 約 700 MB、Gradle 8.10.2 |
| macos | Xcode Command Line Tools、CMake | CLT は Apple のインストーラー（`xcode-select --install`）、CMake |
| ios | Xcode（App Store）、CMake | CMake |
| windows（Windows 上） | Visual Studio 2022 / Build Tools（C++）、CMake | CMake。VS は `winget` コマンドを案内 |
| linux | C++ コンパイラー、CMake | CMake |

```bash
aiversebuild --check-toolchain web            # what is present and what is missing (--json for the editor and MCP)
aiversebuild --install-toolchain web          # install what is missing (or one id: emsdk, cmake, jdk, android-ndk, gradle, ...)
aiversebuild --project ... --target web --install-missing   # install and build right away
```

`--install-missing` なしでツールが足りない場合、ビルドは終了コード 3 と `AIVERSE_MISSING_TOOLCHAIN web: emsdk` という行で止まります
（エディターはこれを認識します）。エディターの **ビルド** ウィンドウは選んだプラットフォームについて見つかったもの（パス、バージョン）と
足りないものを表示し、**自動インストール** ボタンと「足りないものを自動でインストール」チェックボックスを備えています。進捗は **コンソール**
に表示されます（テキストは選択・コピー可能。「ログフォルダを開く」で `<エンジン>/logs` を開きます）。エージェントは MCP で同じことを行います：
`toolchain_check`、`toolchain_install`、`build_start {"installMissing": true}`。

すべて `~/.aiverse/toolchains` にインストールされます（Windows：`%USERPROFILE%\.aiverse\toolchains`。別のフォルダは変数
`AIVERSE_TOOLCHAINS_DIR` で指定）。`sudo` もシステムの変更も不要です：`emsdk/`、`py/`（cmake と ninja を含む venv）、`cmake/`、`ninja/`、
`jdk-17/`、`android-sdk/`、`gradle-8.10.2/`。再実行しても安全です（済んでいるものはスキップ）。すべて消すにはこのフォルダを削除します。
例外：Command Line Tools は Apple のインストーラーが入れ、ユーザーの Android SDK が見つかった場合は `sdkmanager` がそこへ不足パッケージを
入れます。

### リソースパック（.rpak）とビルドサイズ

ゲームのコンテンツ（ゲームフォルダ + `engine/assets`）はビルドに 1 つのファイル `content/content.rpak` として入ります。各ファイルは個別に
zstd で圧縮され、同一ファイルは 1 回だけ保存され、既に圧縮された形式（png、ogg、mp3 など）はそのまま入ります。エンジンは `content.rpak` を
自動でマウントします。パック内のファイルはフォルダ内のファイルより優先されるので、パックなし（開発中）でも従来どおり動きます。
`platform::mountPack("patch.rpak")` で上にパックを重ねられます（パッチ、DLC）。各ビルドはサイズレポートを表示し、
`dist/<プラットフォーム>/size-report.txt` に書き出します。

```bash
aiversebuild ... --no-pack          # ship folders as is (debugging)
aiversebuild ... --pack-level 9     # zstd level (default 19 for Release, 3 for Debug)

assetpipe pack -o content.rpak templates/skirmish engine/assets=engine   # build a pack by hand
assetpipe list content.rpak       # files, sizes, compression
assetpipe verify content.rpak     # unpack everything and check hashes
assetpipe extract content.rpak out/
assetpipe diff old.rpak new.rpak  # what changed between versions (for patches)
```

ビルドに不要なものはゲームフォルダの `.rpakignore` に書きます（マスク `*`、`?`。`フォルダ/` はディレクトリのみ）。隠しファイル（`.git`、
`.DS_Store`）、リプレイ `*.rpl`、入れ子の `*.rpak` は常に除外されます。パックの作成は再現可能です：同じ内容からはバイト単位で同一のファイル
ができ、パックの「コンテンツハッシュ」がデータセットを識別します。

### ビルドの分割とコンテンツのダウンロード

ビルドは任意で分割できます。アプリには起動に必要なものだけを含むベースパックを入れ、残りは指定サイズの分割ファイル（`content.001.rpkc`、…）
にして、ゲームが任意の配信元からダウンロードします。すべてゲームの作者が `game.json`（共通設定とプラットフォーム別設定）か `aiversebuild` の
フラグで決めます：

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

分割ファイルはビルドの隣（`dist/<プラットフォーム>/chunks/`）に置かれ、`sources` のアドレスで公開します（`{hash}` はビルドのコンテンツ
ハッシュなので、バージョンが混ざりません）。ゲーム内では、未ダウンロードの分割ファイルにあるファイルは自動で要求されます（デスクトップと
モバイルでは `readContentFile` が待ち、Web では false を返します）。進捗は `platform::contentProgress()`、明示的な要求は
`platform::requestContent(paths)` です。各分割ファイルは SHA-256 で検証され（ミラーから改ざんされたものは拒否して次のミラーから取得）、
キャッシュされて次回起動時には再ダウンロードされません。プロトコル：https/http（Linux は libcurl、WinHTTP、NSURLSession、Android は
HttpURLConnection、Web は fetch）、`file://` とフォルダ。ゲーム独自のスキーム（Steam、Play Asset Delivery、P2P）も登録できます。
`assetpipe pack --chunk-size 8MB ...` で同じことを手作業で行え、`assetpipe verify` は分割ファイルも検証します。

### 外部アーカイブのインポート（アンパッカー）

エンジンが知らない形式（zip、tar、dmg、他のゲームのアーカイブ）は、エンジンを再ビルドせずにアンパッカープラグインとして追加できます：
`unpacker.json` と任意の言語で書いたプログラムを入れたフォルダです（プロトコル：
[`tools/unpackers/PROTOCOL.md`](../../tools/unpackers/PROTOCOL.md)）。アンパッカーはビルドツール内でのみ動きます。外部アーカイブは
`.rpak` 内のファイルになり、ゲームは引き続き `.rpak` だけを読みます。

標準で対応：zip（と `.oramap`）、tar、dmg、Warcraft I/II のアーカイブ（`.WAR/.SUD/.INS`）、Westwood の `.PAK`（Dune II、Lands of Lore、
Kyrandia）、Bitmap Brothers の Z（`.PAC`、`.RSC`）。

```bash
assetpipe formats                                   # which formats are known
assetpipe probe ~/Downloads/Warcraft2/war2/DATA/*   # what these files are
assetpipe list MAINDAT.WAR                          # what is inside
assetpipe import -o war2.rpak MAINDAT.WAR=imported/war2 SFXDAT.SUD=imported/war2/sfx
assetpipe unpackers new tools/unpackers myformat    # scaffold for a new unpacker
assetpipe unpackers check                           # check all plugins
```

どのコマンドにも `--json` があり、MCP サーバーはこれで呼び出します。`game.json` に `"imports"` を列挙すると、`aiversebuild` がビルド時に
パックへ展開します（`"optional": true` — このマシンにファイルがなければ、それ抜きでビルドを続けます）。インポートしたサードパーティの
コンテンツを含むパックには「external」フラグが付きます。そうしたビルドは個人利用専用で、コミットや配布はできません。

### ゲームのインポート（インポーター）

インポーターは手持ちのクラシックゲームのコピーを、エンジン用のゲームフォルダに変換します。現在は Dune II に対応：

```bash
python3 tools/importers/dune2/import.py --source "~/Downloads/Dune II" --out ~/AiverseGames/dune2
aiversebuild --project ~/AiverseGames/dune2 --target macos
```

結果はエンジン形式の 2D ゲームです（`docs/game-format.md`、「2D games」）：見下ろしカメラ、タイルレイヤー付きの 66 のキャンペーンミッション
（ICON.ICN からの `tilesets/dune.json`：砂、岩、砂丘、山、オートタイル付きのスパイス）、SHP からのユニットスプライト（ミラー付き 5 方向、戦車の
砲塔は車体に重ねる）、ICN タイルからの建物（建設中、廃墟、アニメーション）、マゼンタの色調から作るハウスカラー。さらに `rules.json`（エンジンの
ブロックで組んだ Dune II のバランス）、CPS 画像と WSA アニメーションを PNG に、VOC 音声 → WAV + `audio.json`、英語とロシア語の名称。地形は
近似です：オリジナルの地形生成は使わず、基地は岩の上、スパイスは `Field=` から、ミッションのノルマはトリガーで実装。歩兵、オーニソプター、
キャリーオールはまだ 3D 形状で描かれます。フォルダには `"external": true` が付いており、コミットや配布はできません。

## プロジェクトファイル .rtsproj

プロジェクトはディスク上の任意の場所にあるフォルダです（`game.json`、`rules.json`、`maps/` など）。そのルートにプロジェクトファイル
**`<名前>.rtsproj`** があり、Unreal の `.uproject` や `project.godot` と同じくダブルクリックで開けます。`game.json` は引き続きゲームの
マニフェスト（クライアント、aiversebuild、パックが読む）で、`.rtsproj` はフォルダをプロジェクトとしてマークし、エディターの情報を保存する
だけです：

```json
{"format": 1, "name": "My Strategy", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
```

- プロジェクトと一緒に作られます（ハブの「新しいプロジェクト」、MCP `create_project`）。これがない古いプロジェクトは、エディターで初めて開いた
  ときに作られます。`templates/*/` のテンプレートには既にあります（新しいプロジェクトにはコピーされず、新しいプロジェクトは自分のものを持ちます）。
- エディターがプロジェクトのパスを受け付けるところでは、`.rtsproj` とフォルダのどちらも使えます：`aiverse_editor "My Strategy.rtsproj"`、
  `--project`、ハブの「開く…」（`.rtsproj` とプロジェクトフォルダを表示）、最近使ったもの、MCP `open_game`、
  `aiversebuild --project x.rtsproj`。`.rtsproj` はビルドに含まれません。
- **ダブルクリック。** 起動時、関連付けがない場合や別のエディターインスタンスに関連付けられている場合、エディターは `.rtsproj` を自分に
  関連付けます（ユーザー単位、管理者権限不要）。あなたが選んだ別のプログラムは上書きしません。手動では「プロジェクト → エディターを .rtsproj の
  既定アプリにする」。状態はハブの左下に表示されます。無効にするには `AIVERSE_FILE_ASSOC=off`。
  - **macOS：** 型 `com.aiverse.project` は `AiverseEditor.app` の Info.plist で宣言されています（`platform/macos/EditorInfo.plist.in`。
    ドキュメントアイコン `rtsproj.icns` は `aiverse_projicon` がエディターのロゴ `apps/editor/branding/icon_1024.png` から描画）。エディターは
    バンドルを LaunchServices に登録し（`lsregister -f`）、既定のハンドラーになります。ダブルクリックや `open -a AiverseEditor x.rtsproj` で
    起動中のエディターにプロジェクトが開きます（マップが未保存なら保存するか尋ねます）。
  - **Windows：** `HKCU\Software\Classes\.rtsproj` → `AIVERSE.Project`（アイコン `%USERPROFILE%\.aiverse\icons\rtsproj.ico`、コマンド
    `"<aiverse_editor.exe>" "%1"`）。Windows で既に `.rtsproj` に別のプログラムが選ばれていれば、ハブが「プログラムから開く → 常に」を案内します。
  - **Linux：** 型 `application/x-aiverse-project`（`~/.local/share/mime/packages/aiverse-project.xml`）、
    `~/.local/share/applications/aiverse-editor.desktop`（`Exec=<エディター> %f`）、アイコンは `~/.local/share/icons/hicolor`、既定アプリは
    `~/.config/mimeapps.list`。`update-mime-database`、`update-desktop-database`、`xdg-mime` がインストールされていれば呼び出します。

## ゲームを作る（ウィザード）

プロジェクトウィンドウの **ゲームを作る** ボタン（および **プロジェクト → ゲームを作る…**）：アート、音楽、テキストのフォルダかリンクを
渡し、アイデアを書けば（何も書かなくてもウィザードが自分で選びます）、完成した `.rtsproj` プロジェクトができあがります。適したテンプレートに
基づくゲーム、段階的なチュートリアル（強制）と戦闘中のヒント（さりげない）、星と報酬のあるレベル、メタ（メニュー、アップグレード、ショップ、
デイリー報酬、プロフィール、設定）、必要なら広告と課金、言語、アートから作るアイコンとスプラッシュ画面。ウィザードはボットでチュートリアルを
自らクリアし、レベルを検証します。何が割り当てられ、何を確認すべきかはプロジェクトの `WIZARD.md` にあります。エージェント向け：MCP
`game_make_options` / `game_make_plan` / `game_make`。詳細：[game-wizard.md](game-wizard.md)。

## ゲームの記述のしかた

オブジェクトのタイプは任意のブロックの集合です。設定したブロックの分だけ、オブジェクトはその能力を持ちます：

| ブロック | 意味 |
|---|---|
| `health` | 体力、装甲、装甲タイプ、回復 |
| `move` | 速度、レイヤー（`ground`/`air`/`naval`）、半径 |
| `structure` | グリッド上で `footprint` の大きさを持つ建物 |
| `weapons` | 射程、ダメージ、ダメージタイプ、クールダウン、弾、範囲ダメージ、攻撃できるレイヤー |
| `cost` | 資源、時間、人口 |
| `produces` / `builds` | 訓練するもの / 建てるもの |
| `gather` / `resourceNode` / `dropoff` | 資源採集（Warcraft/StarCraft） |
| `income` / `storage` + 資源 `"kind": "flow"` | フロー型経済（Total Annihilation） |
| `supply` | 人口を提供 |
| `requires` | 技術ツリー |
| `garrison` + `entersGarrison` | 駐屯用の建物、部隊の一定割合を送る、占領（Mushroom Wars） |
| `view` | 見た目：`shape`、`scale`、`color` |

グローバル：`resources`、`damageTable`（ダメージタイプ × 装甲タイプ）、`supplyCap`、`victory`。
マップ：サイズ、地形（`rows`：`.` 地面、`~` 水、`#` 岩、`,` 建設不可、`=` 浅瀬。`heights`）、プレイヤーとチーム、初期資源、オブジェクト、
道路、`roadsOnly`。

## ローカライズ

ゲームは `rules.json` を変えずに翻訳できます。データ内の名前はゲームの元の言語のテキストで、翻訳は `<プロジェクト>/lang/<言語>.json` に
`type.footman`、`resource.gold`、`map.valley.title` などのキーで置きます。メニューと HUD の文字列は `engine/assets/lang/` にあり（現在は
英語とロシア語）、ゲームはどれでも上書きできます。

```jsonc
// game.json
"sourceLanguage": "en",
"languages": ["en", "ru"]
```

```bash
./build/mac/aiverse_i18n extract --project templates/skirmish --lang de --write   # lang/de.json skeleton, new strings marked // TODO
./build/mac/aiverse_i18n check   --project templates/skirmish                     # placeholders, plural forms, untranslated strings
```

言語は次の順で選ばれます：メニューでのプレイヤーの選択（記憶される）→ システムの言語 → ゲームの元の言語。確認用：`--lang ru`、
`AIVERSE_LANG=ru`、疑似言語 `--lang qps`（すべての文字列が変形され長くなるので、未翻訳やはみ出しがすぐ分かります）。複数形は CLDR に従います
（「1 second / 2 seconds / 5 seconds」）。詳細：`engine/i18n/README.md`。

## ゲームの UI（RmlUi、HTML/CSS）

クライアントのメニュー、HUD、コマンドカード、ミニマップ、ポーズは、コードではなく RmlUi のドキュメント（マークアップ `.rml`、スタイル
`.rcss`）です。エンジンは独自の RHI（GL/Metal）でそれを描画し、フォントは stb_truetype（ラテン文字 + キリル文字）で描きます。

- 既定のドキュメント：`engine/assets/ui/`（`menu.rml`、`hud.rml`、`theme.rcss`）。
- ゲームは自分のフォルダに `ui/<同じ名前>` を置けば任意のファイルを上書きできます。たとえば `ui/theme.rcss` だけを置けば、既定のマークアップの
  まま独自の配色になります。ゲームのフォントは `fonts/ui.ttf`。
- データは `game` モデルから来て（フィールド一覧は `hud.rml` のコメントにあります）、ボタンの押下はシミュレーションのコマンドになります
  （`apps/client/ClientUi.cpp`）。
- サイズは `dp` 単位で、UI は画面密度に合わせて拡大縮小します。タッチデバイスではルートに `touch` クラスが付きます（ボタンが大きくなる）。
- クライアントで F8 を押すと RmlUi のデバッガー（要素ツリー、スタイル、ログ）が開きます。
- ゲームのスキンは `ui/skin.rcss`（各ドキュメントで `theme.rcss` の後に読み込まれる：色、枠、ボタン、レイアウト）と見出しフォント
  `fonts/title.ttf`（ファミリー `title`）です。`templates/` の各プロトタイプは独自のスキンを持ちます。縦向きレイアウト
  （`@media (orientation: portrait)`）はエンジンのテーマにあり、PC では `--size 540x960` で確認できます。
- ドキュメント内のテキストはキー `@menu.play` として書きます。翻訳はドキュメントの読み込み時に `engine/assets/lang`（とゲームの `lang/`）から
  取られ、メニューで言語を切り替えるとドキュメントが再読み込みされます。
- 画面キーボード（`engine/ui/UiKeyboard.*`）：RmlUi で作られており、タッチデバイスで入力欄にフォーカスすると開きます（PC では
  `AIVERSE_OSK=1`）。配列（en、ru、記号。Shift、ダブルタップで Caps、言語切り替え）は `keyboard.json`、見た目は `keyboard.rml` /
  `keyboard.rcss` にあり、ゲームは自分の `ui/` でどれでも差し替えられます。カーソルが画面下部にあるとキーボードは上に出ます。
- マルチプレイ（`lobby.rml`、`apps/client/Lobby.cpp`）：サーバーアドレスと名前、サーバーのキューによるクイック対戦、ルーム一覧（参加 / 観戦）、
  自分のルーム（スロット、ボット、準備完了、開始、チャット）。対戦は `NetClient` が進めます：`attach(sim)`、`sim.advance(dt)` の代わりに
  `advance(dt)`、コマンドはスロット `match().localSlot` から（0 は観戦者）。サーバーがこのクライアントに割り当てたボットもここで計算されます。

## 決定論

シミュレーションはすべてのプラットフォームでビット単位で同一です：固定小数点のみ、独自の乱数生成器、正規の順序のコマンド、リプレイ内の
チェックサム。検証済み：Linux x86-64（GCC、Clang、MinGW）、Linux ARM64（GCC）、macOS ARM64（AppleClang）— 対戦のチェックサムが一致。

## グラフィックとモバイル

1 回のフォワードパス：太陽光（Lambert + スペキュラー）、マップの見えている部分に合わせテクセルにスナップしたシャドウマップ、ハードウェア PCF
（1/4/9 サンプル）、半球アンビエント、距離フォグ、ACES。影の品質は GPU に応じて選ばれ（古い Mali/Adreno/PowerVR では低）、ポーズメニューで
変更できます。

## マルチプレイ

`engine/net` は、ゲームコードなしでエンジン上のどのゲームにもマルチプレイを提供します。ネットワークを流れるのはコマンドだけです。
シミュレーションは決定論的なので、全員の状態が同じになります。

- **トランスポート：** 独自の信頼性レイヤーを持つ UDP（iOS、Android、PC）と WebSocket（HTML5。ネイティブでは UDP が塞がれているときの代替）。
  アドレス：`host[:27015]`、`udp://…`、`ws://host:27016`。
- **ロールバック付きロックステップ。** サーバーは各 tick の入力を、全プレイヤーのコマンドが届いたとき、または締め切り（`lagToleranceMs`）を
  過ぎたときに確定します。遅れたコマンドは次の tick に回るので、ラグのあるプレイヤーが他の人を止めることはありません。クライアントは確定を
  待たずに自分のコマンドで先に進め、食い違いが出たらスナップショットを復元して再計算します（スナップショットは約 10 µs）。
  `rollbackWindow: 0` は性能の低い端末向けの従来型ロックステップです。
- **`aiverse_server`** はゲームのコンテンツを必要としません：ルーム（スロット、ボット、観戦者、準備完了、データバージョンの確認）、
  `server.json` のキューによる自動マッチメイキング（幅が広がっていく Elo レーティング、共通マップ、チームバランス、ボットによる補充）、
  チェックサムの照合（非同期の検出）、リプレイ（`aiverse_headless --replay` で再生）、再接続、遅延付き観戦。
- **退出 = 降参。** 退出したプレイヤー（または `reconnectGraceSec` 以内に戻らなかったプレイヤー）は、共通の入力ストリーム内のコマンドで
  降参します。結果はシミュレーションが全員同じように決め、リプレイにも残ります。レーティングは差し引かれます。
- **サーバーの判定。** 誰も結果を確定しなかった場合（全員退出、結果が食い違う）、サーバーはバックグラウンドスレッドで自分のゲームデータ
  （`server.json` の `"games"`）を使って対戦を再生し、勝者を決めます。
- **再接続：** ネットワークが切れても、クライアントは自動で再接続し（`NetClient(address, …)` または `setReconnect`）、シミュレーションは
  動き続けます。サーバーは最後に確定した tick 以降の入力だけを送ります。ゲームを再起動した場合は、クライアントが履歴全体を受け取り、対戦を
  最初から再計算します。
- **ゲーム内：** `NetClient`（`engine/net/NetClient.h`）がロビーと対戦を担います。ゲームは `match()` から `Simulation` を作って
  `attach(sim)` を呼び、`sim.advance(dt)` の代わりに `advance(dt)` を呼びます。UI は引き続き `sim.submit()` でコマンドを送ります。
- **テキスト：** サーバーは完成した文を送らず、引数付きのローカライズキー（`NetText`）だけを送ります。同じ対戦のプレイヤーが別々の言語を
  使えるからです。クライアントは `engine/net/NetLocalize.h` の `net::translate(loc, event.text)` と `net::displayName(...)` で翻訳し、
  文字列は `engine/assets/lang/*.json` の `net.*` にあります。`server.json` のキュー名もキーです（`"@net.queue.1v1"`）。

```bash
./build/mac/aiverse_server --config apps/server/server.json            # UDP 27015, WebSocket 27016
./build/mac/aiverse_netbot --server 127.0.0.1 --project templates/mushrooms --queue 1v1   # a bot in the queue
./build/mac/aiverse_netbot --server ws://127.0.0.1:27016 --project templates/mushrooms --queue 1v1
./build/mac/aiverse_net_tests                                          # network tests
docker build -f apps/server/Dockerfile -t aiverse-server .             # server in a container
```

`wss://`（HTTPS で配信するゲームページ）には、サーバーの前に TLS プロキシ（nginx、Caddy）を置いてください。

## サウンド

`engine/audio` はミキサー、データ駆動のサウンドバンク、プロシージャル合成、音楽です。出力は
[miniaudio](../../third_party/miniaudio)（CoreAudio、AAudio/OpenSL ES、Web Audio、WASAPI、ALSA/PulseAudio）経由です。
`modules/rts/audio` はシミュレーションのイベントを音に変換し、シミュレーションには何も書き込みません。デバイスがなければゲームは無音で動き、
`audio.json` がなければ単に音のないゲームになります。

ゲームのサウンドは `game.json` の隣にある `audio.json` ファイルです（別名にするには game.json の `"audio"`）。完全な形式は
`docs/game-format.md` の audio.json の節にあります。概要：

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

| 項目 | 意味 |
|---|---|
| 形式 | WAV、OGG Vorbis、MP3、FLAC。`"synth"` はパラメーターから作る音（`sine/square/saw/triangle/noise`、周波数スイープ、エンベロープ、ビブラート、フィルター、レイヤー） |
| バス | `sfx` と `ambient` は位置付き（3D：減衰、パン、高さ）。`voice`、`ui`、`music` は位置なし |
| 距離 | `spatial`：減衰モデル、範囲、「空気」（遠いほど高音が消える）、距離に応じて増える残響、音速。音ごとに範囲と `far`（クロスフェード付きの遠距離版）を持てる |
| `maxInstances`、`cooldown`、`priority` | 大規模戦闘での間引き：40 人の弓兵が 40 回同じ射撃音を出すことはない |
| `groups` | 複数の音で共有する制限（近接戦闘全体、爆発全体、バス全体）：`max`、`cooldown`、どれを止めるか — `farthest`（既定：遠くの戦闘が画面内の音を切らない）、`oldest`、`quietest`、`none` |
| イベント（キュー） | `attack hit miss died spawned trainStarted ready buildPlaced buildDone gathered captured lost playerDefeated victory defeat notEnough notEnoughSupply noRoom researched levelUp ability readyToPlace sold regionClaimed wave lowPower upgraded message underAttack baseUnderAttack select move attackOrder buildOrder click error` |
| 音の検索 | タイプ → ID による武器/能力 → `events` |
| 音楽 | プレイリスト `menu`、`match`、`battle`、`victory`、`defeat`（と `match.<マップ>`）。クロスフェード、イントロ + 継ぎ目のないループ、戦闘の激しさに応じたレイヤー、拍/小節での遷移、スティンガー、ボイス中の音楽ダッキング |
| ビルド時の圧縮 | `aiversebuild` はプラットフォームごとに音声を再エンコード：効果音は ADPCM、音楽は Vorbis、サンプルレートを下げ、モノラル化。`audio.json` → `build` の `desktop`/`mobile`/`web` プロファイル。`--audio off` で再エンコードなし |

戦闘音は戦場の霧がない場所でのみ聞こえます。「攻撃を受けている」は間隔を置いて、カメラが別の場所を見ているときだけ鳴ります。音量（全体と音楽）は
ポーズメニューにあり、`userDataDir/audio.cfg` に保存されます。スマートフォンではアプリがバックグラウンドに回ると音が一時停止します。

サウンドカードなしでのデバッグ：`AIVERSE_AUDIO=null`（ハードウェアなしで実際のオーディオスレッドを動かす）、`AIVERSE_AUDIO=off`、
`AIVERSE_AUDIO_CAPTURE=/tmp/mix.wav`（鳴ったものをすべて WAV に録音）。

## モデル、マテリアル、アニメーション

タイプは `"view"` ブロックに従って描かれます：組み込みの形状（`"shape"`）またはモデルファイル（`"model"`）— **glTF 2.0**（`.glb`、
`.gltf` + `.bin` + 画像）と **OBJ + MTL**。すべて 1 つのパイプラインを通ります：

- **PBR マテリアル**（metallic-roughness）：ベースカラー、ノーマル、メタリック・ラフネス、オクルージョン、エミッシブ。透明モード opaque /
  mask / blend、両面、unlit、マテリアルバリアント（`KHR_materials_variants`）、`"materials"` での上書き。プレイヤーカラー：マテリアル全体、
  マスクテクスチャ、テクスチャのアルファ（Warcraft III と同様）、頂点アルファのいずれか。
- **スケルタルアニメーション** を GPU で（4 影響、ボーン数は無制限）。ユニットの状態ごとのクリップ（`idle`、`move`、`attack`、`gather`、
  `build`、`die`、`spawn`、`hit`、`cast`…）、滑らかな遷移、ボーンマスク付きレイヤー（歩きながら上半身で攻撃）、実際の速度に合わせた歩幅。
- **プロシージャルアニメーション** はモデルの任意のノードや形状に：揺れ、回転（風車、プロペラ）、移動距離に応じた車輪、目標を狙う砲塔、反動、
  旋回時の傾き、死亡時の倒れ込みや沈み込みなど。状態や戦闘イベントで発動します。
- マテリアルの**品質レベル**（`low` / `medium` / `high`）は影と同様に GPU で選ばれます。テスト用に `AIVERSE_MATERIALS=low|medium|high`
  で上書きできます。
- **群衆**：クリップをテクスチャにベイクし、同じユニットはインスタンシングで描画（アニメーションする 300 ユニットで 1801 ではなく
  61 ドローコール）。距離によるモデルの LOD。画面外や遠くのアニメーションは更新頻度を下げます。
- **高度なアニメーション**：歩き/走りのブレンドスペース、加算クリップ（呼吸、反動）、地形に合わせた足の IK、目標への頭の向き、坂での乗り物の傾き、
  GPU でのモーフターゲット（ブレンドシェイプ）。
- **ソケット**：手に持つ武器、背中の荷物（運んでいる資源に応じて）、弾の発射点、エフェクトの位置（煙、炎、排気）。クリップのイベントと
  `RtsView::events()/effects()` を音とパーティクルに使います。
- **段階**：足場と建設段階、体力に応じた損傷版、出現時と死亡時のディゾルブ、選択時の輪郭線、建物を置くときの半透明のゴースト、ポートレート。
- **形式**：さらに **FBX**（ufbx）と圧縮テクスチャ **KTX2**（ETC2/BC/ASTC、GPU 上でも圧縮のまま）。**Collada**、**3DS**、**PLY**、
  **STL**。ボクセル **MagicaVoxel .vox** と **.vxl/.hva**（Tiberian Sun、Red Alert 2）。ユーザーが持つゲームのコピーからのクラシック RTS の
  モデル：**Warcraft III .mdx/.mdl**（+ BLP）、**Total Annihilation .3do**、**C&C Generals .w3d**（+ DDS）。assimp が読めるその他の形式
  （.blend、.lwo、.x、.ms3d、.md2/.md3、.smd、.b3d…）はユーティリティ **modelconv** が glTF に変換します。エディターは開くときと保存するときに
  自動で行い、パッケージャーは変換結果だけをパッケージに入れます。
- **必要に応じたマテリアル**：共有ライブラリ `materials.json`（`base` で継承）、モデルのノードへのマテリアル割り当て
  （`"assign": {"Turret": "steel"}`）、ノーマルマップとプレイヤーカラーのマスク付きのプロシージャルなシームレステクスチャ。プレビュー付きの
  **マテリアル** ウィンドウと、エージェント用の MCP 操作 `material_*` / `texture_generate`。

壊れたモデルがあってもゲームは壊れません：そのタイプは形状で描かれ、理由はエディターのコンソールに出ます。表示：
`aiverse_modelview model.glb@Walk` または `aiverse_modelview --game templates/showcase`（ゲームのすべてのタイプを、ゲームが描くとおりに表示）。
エディターでは **Models** ウィンドウ（クリップ、ノード、マテリアル、ポートレート、モデルファイル保存時の再読み込み）。例：
`templates/showcase`。完全な形式：[models.md](../models.md)。

## ライティング

ダイナミックライトは古い Android（Mali-T、Adreno 3xx/4xx）を想定しています：すべての光源を見えている地面の上の小さなライトマップに描き、
シェーダーは光源がいくつあっても 1 回のテクスチャサンプルで光を取得します。レベル：`off`、`low`（128² のマップ、1 フレームおきに更新）、
`medium`（256²）、`high`（+ スペキュラー付きのピクセル単位の光源 8 個）。影と同様に GPU で選ばれ、手動では `AIVERSE_LIGHTS=…`。対戦中に
フレームが継続して予算を超えると、クライアントはライトと影を段階的に下げ（最も重く目立たないものから）、激しい戦闘の後に 1 段階戻します。
メニューで影を手動で選ぶとこの仕組みは無効になります（`AIVERSE_ADAPTIVE=off`）。

- **タイプと戦闘の光** — `view.light`（焚き火、鍛冶場、光るキノコ）、射撃・命中・爆発の閃光、弾の光（`attackLight`、`projectileLight`、
  `hitLight`、`deathLight`）、能力の光（`abilityLights`。既定では色が効果に従う：ダメージはオレンジ、回復は緑）、重い bloom の代わりの
  `halo` の光輪。戦場の霧は閃光を隠します。
- **昼と夜、天候** — `game.json` またはマップの `lighting` ブロック：`time`、`dayLength`、`nightLevel`、`clouds`、`wind`、`storm`（雲の影と
  雷、リプレイでも同一）、`preset`：`desert`、`snow`、`volcanic`、`overcast`、`cave`（地下：永遠の夜、光源からの光のみ）。夜はエミッシブ
  マテリアルが明るく光り、灯りが水面に映ります。
- **マップの光源** — マップの `lights` 配列（ランタン、焚き火、溶岩）。
- **接地影と AO** — ユニットの下や建物の周りの柔らかい影。すべてのレベルで有効（タイプの `contactShadow`、`contactShape`）。
- **エディター** — 「Lighting」ウィンドウ：時刻と雷雨のプレビュー、ライトの品質レベル、光源一覧。エージェント向け：`light_add`、
  `light_update`、`light_delete`、`light_list`、`lighting_set`、`view_lighting`。

フィールドの形式は `modules/rts/view/RtsLights.h` のヘッダーにあります。テスト用変数：`AIVERSE_TIME_OF_DAY`、`AIVERSE_NIGHT`、
`AIVERSE_CLOUDS`、`AIVERSE_STORM`。

## MCP によるメカニクスの検証

コードを書かずにゲームを作るエージェントは、ボット対戦だけでなく、メカニクスをピンポイントで検証します。
- **`scenario_run`** — 開いているマップのコピーでシナリオを実行（何も保存しない）：`clear`（オブジェクト、ウェーブ、初期ユニットのない空の
  マップ）、名前付きの `spawn`（`"id": "foot", "count": 3` → `foot#1..3`、`"foot"` は全部）、秒単位のコマンド（`play_command` と同じキー）、
  一部のスロットの `bots`、合否の結果を返す `checks`（`alive`、`dead`、`count`、`hp`、`resource`、`event`、`trigger`、`variable`、
  `distance`、`status`、`result` + 比較）、`watch` + `every`（オブジェクトの状態の時間変化）、`events`（イベントログ）。
- **`play_units` / `play_command`** — Game ウィンドウのライブテストへのコマンド（以前はエディター内部のみ、今は MCP クライアントから見える）。
  オブジェクトの参照：`play_units` の ID、マップの `id` 名、またはフィルター `{type, owner, tags, kind, area, near + radius}`。
  `queue: true` はキュー（Shift）、`ability` は ID で指定。
- **`match_run`** — さらに発動したトリガーのログと `events`（名前別のイベントログ）。
- **`rules_lint`** — `rules.json` とマップの中でローダーが読まないキー（タイプミス、ブロックの置き場所違い）を、ヒント付き
  （「did you mean 'armor'?」）で示します。実際のローダー（`engine/data/Json.h` の `JsonAccessLog`）上で動作し、`rules_set`、
  `rules_merge`、`map_validate` もそうしたキーを即座に報告します。
- **`rules_schema`** — `part: "keys"`：各ブロック（`types.*.weapons[]`）でローダーが読むすべてのキーをすべてのゲームから集めたものと、
  `docs/game-format.md` にないキー。`part: "doc"`：ドキュメントそのもの。
- シミュレーションイベント `kEvTriggerFired`（ツールと表示用のみ、チェックサムには含まれない）。
- ブランチ `agent-mech-tools`。

## ポストエフェクト

3D シーンに重ねるフルスクリーン効果はデータで設定します：`game.json` の `"postfx"` ブロック（マップで上書き可能）。bloom、カラー
グレーディング、LUT、ビネット、ティルトシフト、太陽光線、FXAA、グレイン、色収差、シャープ、ピクセル化、ポスタリゼーション、色付け、
レターボックスに加え、既成のセット（シネマ、ビビッド、ノワール、レトロ、ミニチュア、夕暮れ、冬、暗視）があります。各効果には `minQuality`
があります。レベルは GPU で選ばれます：非力な Android は「低」、スマートフォンは「中」、PC は「高」。`AIVERSE_POSTFX` で上書きできます。
効果がなければポストプロセスのコストはゼロです。軽い効果は 1 パスにまとめ、重い効果（bloom、FXAA、光線）はそれぞれパスを追加します。

エディター：**ポストエフェクト** ウィンドウ（レベルのプレビュー、端末クラス別のフレームコスト表）。
MCP：`postfx_catalog/get/add/set/remove/preset/enable/estimate/lut_neutral`、`view_postfx`。変更への応答には ms/フレームの見積もりと
重い効果への警告が含まれます。詳細：`docs/postfx.md`。

## 検証済み / 未検証

- ✅ Linux：クライアント、エディター、テスト。Windows .exe（MinGW でクロスビルドし Wine で実行）。
- ✅ macOS：ビルド、テスト、Metal でのゲーム実行。aiversebuild → .app と iOS プロジェクト（+ シミュレーター向けビルド）。
- ✅ Web：ビルド可能（Emscripten 3.1.50）、ヘッドレス Chromium（WebGL2/SwiftShader）で対戦が動作。
- ⏳ Android：Gradle プロジェクトは生成される。APK とエミュレーターでの実行は CI（ジョブ `android`）にあり、ローカルではビルドしていない。
- ⏳ 実機での Windows と iOS — 未実行（CI では MSVC ビルドと iOS シミュレーター向けビルド）。

## プラットフォームと CI

`.github/workflows/ci.yml` は `master`/`agent-*` へのプッシュごと、およびプルリクエストで実行されます：

| ジョブ | 内容 |
|---|---|
| `linux` | gcc と clang（x64）+ gcc（ARM64）、警告はエラー扱い、テスト、ボット対戦、Xvfb でのスクリーンショット |
| `windows-msvc` | Visual Studio 2022、テスト、対戦、`aiversebuild --target windows` |
| `windows-mingw` | MinGW-w64 によるクロスビルド、Wine でのテストと対戦 |
| `macos` | ユニバーサル Xcode ビルド、テスト、対戦、`aiversebuild` → .app とシミュレーター向けビルド付きの iOS プロジェクト |
| `android` | NDK で `.so`、`aiversebuild` で APK。エミュレーターで：起動 → タップ → バックグラウンド → 復帰 →「戻る」→ 再起動、logcat でクラッシュを確認 |
| `web` | Emscripten、ヘッドレス Chromium での対戦、コンソールとフレームの確認 |
| `determinism` | 全プラットフォームの対戦チェックサム（2 ゲーム × 3 シード）を比較 — ビット単位で一致しなければならない |

スクリーンショット、APK、Web ビルド、.app は実行の成果物として保存されます。

ローカルで：

```bash
# Windows .exe from Linux/macOS
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64.cmake
# bot matches on every game → a checksum file (compare between machines with diff)
.github/scripts/headless-checksums.sh build/linux-release/aiverse_headless sums.txt
# smoke test of the web build
node platform/web/smoke.mjs build/web
```

Web ビルドではコマンドラインの引数を URL で渡します：`index.html?map=0&spectate` = `--map 0 --spectate`。

画面キーボード：`platform::startTextInput(area)` / `stopTextInput()` は iOS、Android、モバイルブラウザーで開閉します（デスクトップでは何も
しません）。テキストはどの言語でも `Char` イベントとして届き、Backspace/Enter は `KeyDown`/`KeyUp` として届きます。`softKeyboardHeight()` と
`KeyboardChanged` イベントは画面の下がどれだけ隠れているかを、`TextInputEnded` はユーザーが自分でキーボードを閉じたことを伝えます。UI
（`UiLayer`）は `<input type="text|password|search|email|number">` と `<textarea>` にフォーカスしたとき、これを自動で呼びます。

`Platform.h` にあるモバイルとブラウザー向けのその他の機能：
- `safeAreaInsets()` — カメラの切り欠き、角丸、ホームインジケーター、システムバーのための余白（iOS、Android、モバイル Safari/Chrome。
  デスクトップでは 0、ノッチ付き MacBook ではフルスクリーン時）。
- `setFullscreen()` / `isFullscreen()`。組み込みの切り替え — F11（Windows、Linux、Web）、Alt+Enter（Windows）。ブラウザーではフル
  スクリーン時に画面の向きも固定されます（`AppConfig::orientation`）。
- Android：戻るボタンは `Key::Escape` としてゲームに届きます。自動で隠れるバー付きのフルスクリーン。物理キーボードとマウス
  （Chromebook、DeX）は通常のキーとマウスのイベントになります。
- iPad：Magic Keyboard / Bluetooth キーボード、マウスとトラックパッド（右クリック、ホバー、スクロール）。
- クリップボードはどこでも使えます。ブラウザーではブラウザーがテキストを渡した後に Ctrl/Cmd+V がゲームに届きます。
- Web：`userDataDir()` は IndexedDB に保存され、設定とセーブデータはページを再読み込みしても残ります。

### ネイティブプラグイン：決済と広告

どのプラットフォームの SDK も、そのネイティブ言語のプラグインとして接続します — Java（Android）、Objective-C（iOS/macOS）、JavaScript
（Web）、C++ — 共通の非同期ブリッジ `engine/platform/Plugins.h` 経由です。ゲームは `engine/services` の共通 API `aiverse::store`（課金）と
`aiverse::ads`（広告）を使い、プロバイダーは `game.json` でプラットフォームごとに選びます。`plugins/` にある既成のプラグイン：**App Store**、
**Google Play**、**RuStore**、**Yandex Games**（Web：課金と広告）、**Steam**（Inventory Service と DLC による課金、Windows/Linux/macOS）、
**Yandex Mobile Ads**、**AdMob**、**AppLovin MAX**、**ironSource (Unity LevelPlay)**。ビルドに入るのはゲームが選んだプラグインだけです
（game.json の `"plugins"` または `--plugins`）。無関係な SDK、依存関係、マニフェスト項目、Info.plist のキーは入らず、CI がそれを確認します。
デスクトップでのデバッグ用にストアと広告のモック（`mock-store`、`mock-ads`。`AIVERSE_MOCK_STORE`、`AIVERSE_MOCK_ADS`）があり、購入の流れ全体を
スマートフォンなしで試せます。モックはリリースビルドにはコンパイルされません。aiversebuild はプラグインを Gradle/マニフェスト、
Info.plist/Podfile、HTML に自動で書き込みます。Google Play と RuStore は別々にビルドしてください：`--plugins googleplay,admob` と
`--plugins rustore,yandex-ads`。詳細：[plugins.md](../plugins.md)。

### リリース

`.github/workflows/release.yml`：タグ `v1.2.3`（または任意のゲームフォルダを指定した手動実行）で、`aiversebuild` によって全プラットフォームを
ビルドし GitHub Release を公開します：

| プラットフォーム | 成果物 | 署名（リポジトリのシークレット） |
|---|---|---|
| Windows | インストーラー（Inno Setup、管理者権限不要）+ ポータブル .zip | `WINDOWS_CERT_PFX_BASE64`、`WINDOWS_CERT_PASSWORD` |
| Android | Google Play 用の .apk と .aab | `ANDROID_KEYSTORE_BASE64`、`ANDROID_KEYSTORE_PASSWORD`、`ANDROID_KEY_ALIAS`、`ANDROID_KEY_PASSWORD` |
| macOS | .zip に入れた .app、公証済み | `MACOS_CERT_P12_BASE64`、`MACOS_CERT_PASSWORD`（Developer ID）+ App Store Connect のキー |
| iOS | Xcode プロジェクト。キーがあれば TestFlight 向けビルド | `APPLE_TEAM_ID`、`ASC_KEY_ID`、`ASC_ISSUER_ID`、`ASC_KEY_P8_BASE64` |
| Linux | .tar.gz | — |
| Web | .zip。`DEPLOY_WEB_PAGES=true`（リポジトリ変数）なら GitHub Pages に公開 | — |

シークレットがなくてもリリースは出ます：APK はデバッグキーで署名、.app は ad-hoc、Windows は署名なしです。

### ビルド済みエンジン（`builds` ブランチ）とコンパイラー不要の aiversemcp

ビルド済みのエディターとツールは **`builds`** ブランチで公開されます：`<platform>/AIVERSE-<version>-<commit>-<platform>.zip`（現在は
`windows` x86_64 と `macos` arm64。`linux` x86_64 はインテグレーターが公開したとき）。中にはそのコミット時点のエンジンのソース（テンプレート
`templates/`、`engine/assets`、`docs/` — MCP サーバーが読む）を含む `AIVERSE/` フォルダと、プログラムを入れた `AIVERSE/bin/` があります。
パッケージ化は `tools/release/package_engine.py`、公開は `tools/release/publish_builds.py`（AGENTS.md §8）。Linux パッケージはエディター
なしで、`aiversemcp` と `aiverse_headless` だけのこともあります。

展開したフォルダから MCP サーバーを起動（ボット対戦と `balance_*` は aiversemcp の中で動くので別の `aiverse_headless` は不要。エディターは
任意で、なければ `--no-editor`）：

```bash
# ARCHIVE is the path to the AIVERSE archive downloaded from index.json.
unzip "$ARCHIVE" -d ~/aiverse      # unzip keeps the executable bit
~/aiverse/AIVERSE/bin/aiversemcp --no-editor --engine ~/aiverse/AIVERSE
```

aiversemcp が自分の隣でエンジンを探すようになったコミット（`engine/platform/EngineDir.h`、エディターと同じ）以降のビルドでは `--engine` を
省略できます。それより前に公開されたビルドでは必須です。権限を復元しない展開ツール（Python の `zipfile`）を使った場合は、`bin/` のファイルに
`chmod +x` を実行してください。macOS では、ブラウザーでダウンロードしたファイルが隔離されることがあります：
`xattr -dr com.apple.quarantine ~/aiverse/AIVERSE`。

エンジンを自分でダウンロードするプログラムは、README ではなくブランチのルートにある **`index.json`**
（`https://raw.githubusercontent.com/chromealex/AIVERSE/builds/index.json`）を読みます。以下は過去の RTSEngine のエントリの例です。新しい
ビルドではパスに AIVERSE が含まれ、キー `rtsmcp` は維持されます：

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

- `packages` は新しい順。必要な `platform` + `arch` に一致する最初のエントリが最新のビルドです。
- `platform`：`windows` | `macos` | `linux`。`arch`：`x86_64` | `arm64` | `universal`（macOS のユニバーサル）| `null`（判別不能）—
  zip 内の `aiversemcp` のヘッダーから判定。
- `rtsmcp`、`editor`、`headless` はアーカイブ内のパス、またはパッケージにそのプログラムがなければ `null`。`--engine` は `root` フォルダです。
  macOS では `editor` は `.app` バンドルです。
- `url` は公開のたびに書き換えられるブランチを指します（プラットフォームごとに 5 バージョンを保持）。古いエントリは消えるので、最新の
  `index.json` からダウンロードし `sha256` を確認してください。
- 新しいフィールドが追加されることがあります。互換性のない形式変更では `schema` が上がります。

確認：`python3 tools/release/test_release.py [build/linux-release/aiversemcp]`（CTest `release_tools`）— `index.json` の形式と、aiversemcp
へのパスを渡した場合は Linux パッケージの作成、一時フォルダへの展開、そこから `--engine` なしでの MCP 呼び出し（`tools/list`、
`create_project`、`map_generate`、`map_validate`、`map_ascii`）。

スクリーンショットモード（`AIVERSE_SCREENSHOT`、`AIVERSE_EXIT_AFTER_FRAMES`）は 1 フレームあたりちょうど 1/60 秒進むので、N 番目の
フレームはどのマシンでも同じです。

## 今後

glTF モデルとアニメーション、サウンド、ネットワーク（ロックステップ）、データで定義する能力とバフ、大軍向けのフローフィールドと RVO、AI の改善、
C# スクリプト（iOS では AOT）。

## C++ のゲームモジュール

Lua に加えて、プロジェクトには `native/CMakeLists.txt` と C++20 のモジュールを含められます。ビルド時に `aiverse_native_module(...)` で追加し、
`rules.json` の `nativeModules` 配列で有効にします。tick、コマンド、ロールバック、状態の保存に対応し、Lua と C++ は併用できます。C++ を変更
したらゲームとエディターの再ビルドが必要です。[API、例、ビルド手順](../native-modules.md)。

## 再利用できる C++ のメタライブラリ

`aiverse::meta` — 設定可能なショップ/オファー/クラフト、通貨とアイテム、クエストとパス、デイリー報酬、スタミナ、メール、ランキング、チャット、
セグメント。Lua/GPU なしでプロジェクトのネイティブコードから使え、定義は JSON なのでアルゴリズムを生成し直す必要はありません。開始：
`python3 tools/meta/scaffold.py --preset mobile --out <project>`。ドキュメント：[meta-game.md](../meta-game.md)。エージェント向けは
`skills/meta-game/SKILL.md`。ローカルのポリシーはサーバーなしで動きますが、端末間の通信やグローバルランキングには実際のバックエンドが必要です。
完成したクラウドサービスはパッケージに含まれません。

プリセット `full --with-ui` はフレンド/クラン、インスタンスのコレクション、装備、合成、サブスクリプション、トーナメントリーグを追加します。
`aiverse::meta::Service` は操作をアトミックに実行し、`tools/meta/server.py` はセッション検証付きのローカル HTTP/SQLite ホストです。ブラウザー用の
メタ画面と、エージェント向けの短いレシピのカタログが付属します。[ビルド、起動、制限](../meta-service.md)。

`full` には LiveOps の 15 グループも含まれます：天井付きの宝箱、アルバム/交換、ジョブキュー、研究/プレステージ、複合クエスト、イベント、クラン
戦、非同期 PvP、パーティのマッチメイキング、Elo/Glicko-2、紹介/プロモコード、リワード広告、オファーの連鎖、通知、オフライン収益。すべての
パラメーターは `extensions.liveops` にあり、このプリセットは独立した新しい UI 画面や外部 SDK をインストールしません。
`python3 tools/meta/catalog.py describe matchmaking` は必要なスキーマと例だけを出力します。[API、ポリシー、制限](../meta-liveops.md)。

### オプションの 3D 物理

Box3D はフラグ `AIVERSE_ENABLE_PHYSICS=ON` で有効になります。通常の RTS プロジェクトには不要です。独立した例 `aiverse_physics_demo` で、
剛体、衝突、振り子、状態の保存を確認できます。[ビルド、API、統合の制限](../physics.md)。

### ビジュアル用の XPBD クロス

オプションモジュール `AIVERSE_ENABLE_CLOTH=ON`：独自の XPBD クロスソルバーを CPU または Metal compute で動かし、共有バッチ、時間方向の LOD、
自動スリープ、高密度メッシュを 1 回のインスタンス描画で描く機能を備えます。GPU では位置はビデオメモリに置かれたままで、対応していない
バックエンドは CPU を使います。例では GPU と CPU の参照結果の比較や、スリープ/ウェイクの確認ができます。
[独立した例、制限、測定値](../cloth.md)。
