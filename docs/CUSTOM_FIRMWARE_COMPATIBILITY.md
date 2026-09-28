# Custom Firmware Compatibility / カスタムFW互換性

## 日本語

ULSA EVOのSTM32は、通常計測時にESP32 firmwareのhash、署名、commit、公式版かどうかを
認証しません。公開snapshotのDemo profileを変更してUSBから書き込んでも、利用する機能に対応する
次の契約を満たせば動作できます。

### 用途別の最低条件

| 目的 | 保持する条件 | 公式partition layoutの完全一致 |
| --- | --- | --- |
| 風向・風速の取得 | ESP32-C3 pinと公開I2C register/snapshot契約 | 不要 |
| 公式appの基本計測 | `0x181A` service、風向`0x2A73`、風速`0x2A72` Notify | 不要 |
| 公式appの全機能 | ULSA service、Capabilities、使用するcontrol protocol | 機能による |
| ESP32自己OTA | `otadata`と2つのOTA application slot | roleと容量が必要 |
| 公式STM32更新 | update contract、公開署名鍵、`stm32pkg`、BOOT0/NRST/UART | `stm32pkg`が必要 |
| Initial factory/recovery | clean Initial identityと公式layout | 必要 |

custom開発には`m5stamp-c3u`のDemo profileを使ってください。Initialはfactory/recoveryの整合性を
確認するために意図的に厳しいprofileであり、一般的なcustom runtimeではありません。

### 通常計測

- I2C SDA GPIO1、SCL GPIO0、既定400 kHz、address `0x50`
- `WHOAMI` register `0xFF`の値は`0xEA`
- 公開register version `0x0E`以上
- register `0x20`から14 byteのsnapshotを読み、statusとsequenceを検証
- UART fallbackはGPIO3/10、115200 8N1、8-token frame

BLE、SD、LED、button、network、表示、後処理は、この計測契約を壊さない範囲で自由に変更できます。

### 保証範囲と条件付き範囲

記載した契約を保持すれば、公開I2C計測、公式appの基本風向・風速表示、Capabilitiesで表明した
各control、update contract v3の公式STM32更新を利用できます。ESP32-C3のUSB ROM download経路も
実行中のcustom applicationとは独立して利用できます。

custom FWがESP32 OTA serviceや2-slot layoutを削除した場合、公式app OTAからの復帰は保証しません。
custom code自体の品質、local buildと公式binaryのbyte一致、古いforkへ取り込んでいない署名鍵rotation、
全消去後のESP32 NVS保持は保証範囲外です。

通常の完全復旧は[ULSA EVO ESP32書込みページ](https://ulsa-evo.strvsn.net/firmware/)を使います。
公開済みstable ReleaseのSHA-256を確認し、ESP32-C3を識別してからDemoまたはInitialを全体書込みします。
sourceから復旧する場合は[ビルドと書込み](BUILD_AND_FLASH.md)を参照してください。

### 公式app

基本的な風向・風速表示にはEnvironmental Sensing service `0x181A`、Apparent Wind Direction
`0x2A73`、Apparent Wind Speed `0x2A72`をNotifyしてください。公式appの設定、SD、RTC、LED、OTA、
STM32更新を残す場合はULSA service `E147A12A-67FF-4249-930B-C35D372BA000`と、Capabilitiesが示す
各protocolを保持します。現行Capabilities protocolは1、BLE interface revisionは14です。

### partitionと更新

partition tableの完全一致は通常計測には不要です。ESP32自己OTAを使う場合は`otadata`、`app0`、
`app1`を、公式STM32更新を最大容量まで使う場合はdata subtype `0x40`、label `stm32pkg`、512 KiB以上を保持します。
公式full-flashとInitialを利用する場合は、offsetを含む公式layout全体を保持してください。

旧384 KiB layoutでも通常計測と現行packageは利用できます。384 KiBを超える将来packageを使うには、
公式Demo full-flashで現行layoutへ更新してください。この操作ではESP32 NVSが消去されます。

公式STM32 packageはEd25519署名、hash、target、update contract、内部の更新整合性を検証します。
ESP32はAES復号鍵を持たず、STM32 custom bootloaderがAES-256-GCM chunkを認証・復号します。
将来署名鍵がrotationした場合、custom forkにも新しい公開鍵と互換update実装を取り込む必要があります。
`minEsp32Fw`は案内情報で、公式SemVerだけではcustom forkを拒否しません。実際のmachine gateは
`minEsp32UpdateContract`です。

### 検証

機械可読な契約は`config/ulsa_evo_compatibility_contract.json`です。

```bash
python3 scripts/verify_ulsa_evo_compatibility.py
python3 scripts/verify_public_source.py --build
```

この契約はinterface互換性の範囲を定めます。custom code自体の欠陥、改造後の品質、破壊的な独自処理、
別hardwareの動作を保証するものではありません。

## English

The ULSA EVO STM32 does not authenticate the ESP32 application hash, signature, commit, or official-build
status during normal measurement. A USB-flashed custom Demo build remains interoperable when it preserves the
contracts for the features it claims to support.

### Minimum contract by feature

| Feature | Contract to preserve | Exact official partition layout |
| --- | --- | --- |
| Wind measurement | ESP32-C3 pins and the public I2C register/snapshot contract | Not required |
| Basic companion-app measurement | Service `0x181A` and direction `0x2A73` / speed `0x2A72` notifications | Not required |
| Full companion-app feature set | ULSA service, Capabilities, and each retained control protocol | Feature-dependent |
| ESP32 self-OTA | `otadata` and two OTA application slots | Required roles and capacity |
| Official STM32 update | Update contract, public signing key, `stm32pkg`, BOOT0/NRST/UART | `stm32pkg` required |
| Initial factory/recovery | Clean Initial identity and official layout | Required |

Use the `m5stamp-c3u` Demo profile as the customization base. Initial deliberately performs strict
factory/recovery integrity checks and is not the general custom runtime profile.

### Normal measurement

- I2C SDA GPIO1, SCL GPIO0, default 400 kHz, address `0x50`
- `WHOAMI` register `0xFF` returns `0xEA`
- public register version `0x0E` or newer
- coherent 14-byte snapshot beginning at register `0x20`, including status and sequence validation
- optional UART fallback on GPIO3/10 at 115200 8N1 with the bounded 8-token frame

BLE additions, SD behavior, LEDs, buttons, networking, presentation, and post-processing may be changed freely
without changing this measurement contract.

### Guaranteed scope and conditional scope

When the stated contracts are preserved, public I2C measurement, basic app wind notifications, advertised app
controls, and official STM32 update contract v3 are supported. The ESP32-C3 ROM USB download path also remains
available independently of the running custom application.

Companion-app ESP32 OTA is not guaranteed after a custom firmware removes its OTA service or two-slot layout.
Custom-code correctness, byte-identical local builds, signing-key rotations not merged into an old fork, and
retention of ESP32 NVS after a full erase are outside the guarantee.

To return an unmodified public snapshot to the device, use:

[ULSA EVO ESP32 writer](https://ulsa-evo.strvsn.net/firmware/) is the primary USB recovery path. It accepts only
a published stable release, verifies the selected Full Flash SHA-256, identifies ESP32-C3 before erase, and then
restores Demo or Initial. The source-based fallback is:

```bash
python3 scripts/restore_public_demo.py --port <serial-port>
python3 scripts/restore_public_demo.py --port <serial-port> --full-erase
```

The second command restores the current partition layout and clears ESP32 NVS. It does not write STM32
firmware/EEPROM or the SD card. See [Build and Flash](BUILD_AND_FLASH.md) for ROM-button entry details.

### Companion app

Basic wind presentation requires Environmental Sensing service `0x181A` and notifications for Apparent Wind
Direction `0x2A73` and Apparent Wind Speed `0x2A72`. To retain settings, SD, RTC, LED, OTA, and STM32 update
features in the official app, preserve ULSA service `E147A12A-67FF-4249-930B-C35D372BA000` and the protocols
advertised by Capabilities. The current Capabilities protocol is 1 and BLE interface revision is 14.

### Partitions and updates

The exact partition table is not required for normal measurement. ESP32 self-OTA needs `otadata`, `app0`, and
`app1`. Official STM32 update through the full supported size needs a data partition with subtype `0x40`, exact
label `stm32pkg`, and at least 512 KiB. Keep the complete official layout, including offsets, when using official
full-flash artifacts or Initial.

The legacy 384 KiB layout still supports normal measurement and the current package. A future package larger
than 384 KiB requires an official Demo full-flash to the current layout, which erases ESP32 NVS.

Official STM32 packages verify Ed25519 signatures, hashes, target, update contract, and internal update integrity.
The ESP32 does not contain the AES decryption key; the STM32 custom bootloader authenticates and decrypts the
AES-256-GCM chunks. A future signing-key rotation requires the custom fork to merge the new public key and
compatible updater implementation.
`minEsp32Fw` is advisory and does not reject a custom fork solely by official SemVer. The enforced machine gate
is `minEsp32UpdateContract`, allowing a compatible implementation to be backported.

### Verification

`config/ulsa_evo_compatibility_contract.json` is the machine-readable contract.

```bash
python3 scripts/verify_ulsa_evo_compatibility.py
python3 scripts/verify_public_source.py --build
```

This contract defines interface interoperability. It does not warrant custom-code correctness, modified-product
quality, destructive custom operations, or operation on other hardware.
