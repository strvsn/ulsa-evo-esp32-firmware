# Build and Flash / ビルドと書込み

## 日本語

### 必要な環境

- Python 3.12以降
- PlatformIO Core 6.1.18
- Git
- 対応するESP32-C3 targetへのUSB接続

### 両プロファイルのビルド

```bash
pio run -e m5stamp-c3u
pio run -e m5stamp-c3u-initial
```

### 公開スナップショットの検証

```bash
python3 scripts/verify_public_source.py --build
```

ULSA EVOとの最低互換条件だけを確認する場合:

```bash
python3 scripts/verify_ulsa_evo_compatibility.py
```

第三者ライセンス本文と、LGPL対象ライブラリの対応ソース・再リンク方法は[オープンソースライセンス](OPEN_SOURCE_LICENSES.md)にまとめています。

### USB経由の書込み

Demo:

```bash
pio run -e m5stamp-c3u -t upload --upload-port <serial-port>
```

Initial:

```bash
pio run -e m5stamp-c3u-initial -t upload --upload-port <serial-port>
```

### 公開snapshotのDemoへ戻す

通常は[ULSA EVO ESP32書込みページ](https://ulsa-evo.strvsn.net/firmware/)でDemoまたはInitialを選び、
USBからcleanなFull Flashへ戻せます。公開snapshotから自身でbuildする場合は次を使います。

変更していない公開snapshotのcloneで、次を実行します。

```bash
python3 scripts/restore_public_demo.py --port <serial-port>
```

partitionを変更した、起動しない、OTA slotが不明な場合は、ESP32だけを全消去してから
bootloader、partition table、boot application、Demoを復元します。

```bash
python3 scripts/restore_public_demo.py --port <serial-port> --full-erase
```

`--full-erase`はESP32 NVSと設定を消去します。STM32 firmware/EEPROMとSD cardは書き換えません。
自動resetで接続できない場合はUSBを外し、本体button（GPIO9）を押したままUSBを接続し、ROM serial
portが現れたらbuttonを離して再実行します。このROM download経路はcustom applicationに依存しません。

この手順は公開snapshotのsourceから機能的な標準Demoを復元します。local buildが配布済み公式binaryと
byte単位で同一になることまでは保証しません。

full-flashやpartition table変更の前に、必要なデータをバックアップしてください。custom code自体は無保証ですが、[カスタムFW互換性契約](CUSTOM_FIRMWARE_COMPATIBILITY.md)を満たすDemo buildには、記載された範囲の技術的interface互換性があります。

## English

### Requirements

- Python 3.12 or later
- PlatformIO Core 6.1.18
- Git
- USB connection to the supported ESP32-C3 target

### Build both profiles

```bash
pio run -e m5stamp-c3u
pio run -e m5stamp-c3u-initial
```

### Verify the public snapshot

```bash
python3 scripts/verify_public_source.py --build
```

To check only the minimum ULSA EVO interoperability contract:

```bash
python3 scripts/verify_ulsa_evo_compatibility.py
```

See [Open-source licenses and source offer](OPEN_SOURCE_LICENSES.md) for the complete third-party license bundle and the corresponding-source/relink route for LGPL libraries.

### Flash over USB

Demo:

```bash
pio run -e m5stamp-c3u -t upload --upload-port <serial-port>
```

Initial:

```bash
pio run -e m5stamp-c3u-initial -t upload --upload-port <serial-port>
```

### Return to the public-snapshot Demo

The normal recovery route is the [ULSA EVO ESP32 writer](https://ulsa-evo.strvsn.net/firmware/), which restores
a clean Demo or Initial Full Flash over USB. To build from the public snapshot yourself, use the following route.

Run this from an unmodified clone of the public snapshot:

```bash
python3 scripts/restore_public_demo.py --port <serial-port>
```

If partitions were changed, the device no longer boots, or its OTA slot is unknown, erase only the ESP32 flash
and restore the bootloader, partition table, boot application, and Demo:

```bash
python3 scripts/restore_public_demo.py --port <serial-port> --full-erase
```

`--full-erase` clears ESP32 NVS and settings. It does not write STM32 firmware/EEPROM or the SD card. If automatic
reset cannot connect, disconnect USB, hold the device button (GPIO9), reconnect USB while holding it, then release
the button when the ROM serial port appears. This ROM download path is independent of the custom application.

This restores a functionally standard Demo from the public snapshot source. A local build is not guaranteed to be
byte-for-byte identical to the distributed official binary.

Back up any required data before a full-flash or partition-table change. Custom code remains unwarranted, but a Demo build that meets the [Custom Firmware Compatibility](CUSTOM_FIRMWARE_COMPATIBILITY.md) contract has the technical interface interoperability stated there.
