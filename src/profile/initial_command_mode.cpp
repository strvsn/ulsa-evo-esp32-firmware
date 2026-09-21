/**
 * @file initial_command_mode.cpp
 * @brief Initial profile USB command-mode composition and dispatch.
 */

#include "profile/initial_command_mode.h"

#include <M5Unified.h>
#include <esp_task_wdt.h>

#include "commands/command_handler.h"
#include "hardware/led_controller.h"

extern CommandHandler cmdHandler;
extern LedController ledCtrl;

void processInitialCommandMode() {
  static bool welcomeShown = false;

  if (!welcomeShown) {
    cmdHandler.printWelcome();
    welcomeShown = true;
  }

  cmdHandler.processUsbInput();

  ledCtrl.update();
  esp_task_wdt_reset();
  yield();
}
