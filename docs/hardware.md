# ハードウェアと実装

M5Stack CoreS3の内蔵マイク・スピーカー・近接センサー・LCDを使用します。外付けの音声機器は不要です。

## 手かざしで会話を始める

LTR-553ALS-WA近接センサーの値を読み取り、本体の前に手が近づいたことを検出します。起動時に周囲の明るさや置き方に応じた背景値を取得するため、センサーの前を空けておきます。

センサーは会話の開始に使います。手を離しても会話は続き、手を置いたまま会話を繰り返し起動することはありません。カメラによる人物や手の形の認識は行いません。

## 音声と通信

CoreS3からWi-Fi経由でGPT-Liveへ接続します。音声会話には `gpt-live-1`、検索・推論にはOpenAI管理のResponses delegationを通じて `gpt-5.6-luna` を使います。

内蔵のES7210マイクADCとAW88298スピーカーアンプをI2Sで同時に動かします。音声は16kHz・モノラル・16bit PCMで送受信し、Espressif ESP-SRのAEC（音響エコーキャンセル）でスピーカー音のマイクへの回り込みを抑えます。

音声処理とネットワーク処理は別タスクです。開始時の短い声は、本体に保存したPCM音声を再生します。接続が終わると、マイク入力を使った会話へ移ります。

## 表情の描画

[M5GFX](https://github.com/m5stack/M5GFX)のM5Canvasを使い、変化した部分だけを描き直します。顔の共通画像に、口とまぶたの画像を重ねます。口は再生する声の音量に応じて3段階で動きます。

画像を差し替える場合は、[カスタマイズ](customization.md)と[素材の構成](../assets/README.md)を参照してください。

## 主なソースファイル

| ファイル | 内容 |
| --- | --- |
| [config/character.json](../config/character.json) | 名前・性格・声・検索・地域の設定 |
| [src/assistant_config.cpp](../src/assistant_config.cpp) | 音声会話と検索側への指示 |
| [src/main.cpp](../src/main.cpp) | 状態遷移、ネットワーク通信 |
| [src/burner_config.cpp](../src/burner_config.cpp) | M5BurnerのUSB設定とNVS保存 |
| [src/duplex_audio.cpp](../src/duplex_audio.cpp) | マイク入力、スピーカー再生、AEC |
| [src/character_ui.cpp](../src/character_ui.cpp) | 表情・状態アイコン・操作ボタンの描画 |
| [src/proximity_sensor.cpp](../src/proximity_sensor.cpp) | 近接センサーの読み取り |

## 接続と証明書

接続先は `wss://api.openai.com/v1/live/sessions` です。時刻同期後に、TLSのホスト名と証明書を確認して接続します。同梱のGTS Root R4は公開ルート証明書です。API側の証明書チェーンが変わった場合は、証明書を更新して再ビルドします。

## 公式資料

- [CoreS3仕様](https://docs.m5stack.com/en/core/CoreS3)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [M5GFX](https://github.com/m5stack/M5GFX)
- [CoreS3近接センサー](https://docs.m5stack.com/en/arduino/m5cores3/ltr553)
- [Espressif ESP-SR v1.9.5](https://github.com/espressif/esp-sr/tree/v1.9.5)
- [GPT-Live WebSocket](https://developers.openai.com/api/docs/guides/voice-websockets?api=live)
- [Responses delegation](https://developers.openai.com/api/docs/guides/live-delegation)
