# サラの素材

サンプルキャラクター「サラ」の元イラストと表示用の画像です。画像とその加工物は[CC BY-NC 4.0](LICENSE.md)で提供します。個人の非商用利用が可能です。再配布時は元リポジトリとライセンスへのリンク、加工内容を表示してください。

## 画像ファイル

| ファイル | 内容 |
| --- | --- |
| [source/sara-front.png](source/sara-front.png) | 元イラスト |
| [source/sara-expressions.png](source/sara-expressions.png) | 元イラストをもとにAIで表情・色調を加工した画像。閉じた口、小さく開いた口、大きく開いた口、まばたきの4種類 |
| [generated/portrait.png](generated/portrait.png) | 表示用の共通画像 |
| `generated/mouth_*.png` / `generated/eye_*.png` | 口・まぶたを重ねるための画像 |
| `generated/sara.rgb` | ファームウェアへ組み込むRGB565画像データ |
| `generated/manifest.json` | 各画像のサイズ・位置・データ内の配置 |

表情画像を変更した後、[ビルド環境](../docs/build.md)とFFmpegを用意し、リポジトリのルートで次を実行します。

```sh
.venv/bin/python scripts/prepare_character.py
.venv/bin/python -m platformio run
```

切り出し位置は [scripts/prepare_character.py](../scripts/prepare_character.py) にあります。画像の差し替え方は[カスタマイズ](../docs/customization.md)を参照してください。

## 開始音声

`audio/startup.wav` はOpenAI Speech APIで生成したAI音声です。`generated/startup.pcm` は本体再生用の16kHz・モノラル・16bit PCMです。画像とは別に[MITライセンス](LICENSE.md#開始音声)で提供します。

開始音声は本体に保存して再生するため、起動のたびに音声生成APIを呼び出す必要はありません。声や台詞の変更方法は[開始音声を変える](../docs/customization.md#開始音声を変える)を参照してください。
