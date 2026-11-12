# 「ゲームを作る」ウィザード

1 つのウィンドウ（プロジェクトハブ → **ゲームを作る**、または **プロジェクト → ゲームを作る…**）か、1 回の MCP 呼び出し（`game_make`）で、
`.aiverse` を備えたゲームプロジェクト一式ができあがります。アイデアから選ばれたテンプレートのゲーム、ユーザーのアート / モデル / 音楽 /
効果音、強制チュートリアルのマップ、対戦中のさりげないヒント、生成されたレベル、メタゲーム（メニュー、アップグレード、ショップ、
デイリー報酬、プロフィール、設定、リザルト）、希望に応じた課金と広告、言語、ブランディング、そしてボットによる検証です。書き出される
ものはすべて通常のゲームデータなので、あとから自由に編集できます。

コード：`apps/editor/core/GameWizard*.cpp`（UI なし）、`apps/editor/core/WizardOps.cpp`（MCP）、`apps/editor/EditorGameWizard.cpp`
（ハブのページ）。各ゲームにコピーされるランタイムキット：`templates/_wizard/`（Lua + RmlUi、README を参照）。

## 回答（`WizardSpec`、すべて任意）

| グループ | キー |
|---|---|
| コンテンツ | `artDir`（フォルダ：png/jpg/tga、glb/gltf/obj/fbx、wav/ogg/mp3/flac、ttf/otf、md/txt）、`artUrl`（ファイルまたは .zip / .tar.gz。システムの curl で `~/.aiverse/wizard/<時刻>` にダウンロード） |
| アイデア | `idea`（任意の言語。フォルダ内の .md/.txt も読み込む）、`genre`（テンプレート ID または `auto`）、`features`（economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait scifi modern） |
| プロジェクト | `title`、`location`、`studio`（bundle id `com.<studio>.<slug>`）、`version`、`orientation`、`languages`、`platforms`、`seed` |
| チュートリアル | `tutorial`（`both` / `hard` / `soft` / `none`）、`tutorialForced`、`tutorialSkippable`、`tutorialDim`、`hints` |
| レベル | `levels`（0..40）、`mapPreset`（`auto`、`none` = ベースマップのコピー、またはジェネレーターのプリセット）、`mapSize`、`bossEvery`、`difficultyCurve`、`startResourcesPercent`、`keepTemplateMaps`、`verifyLevels` |
| メタ | `metaIdea`（自由記述：メタに入れたいもの。エージェントのタスクに渡される）、`currencies`（soft、hard）、`upgrades`、`shop`、`daily`、`profileScreen`、`xpPerLevel` |
| 収益化 | `ads`、`interstitialEvery`、`purchases`、`stores` |
| 見た目 | `brandingFromArt`、`paletteFromArt` |

`game_make_options` は選択肢を一覧表示し、`game_make_plan` は何が作られるかを（ファイルを書かずに）示します。同じ回答 + seed = 同じゲーム。

## 判断のしかた

- **テンプレート**：アイデアの単語を各テンプレートのキーワード（語幹、ru / en）と、希望する機能をそのメカニクスと、画面の向きをその画面と
  照合します。同点なら seed で決まります。何も指定がなければ、バランスのよいテンプレートのいずれか。
- **コンテンツ**：モデルと 2D スプライトは、ファイル名に ID / 名前（ru / en）が含まれるタイプに割り当てられます。残りはコピーされ
  `WIZARD.md` に列挙されます。キーアート（名前とサイズで判断）はスプラッシュ画面、メニュー背景、UI のアクセントカラーになり、最も正方形に
  近い画像（または「icon」/「logo」）は 1024 アイコンになります。音楽は名前に応じて `menu` / `match` / `battle` / `victory` / `defeat` に、
  短い効果音はイベント（`click`、`select`、`move`、`died`、`underAttack` など）に割り当てられます。最初の TrueType フォントは
  `fonts/title.ttf` になります。
- **強制チュートリアル**（`maps/wiz_tutorial.json` + `scripts/wizard/tutorial_steps.lua`）はルールから自動で書かれます：
  - *経済*（労働者、サイドバーの造船所、展開式 MCV、TA の建設者）：展開、採集、必要ならワープフィールド / 補給、兵舎のような生産施設の
    前提条件チェーン、2 体のユニットを訓練、反撃してこない敵の建物を破壊；
  - *駐屯*（Mushroom Wars、State.io）：中立の建物へ送る、さらに 2 つ奪う、敵の建物を奪う；
  - *自動建設*（レーンバトル）：「建設」を 2 回押す、アップグレード、守り抜く；
  - *汎用*：軍を動かす、攻撃する。
  そのステップのコマンドだけが通り（`before_command`）、敵は待機します（ただしユニットは防御します）。パネルと矢印はビュースクリプトが描き、
  詰まらないようにステップに必要な資源は補充されます。Knights and Merchants 型の道路経済には向きません（その場合は汎用形式）。
- **さりげないヒント**：`scripts/wizard/hints_config.lua` からの一度きりのヒント（カメラ、手の空いた労働者、資源不足、人口上限、軍、
  駐屯、攻撃を受けている）。表示済みのヒントはプロフィールに記録され、設定でオフにできます。
- **レベル**：`maps/wiz_level_N.json` = ベースマップ + `generate`（テンプレートのプリセット、レベルごとの seed、ベースマップを雛形にする）。
  難易度は曲線に沿って 0..2、N レベルごとにボス。報酬、初勝利報酬、星の獲得タイムは `ui/wizard.json` に入ります。
- **メタ**：`ui/wizard.json`（設定）、`ui/profile.json`、`ui/shop.json`、`ui/screens.json`（`start: home`、`afterMatch: result`）、
  キットの画面。アップグレードは `initialVars` `wiz_up_<id>` として対戦に渡り、`scripts/wizard/meta_bonus.lua` がバフ / 初期資源に変換します。
- **収益化**：`game.json` にプラットフォームごとの `plugins` / `store` / `ads`（Yandex のデモ広告枠とデスクトップ用モック。ID は差し替えてください）。

## エージェントのステップ：記述どおりのゲームを 1 対 1 で

テンプレートは出発点であって、ゲームそのものではありません。`agent` がオン（既定）のとき、ウィザードはプロジェクトに `WIZARD_TASK.md` を
書きます。中身は説明 / GDD の原文と、ウィザードが作った各部分（チュートリアルのステップ、ヒント、メタ設定、言語ファイル）の整合を保つ方法です。
これを接続済みの AI エージェントに渡すと、エージェントはエディターの MCP ツールを通じて（エディター上でリアルタイムに）プロジェクトを
作り直します。タイプ、能力値、勢力、メカニクス（ルールまたは Lua）、マップ、勝利条件、テキスト。その後 `rules_lint`、`map_validate`、
`match_run` で確認し、WIZARD.md に「## Агент: что сделано」を書き込みます。

- 接続：ウィザードには Agent ウィンドウと同じ「AI エージェントを接続」パネルが表示されます（`EditorMcpConnect.*`、`core/McpInstall.h`）。
  1 クリックで aiversemcp がクライアントの設定に書き込まれ、その 1 つの接続を両方で使います。
- エディターからの起動（`core/WizardAgent.*`、CLI が見つかるようにユーザーのログインシェル経由）：Claude Code
  `claude -p --output-format stream-json --allowedTools mcp__aiverse` と Codex CLI `codex exec --full-auto`。タスクは stdin に渡され、
  作業ディレクトリはプロジェクトフォルダです。CLI は PATH、一般的なインストール先、ユーザーのシェル、デスクトップアプリ内
  （Claude Code は `~/Library/Application Support/Claude/claude-code/<version>/…/claude.app`、Codex は
  `ChatGPT.app/Contents/Resources/codex-cli`）から探します。進捗はアクティビティフィード（Agent ウィンドウ）、ステータスバー、ウィザードの
  ページ（停止ボタン）に表示され、完全なログは `logs/wizard-agent-<時刻>.log` に残ります。
- Cursor、Windsurf、Claude Desktop：「エージェント用タスクをコピー」でタスクをクリップボードに入れ、クライアントに貼り付けます。
- エディターに API キーは不要：エージェントはユーザー自身のクライアントとアカウントで動きます。
- 音声入力：アイデア欄（「アイデア」ページ）とメタのアイデア欄（「メタ」ページ）の下の「話して入力」— macOS の音声認識
  （`apps/editor/EditorSpeech_mac.mm`：Speech + AVFoundation、ゲームの第一言語をロケールに使用。マイクと音声認識の許可は初回に一度だけ
  求められます）。テキストは欄に追記されます。他のプラットフォームではボタンは無効です。
- 仕様の `ai`（言語ごとのテキスト：キャッチコピー、導入文、タイプ、資源、通貨、アップグレード、レベル）を使うと、自分で `game_make` を
  呼ぶエージェントがゲームの名前やテキストを渡せます。

エディターの Game ビューでのチュートリアル：エディターにはステップパネルを表示するゲーム UI がないため、ビュースクリプトが `wiz_tut_off` を
送り、チュートリアルマップは通常の対戦として遊ばれます。パネル、矢印、コマンドの絞り込みはゲームクライアント（AiverseGame、ビルド）で
動作します。エディターが開くのはチュートリアルではなく最初のレベルです。

## 検証

`verifyLevels` を有効にすると、ウィザードはチュートリアルを自分でクリアし（`initialVars {wiz_tutorial_auto: 1}`：各ステップの `auto`
命令はプレイヤーと同じフィルターを通ります）、ボットでいくつかのレベルを抜き取りプレイして、読み込みエラー、スクリプトエラー、終わらなかった
チュートリアルを報告します。全テンプレートで実施済み：ambersands（岩の上にしか建てられない）、planets と underlord（特殊な地形での移動 /
掘削）以外はすべて完走します。ウィザードは警告でそのことを伝えます。

テスト：`game_wizard_plan_picks_a_template_by_the_idea`、`game_wizard_makes_a_playable_project`、
`script_command_card_false_hides_the_button`。

## エンジンの変更

スクリプトコマンドは `"card": false` を受け付けます（ボタン / ホットキーなし。ビュースクリプト、HUD ドキュメント、MCP から送信）。
チュートリアルの「次へ / スキップ」に使われます（`modules/rts/RtsScriptCommands.cpp`、クライアントとエディターのコマンドカード）。
