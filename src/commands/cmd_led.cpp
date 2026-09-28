/**
 * @file cmd_led.cpp
 * @brief LEDコマンド実装
 */

#include <M5Unified.h>
#include "led_controller.h"

// 外部からLedControllerインスタンスを参照
extern LedController ledCtrl;

/**
 * @brief ledコマンドハンドラー
 * 
 * 使用例:
 *   led get              - 現在の輝度を表示
 *   led brightness 3     - 輝度をレベル3に設定（0=消灯、1-7）
 * 
 * @param argc 引数の数（argv[0]="led"を含む）
 * @param argv 引数配列（argv[0]="led"、argv[1]=サブコマンド）
 * @return true=成功、false=失敗
 */
bool cmdLed(int argc, const String* argv) {
  // サブコマンドチェック
  // argv[0]="led", argv[1]=サブコマンド（get/brightness）
  if (argc < 2) {
    M5.Log.printf("Usage: led <get|brightness> [value]\n");
    M5.Log.printf("Examples:\n");
    M5.Log.printf("  led get           : Show current brightness\n");
    M5.Log.printf("  led brightness 3 : Set brightness level (0=off, 1-7)\n");
    return false;
  }

  String subCmd = argv[1];  // サブコマンドはargv[1]
  subCmd.toLowerCase();

  if (subCmd == "get") {
    // 現在の輝度を表示
    M5.Log.printf("LED Brightness: level %d / %d (%d / 255)\n",
                  ledCtrl.getBrightnessLevel(),
                  LedBrightnessLevels::kCount - 1U,
                  ledCtrl.getBrightness());
    return true;
  }
  else if (subCmd == "brightness") {
    if (argc < 3) {  // led brightness <value> = 3個の引数が必要
      M5.Log.printf("Usage: led brightness <0-7>\n");
      return false;
    }

    int value = argv[2].toInt();  // 値はargv[2]
    if (value < 0 || value >= LedBrightnessLevels::kCount) {
      M5.Log.printf("Error: Brightness level must be 0-%d\n", LedBrightnessLevels::kCount - 1U);
      return false;
    }

    // 輝度を設定
    ledCtrl.setBrightness(LedBrightnessLevels::valueForLevel((uint8_t)value));
    
    // NVSに保存
    if (ledCtrl.saveBrightness()) {
      M5.Log.printf("LED Brightness set to level %d (%d / 255, saved to NVS)\n",
                    value, ledCtrl.getBrightness());
    } else {
      M5.Log.printf("LED Brightness set to level %d (%d / 255, NVS save failed)\n",
                    value, ledCtrl.getBrightness());
    }
    
    return true;
  }
  else {
    M5.Log.printf("Unknown subcommand: %s\n", subCmd.c_str());
    M5.Log.printf("Available: get, brightness\n");
    return false;
  }
}
