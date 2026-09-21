/**
 * @file cmd_help.cpp
 * @brief Helpコマンド実装
 */

#include <M5Unified.h>
#include "command_handler.h"

// 外部からCommandHandlerインスタンスを参照
extern CommandHandler cmdHandler;

/**
 * @brief helpコマンドハンドラー
 * @param argc 引数の数（未使用）
 * @param argv 引数配列（未使用）
 * @return true=成功
 */
bool cmdHelp(int argc, const String* argv) {
    (void)argc;   // 未使用警告を抑制
    (void)argv;
    
    cmdHandler.printHelp();
    return true;
}

/**
 * @brief exitコマンドハンドラー（コマンドモード終了）
 * @param argc 引数の数（未使用）
 * @param argv 引数配列（未使用）
 * @return true=成功
 */
bool cmdExit(int argc, const String* argv) {
    (void)argc;
    (void)argv;
    
    M5.Log.printf("Exiting command mode...\n");
    M5.Log.printf("Press button to toggle mode.\n\n");
    return true;
}
