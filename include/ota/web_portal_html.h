/**
 * @file web_portal_html.h
 * @brief WiFi設定ポータルのHTMLテンプレート宣言
 * @date 2025-12-07
 */

#ifndef WEB_PORTAL_HTML_H
#define WEB_PORTAL_HTML_H

#include <Arduino.h>

// HTMLテンプレート（PROGMEM）
extern const char COMMON_STYLE[] PROGMEM;
extern const char OTA_UPDATE_HTML[] PROGMEM;
extern const char OTA_APP_RETURN_HTML[] PROGMEM;
extern const char OTA_SUCCESS_HTML[] PROGMEM;

// ヘルパー関数
String applyStyle(const char* html);
const char* getPortalFirmwareVersion();

#endif // WEB_PORTAL_HTML_H
