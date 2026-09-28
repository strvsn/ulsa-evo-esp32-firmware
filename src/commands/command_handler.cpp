/**
 * @file command_handler.cpp
 * @brief コマンドハンドラー実装
 */

#include "command_handler.h"
#include <M5Unified.h>

/**
 * @brief コンストラクタ
 */
CommandHandler::CommandHandler() : _commandCount(0) {
}

/**
 * @brief コマンドを登録
 * @param name コマンド名（例: "rtc"）
 * @param func コマンド関数ポインタ
 * @param description コマンドの説明文
 */
void CommandHandler::registerCommand(const String& name, CommandFunction func, const String& description) {
    if (_commandCount >= MAX_COMMANDS) {
        M5.Log.printf("[CMD] Error: Maximum command count (%d) exceeded\n", MAX_COMMANDS);
        return;
    }

    _commands[_commandCount].name = name;
    _commands[_commandCount].func = func;
    _commands[_commandCount].description = description;
    _commandCount++;
}

/**
 * @brief コマンド文字列を実行
 * @param line コマンド文字列（例: "rtc get"）
 * @return true=実行成功, false=コマンド不明または実行失敗
 */
bool CommandHandler::execute(const String& line) {
    // コマンド文字列を解析
    if (!_parser.parse(line)) {
        // 空行はスキップ
        return true;
    }

    String cmdName = _parser.getCommand();
    
    // コマンドを検索
    CommandInfo* cmd = findCommand(cmdName);
    if (cmd == nullptr) {
        M5.Log.printf("Unknown command: %s\n", cmdName.c_str());
        M5.Log.printf("Type 'help' for available commands.\n");
        return false;
    }

    // コマンド関数を実行
    bool result = cmd->func(_parser.getArgCount(), _parser.getArgs());
    
    return result;
}

void CommandHandler::processUsbInput() {
    size_t processed = 0;
    while (Serial.available() > 0 &&
           processed < UsbCommandLineInput::MAX_LENGTH) {
        const char c = Serial.read();
        ++processed;
        switch (_usbInput.feed(c)) {
            case UsbCommandLineEvent::CharacterAppended:
                Serial.write(c);
                break;
            case UsbCommandLineEvent::CharacterRemoved:
                Serial.write('\b');
                Serial.write(' ');
                Serial.write('\b');
                break;
            case UsbCommandLineEvent::LineReady:
                Serial.println();
                execute(String(_usbInput.line()));
                Serial.print("> ");
                _usbInput.clearLine();
                break;
            case UsbCommandLineEvent::EmptyLine:
                Serial.println();
                break;
            case UsbCommandLineEvent::LineRejected:
                Serial.println();
                Serial.printf(
                    "ERROR: Command line too long (max %u bytes); discarded.\n",
                    (unsigned)UsbCommandLineInput::MAX_LENGTH);
                Serial.print("> ");
                break;
            case UsbCommandLineEvent::None:
            default:
                break;
        }
    }
}

/**
 * @brief ヘルプメッセージを表示（全コマンド一覧）
 */
void CommandHandler::printHelp() {
    M5.Log.printf("\n=== Available Commands ===\n");
    for (int i = 0; i < _commandCount; i++) {
        M5.Log.printf("  %-12s : %s\n", 
                      _commands[i].name.c_str(), 
                      _commands[i].description.c_str());
    }
    M5.Log.printf("\nType 'exit' to leave command mode.\n\n");
}

/**
 * @brief コマンドモード開始時のウェルカムメッセージ表示
 */
void CommandHandler::printWelcome() {
    M5.Log.printf("\n");
    M5.Log.printf("========================================\n");
    M5.Log.printf("   ULSA EVO Command Mode\n");
    M5.Log.printf("========================================\n");
    M5.Log.printf("Type 'help' for available commands.\n");
    M5.Log.printf("Type 'exit' to leave command mode.\n");
    M5.Log.printf("========================================\n\n");
}

/**
 * @brief コマンド名からコマンド情報を検索
 * @param name コマンド名
 * @return コマンド情報のポインタ、見つからない場合はnullptr
 */
CommandInfo* CommandHandler::findCommand(const String& name) {
    for (int i = 0; i < _commandCount; i++) {
        if (_commands[i].name.equalsIgnoreCase(name)) {
            return &_commands[i];
        }
    }
    return nullptr;
}
