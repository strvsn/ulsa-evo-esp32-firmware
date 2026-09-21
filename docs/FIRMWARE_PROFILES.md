# Firmware Profiles / ファームウェアプロファイル

## 日本語

### Demo

PlatformIO環境: `m5stamp-c3u`

BLE計測・設定連携、SD logging、RTC、ESP32 OTA、公式STM32更新transportを提供します。

購入者によるcustom開発は、このprofileを起点にします。通常計測に公式binaryのhash一致は不要です。

### Initial

PlatformIO環境: `m5stamp-c3u-initial`

物理ボタンによるInitialからDemoへの更新を含む、工場出荷時のsetupおよびrecovery経路を提供します。DemoのBLEアプリ連携serviceは提供しません。

Initialはclean identityと公式partition layoutを確認するため、一般的なcustom runtimeの起点ではありません。

両プロファイルは、同じsemantic version、source snapshot、partition contract、公開build設定を共有します。runtime modeや別source branchではなく、compile時に決まるプロファイルです。

## English

### Demo

PlatformIO environment: `m5stamp-c3u`

Provides BLE measurement and settings integration, SD logging, RTC support, ESP32 OTA, and the official STM32 update transport.

This is the customization base for product owners. Normal measurement does not require an exact official binary hash.

### Initial

PlatformIO environment: `m5stamp-c3u-initial`

Provides the factory setup and recovery path, including the physical-button Initial-to-Demo update flow. It does not provide the Demo BLE application service.

Initial validates a clean identity and the official partition layout, so it is not the general custom-runtime base.

Both profiles share one semantic version, source snapshot, partition contract, and public build configuration. They are compile-time profiles, not runtime modes or separate source branches.
