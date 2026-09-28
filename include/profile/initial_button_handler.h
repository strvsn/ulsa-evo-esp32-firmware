#ifndef ULSA_INITIAL_BUTTON_HANDLER_H
#define ULSA_INITIAL_BUTTON_HANDLER_H

#include <Arduino.h>

enum ButtonMode {
  BTN_MODE_NORMAL,
  BTN_MODE_BRIDGE,
  BTN_MODE_I2C_MEASURE,
  BTN_MODE_COMMAND
};

ButtonMode getButtonMode();
void setButtonMode(ButtonMode mode);
void processButton();
void handleWifiPortalToggle();
void handleStm32ModeToggle();
void handleModeSelection(ButtonMode target);

#endif
