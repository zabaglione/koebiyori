# 性格・声・検索の設定

## 本体で声を変える

1. 会話を終了し、待機中の画面をタップします。
2. 画面上部中央の **VOICE** を押します。
3. 左右の矢印で声を選びます。**Style** を押すと **Anime／Natural** が切り替わります。
4. **Save** で本体へ保存します。**Cancel** は変更を破棄します。
5. 次の会話から選んだ声と話し方になります。再起動しても設定は残ります。

音声設定画面は自動で閉じず、選択中は手かざしで会話を開始しません。矢印を押すだけでは試聴せず、API通信も発生しません。聴き比べるときは保存して短く会話し、終了してから別の声を選びます。

サラの初期設定は **Marin + Anime** です。Animeは、このアプリが「高めの軽い声、明るく柔らかい響き、自然な日本語の抑揚、聞き取りやすい速さ」を指定する話し方です。OpenAIの専用アニメ音声名ではなく、声の高さや声優の演技を固定する機能でもありません。Naturalは落ち着いた通常の話し方です。実際の表現は返答や選んだ声により変わります。[公式の話し方の指定](https://developers.openai.com/api/docs/guides/live-prompting#personality)

開始直後は全ての声で共通の短い電子チャイム（約0.7秒）を再生します。接続後の挨拶・返答・終了の挨拶には、選んだ声と話し方を使います。[開始チャイムを変える](#開始チャイムを変える)も参照してください。

### 選べる22種類の声

2026年9月12日の[GPT-Live API仕様](https://developers.openai.com/api/reference/typescript/resources/live/subresources/sessions/methods/accept)にある組み込み音声を収録しています。

従来の10種類は `marin`（GPT-Liveの標準）、`cedar`、`alloy`、`ash`、`ballad`、`coral`、`echo`、`sage`、`shimmer`、`verse` です。この10種類の声質・男女の分類は、参照したLiveの仕様に記載されていないため、本体では名前を表示します。

追加の12種類には次の説明があります。[公式の音声一覧](https://developers.openai.com/api/docs/guides/live-conversations#voice-options)

| 名前（API名） | 言語・地域の傾向 | 声の印象 |
| --- | --- | --- |
| Quartz (`quartz`) | 英語・オーストラリア | 女性的 |
| Ripple (`ripple`) | 英語・オーストラリア | 男性的 |
| Vesper (`vesper`) | 英語・イギリス | 男性的 |
| Willow (`willow`) | 英語・アイルランド | 女性的 |
| Stone (`stone`) | 英語・アイルランド | 男性的 |
| Gleam (`gleam`) | 英語・北米 | 女性的 |
| Meridian (`meridian`) | 英語・北米 | 男性的 |
| Bossa (`bossa`) | ポルトガル語・ブラジル | 女性的 |
| Tempo (`tempo`) | ポルトガル語・ブラジル | 男性的 |
| Beacon (`beacon`) | 英語・フィリピン | 男性的 |
| Delta (`delta`) | 英語・米国南部 | 女性的 |
| Cinder (`cinder`) | 英語・米国南部 | 男性的 |

言語・地域は声の特徴の説明であり、アクセントを保証するものではありません。本アプリはどの声でも日本語で話すよう指示します。日本語での印象は実際に聴いて選んでください。声の変更には新しいセッションが必要なため、会話中の声の切り替えは行いません。

### USBから設定する

M5BurnerのBurner NVSでは `voice` に上記のAPI名、`voice_style` に `anime` または `natural` を入力して保存できます。空欄を保存すると、その項目を設定ファイルの初期値へ戻します。未対応の名前は保存せずエラーを返します。設定後は `status` を確認し、Burner NVSを閉じて本体をリセットしてください。

## 性格や初期値を変える

[config/character.json](../config/character.json) を編集し、[ビルドガイド](build.md)に沿って再ビルド・書き込みすると反映されます。**本体に保存済みの声・話し方は設定ファイルの初期値より優先されます。** M5Launcherではアプリ単体版を使います。M5Burner用の統合ファームウェアを書き込む場合は設定を入れ直してください。

| 項目 | 内容 |
| --- | --- |
| `name` | キャラクターが会話で名乗る名前 |
| `personality` | 性格・口調・返答の長さ。文字列を並べて編集 |
| `voice` | 本体に保存がない場合の声。標準は `marin` |
| `voice_style` | 本体に保存がない場合の話し方。`anime`（標準）または `natural` |
| `voice_styles` | Anime／Naturalそれぞれの話し方の指示 |
| `greeting` | 接続後、選んだ声で挨拶し会話へ誘うときの指示 |
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

## 開始チャイムを変える

チャイムは声の種類に依存しない共通音です。[scripts/prepare_startup_cue.py](../scripts/prepare_startup_cue.py) の音の高さ・開始時刻・長さを編集して、次を実行します。Pythonの標準機能で生成するため、APIキーやFFmpegは不要です。

```sh
.venv/bin/python scripts/prepare_startup_cue.py
.venv/bin/python -m platformio run
```

生成・本体でのチャイム再生にはAPI料金はかかりません。接続を待つ間に一度だけ鳴り、会話中にBGMとして繰り返し流すことはありません。

## イラストを変える

サラは同梱サンプルです。商用利用する場合は、商用利用できる自作素材などへ差し替えてください。サラの画像はMITではなく、[CC BY-NC 4.0](../assets/LICENSE.md)です。

表情アトラスの配置と切り出し位置は [scripts/prepare_character.py](../scripts/prepare_character.py)、口・目の描画位置は [src/character_ui.cpp](../src/character_ui.cpp) にあります。画像だけでなく、差し替えた顔に合わせて口・目の位置も調整してください。既存素材の加工手順は [assets/README.md](../assets/README.md) を参照してください。
