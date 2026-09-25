# Third-Party Notices / 第三者ソフトウェア通知

## 日本語

ULSA EVO ESP32ファームウェアのプロジェクト部分は[MIT License](LICENSE)で提供します。ビルドとリンクに含まれる第三者ソフトウェアには、それぞれのライセンス条件が適用されます。

| Component | Upstream version | PlatformIO package version | License | Profiles | Source |
|---|---:|---:|---|---|---|
| PlatformIO Core | `6.1.18` | `6.1.18` | `Apache-2.0` | demo, initial | [source](https://github.com/platformio/platformio-core) |
| PlatformIO Espressif 32 | `6.12.0` | `6.12.0` | `Apache-2.0` | demo, initial | [source](https://github.com/platformio/platform-espressif32) |
| Arduino-ESP32 | `2.0.17` | `3.20017.241212+sha.dcc1105b` | `LGPL-2.1-or-later` | demo, initial | [source](https://github.com/espressif/arduino-esp32) |
| GCC Toolchain for ESP32 RISC-V | `8.4.0+2021r2-patch5` | `8.4.0+2021r2-patch5` | `GPL-2.0-or-later` | demo, initial | [source](https://github.com/espressif/crosstool-NG) |
| esptool | `4.9.0` | `2.40900.250804` | `GPL-2.0-or-later` | demo, initial | [source](https://github.com/espressif/esptool) |
| M5GFX | `0.2.22` | `0.2.22` | `MIT` | demo, initial | [source](https://github.com/m5stack/M5GFX/tree/3944fedcaa7999b3c8943dfce972d804cb6dacc5) |
| M5Unified | `0.2.17` | `0.2.17` | `MIT` | demo, initial | [source](https://github.com/m5stack/M5Unified/tree/8108bfad04a20ff4e57c0751f62dd6fdf6b137a6) |
| Adafruit NeoPixel | `1.15.5` | `1.15.5` | `LGPL-3.0-only` | demo, initial | [source](https://github.com/adafruit/Adafruit_NeoPixel/tree/d514fc3beae85dd4c2b19781b93faa47bd6e996f) |
| NimBLE-Arduino | `1.4.3` | `1.4.3` | `Apache-2.0` | demo | [source](https://github.com/h2zero/NimBLE-Arduino/tree/e2b40749ef1e1aae9b1f5b2647d2bbadc5abb44f) |
| AceCommon | `1.6.2` | `1.6.2+sha.8698645` | `MIT` | demo, initial | [source](https://github.com/bxparks/AceCommon/tree/8698645868ac4e6631b8fa3d8308d77377b507bd) |
| AceSorting | `1.0.0` | `1.0.0+sha.e58dd60` | `MIT` | demo, initial | [source](https://github.com/bxparks/AceSorting/tree/e58dd60a3f3efb09e877c2c8a144f40627ba3359) |
| AceTime | `4.1.0` | `4.1.0+sha.3dc2f588` | `MIT` | demo, initial | [source](https://github.com/bxparks/AceTime/tree/3dc2f58811e153e02bd16161da579dd201746372) |
| IANA Time Zone Database | `2025b` | `bundled by AceTime 4.1.0` | `Public domain` | demo, initial | [source](https://data.iana.org/time-zones/releases/tzdb-2025b.tar.lz) |
| ESP-IDF bundled SDK | `4.4.7` | `4.4.7` | `Multiple (see COPYRIGHT.rst)` | demo, initial | [source](https://github.com/espressif/esp-idf/tree/v4.4.7) |

Release asset `ulsa-evo-esp32-license-bundle.tar.gz`には、LGPL本文、NimBLE NOTICE／tinycrypt帰属表示、ESP-IDFのCOPYRIGHT一覧を含む、契約に対して解決した全ライセンス本文とNOTICEを同梱します。bundle manifestには各ファイルの取得元URLとSHA-256を記録します。

表はパッケージ単位の情報です。再現性のためビルドツールも記載しますが、`firmware.bin`へ自動的に含まれるとは限りません。runtime libraryはprofileのlinker mapに従います。Arduino-ESP32とESP-IDF bundled SDKには追加のコンポーネント条件があるため、bundleの本文を確認してください。

LGPL対象コンポーネントの対応ソースと再リンク手順は、`docs/OPEN_SOURCE_LICENSES.md`に記載します。プロジェクトのMIT Licenseを依存ソフトウェアのライセンスの代わりに扱わないでください。

## English

ULSA EVO ESP32 firmware is distributed under the project [MIT License](LICENSE). It builds with and links third-party software governed by its own license terms.

The release asset `ulsa-evo-esp32-license-bundle.tar.gz` contains the complete license and notice texts resolved for this contract, including LGPL terms, the NimBLE NOTICE and tinycrypt attribution, and the bundled ESP-IDF COPYRIGHT inventory. The bundle manifest records the source URL and SHA-256 of every file.

The table records package-level metadata. Build-only tools are listed for reproducibility but are not automatically part of `firmware.bin`; runtime libraries are linked according to the profile linker map. Arduino-ESP32 and the bundled ESP-IDF components may contain additional component-specific terms, which are included in the bundle and remain authoritative at the pinned upstream revision.

This inventory is technical evidence and not a substitute for legal review. A formal Release remains on hold until the legal review status in `config/license_compliance.json` is approved by an authorized reviewer.
