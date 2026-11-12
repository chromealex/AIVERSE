# 他のエンジンのプロジェクトをインポートする

実装は `agent-engine-import` にあります。元の仕様は [engine-import-spec.md](../engine-import-spec.md) です。シミュレーションとテンプレートの
チェックサムは変わっていません。これは動作するインポートの基盤であり、**仕様のすべての段階を完了したものではありません**。以下の対応範囲と
残作業は MAIN への引き継ぎの一部です。

## CLI と中間データ

Python 3.8+。Python パッケージは不要です。実行用ランチャーは `tools/importers/port/aiverse_port`（または下記の Python を明示する形）です：

```sh
python3 tools/importers/port/aiverse_port.py scan /path/to/UnityProject
python3 tools/importers/port/aiverse_port.py extract /path/to/UnityProject --out /tmp/port.json
python3 tools/importers/port/aiverse_port.py emit /tmp/port.json --out /path/to/Imported
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported --scale 1 --mode auto
python3 tools/importers/port/aiverse_port.py report /path/to/Imported
python3 tools/importers/port/aiverse_port.py item /path/to/Imported --id ITEM_ID
python3 tools/importers/port/aiverse_port.py resolve /path/to/Imported --id ITEM_ID --how lua --reason 'Verified by scenario…'
```

どのコマンドも JSON を出力し、失敗時はステータス 2 で終了します。`scan` は何も書きません。入力と出力のフォルダは重なってはいけません。
同じ出力先への同時インポートは行わないでください。`extract` はソースのパス、完全なシリアライズ済みコンポーネント、フィールド、スクリプトの
ソースコードとその使用箇所を記録します。`emit` はゲームのマニフェスト、ルール、マップ、`.aiverse`、コピーしたアセット、必要に応じて
スプライト/タイル/オーディオ/エフェクト、`port.json`、JSON/Markdown のレポートを書き出します。コピーしたアセットを書き出す時点で
`sourceRoot` が存在している必要があります。生成される `.rpakignore` は、スクリプトの中間インデックスとレポートを実行時パックから除外します。
マニフェストはインポートしたコンテンツに `external: true` を付け、aiversebuild はそれをリソースパックに引き継ぎます。

`--scale` は 1 メートルあたりのセル数、`--mode auto|2d|3d` はシーンの平面とカメラを選びます。Unity 3D は Z を反転し、Unity 2D は XY を地面に
投影します。Godot の Vector2 は XZ に対応付けます。マップ上の座標が正になるよう原点を移動します。所有者を勝手に推定はしません。Unity の明示的な
`owner` フィールドだけを近似の警告付きで対応付けます。ソース側のプレイヤー番号は確認が必要です。

`lib/port/mapping.json` はフィールドの別名を定義します。`--mapping FILE` は対応する対象の別名を追加または上書きします。`fireRate` は
毎秒の発射数（クールダウンの逆数）として解釈され、近似であることが明示されます。エンジンの JSON のクールダウンと建設時間は**秒**であって、
シミュレーションの tick ではありません。推定したフィールドにはすべて出所、信頼度、出力先のパスが付きます。

### 再インポート

`.port-state.json` は生成したファイル/タイプのハッシュを保存します。編集済みのファイルや元からあったファイルは、エディターが `ported`
マーカーを残していても保持されます。ルールはタイプ単位でマージされ、編集されたタイプはそのマーカーを失います。新しいタイプは引き続き追加
できます。ルールレベルの設定に対するユーザーの変更は保持されます。マップとバイナリファイルはファイル単位で保護されます。古くなった生成
ファイルは削除されません。状態マニフェストはインポーターが管理するので編集しないでください。

解決済みの注記は、項目と関連するソースのエンティティ/スクリプトのフィンガープリントが変わらない間だけ再インポート後も残ります。ソースが
変わると、その項目は再び確認対象になります。

## エディターと MCP

**プロジェクト → エンジンプロジェクトをインポート** を開くか、`editor_window {id:"project_import", open:true}` を使います。ソースをスキャンし、
出力先/スケール/2D/3D を選んでインポートします。UI はインポーターをレンダースレッドの外で実行し、結果を開くこともできます。
**AI への引き継ぎをコピー** は明示的なタスクをコピーして Agent ウィンドウを開きますが、エージェントを起動したとは主張しません。

現在のベースブランチには「ゲームを作る」ウィザードがありません。別ブランチ `agent-gamewizard` との統合は MAIN に委ねられ、無関係なブランチは
マージしていません。

登録されている操作（インポート設定は `options` の中に入れ子にすることもできます）：

- `port_scan {path}`：ソースの判別、シリアライズ方式の確認、アセット/シーン/スクリプトの一覧。
- `port_import {path,out,scale?,mode?,mapping?}`：抽出と書き出しを実行。
- `port_report {path?}`：出力先のレポートを読む（既定は開いているプロジェクト）。
- `port_item {path?,id}`：ソースの項目、コンポーネント、関連スクリプト、生成されたタイプ。
- `port_resolve {path?,id,how,reason}`：検証済みの作業を注記（`rules`、`lua`、`native`、`dropped`）。

MCP 経由のインポートは既存の VFX インポーターと同様に同期的で、大きなプロジェクトでは時間がかかることがあります。`AIVERSE_PYTHON` で
インタープリターを選べます。コマンド引数に含まれる Windows シェルのメタ文字 `%`、`!`、引用符、改行は展開されず拒否されます。

エージェントの作業手順：不足している項目を確認し、`rules_schema` を参照して、適切なメカニクスをまずデータで、次に Lua で、必要ならネイティブ
モジュールで実装します。`scenario_run` とボット対戦で DPS、生産時間、移動速度を比較します。元の画面があればスクリーンショットを比較します。
検証済みの作業だけを解決済みにし、意図的に省いた挙動は説明します。C#/GDScript/Blueprint は機械的に翻訳も実行もされません。

## 対応範囲と仕様の残作業

| 領域 | 実装済み | 未対応 |
|---|---|---|
| Unity 共通コード | YAML、GUID 検索、ネストしたプレハブとバリアントを `lib/unity` に移動。互換インポートで VFX は引き続き動作 | 共通パーサーの既存のネスト制限は引き続き有効 |
| Unity のシーン/タイプ | ワールド変換、シーンオブジェクト、プレハブのオーバーライド、スクリプトの使用状況とシリアライズ済みフィールド、ScriptableObject の候補、ビルドシーンの順序 | 変換のスケールをモデルの境界に反映、堅牢な意味的タイプのグループ化、プレイヤー/開始地点の推定 |
| アセット/マテリアル | モデル/テクスチャ/オーディオと隣接する依存ファイルのパスを保持。PBR 係数と最初のマテリアルオーバーライド。モデルのスケールのメタデータ。既存の ImageMagick/ffmpeg アダプターで PSD/TIFF → PNG（任意） | マテリアルスロットの対応付け、パックされたマップ、インポート時の軸設定、アニメーションフレームの切り出し。画像変換ツールがない場合はレポートに記載 |
| Unity 2D | アトラスからのスプライト切り出し、明示的な Tilemap セルをアトラス/固定レイヤーにベイク、正投影カメラ | RuleTile、変換/着色されたタイル、任意の Grid レイアウト、アニメーションタイル、スプライトのピボット/ソート |
| オーディオ/エフェクト | カメラ/マネージャーのループ音 → 対戦のプレイリスト。既存の VFX コンバーターは保護されたステージング経由で書き出す | ミキサーのルーティングと、エフェクトとイベントの意味的な紐付け |
| 地形 | 不透明なベイク済みデータと Terrain コンポーネントはレポートに記載 | TerrainData のハイトフィールド/スプラット/木/ディテールの変換は未実装 |
| Godot | テキストのシーン/リソースの検出、外部アセット、単純な平行移動、エクスポートされた数値のオーバーライド、スクリプトのインデックス、メインシーンの順序 | シーンの継承、変換の完全な合成、複数行の複雑な Variant のデコード、TileMap/GridMap の変換、アニメーションクリップ |
| Unreal | JSON の受け取り側と、エディター用 Python エクスポーター（読み込まれたアクター、リフレクションによる候補プロパティ、AssetExportTask によるメッシュ/テクスチャ/オーディオ） | エクスポーターは **Unreal で実行されていない**。ランドスケープ、Blueprint 変数の完全な列挙、グラフのエクスポートは未実装 |
| エディター | 独立したローカライズ済みインポートウィンドウ、メニュー項目、MCP、コピーできる AI への引き継ぎ | ウィザードとの統合、アセットのカテゴリ選択、エージェントの直接起動 |

Force Text でも未知の Unity ネイティブバイナリ `.asset`（ベイク済みライティング/NavMesh）が存在することがあります。それらは 1 つずつレポートに
記載されます。シーン/プレハブ/マテリアルのバイナリシリアライズは、Force Text を有効にする指示とともに拒否されます。`.uasset` を Unreal の外で
解析することはありません。

Unreal 用のヘルパーは `tools/importers/unreal_export/export.py` です。Python プラグインを有効にした Unreal Editor 内で、新しい出力フォルダを
指定して `export_project(destination)` を呼び出します。API の参考：
[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6)、
[EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6)。
現在読み込まれているレベルと `/Game` のアセットを書き出し、読み込まれていないレベルはたどりません。

## 検証、2026-11-03

`tests/data/port` の合成プロジェクトはオリジナルのテストデータです。Unreal の FBX は既存の手書きの CC0 ファイル
`tests/data/models/cube.fbx` です。ダウンロードしたゲームのアセットはコミットしていません。

- 共通コードの移動後も、既存の VFX インポーターのテスト 34 件はすべて合格。
- プロジェクトインポーターのテスト 15 件で、シリアライズの拒否、プレハブのオーバーライド、単位、保護された編集、冪等性（`port.json` の
  再書き出しを含む）、ソース変更時の無効化、パスの安全性、スプライト/タイル、CLI による解決をカバー。`AIVERSE_PORT_HEADLESS` を使うと
  5 つのプロジェクトすべてが実際のエンジンで読み込まれ、Unity の戦闘プロジェクトはプレイヤー 1 の勝利で終わります。
- C++ テストスイート：5857 件のチェック。サンドボックス内でのみ起きる localhost リスナーの失敗が 1 件あり、そのテストはネットワーク
  サンドボックスの外で単独実行すると合格（27 件）。追加の MCP ブリッジテストも合格（5 件）。
- この変更とは無関係な既存のコンパイラ警告：`engine/fx/FxEffect.cpp:1058` の未使用の `fb`。

公開されている RTS リポジトリ 2 つを `/tmp` にだけダウンロードしてインポートしました。**生成されたマップはすべて
`aiverse_headless --max-seconds 0` で個別に読み込み**、最初/既定のマップだけではありません：

| ソース | リビジョン | 結果 | レポート |
|---|---|---|---|
| [Unity FPS RTS](https://github.com/aydenspringer/Unity-FPS-RTS-System) | `df468f644e6aff145d2eb219dca87f99742ca81d` | コピーしたアセット 509、タイプ候補 375、マップ 30。30/30 読み込み可 | 近似 486、欠落 1913。スクリプト、レンダリング/物理コンポーネント、不透明なベイク済みデータ、未対応のアセットは要確認 |
| [Godot Open RTS](https://github.com/lampe-games/godot-open-rts) | `a628ad3bb6a9a903587a181a7ee3300dbf735e81` | コピーしたアセット 388、タイプ候補 552、マップ 84。84/84 読み込み可 | 近似 638、欠落 2017。スクリプトのロジック、シーンの継承、埋め込みリソース、タイル/アニメーションのシステムは要確認 |

これらはインポート/読み込みの確認であって、**1 対 1 で遊べる移植ではありません**。候補の数を、再現に成功したゲームメカニクスとして示しては
いけません。Unreal での実行、および Windows、Android、Web での実行は未検証のままです。

独立したインポートフォームは macOS のネイティブエディターで確認しました。クリップボード/高速キー入力による自動入力では ImGui のテキストが
安定して入力されなかったため、クリック操作による完全なインポートは主張しません。同じ操作は C++ の MCP ブリッジと CLI で検証済みです。
macOS エディター/Metal はコンパイルできます。

現在の master（`82c47120`）をマージした後、AppleClang でコンパイルできるよう `NativeBuild.cpp` の `std::filesystem::path` のループ 2 つを
`const auto&` に変更しました。共有ファイルへの最小限の互換性修正です。このブランチを統合・検証した後、MAIN はエディターのパッケージを
再公開してください。このブランチ自体はビルドを公開しません。

master のマージと互換性修正の後の最終確認：

- macOS エディター（Metal）、aiversemcp、aiverse_headless、aiverse_tests を正常に再ビルド。
- ソケット制限のあるサンドボックスの外での C++ テストスイート全体：**5930 件のチェック、失敗 0**。
- CTest：既存の VFX インポーターとプロジェクトインポーターのターゲットが合格（Python テスト 34 + 15 件）。
- 対象を絞った MCP ブリッジ：5 件。ネイティブエディターのビルド補助：42 件、すべて合格。
- `aiversemcp --no-editor` によるエンドツーエンドのインポートは、入れ子の `options` を受け付け、scale=2 を適用し、空白とアポストロフィを
  含む出力パスを扱えます。
- `skirmish --seed 3`：青が 470 ゲーム秒で勝利。`mushrooms --seed 3`：赤が 92 秒で勝利。
