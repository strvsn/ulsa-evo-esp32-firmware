/**
 * @file command_handler.h
 * @brief コマンドハンドラー
 * 
 * コマンドの登録、実行、ヘルプ表示を管理します。
 * コマンド関数ポインタを登録し、コマンド文字列から適切な関数を呼び出します。
 */

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include "command_parser.h"
#include "usb_command_line_input.h"

// コマンド関数の型定義
// 引数: argc=引数数, argv=引数配列
// 戻り値: true=成功, false=失敗
typedef bool (*CommandFunction)(int argc, const String* argv);

// コマンド情報構造体
struct CommandInfo {
    String name;               // コマンド名（例: "rtc"）
    CommandFunction func;      // コマンド関数ポインタ
    String description;        // コマンドの説明文
};

/**
 * @brief コマンドハンドラークラス
 */
class CommandHandler {
public:
    /**
     * @brief コンストラクタ
     */
    CommandHandler();

    /**
     * @brief コマンドを登録
     * @param name コマンド名（例: "rtc"）
     * @param func コマンド関数ポインタ
     * @param description コマンドの説明文
     */
    void registerCommand(const String& name, CommandFunction func, const String& description);

    /**
     * @brief コマンド文字列を実行
     * @param line コマンド文字列（例: "rtc get"）
     * @return true=実行成功, false=コマンド不明または実行失敗
     */
    bool execute(const String& line);

    /**
     * @brief USB-CDC入力をbounded line buffer経由で処理
     */
    void processUsbInput();

    /**
     * @brief ヘルプメッセージを表示（全コマンド一覧）
     */
    void printHelp();

    /**
     * @brief コマンドモード開始時のウェルカムメッセージ表示
     */
    void printWelcome();

private:
    static const int MAX_COMMANDS = 16;   // 最大コマンド数
    CommandInfo _commands[MAX_COMMANDS];  // コマンド情報配列
    int _commandCount;                    // 登録済みコマンド数
    CommandParser _parser;                // コマンドパーサー
    UsbCommandLineInput _usbInput;         // Demo/Initial共通の固定長行入力

    /**
     * @brief コマンド名からコマンド情報を検索
     * @param name コマンド名
     * @return コマンド情報のポインタ、見つからない場合はnullptr
     */
    CommandInfo* findCommand(const String& name);
};

#endif // COMMAND_HANDLER_H
