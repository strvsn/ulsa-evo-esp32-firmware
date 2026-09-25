# ULSA EVO ESP32 Firmware

## 日本語

ULSA EVOカスタム可能型超音波風速センサーモジュール向けの、オープンソースESP32-C3ファームウェアです。

このリポジトリは、正式なDemo／Initialファームウェアのビルドに使用したソースを、リリース単位のスナップショットとして提供します。ESP32ファームウェアの確認、ビルド、書込み、改変に利用できます。

### 対応環境

- ULSA EVOで使用するESP32-C3／M5Stamp C3U構成
- PlatformIO Core 6.1.18
- `platformio/espressif32@6.12.0`に含まれるArduino-ESP32 2.0.17

### ビルド

```bash
pio run -e m5stamp-c3u
pio run -e m5stamp-c3u-initial
```

詳しくは[カスタムFW互換性](docs/CUSTOM_FIRMWARE_COMPATIBILITY.md)、[ビルドと書込み](docs/BUILD_AND_FLASH.md)、[ファームウェアプロファイル](docs/FIRMWARE_PROFILES.md)、[公開インターフェース](docs/PUBLIC_INTERFACES.md)を参照してください。

第三者ライセンス本文、NOTICE、対応ソースの扱いは[オープンソースライセンス](docs/OPEN_SOURCE_LICENSES.md)を参照してください。配布バイナリには、プロジェクトMIT Licenseとは別に、依存ソフトウェアのライセンスバンドルを同梱します。

本体・アプリでの記録操作とCSVの時刻については[SDカード記録](docs/SD_RECORDING.md)を参照してください。

custom firmwareからcleanなDemo／Initialへ戻す場合は[USBファームウェア書込み](https://ulsa-evo.strvsn.net/firmware/)を使用できます。

### 公開方針

- プロジェクト部分のソースは[MIT License](LICENSE)に基づき、無保証で提供します。依存ソフトウェアには個別のライセンス条件が適用されます。
- 各commitは正式版のソーススナップショットであり、日常開発用branchではありません。
- このリポジトリではIssues、Discussions、機能追加要望、pull request、個別サポートを受け付けません。
- 利用者は自己の責任でclone、fork、改変、ビルド、書込みを行えます。
- ULSA EVO STM32のソースと超音波計測アルゴリズムは含まれません。

セキュリティ上の問題は[SECURITY.md](SECURITY.md)の非公開窓口から報告してください。

## English

Open-source ESP32-C3 firmware for the customizable ULSA EVO ultrasonic wind sensor module.

This repository provides release-by-release source snapshots used to build the official Demo and Initial firmware profiles. You may inspect, build, flash, and modify the ESP32 firmware.

### Supported environment

- ESP32-C3 / M5Stamp C3U configuration used by ULSA EVO
- PlatformIO Core 6.1.18
- Arduino-ESP32 2.0.17 through `platformio/espressif32@6.12.0`

### Build

```bash
pio run -e m5stamp-c3u
pio run -e m5stamp-c3u-initial
```

See [Custom Firmware Compatibility](docs/CUSTOM_FIRMWARE_COMPATIBILITY.md), [Build and Flash](docs/BUILD_AND_FLASH.md), [Firmware Profiles](docs/FIRMWARE_PROFILES.md), and [Public Interfaces](docs/PUBLIC_INTERFACES.md).

See [Open-source licenses and source offer](docs/OPEN_SOURCE_LICENSES.md) for the bundled license texts, notices, and corresponding-source guidance. Distributed binaries include dependency terms in addition to the project MIT License.

See [SD Recording](docs/SD_RECORDING.md) for device/app controls and CSV timestamps.

Use the [USB firmware writer](https://ulsa-evo.strvsn.net/firmware/) to return from a custom build to a clean Demo or Initial.

### Publication policy

- Project source is provided under the [MIT License](LICENSE), without warranty. Dependencies remain subject to their own license terms.
- Each commit is an official release source snapshot, not a live development branch.
- This repository does not accept Issues, Discussions, feature requests, pull requests, or individual support requests.
- Users may clone, fork, modify, build, and flash the source at their own responsibility.
- ULSA EVO STM32 source code and ultrasonic measurement algorithms are not included.

Report security concerns through the private process described in [SECURITY.md](SECURITY.md).
