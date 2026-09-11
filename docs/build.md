# 自分でビルドする

性格や画像を変更する場合、または配布用ファームウェアを作る場合の手順です。通常の利用は[導入ガイド](setup.md)に沿ってM5Burnerから行います。

## 環境を準備する

Python 3.11以降（3.12推奨）を用意し、リポジトリを取得します。Gitを使わない場合はGitHubの **Code → Download ZIP** でも取得できます。

```sh
git clone https://github.com/zabaglione/koebiyori.git
cd koebiyori
```

macOS / Linux：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 -r scripts/requirements.txt
```

Windows PowerShell：

```powershell
py -m venv .venv
.venv\Scripts\python.exe -m pip install platformio==6.2.0 -r scripts/requirements.txt
```

以下はmacOS / Linuxの表記です。Windowsでは `.venv/bin/python` を `.venv\Scripts\python.exe` に置き換えてください。

画像と開始音声は加工済みなので、通常のビルドにAPIキーやFFmpegは不要です。Wi-FiとAPIキーをソースコードへ書き込まないでください。

## 本体へ書き込む

[性格・声・画像](customization.md)を変更し、CoreS3をUSBで接続して次を実行します。

```sh
.venv/bin/python -m platformio device list
.venv/bin/python -m platformio run
.venv/bin/python -m platformio run --target upload --upload-port PORT
```

`PORT` は一覧にあるCoreS3のポートへ置き換えます。例：macOSの `/dev/cu.usbmodem...`、Linuxの `/dev/ttyACM0`、Windowsの `COM3`。

この方法はアプリ部分などを書き換え、既存のkoebiyoriのWi-FiとAPIキーを保持します。初回は[Burner NVSの設定手順](setup.md#3-wi-fiとapiキーを保存する)で設定してください。

## M5Burner用の配布ファイルを作る

```sh
.venv/bin/python scripts/package_firmware.py
```

ファームウェアをビルドし、次のファイルを `dist/m5burner/` に作ります。

| ファイル | 用途 |
| --- | --- |
| `koebiyori-VERSION-cores3.bin` | 0x0000から書き込む16MBフラッシュ向けの統合ファームウェア |
| `cover.png` | M5Burnerのカバー画像 |
| `description.txt` | M5Burnerの説明欄へ記載する文章 |
| `entry.json` | 名前・バージョン・機種・ファイル名などの投稿用情報 |
| `source.zip` | ソース、設定、素材、ビルドスクリプト |
| `dependencies.zip` | 使用したArduinoコア・WebSocketsなどのライブラリソースとPlatformIOのビルド定義 |
| `SHA256.json` | 同梱ファイルのSHA-256 |
| `LICENSE`、`assets/LICENSE.md`、`licenses/` など | コード・画像・外部コンポーネントのライセンス |

同じ内容をまとめた `dist/koebiyori-VERSION-m5burner.zip` も作ります。`entry.json` は投稿欄を埋めるための控えで、M5Burnerへ読み込ませる形式ではありません。

配布用 `.bin` はビルド成果物から作り、**NVSを空にします。設定済みの本体からExportしたデータは使いません。** この統合ファームウェアを本体へ書き込むと、Wi-FiとAPIキーを再設定する必要があります。`source.zip` は明示した配布対象ファイルだけを含みます。フォルダー全体をZIPにして配布しないでください。

## M5Burnerへ掲載する

[M5Stack公式の投稿手順](https://docs.m5stack.com/ja/uiflow/m5burner/publish)に沿って、M5Burnerにログインし **USER CUSTOM → Publish** を開きます。

`entry.json` の内容を参考にName、Version、Device Type、GitHubを入力し、生成された `.bin` と `cover.png` を指定します。Descriptionには `description.txt` の内容を記載します。利用者自身のAPIキーが必要なこと、API料金、サラの画像のCC BY-NC 4.0と元リポジトリの表示を残してください。

GitHub側にも対応するソースと配布ファイルを置き、各外部コンポーネントのソース・ライセンスにアクセスできる状態で掲載します。ファームウェアの投稿・公開状態はM5Burner側で操作します。このスクリプトは外部への投稿を行いません。

## リリースと同じライブラリでビルドする

[GitHub Releases](https://github.com/zabaglione/koebiyori/releases)から、対象バージョンの `koebiyori-VERSION-m5burner.zip` を取得して展開します。中の `source.zip` を展開し、その `koebiyori-source` フォルダーで上記のPython環境を準備してください。同梱の `dependencies.zip` をこのフォルダーへコピーし、次を実行します。

```sh
.venv/bin/python -m platformio pkg install
.venv/bin/python scripts/package_dependencies.py --restore dependencies.zip
.venv/bin/python -m platformio run
```

初回の環境構築にはインターネット接続が必要です。コンパイラーとEspressifのビルド済みSDKはPlatformIOから取得します。Arduinoコアは2.0.17（パッケージ3.20017.0）、ESP-IDFは4.4.7、ESP32-S3用コンパイラーは8.4.0+2021r2-patch5です。各ライブラリの版は `platformio.ini` に指定しています。アーカイブ内の `MANIFEST.json` には全ソースファイルのSHA-256を記録しています。

`--restore` はインストール済みライブラリのソースを同梱版で上書きします。ローカルで変更済みのライブラリがある場合は、先に別の場所へ保存してください。SDKやコンパイラーは上書きしません。

LGPL対象ライブラリを変更する場合は、復元後に次のソースを編集します。

| 対象 | 編集場所 |
| --- | --- |
| Arduinoコア | `.pio-core/packages/framework-arduinoespressif32/cores/esp32/` |
| Wi-Fi、USB、WireなどのArduinoライブラリ | `.pio-core/packages/framework-arduinoespressif32/libraries/` |
| WebSockets | `.pio/libdeps/cores3/WebSockets/src/` |

編集後は `--restore` を再実行せず、次で再コンパイル・再リンクします。

```sh
.venv/bin/python -m platformio run --target clean
.venv/bin/python scripts/package_firmware.py
```

生成した `.bin` は変更したライブラリを含みます。書き込み方法は上記を参照してください。セキュアブートによる署名や作者の承認は必要ありません。変更したライブラリを再配布する場合も、そのソース・ライセンス・変更の表示を添えてください。

## USB設定の実装

[src/burner_config.cpp](../src/burner_config.cpp) が [M5BurnerNVSの通信形式](https://github.com/m5stack/M5BurnerNVS)に対応します。保存先はNVSの `koebiyori` 名前空間です。

編集できるキーは `wifi_ssid`、`wifi_password`、`openai_api_key` に限定し、`status` は読み取り専用です。秘密値はGETで伏せ字にし、購読による送信も行いません。USB設定中は自動会話開始と通常ログを止め、保存した内容は再起動後に使います。
