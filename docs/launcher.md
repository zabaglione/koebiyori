# ランチャーで使う

M5LauncherをCoreS3に入れると、koebiyoriをほかのアプリと切り替えて使えます。koebiyoriの実行中は、本体のマイク・スピーカーから直接OpenAIへ接続します。PCや中継サーバーを動かしておく必要はありません。

このガイドは **M5Launcher 2.9.1のCoreS3版**を対象にしています。M5Launcherは別の開発者によるプロジェクトです。[配布元](https://github.com/bmorcelli/Launcher)と[操作ガイド](https://github.com/bmorcelli/Launcher/wiki/Functionalities-explained)も参照してください。

## 1. M5Launcherを入れる

1. CoreS3をUSBでPCに接続し、[M5Burner](https://docs.m5stack.com/ja/uiflow/m5burner/intro)を起動します。
2. **CORES3**で `M5Launcher for CoreS3 and CoreS3SE`（作者 `bmorcelli`）を検索し、Download → Burnで書き込みます。
3. Launcherの起動画面が出たら、画面中央をタップしてメニューへ入ります。

**初めてLauncherを書き込むと、既存のアプリや保存設定が失われるため、Wi-FiとAPIキーを手元に用意してください。** 既にLauncherを使っている場合、この書き込みは不要です。koebiyoriの対象機種はCoreS3です。Launcher側が対応するCoreS3 SEなどの別機種には対応していません。

## 2. koebiyoriを追加する

[GitHub Releases](https://github.com/zabaglione/koebiyori/releases)のAssetsから、**`koebiyori-VERSION-cores3-app.bin`** を取得します。`VERSION`はリリースの番号です。ZIPを取得した場合も、展開後に同名のファイルを選びます。

| ファイル | 使う場所 |
| --- | --- |
| `koebiyori-VERSION-cores3-app.bin` | LauncherのWebUI・SDから追加する |
| `koebiyori-VERSION-cores3.bin` | M5Burnerで直接書き込み、専用機として使う |

**Launcherを残す場合、PCのM5BurnerでkoebiyoriのBurnを押さないでください。** 直接書き込み用のファイルはLauncherの構成も上書きします。

### SDカードなし：ブラウザーから追加

1. Launcherのメニューで **WUI → my Network** を選び、2.4GHz Wi-Fiへ接続します。
2. 同じネットワークにつないだPCやスマートフォンのブラウザーで、CoreS3に表示されたアドレスを開きます。
3. ログインが求められたら、LauncherのWebUI用アカウントを使います。初期値は `admin` / `launcher` です。必要に応じて **Config → User/Pass** から変更できます。
4. WebUIの **Menu → OTA** を開き、取得した `-cores3-app.bin` を選びます。`APP offset 0x0`と表示されたら **Start Update** を押します。
5. 書き込み中は電源を切らず、完了表示を待ちます。**Config → Reboot** で再起動し、koebiyoriを起動します。

Launcherがネットワークに接続できない場合は、**WUI → AP mode** でCoreS3が作るWi-FiにPCを接続し、画面のアドレスを開く方法も使えます。インストール後はPCを元のネットワークへ戻してください。

### SDカードから追加

1. Launcherで読めるmicroSDカードに `-cores3-app.bin` をコピーし、CoreS3へ挿します。
2. Launcherの **SD** でそのファイルを選び、**Install** を実行します。
3. 完了後にkoebiyoriを起動します。一度インストールすれば、毎回書き込み直す必要はありません。

Launcherのオンライン一覧はLauncher側のカタログです。このプロジェクトの配布ファイルはGitHub Releasesから取得できるため、一覧への掲載を待たずに導入できます。

## 3. Wi-FiとAPIキーを保存する

**サラが表示され、koebiyoriが起動している状態**で、USBから[M5BurnerのBurner NVS](setup.md#3-wi-fiとapiキーを保存する)を使います。`wifi_ssid`、`wifi_password`、`openai_api_key`をそれぞれ保存し、`status`を確認してください。設定後はBurner NVSを閉じて本体をリセットし、koebiyoriを起動します。

Launcher用のWi-Fiとkoebiyori用のWi-Fiは別の設定です。同じネットワークを使う場合も、それぞれに保存してください。APIキーが必要なのはkoebiyoriです。[APIの準備と料金](setup.md#1-openai-apiを準備する)を確認してください。

設定は本体のNVSに保存します。Launcher経由でアプリを追加・更新する場合、koebiyoriの設定は保持されます。NVSの消去やLauncher自体の再書き込みでは失われます。複数のkoebiyoriを入れた場合、同じWi-Fi・APIキーを共有します。

## 起動・終了・切り替え

- **起動する**：Launcherの起動画面にあるkoebiyoriのカードをタップします。メニューからはkoebiyoriを選び、**Launch the app**を実行します。
- **話す**：サラが待機状態になったら手をかざします。[会話の操作](setup.md#4-話してみる)は直接起動と同じです。
- **Launcherへ戻る**：会話を終了して待機状態にし、画面をタップして操作ボタンを出します。左の**四つの四角のアイコン**を押すと、Launcherの起動画面へ戻ります。そこで画面中央をタップするとメニューを開けます。

標準設定では、Launcherの起動画面で約5秒何もしないと選択中のアプリが起動します。毎回メニューに留まりたい場合は、Launcherの **CFG → Boot to Launcher** をオンにしてください。

操作ボタンは普段は隠れ、5秒で消えます。Launcherへ戻るボタンは、Launcherが入っている場合にだけ現れます。Wi-Fi接続待ちや設定待ちの画面からも戻れます。会話中は先に赤い受話器で会話を終了してください。

## 更新する

新しい `-cores3-app.bin` を取得し、初回と同じくLauncherのWebUIまたはSDからインストールします。Launcherが空き領域や置き換えるアプリを尋ねたら、対象を確認して選びます。古い版が一覧に残った場合は、新しい版で起動できることを確認してから、古いアプリの **Delete the app** で削除できます。NVSを消去する操作は不要です。

koebiyoriの配布ZIPには、アプリ・ライブラリのソースとライセンスも含めています。コードはMIT、サラの画像はCC BY-NC 4.0です。[ライセンスとクレジット](../README.md#ライセンス)はLauncher経由の利用・再配布にも適用されます。M5Launcher本体はこの配布ZIPに含めません。
