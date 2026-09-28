/**
 * @file ota_arduino.cpp
 * @brief SoftAP Web OTA portal lifecycle（legacy filename）
 * @date 2025-12-07
 * 
 * 一時SoftAP、DNS、HTTP serverの開始・停止を実装する。
 * LAN/STA ArduinoOTAは製品surfaceに含めない。
 */

#include "ota_manager.h"
#include <M5Unified.h>

// ============================================
// WiFi設定ポータル
// ============================================
bool OtaManager::startPortal() {
  if (!_initialized) {
    // M5.Log.println("Portal: Not initialized");
    return false;
  }

  if (_portalActive) {
    return true;
  }

  _portalStopDeadline.clear();
  _stm32WriteStartDeadline.clear();

  // Runtime code must never create an unprepared open AP. App sessions are
  // prepared after physical authorization; manual sessions are boot-held
  // Recovery sessions prepared with an explicit purpose.
  if (_portalToken[0] == '\0') {
    setLastError("Portal session not prepared");
    return false;
  }
  
  // M5.Log.println("Portal: Starting WiFi setup portal...");
  clearLastError();
  resetWebOtaProgress();
  snprintf(_portalIpString, sizeof(_portalIpString), "%d.%d.%d.%d", PORTAL_IP);
  
  // 既存のWiFi接続を切断
  WiFi.disconnect(true);
  delay(100);
  
  // APモードで起動
  IPAddress apIP(PORTAL_IP);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0))) {
    setLastError("SoftAP config failed");
    clearPortalSession();
    _state = OTA_STATE_ERROR;
    return false;
  }
  const int maxConnections = _initialSetupPortal ? 1 : 4;
  if (!WiFi.softAP(_portalSsid, getPortalPassword(), 1, false,
                   maxConnections)) {
    setLastError("SoftAP start failed");
    clearPortalSession();
    _state = OTA_STATE_ERROR;
    WiFi.mode(WIFI_OFF);
    return false;
  }
  
  // M5.Log.printf("Portal: AP started, SSID: %s\n", _portalSsid);
  // M5.Log.printf("Portal: IP: %s\n", WiFi.softAPIP().toString().c_str());
  
  // DNSワイルドカードは常に起動する。
  // iOSはSoftAP接続時にcaptive portal検出用の名前解決を行うため、
  // app-driven OTAでもDNSを止めると接続完了判定が不安定になる。
  if (_pDnsServer == nullptr) {
    _pDnsServer = new DNSServer();
  }
  _pDnsServer->setTTL(0);
  _pDnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  _pDnsServer->start(DNS_PORT, "*", apIP);
  
  // Webサーバー起動
  if (_pWebServer == nullptr) {
    _pWebServer = new WebServer(80);
  }
  const char* headerKeys[] = { "X-ULSA-STM32-Package-SHA256" };
  _pWebServer->collectHeaders(headerKeys, 1);
  setupPortalHandlers();
  _pWebServer->begin();
  
  _portalActive = true;
  _portalStartTime = millis();
  _state = OTA_STATE_PORTAL_ACTIVE;
  
  // M5.Log.printf("Portal: Ready (timeout: %ds)\n", PORTAL_TIMEOUT);
  // M5.Log.println("Portal: ========================================");
  // M5.Log.println("Portal: If captive portal doesn't open automatically,");
  // M5.Log.printf("Portal: Open browser and go to: http://%s/\n", WiFi.softAPIP().toString().c_str());
  // M5.Log.println("Portal: ========================================");
  return true;
}

void OtaManager::stopPortal() {
  if (!_portalActive) {
    _portalStopDeadline.clear();
    _stm32WriteStartDeadline.clear();
    return;
  }

  if (isUpdating()) {
    M5.Log.println("Portal: Stop ignored during update");
    return;
  }

  if (_portalPurpose == OtaPortalPurpose::Stm32Update) {
    const stm32_update::UpdatePhase phase = _stm32UpdateManager.getPhase();
    if (stm32_update::updatePhaseHasActiveWriter(phase)) {
      M5.Log.println("Portal: Stop ignored during STM32 write");
      return;
    }
    if (_stm32UpdateManager.shouldDiscardScratchOnPortalStop() &&
        !_stm32UpdateManager.discardScratchBeforeWrite()) {
      M5.Log.println("Portal: Stop blocked because stm32pkg erase failed");
      _portalStopDeadline.clear();
      _portalStartTime = millis();
      return;
    }
  }
  
  M5.Log.println("Portal: Stopping...");
  restoreWatchdogAfterWebOta();
  
  if (_pWebServer) {
    _pWebServer->stop();
    delete _pWebServer;
    _pWebServer = nullptr;
  }
  
  if (_pDnsServer) {
    _pDnsServer->stop();
    delete _pDnsServer;
    _pDnsServer = nullptr;
  }
  
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  
  _portalActive = false;
  _portalStopDeadline.clear();
  _stm32WriteStartDeadline.clear();
  _state = OTA_STATE_WIFI_DISCONNECTED;
  resetWebOtaProgress();
  clearPortalSession();
  
  // M5.Log.println("Portal: Stopped");
}

bool OtaManager::isPortalActive() const {
  return _portalActive;
}

bool OtaManager::isPortalClientConnected() const {
  return _portalActive && WiFi.softAPgetStationNum() > 0;
}

bool OtaManager::isAppDrivenPortal() const {
  return _appDrivenPortal;
}
