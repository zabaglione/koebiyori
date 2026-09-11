# ESP-SR AEC

ESP32-S3用のEspressif ESP-SR v1.9.5から、AECに必要なヘッダーと静的ライブラリーを使用しています。

- https://github.com/espressif/esp-sr/tree/v1.9.5
- `include/esp32s3/esp_aec.h`
- `lib/esp32s3/libesp_audio_processor.a`
- `lib/esp32s3/libdl_lib.a`
- `lib/esp32s3/libc_speech_features.a` (FFT routines)

ライセンスは同梱の `LICENSE` とヘッダー記載を参照してください。Espressif製品向けです。音声認識モデルは含みません。
