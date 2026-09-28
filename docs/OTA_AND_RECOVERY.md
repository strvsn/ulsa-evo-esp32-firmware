# OTA and Recovery / OTAと復旧

## 日本語

- DemoのESP32／STM32更新は連携アプリで準備し、案内された物理ボタン操作による確認が必要です。
- Initialは、物理ボタンを確認受付時間内に離した場合だけ、一時setup network `ULSA-EVO-INITIAL`を開始します。
- 保守判定までボタンを保持すると、OTAではなくSTM32 USB／UART手動bootloader経路へ入ります。
- 一時setup networkは汎用access pointではなく、timeout、cancel、完了時に停止します。
- ESP32 full-flash imageは、空または消去済みdevice向けのrecovery artifactです。対応リリースと同じapplication profileを含みます。
- custom DemoでESP32自己OTAを残すには`otadata`と2つのOTA application slotが必要です。
- 公式STM32更新を最大容量まで残すには、公開update実装、受理する署名公開鍵、512 KiB以上の`stm32pkg` partitionが必要です。

書込み中に電源を切らないでください。software buildの成功は、カスタムファームウェアや別hardware revisionの実機検証を代替しません。

## English

- Demo ESP32 and STM32 updates are prepared by the companion app and require the guided physical-button confirmation.
- Initial starts the temporary `ULSA-EVO-INITIAL` setup network only when the physical button is released within the accepted confirmation window.
- Holding the button to the maintenance threshold enters the manual STM32 USB/UART bootloader path instead of OTA.
- The temporary setup network is not a general-purpose access point and stops on timeout, cancellation, or completion.
- ESP32 full-flash images are recovery artifacts for an empty or erased device. They contain the same application profile as the corresponding release.
- A custom Demo that retains ESP32 self-OTA needs `otadata` and two OTA application slots.
- Retaining official STM32 updates through the full supported size requires the public updater, an accepted signing public key, and a `stm32pkg` partition of at least 512 KiB.

Do not interrupt power during a write. A successful software build does not replace real-device validation for custom firmware or another hardware revision.
