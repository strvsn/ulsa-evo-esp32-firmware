# Public Interfaces / 公開インターフェース

## 日本語

ESP32ファームウェアは、ULSA EVOの公開計測・設定インターフェースを使用します。公開データモデルは次を含みます。

- 0／重複／変更可能なユーザー設定Node ID labelとstatus
- 風向と風速
- A／B軸風速成分と機首方向成分
- 温度と音速
- 公開出力周期と設定値
- ファームウェアversion

Demoは計測、時刻、logging、device情報、設定、reset、ESP32 OTA、公式STM32更新制御のBLE serviceを提供します。InitialはDemo導入に必要な物理ボタン操作と一時Wi-Fi setup経路を提供します。

### RTCとtimezone

PCF8563はUTCを保持します。DemoのCTS Current Time `0x2A2B`は設定済みIANA地域のローカル暦をRead／Write／Notifyし、Local Time Information `0x2A0F`は標準UTC差と現在のDST差をReadします。`E147A12A-67FF-4249-930B-C35D372BA019`はUTC Unix秒とAceTime stable zone IDを同時設定するRTC Timezone Controlです。requestは6-byte `SET_ZONE`または14-byte `SYNC_UTC_AND_ZONE`、Read statusは28 bytesで、全multi-byte値はlittle-endianです。このcharacteristicはNotifyを持たないためgenerationをReadでpollし、writeは本体ごとに直列化します。Capabilities byte 7 bit 0がこの機能を示し、対応interface revisionは14です。

旧RTC値は自動移行されず、最初の明示同期まで時刻未確定です。DST gap／foldのローカル時刻は推測せず拒否します。Initialは同じUTC／timezone基盤とUSB CLIを持ちますが、このBLE characteristicは公開しません。

用途ごとに保持すべきinterfaceとpartitionの範囲は[カスタムFW互換性](CUSTOM_FIRMWARE_COMPATIBILITY.md)を参照してください。

超音波のraw time-of-flight、pulse選択状態、calibration内部情報、計測algorithm診断は公開インターフェースではなく、標準プロファイルにも含まれません。

## English

The ESP32 firmware consumes the public ULSA EVO measurement and configuration interface. The public data model includes:

- user-configurable Node ID label (zero, duplicates, and changes are valid) and status
- wind direction and wind speed
- A/B-axis wind components and heading component
- temperature and sound speed
- public output cadence and configuration values
- firmware version

Demo exposes BLE services for measurement, time, logging, device information, configuration, reset, ESP32 OTA, and official STM32 update control. Initial exposes the physical-button and temporary Wi-Fi setup path required to install Demo.

### RTC and timezone

The PCF8563 stores UTC. Demo CTS Current Time `0x2A2B` reads, writes, and notifies local civil time in the configured IANA zone. Local Time Information `0x2A0F` reads the standard UTC offset and current DST offset. RTC Timezone Control `E147A12A-67FF-4249-930B-C35D372BA019` synchronizes Unix seconds and an AceTime stable zone ID. Its requests are a 6-byte `SET_ZONE` or 14-byte `SYNC_UTC_AND_ZONE`; its Read status is 28 bytes, with all multi-byte fields little-endian. The characteristic has no Notify property: poll generation with Read and serialize writes per device. Capabilities byte 7 bit 0 advertises the feature at interface revision 14.

Legacy RTC values are not migrated and remain unknown until the first explicit sync. Local times in a DST gap or fold are rejected rather than guessed. Initial shares the UTC/timezone core and USB CLI but does not expose this BLE characteristic.

See [Custom Firmware Compatibility](CUSTOM_FIRMWARE_COMPATIBILITY.md) for the interfaces and partition roles required by each feature tier.

Raw ultrasonic time-of-flight data, pulse-selection state, calibration internals, and measurement-algorithm diagnostics are not public interfaces and are not present in the standard profiles.
