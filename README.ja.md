<p align="center">
  <img src="docs/images/aiverse-logo-original.png" alt="AIVERSE" width="480">
</p>

<h3 align="center">あらゆるジャンルのためのゲームエンジン。AI エージェントがコードではなくデータでゲームを作る</h3>

<p align="center">
  あらゆるジャンル · 完全な RTS / FPS モジュール · 88 のメカニクスライブラリ · 決定論的シミュレーション · 約 190 の MCP ツール · 6 プラットフォーム
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-iOS%20·%20Android%20·%20Web%20·%20Windows%20·%20macOS%20·%20Linux-444">
  <img alt="Rendering" src="https://img.shields.io/badge/rendering-Metal%20·%20GLES3%20·%20WebGL2%20·%20Vulkan-6a3de8">
  <img alt="MCP" src="https://img.shields.io/badge/AI%20agents-MCP-d97757">
  <img alt="MIT" src="https://img.shields.io/badge/license-MIT-2ea44f">
  <a href="https://chromealex.github.io/AIVERSE/en/"><img alt="Website" src="https://img.shields.io/badge/website-chromealex.github.io%2FAIVERSE-111?logo=googlechrome&logoColor=white"></a>
  <a href="https://t.me/aiverseengine"><img alt="Telegram" src="https://img.shields.io/badge/Telegram-@aiverseengine-26A5E4?logo=telegram&logoColor=white"></a>
</p>

<p align="center">
  <a href="README.md">English</a> · <a href="README.ru.md">Русский</a> · <a href="README.zh-CN.md">简体中文</a> · <b>日本語</b> · <a href="README.ko.md">한국어</a> · <a href="README.es.md">Español</a> · <a href="README.pt-BR.md">Português</a> · <a href="README.de.md">Deutsch</a> · <a href="README.fr.md">Français</a>
</p>

<p align="center">
  <a href="#なぜ必要か">なぜ</a> ·
  <a href="#あらゆるジャンル">ジャンル</a> ·
  <a href="#仕組み">仕組み</a> ·
  <a href="#すぐに使えるテンプレート">テンプレート</a> ·
  <a href="#クイックスタート">クイックスタート</a> ·
  <a href="docs/ja/overview.md">詳細な概要</a>
</p>

---

## スクリーンショット

<p align="center">
  <img src="docs/images/readme/sponza.jpg" alt="Sponza" width="860"><br>
  <sub>Sponza：PBR、影、アンビエントオクルージョン</sub>
</p>

<p align="center">
  <img src="docs/images/readme/sponza-light-shafts.png" alt="Sponza light shafts" width="860"><br>
  <sub>霧の中の光芒：オフ / オン</sub>
</p>

<p align="center">
  <img src="docs/images/readme/city-roads.jpg" alt="City" width="860"><br>
  <sub>エージェントが <code>aiverse-roads</code> と <code>aiverse-city-blocks</code> スキルで作った街：スプライン道路、横断歩道、街区</sub>
</p>

---

## 概要

**AIVERSE** は、ひとつの目的のためにゼロから書かれた C++20 エンジンです。**どんなジャンル**のゲームも、
一からプログラミングするのではなく、**記述する**だけで作れるようにすること。

ゲームはデータのフォルダです。オブジェクト、ルール、レベル、UI、サウンド、エフェクト。エンジンは
それらのデータに命を吹き込む、決定論的なメカニクスのライブラリです。基地建設や数千ユニットの軍勢から、
プラットフォームアクション、近接戦闘、会話、カードのルール、パズルまで。その間にエディターと MCP サーバーがあり、
人間や AI エージェントがそれを通じてプロジェクトに取り組みます。ゲームを作り、バランスを調整し、
ボット同士の対戦を回し、スクリーンショットを確認し、ストア向けビルドを出力します。

> アイデアを伝えてアート素材のフォルダを指定するだけ。**「ゲームを作る」**ウィザードが、レベル、
> チュートリアル、メタ、ショップ、アイコン、スプラッシュ画面を備えたプロジェクトを作り、
> ボットでチュートリアルを通しプレイして、すべてのレベルを自分で検証します。

---

## なぜ必要か

ゲーム作りを難しくしているのはグラフィックではありません。難しいのはその下にあるもの、つまり移動、戦闘、AI、
ネットワーク、バランス、成長要素です。汎用エンジンはレンダラーを提供し、残りはすべてあなた任せです。
AIVERSE はメカニクスそのものを提供し、エージェントがゲームに取り組めるようにします。

| 課題 | AIVERSE の解決策 |
|---|---|
| **汎用エンジンはゲームプレイを提供しない。** Unity、Unreal、Godot はレンダリングと物理を提供しますが、大群の経路探索、コンボとパリィのある戦闘、敵 AI、ターン順、カードのルール、経済は、どのスタジオも数か月かけて自作しています。 | メカニクスがエンジンの構成要素です。完全な RTS / FPS モジュールに加え、他ジャンル向けの 88 の決定論的ライブラリ。データで設定し、足りなければ Lua やプロジェクト独自の C++ で拡張します。 |
| **従来のエンジンは AI エージェントに向いていない。** バイナリのシーン、すべてが GUI 経由、素早いフィードバックがない。エージェントは手探りで作業することになります。 | プロジェクトは読めるテキストファイルです。約 190 の MCP ツールがマップ、ルール、地形、UI、エフェクト、ライティング、ビルドをカバー。エージェントは結果を自分で検証できます。ボット対戦、合否判定付きシナリオ、バランス表、「did you mean…」と提案するルールリンター、スクリーンショット。 |
| **マルチプレイは同期ズレとの終わりなき戦い。** `float` がひとつあるだけで、iPhone と Android の対戦はずれていきます。 | シミュレーションはどこでもビット単位で同一です。浮動小数点ではなく固定小数点、独自の乱数生成器、チェックサム。CI はプッシュのたびに Linux x64/ARM64、Windows、macOS の対戦結果を照合します。ネットワークに流れるのはコマンドだけ。ロールバック付きロックステップと、マッチメイキングとリプレイ判定を備えたサーバーがあります。 |
| **モバイル GPU は非力なのに、ゲームは美しくなければならない。** | 影、パーティクル、地形、ポストエフェクトなど、負荷の高い機能はすべて品質レベルを持ち、古い Mali や Adreno から Apple Silicon まで GPU に合わせて自動調整されます。 |
| **全プラットフォームでのリリースはそれだけで一大プロジェクト。** 署名、アイコン、ストアや広告の SDK。 | `aiversebuild` ひとつのコマンドで .app、Xcode プロジェクト、Gradle/APK、Web、.exe を作成。アイコンとスプラッシュ画面は自動生成。App Store、Google Play、RuStore、Yandex Games、Steam、AdMob、AppLovin などのプラグインを用意し、ビルドにはゲームが選んだものだけが含まれます。 |
| **新しいゲームは毎回ゼロから。** | 30 以上のプレイ可能なテンプレート、受け入れチェックリスト付きのジャンルキット、エージェント向けの 238 のスキル。新しいゲームは動くゲームから始まります。 |
| **メタと LiveOps はプロジェクトごとに書き直し。** | `aiverse::meta`：ショップ、通貨、クエスト、パス、デイリー報酬、スタミナ、クラン、トーナメント、天井付きの宝箱。JSON で設定します。 |

---

## あらゆるジャンル

2 つのジャンルには、エディター、ボット、テンプレートを備えた完全なエンジンモジュールがあります。それ以外は
メカニクスライブラリを組み合わせて作ります。どのライブラリも決定論的でテスト済み、セーブ/復元に対応し、
検証可能な設定を持っています。

| ジャンル | エンジンが提供するもの |
|---|---|
| **RTS、タワーディフェンス、陣取り** | 完全な `rts` モジュール：データ駆動のユニットと建物、最大 2048² のマップで数千ユニットの軍勢を動かせる A\* / HPA\* / フローフィールド、経済、建設、戦場の霧、AI、地形と惑星、26 のテンプレート |
| **FPS と TPS** | 完全な `fps` モジュール：クライアントと権威サーバー、最大 100 人のストリーミング大規模ワールド、ボット、4 つのテンプレート。`weapon-policies`（反動、弾道、貫通）、`match-rules`（ラウンド、ペイロード、爆破、脱出） |
| **アクション、格闘、ソウルライク** | `combat-actions`、`melee-combat`：ヒットボックスとハートボックス、コンボとキャンセル猶予、回避の無敵時間、パリィ、体幹、ロックオン。対戦相手には `action-bots` |
| **プラットフォーマー、メトロイドヴァニア** | `character-locomotion`：コヨーテタイム、ジャンプ先行入力、崖つかまり、はしご、水泳、壁登り、乗り越え。`zone-graph`：能力ゲート、チェックポイント、詰み状態がないことの証明 |
| **ステルス、イマーシブシム** | `stealth-senses`：視界の円錐、聴覚、疑念と警戒、捜索、変装。`physical-interactions`：持ち運び、投てき、ロープ、ドア、動く足場、破壊可能オブジェクト |
| **ストーリーゲーム、ビジュアルノベル** | `narrative-graphs`：条件付き会話、好感度、結果の分岐、VN のバックログとオート送り。`cinematic-gameplay`：タイムライン、QTE、スローモーション |
| **ターン制タクティクス、カード、ボードゲーム** | `turn-scheduler`：イニシアチブ、行動ポイント、リアクション。`card-runtime`：ゾーン、優先権スタック、非公開情報 |
| **パズル** | `grid-puzzles`：倉庫番のルール、アンドゥ/リドゥ、ソルバー。`puzzle-machine`：プログラミングパズル用の仮想マシン |
| **ローグライク** | `run-graph`：シード付きのラン、エンカウント予算。レア確定枠付きの `loot-tables` |
| **放置、工場、コロニー** | `big-numbers`、`offline-production`（正確なオフライン進行）、`production-networks`、`logistics`、`tasks` |
| **リズムゲーム** | `rhythm-timing`：テンポマップとフレームレートに依存しない判定 |
| **レース、乗り物** | `vehicle-controllers`：車（トルク、ギア、トラクション、ドリフト）、戦車、ホバークラフト、飛行機、座席、ダメージゾーン；レースセッション：スターティンググリッド、カウントダウン、順番どおりのチェックポイント、周回、順位、逆走、レースボット；どのプロジェクトでも使えるマップの `race` ブロック、共通 HUD とチェイスカメラ、テンプレート 2 個 |
| **あらゆるモバイルゲーム** | `aiverse::meta` と LiveOps、チュートリアル、UI 画面、ストアと広告のプラグイン |

スポーツゲーム、ボート、ピットストップ、ペナルティはまだ[ロードマップ](docs/genre-mechanics-roadmap.md)上にあります。ライブラリの全一覧：
[`libraries/catalog.json`](libraries/catalog.json) と[ジャンルパッケージ](docs/mechanics-packages.md)。

---

## 仕組み

```mermaid
flowchart LR
    A["アイデア、アート、音楽"] --> B["「ゲームを作る」ウィザード<br/>または MCP 経由の AI エージェント"]
    B --> C["プロジェクト = データ<br/>game.json · rules.json<br/>レベル · ui · fx · audio"]
    C --> D["AIVERSE エンジン<br/>ジャンルモジュール · ライブラリ<br/>レンダリング · ネットワーク · サウンド"]
    D --> E["検証<br/>ボット · シナリオ · バランス<br/>スクリーンショット · リプレイ"]
    E -- "修正" --> C
    D --> F["aiversebuild<br/>iOS · Android · Web<br/>Windows · macOS"]
```

オブジェクトはただの記述です。これは RTS テンプレート `skirmish` の農民で、金と木を集め、
5 種類の建物を建て、つるはしで戦います。

```jsonc
"peasant": {
  "health":  { "max": 220, "armorType": "light" },
  "move":    { "speed": 2.4, "radius": 0.35 },
  "sight":   7,
  "weapons": [{ "id": "pick", "range": 0.5, "damage": 5, "cooldown": 1.5 }],
  "cost":    { "gold": 75, "time": 12, "supply": 1 },
  "gather":  { "gold": { "carry": 10, "time": 2.0 }, "wood": { "carry": 10, "time": 3.0 } },
  "builds":  ["townhall", "farm", "barracks", "workshop", "tower"],
  "view":    { "shape": "worker", "scale": 0.8 }
}
```

データで足りないときは 3 段階で拡張できます。JSON の**トリガー** → **Lua**（決定論的な数学とホットリロード
を備えたサンドボックス）→ 上記のライブラリを取り込めるプロジェクト独自の**ネイティブ C++ モジュール**。
エンジン自体は変わりません。メカニクスは汎用的な形でのみエンジンに追加され、特定のゲーム専用にはなりません。

---

## Unity からの移行

インポーターに Unity プロジェクトを指定すると、AIVERSE のプロジェクトができあがります。アセット、シーン、パラメーターは自動で移行され、それ以外はレポートにまとめられ、エージェントが MCP 経由で移行を仕上げます。エディターの **プロジェクト → エンジンプロジェクトをインポート**、MCP の `port_scan` / `port_import` / `port_report` / `port_resolve` からも使えます。

```bash
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/MyGame --mode auto
```

| Unity では | AIVERSE では |
|---|---|
| シーン | マップ `maps/*.json`：オブジェクト配置、カメラ、2D/3D モード |
| プレハブ、ネストしたプレハブ、バリアント | `rules.json` のオブジェクトタイプ。シリアライズされたフィールド（`hp`、`speed`、`damage`、`fireRate` など）はデータブロックになる |
| ScriptableObject | ルールのデータ |
| C# の MonoBehaviour ロジック | エージェントが移植：データブロック → Lua → ネイティブ C++ モジュール |
| モデル、テクスチャ、マテリアル、サウンド | そのままコピー。PBR マテリアル、glTF/FBX/OBJ は直接読み込み |
| スプライトと Tilemap | アトラスのスプライトとタイルマップ |
| ParticleSystem | `fx/*.json`。ほぼすべてのモジュールに対応 |
| Inspector、Hierarchy、Scene、Game | AIVERSE エディターの同じウィンドウ |
| Build Settings、Unity IAP、Unity Ads | `aiversebuild`、ストアと広告のプラグイン |
| Addressables / Asset Bundles | `.rpak` パック、ビルド分割とコンテンツのダウンロード |

レポートでは各項目が「正確に移行」「近似で移行」「未移行」に分類されます。再インポートしても編集内容は保持されます。アセットのシリアライズは **Force Text** が必要です。Godot プロジェクトも同じ方法でインポートできます。詳細：[エンジンプロジェクトのインポート](docs/ja/engine-import.md)。

---

## すぐに使えるテンプレート

どのテンプレートもボット付きで完全にプレイ可能なゲームです。エディターで開いてすぐに改造できます。

テンプレートは[カタログ](https://chromealex.github.io/AIVERSE/catalog.html)のパッケージです。スタート画面に公式テンプレートが並び、**More templates** でほかのテンプレートを検索できます。

| ジャンル | テンプレート |
|---|---|
| 古典的な基地建設 RTS | **Iron and Gold**（Warcraft / Red Alert）· **Iron Front**（Red Alert：MCV、鉱石、サイドバー）· **Starfall Sectors**（StarCraft：3 種族）· **Frost Crown**（Warcraft III：ヒーロー、クリープ、地形）· **Four Ages**（Age of Empires II）· **Amber Sands**（Dune II）· **Metal Tide**（Total Annihilation：フロー型経済、レーダー、回収、航空機、核ミサイル） |
| 占領と駐屯 | **Mushroom Clash** と **Shroom Wars**（Mushroom Wars 1–2）· **Spore Lanes** · **Spell Bunkers**（Bunker Wars）· **Border Rush**（State.io）· **Clan Lands**（Northgard）· **Flag Sectors**（Z） |
| 分隊、ウェーブ、サバイバル | **Hold the Line**（Company of Heroes）· **Last Colony**（They Are Billions）· **Lane Clash**（レーンバトル） |
| 経済と建設 | **Hearth and Haul**（Knights and Merchants）· **Underlord**（Dungeon Keeper） |
| 2D と実験 | **Crystal Dunes**（タイル、Dune II）· **Green Hills**（3D 地形上の 2D スプライト）· **Planets**（球体の惑星と軌道）· **Wildfire**（フィールド：山火事とクリープ） |
| FPS | **Crossfire Arena**（5 対 5）· **Breach Protocol**（タクティカルラウンド）· **Last Survivor**（100 人バトルロイヤル）· **Aim Lab**（射撃場） |
| レース | **Oval Sprint**（3 体のボットと競う周回レース）· **Road Circuit**（スプライン道路でのレース） |

---

## 中身

<table>
<tr>
<td width="50%" valign="top">

**ゲームプレイ**
- 固定小数点の ECS シミュレーション、リプレイ、ロールバック用スナップショット
- ジャンルモジュール：RTS（経済、建設、軍勢、AI）と FPS（権威サーバー、大規模ワールド）
- 88 のライブラリ：移動、戦闘、武器、知覚、会話、ターン、カード、パズル、ラン、リズム、生産
- 経路探索：A\*、HPA\*、フローフィールド、ナビメッシュ、ORCA/RVO2
- 傾斜と視線を考慮した地形、最大 2048² のマップ、球体の惑星
- トリガー、Lua 5.4、ネイティブ C++ モジュール

</td>
<td width="50%" valign="top">

**グラフィックとサウンド**
- 独自 RHI：Metal、GLES3 / WebGL2、GL 3.3、Vulkan（Windows / Linux / Android、選択制）
- PBR、glTF/OBJ、スケルタル/プロシージャルアニメーション、プロシージャルケーブル、親子エンティティの描画、物理ベースの空、ボリューメトリック雲と光芒、実行時に晴らせる球体/OBBの局所ボリューメトリック霧
- 影、動的ライト、夜、ポストエフェクト、Shader Graph
- Unity ParticleSystem のモジュールに対応したパーティクルと Unity からのインポーター
- 2D：スプライト、オートタイル付きタイルマップ
- サウンド：3D、アダプティブミュージック、プラットフォーム別圧縮

</td>
</tr>
<tr>
<td valign="top">

**ツール**
- Unity 風のドッキング式エディター：シーン、インスペクター、地形、エフェクト、ライティング、UI、テストプレイ、デバイスシミュレーター
- `aiversemcp`：エージェント向け MCP サーバー — マップの PHYSIS 設定、検証、Undo/Redo
- **「ゲームを作る」**ウィザード
- エージェント向けの 238 のスキルと、受け入れチェックリスト付きのジャンルキット
- 手持ちのコピーから名作ゲームのアセットを取り込むインポーター

</td>
<td valign="top">

**リリース**
- `aiversebuild`：.app、Xcode、Gradle/APK、Web、.exe
- `.rpak` リソースパック（zstd）、ビルド分割とコンテンツのダウンロード
- ストアと広告のプラグイン
- HTML/CSS によるゲーム内 UI（RmlUi）、ローカライズ
- マルチプレイ：UDP と WebSocket、専用サーバー、Elo マッチメイキング
- メタライブラリと LiveOps

</td>
</tr>
</table>

---

## クイックスタート

**1. エンジンをビルドする**（macOS。Xcode と CMake が必要）：

```bash
./studio_mac.command                    # AIVERSE Studio（エディタ。Xcode、CMake、Node.js が必要）
./studio_mac.command ~/.aiverse/packages/templates/mushrooms   # ……別のプロジェクトで
```

Windows：`studio_windows.cmd [project]` で `AiverseStudio.exe` をビルドして開きます（Visual Studio 2022、
CMake、Node.js が必要。WebView2 Runtime と Vulkan で動作）。

`studio_mac.command` は Web UI とネイティブの Studio をビルドして開きます。エンジンだけなら：
`cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release && cmake --build build/mac -j 8`。従来の
ImGui エディタ（`build/mac/AiverseEditor.app`）も引き続きビルドされ、Linux と Windows で動きます。

Linux：`cmake --preset linux-release && cmake --build build/linux-release`。コンパイルしたくない場合は、
`builds` ブランチにビルド済みのエディターとツールがあります
（[詳細](docs/ja/overview.md#ビルド済みエンジンbuilds-ブランチとコンパイラー不要の-aiversemcp)）。

**2. ゲームを作る。** ハブで**「ゲームを作る」**を押すか、テンプレートからプロジェクトを作成します。
Game ウィンドウですぐにボットと遊べます。

**3. AI エージェントを接続する。** エディターの **Agent** ウィンドウに設定がそのまま表示されます。Claude Code の場合：

```bash
claude mcp add aiverse -- /path/to/aiversemcp --engine /path/to/AIVERSE
```

あとはエージェントにこう頼むだけです：*「冬のマップで Northgard 風のストラテジーを作って、
3 勢力のバランスをボットで確認し、Web 版をビルドして」*

**4. リリースする：**

```bash
./build/mac/aiversebuild --project "My Game.aiverse" --target all   # macOS、iOS、Android、Web、Windows
```

---

## ステータス

エンジンは活発に開発中です。検証済みの項目：

- ✅ **Linux**：クライアント、エディター、テスト、ボット対戦。x64 と ARM64 でチェックサムが一致。クライアントは Vulkan（Mesa lavapipe）でも動作
- ✅ **macOS / Metal**：ビルド、テスト、ゲーム。`aiversebuild` → .app とシミュレーター向けビルド付きの iOS プロジェクト
- ✅ **Web**：ヘッドレス Chromium（WebGL2）で対戦が動作
- ✅ **Windows**：MinGW でクロスビルドした .exe を Wine で実行
- 🛠 RTS と FPS 以外のジャンルのテンプレートは制作中

サブシステムごとの詳細は
[検証済み / 未検証](docs/ja/overview.md#検証済み--未検証)をご覧ください。

---

## ドキュメント

リファレンス文書は英語です。概要、ゲーム作成ウィザード、Unity からの移行、ジャンルパッケージは日本語でも読めます。

| | |
|---|---|
| [**詳細な概要**](docs/ja/overview.md) | 全サブシステム、ビルド、プラットフォーム、CI、リリース |
| [ゲームフォーマット](docs/game-format.md) | `game.json`、`rules.json`、マップと全データブロック |
| [ジャンルパッケージ](docs/mechanics-packages.md) · [ライブラリ](docs/libraries.md) | あらゆるジャンルのメカニクス |
| [「ゲームを作る」ウィザード](docs/ja/game-wizard.md) | アイデアとアートが完成プロジェクトになるまで |
| [スクリプト](docs/scripting.md) · [C++ モジュール](docs/native-modules.md) | データで足りないとき |
| [地形](docs/terrain.md) · [惑星](docs/planets.md) · [経路探索](docs/pathfinding.md) | 世界と移動 |
| [パーティクル](docs/particles.md) · [モデル](docs/models.md) · [Shader Graph](docs/shader-graph.md) | ビジュアル |
| [メタゲーム](docs/meta-game.md) · [LiveOps](docs/meta-liveops.md) · [プラグイン](docs/plugins.md) | 収益化とストア |
| [FPS](docs/fps.md) · [大規模ワールド](docs/large-worlds.md) | FPS モジュール |
| [見下ろし型ヒーロー](docs/topdown.md) · [運転](docs/driving.md) · [街の交通](docs/traffic.md) | 街のゲーム: 車、信号、歩行者、緊急サービス、仕事を持つ住民 |
| [AGENTS.md](AGENTS.md) | エンジン本体を変更するエージェント向けのルール |
| [Unity からの移行](docs/ja/engine-import.md) | シーン、プレハブ、アセットのインポートと、エージェントによるロジックの移植 |

## ライセンス

[MIT](LICENSE)。サードパーティのライブラリは `third_party/` にそれぞれのライセンスとともに置かれています
（MIT、MIT-0、BSD、ISC、zlib、OFL、CC0、CC-BY など）。商用ゲームのオリジナルアセットは含まれていません。
インポーターはあなた自身のコピーから読み込みます。

ゲームのビルドには `THIRD_PARTY_NOTICES.txt` が自動的に含まれます。外部 SDK のライセンス通知はプロジェクトで用意してください（[詳細](docs/third-party-notices.md)）。

- [TerraWeave Rocks](docs/terrain/rocks.md): シードによる装飾用の石・巨石・石板・岩片生成。共有PBRテクスチャの出力と3段階のネイティブLOD（任意）。

**Terra を優先:** [ネイティブの樹木、密な草、ワークフロー](docs/terrain/vegetation-authoring.md)。独自の生成処理は TerraWeave にない機能に限定します。

LENS カメラ: [オーサリングと MCP](skills/visuals/aiverse-lens/SKILL.md)、[ネイティブ統合とキャプチャ](skills/visuals/aiverse-lens-native/SKILL.md)。
