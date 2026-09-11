# 性格・声・検索の設定

[config/character.json](../config/character.json) を編集し、[ビルドガイド](build.md)に沿って再ビルド・書き込みすると反映されます。PlatformIOからの書き込みではWi-FiとAPIキーを保持します。M5Burner用の統合ファームウェアを書き込む場合は再設定が必要です。

| 項目 | 内容 |
| --- | --- |
| `name` | キャラクターが会話で名乗る名前 |
| `personality` | 性格・口調・返答の長さ。文字列を並べて編集 |
| `voice` | GPT-Liveの声。サンプルは `marin` |
| `greeting` | 保存済みの開始音声の後、会話へ誘うときの指示 |
| `farewell` | 無言で終了するときの短い挨拶の指示 |
| `web_search_enabled` | `true` でWeb検索を有効化、`false` で無効化 |
| `location_hint` | 地域の初期値。空文字列なら場所を決めつけず質問する |
| `timezone` / `utc_offset_minutes` | 日時の表記とUTCからの差。日本は `Asia/Tokyo` / `540` |
| `search_wait_ms` | 検索等の委譲が長引いたときの案内までの時間。標準20,000ms |
| `backend_model` / `reasoning_effort` | 検索・推論側のモデルと推論量。標準は `gpt-5.6-luna` / `low` |

性格の例：

```json
"personality": [
  "落ち着いた、少しユーモアのある性格。自然な日本語で話す。",
  "返答は1〜2文。質問を重ねず、相手の話を聞く。",
  "知らないことは正直に伝える。事実と感想を区別する。"
]
```

`personality` には、キャラクターの雰囲気・口調・返答の長さを具体的に記述します。会話の流れに応じて、実際の言い回しは変わります。

検索を使う契機、音声側への短い指示、検索結果の扱いは [src/assistant_config.cpp](../src/assistant_config.cpp) にまとめています。I2Sや画面描画コードを編集する必要はありません。検索は「今日」という単語だけでは起動せず、最新の外部情報が必要かどうかをモデルに判断させます。

現在時刻は起動時にNTPで合わせ、セッション開始時と会話中1分ごとに伝えます。UTCオフセットは固定値なので、夏時間がある地域では利用時の値を合わせてください。

`search_wait_ms` を過ぎると、検索に時間がかかっていることを声で知らせます。その後は通常の無言終了の判定に戻ります。検索結果は声で短く返します。

## 開始音声を変える

`voice` を変えても、本体に保存済みの開始音声は変わりません。[scripts/prepare_startup_voice.py](../scripts/prepare_startup_voice.py) の台詞・声・指示も合わせて編集し、FFmpegを用意して生成します。APIキーはPCの環境変数 `OPENAI_API_KEY`、または `.env.example` をコピーした `.env.local` に設定してください。これは素材の生成だけに使う設定で、本体には送られません。

```sh
.venv/bin/python scripts/prepare_startup_voice.py --generate
.venv/bin/python -m platformio run
```

`--generate` はSpeech APIを呼び出すため料金が発生します。省略すると、既存のWAVを再生用のPCMに変換します。

## イラストを変える

サラは同梱サンプルです。商用利用する場合は、商用利用できる自作素材などへ差し替えてください。サラの画像はMITではなく、[CC BY-NC 4.0](../assets/LICENSE.md)です。

表情アトラスの配置と切り出し位置は [scripts/prepare_character.py](../scripts/prepare_character.py)、口・目の描画位置は [src/character_ui.cpp](../src/character_ui.cpp) にあります。画像だけでなく、差し替えた顔に合わせて口・目の位置も調整してください。既存素材の加工手順は [assets/README.md](../assets/README.md) を参照してください。
