/**
 * @file command_parser.h
 * @brief コマンド文字列パーサー
 * 
 * スペース区切りのコマンド文字列を解析し、コマンド名と引数の配列に分割します。
 * 例: "rtc set 2026 1 13 15 30 0" → ["rtc", "set", "2026", "1", "13", "15", "30", "0"]
 */

#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <Arduino.h>

// コマンドパーサーの最大引数数
#define CMD_MAX_ARGS 16

/**
 * @brief コマンド文字列パーサークラス
 */
class CommandParser {
public:
    /**
     * @brief コマンド文字列を解析
     * @param line 入力文字列（改行含む可）
     * @return true=解析成功, false=空行またはエラー
     */
    bool parse(const String& line);

    /**
     * @brief コマンド名を取得
     * @return コマンド名（例: "rtc"）
     */
    String getCommand() const { return _argc > 0 ? _argv[0] : ""; }

    /**
     * @brief サブコマンド名を取得
     * @return サブコマンド名（例: "set"）、存在しない場合は空文字列
     */
    String getSubCommand() const { return _argc > 1 ? _argv[1] : ""; }

    /**
     * @brief 引数の数を取得
     * @return 引数の数（コマンド名を含む）
     */
    int getArgCount() const { return _argc; }

    /**
     * @brief 指定インデックスの引数を取得
     * @param index インデックス（0=コマンド名, 1=サブコマンド, 2以降=引数）
     * @return 引数文字列、範囲外の場合は空文字列
     */
    String getArg(int index) const {
        return (index >= 0 && index < _argc) ? _argv[index] : "";
    }

    /**
     * @brief すべての引数配列へのポインタを取得
     * @return 引数配列の先頭ポインタ
     */
    const String* getArgs() const { return _argv; }

private:
    int _argc;                    // 引数の数
    String _argv[CMD_MAX_ARGS];   // 引数配列

    /**
     * @brief 文字列の前後の空白を削除
     * @param str 対象文字列
     * @return トリム後の文字列
     */
    String trim(const String& str);
};

#endif // COMMAND_PARSER_H
