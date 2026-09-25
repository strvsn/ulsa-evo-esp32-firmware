/**
 * @file command_parser.cpp
 * @brief コマンド文字列パーサー実装
 */

#include "command_parser.h"

/**
 * @brief コマンド文字列を解析してコマンド名と引数に分割
 * @param line 入力文字列（改行含む可）
 * @return true=解析成功, false=空行またはエラー
 */
bool CommandParser::parse(const String& line) {
    _argc = 0;

    // 前後の空白と改行を削除
    String trimmedLine = trim(line);
    
    // 空行はスキップ
    if (trimmedLine.length() == 0) {
        return false;
    }

    // スペースで分割
    int startPos = 0;
    while (startPos < trimmedLine.length() && _argc < CMD_MAX_ARGS) {
        // 先頭の空白をスキップ
        while (startPos < trimmedLine.length() && trimmedLine[startPos] == ' ') {
            startPos++;
        }
        
        if (startPos >= trimmedLine.length()) {
            break;
        }

        // 次のスペースまたは終端を探す
        int endPos = startPos;
        while (endPos < trimmedLine.length() && trimmedLine[endPos] != ' ') {
            endPos++;
        }

        // 引数を抽出
        _argv[_argc] = trimmedLine.substring(startPos, endPos);
        _argc++;

        startPos = endPos;
    }

    return _argc > 0;
}

/**
 * @brief 文字列の前後の空白・改行を削除
 * @param str 対象文字列
 * @return トリム後の文字列
 */
String CommandParser::trim(const String& str) {
    int start = 0;
    int end = str.length() - 1;

    // 先頭の空白・改行を検索
    while (start < str.length() && 
           (str[start] == ' ' || str[start] == '\t' || 
            str[start] == '\r' || str[start] == '\n')) {
        start++;
    }

    // 末尾の空白・改行を検索
    while (end >= start && 
           (str[end] == ' ' || str[end] == '\t' || 
            str[end] == '\r' || str[end] == '\n')) {
        end--;
    }

    return str.substring(start, end + 1);
}
