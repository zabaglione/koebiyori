# Third-party components

The project license does not replace the following component licenses.

| Component | Version | License / source |
| --- | --- | --- |
| M5Unified | 0.2.11 | MIT — https://github.com/m5stack/M5Unified |
| M5GFX | 0.2.28 | MIT, with additional notices for bundled components/fonts — https://github.com/m5stack/M5GFX |
| arduinoWebSockets | 2.6.1 | LGPL-2.1 — https://github.com/Links2004/arduinoWebSockets |
| ArduinoJson | 7.4.2 | MIT — https://github.com/bblanchon/ArduinoJson |
| Arduino core for ESP32 | 2.0.17 | LGPL-2.1, with underlying ESP-IDF component licenses — https://github.com/espressif/arduino-esp32 |
| ESP-SR AEC libraries | 1.9.5 | Espressif license included in [lib/esp-aec/LICENSE](lib/esp-aec/LICENSE), permission limited to use on Espressif Systems products |
| GTS Root R4 | Public root certificate | https://pki.goog/repository/ |

The ESP-SR binary libraries and header are included under `lib/esp-aec/`; retain its license when redistributing them. This is not the unrestricted standard MIT text.

Other firmware dependencies are obtained by PlatformIO. Their copyright and redistribution requirements remain applicable, including when distributing compiled firmware. Source availability alone does not remove third-party obligations.

## Binary releases and LGPL sources

The firmware uses the GNU Lesser General Public License libraries Arduino core for ESP32 and arduinoWebSockets. The project code remains MIT; third-party code retains its own license. Modification for personal use and reverse engineering for debugging modifications to the LGPL libraries are permitted under the applicable LGPL terms.

Each [versioned release](https://github.com/zabaglione/koebiyori/releases) provides `koebiyori-VERSION-m5burner.zip`, containing the firmware, `source.zip` (application sources, assets, AEC libraries and build scripts), `dependencies.zip` (the library sources used to build that release, including Arduino core and WebSockets), and `licenses/`. The dependency snapshot preserves source copyright notices and includes build definitions. See [the rebuild instructions](docs/build.md#リリースと同じライブラリでビルドする) to modify these libraries and relink the application.

The separately installed toolchain and precompiled Espressif SDK are pinned in the build configuration. The SDK is based on ESP-IDF v4.4.7, commit `38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`; the Arduino upstream tag is [2.0.17](https://github.com/espressif/arduino-esp32/tree/2.0.17). SDK component notices, LGPL text, and the GCC Runtime Library Exception are in [distribution/licenses](distribution/licenses/SOURCES.json). `SOURCES.json` records the origin and checksum of each notice. M5GFX's bundled font notices are retained in the dependency sources.

Sara's non-commercial artwork license applies to the artwork, not to the LGPL library sources. To distribute a commercial product, replace the Sara assets and comply with all remaining component licenses. The ESP-SR binary libraries are licensed for Espressif products only.

Python helper dependencies are listed in `scripts/requirements.txt` and are installed separately. FFmpeg and PlatformIO are separate build tools and are not bundled in this source distribution. OpenAI API usage is governed by the applicable OpenAI service terms, independently of the code and artwork licenses.
