/**
 * @file web_portal_handlers.cpp
 * @brief WiFi設定ポータルのWebサーバーハンドラ
 * @date 2025-12-07
 * 
 * OTAアップデート用Webサーバーエンドポイントの設定
 */

#include "ota_manager.h"
#include "web_portal_html.h"
#include <M5Unified.h>
#include <Update.h>
#include <esp_task_wdt.h>

static bool isBinFirmwareFilename(String filename) {
  filename.toLowerCase();
  return filename.endsWith(".bin");
}

static bool isStm32PackageFilename(String filename) {
  filename.toLowerCase();
  return filename.endsWith(".ulsa-stm32pkg");
}

static bool isLowerHex(const String& value, size_t expectedLength) {
  if (value.length() != expectedLength) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

static bool readJsonString(const String& json, const char* key,
                           size_t maxLength, String& value) {
  const String marker = String("\"") + key + "\"";
  int offset = json.indexOf(marker);
  if (offset < 0) return false;
  offset = json.indexOf(':', offset + marker.length());
  if (offset < 0) return false;
  ++offset;
  while (offset < (int)json.length() && json[offset] == ' ') ++offset;
  if (offset >= (int)json.length() || json[offset] != '"') return false;
  const int end = json.indexOf('"', offset + 1);
  if (end < 0 || (size_t)(end - offset - 1) > maxLength) return false;
  value = json.substring(offset + 1, end);
  return value.indexOf('\\') < 0;
}

static bool readJsonUint32(const String& json, const char* key,
                           uint32_t& value) {
  const String marker = String("\"") + key + "\"";
  int offset = json.indexOf(marker);
  if (offset < 0) return false;
  offset = json.indexOf(':', offset + marker.length());
  if (offset < 0) return false;
  ++offset;
  while (offset < (int)json.length() && json[offset] == ' ') ++offset;
  const int start = offset;
  uint64_t parsed = 0;
  while (offset < (int)json.length() && json[offset] >= '0' && json[offset] <= '9') {
    parsed = parsed * 10U + (uint32_t)(json[offset] - '0');
    if (parsed > UINT32_MAX) return false;
    ++offset;
  }
  if (offset == start) return false;
  while (offset < (int)json.length() && json[offset] == ' ') ++offset;
  if (offset < (int)json.length() && json[offset] != ',' && json[offset] != '}') {
    return false;
  }
  value = (uint32_t)parsed;
  return true;
}

static bool isSemanticVersion(const String& value) {
  if (value.length() == 0 || value.length() > INITIAL_SESSION_VERSION_LENGTH) return false;
  uint8_t dots = 0;
  bool digitInPart = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c >= '0' && c <= '9') {
      digitInPart = true;
    } else if (c == '.' && digitInPart && dots < 2) {
      ++dots;
      digitInPart = false;
    } else {
      return false;
    }
  }
  return dots == 2 && digitInPart;
}

static void sendNoStoreHeaders(WebServer* server) {
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
}

static void sendCorsHeaders(WebServer* server) {
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server->sendHeader("Access-Control-Allow-Headers", "Content-Type, X-ULSA-STM32-Package-SHA256");
  server->sendHeader("Access-Control-Allow-Private-Network", "true");
}

// ============================================
// ポータルハンドラ設定
// ============================================
void OtaManager::setupPortalHandlers() {
  // 共有のインスタンスポインタ（ラムダからアクセス用）
  static OtaManager* instance = this;
  
  auto buildPortalPage = [this](const char* htmlTemplate) {
    String html = applyStyle(htmlTemplate);
    html.replace("%VERSION%", String(getPortalFirmwareVersion()));
    html.replace("%IP%", WiFi.softAPIP().toString());
    html.replace("%TOKEN%", String(getPortalToken()));
    return html;
  };

  auto sendUploadPage = [this, buildPortalPage]() {
    String html = buildPortalPage(OTA_UPDATE_HTML);
    html.replace("%NODE_ID%", String(getNodeId()));
    sendNoStoreHeaders(_pWebServer);
    sendCorsHeaders(_pWebServer);
    _pWebServer->send(200, "text/html", html);
  };

  auto sendAppReturnPage = [this, buildPortalPage]() {
    String html = buildPortalPage(OTA_APP_RETURN_HTML);
    sendNoStoreHeaders(_pWebServer);
    sendCorsHeaders(_pWebServer);
    _pWebServer->send(200, "text/html", html);
  };

  auto sendPortalPage = [this, sendUploadPage, sendAppReturnPage]() {
    if (isAppDrivenPortal()) {
      sendAppReturnPage();
      return;
    }
    sendUploadPage();
  };

  auto sendCaptiveSuccess = [this, sendPortalPage]() {
    if (!isAppDrivenPortal()) {
      sendPortalPage();
      return;
    }

    sendNoStoreHeaders(_pWebServer);
    sendCorsHeaders(_pWebServer);
    _pWebServer->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  };

  auto sendCaptiveNoContent = [this, sendPortalPage]() {
    if (!isAppDrivenPortal()) {
      sendPortalPage();
      return;
    }

    sendNoStoreHeaders(_pWebServer);
    sendCorsHeaders(_pWebServer);
    _pWebServer->send(204, "text/plain", "");
  };

  // メインページ。アプリ主導OTAではアップロードフォームを出さない。
  _pWebServer->on("/", HTTP_GET, [sendPortalPage]() {
    sendPortalPage();
  });

  auto sendOptionsOk = [this]() {
    sendCorsHeaders(_pWebServer);
    _pWebServer->send(204, "text/plain", "");
  };
  _pWebServer->on("/status", HTTP_OPTIONS, sendOptionsOk);
  _pWebServer->on("/doUpdate", HTTP_OPTIONS, sendOptionsOk);
  _pWebServer->on("/initial/ota-session", HTTP_OPTIONS, sendOptionsOk);
  _pWebServer->on("/stm32/status", HTTP_OPTIONS, sendOptionsOk);
  _pWebServer->on("/stm32/package", HTTP_OPTIONS, sendOptionsOk);
  _pWebServer->on("/stm32/sync-probe", HTTP_OPTIONS, sendOptionsOk);
  _pWebServer->on("/stm32/write", HTTP_OPTIONS, sendOptionsOk);
  _pWebServer->on("/stm32/cancel", HTTP_OPTIONS, sendOptionsOk);

  auto requirePortalPurpose = [this](OtaPortalPurpose required, const char* requiredName) {
    if (getPortalPurpose() == required) {
      return true;
    }

    String json = "{\"error\":\"wrong_purpose\",\"required\":\"";
    json += requiredName;
    json += "\",\"purpose\":\"";
    json += getPortalPurposeString();
    json += "\"}";
    sendNoStoreHeaders(_pWebServer);
    _pWebServer->send(409, "application/json", json);
    return false;
  };

  _pWebServer->on("/initial/ota-session", HTTP_POST, [this]() {
    sendCorsHeaders(_pWebServer);
    sendNoStoreHeaders(_pWebServer);
    if (!isInitialSetupPortal()) {
      _pWebServer->send(404, "application/json", "{\"error\":\"not_found\"}");
      return;
    }

    const String body = _pWebServer->arg("plain");
    uint32_t contractVersion = 0;
    uint32_t revision = 0;
    String nonce;
    String sha256;
    String version;
    String commit;
    if (body.length() > 512U ||
        !readJsonUint32(body, "contractVersion", contractVersion) ||
        contractVersion != 1U ||
        !readJsonString(body, "clientNonce", INITIAL_SESSION_NONCE_HEX_LENGTH, nonce) ||
        !isLowerHex(nonce, INITIAL_SESSION_NONCE_HEX_LENGTH) ||
        !readJsonString(body, "artifactSha256", INITIAL_SESSION_SHA256_HEX_LENGTH, sha256) ||
        !isLowerHex(sha256, INITIAL_SESSION_SHA256_HEX_LENGTH) ||
        !readJsonString(body, "version", INITIAL_SESSION_VERSION_LENGTH, version) ||
        !isSemanticVersion(version) ||
        !readJsonUint32(body, "revision", revision) || revision == 0U ||
        !readJsonString(body, "commit", INITIAL_SESSION_COMMIT_LENGTH, commit) ||
        !isLowerHex(commit, INITIAL_SESSION_COMMIT_LENGTH)) {
      _pWebServer->send(400, "application/json", "{\"error\":\"invalid_session_request\"}");
      return;
    }

    if (!claimInitialDemoSession(nonce.c_str(), sha256.c_str(),
                                 version.c_str(), revision, commit.c_str())) {
      _pWebServer->send(409, "application/json", "{\"error\":\"session_claimed\"}");
      return;
    }
    _pWebServer->send(200, "application/json", buildInitialDemoSessionJson());
  });

  // OSのキャプティブポータル検出。アプリ主導時は成功応答にして小窓を出にくくする。
  _pWebServer->on("/hotspot-detect.html", HTTP_GET, [sendCaptiveSuccess]() {
    sendCaptiveSuccess();
  });
  _pWebServer->on("/library/test/success.html", HTTP_GET, [sendCaptiveSuccess]() {
    sendCaptiveSuccess();
  });
  _pWebServer->on("/generate_204", HTTP_GET, [sendCaptiveNoContent]() {
    sendCaptiveNoContent();
  });
  _pWebServer->on("/gen_204", HTTP_GET, [sendCaptiveNoContent]() {
    sendCaptiveNoContent();
  });

  // OTA状態取得（アプリ/ブラウザUI用）
  _pWebServer->on("/status", HTTP_GET, [this]() {
    sendCorsHeaders(_pWebServer);
    if (!isPortalTokenValid(_pWebServer->arg("token"))) {
      _pWebServer->send(403, "application/json", "{\"error\":\"forbidden\"}");
      return;
    }

    sendNoStoreHeaders(_pWebServer);
    _pWebServer->send(200, "application/json", buildPortalStatusJson());
  });

  _pWebServer->on("/stm32/status", HTTP_GET, [this, requirePortalPurpose]() {
    sendCorsHeaders(_pWebServer);
    if (!isPortalTokenValid(_pWebServer->arg("token"))) {
      _pWebServer->send(403, "application/json", "{\"error\":\"forbidden\"}");
      return;
    }
    if (!requirePortalPurpose(OtaPortalPurpose::Stm32Update, "stm32_update")) {
      return;
    }

    const String statusJson = buildStm32UpdateStatusJson();
    // Bind cleanup to the body actually returned. If the writer transitions
    // while JSON is being built, a nonterminal body never closes the portal;
    // a body that reports Complete is safe because Complete is terminal.
    const bool responseReportsComplete =
      statusJson.startsWith("{\"phase\":\"complete\"");
    sendNoStoreHeaders(_pWebServer);
    _pWebServer->send(200, "application/json", statusJson);
    if (responseReportsComplete &&
        stm32_update::updatePhaseShouldStopPortalAfterStatus(
          _stm32UpdateManager.getPhase())) {
      requestPortalStopAfterResponse();
    }
  });

  _pWebServer->on("/stm32/cancel", HTTP_POST, [this, requirePortalPurpose]() {
    sendCorsHeaders(_pWebServer);
    if (!isPortalTokenValid(_pWebServer->arg("token"))) {
      _pWebServer->send(403, "application/json", "{\"error\":\"forbidden\"}");
      return;
    }
    if (!requirePortalPurpose(OtaPortalPurpose::Stm32Update, "stm32_update")) {
      return;
    }

    if (!_stm32UpdateManager.cancel()) {
      _pWebServer->send(409, "application/json", buildStm32UpdateStatusJson());
      return;
    }

    sendNoStoreHeaders(_pWebServer);
    _pWebServer->send(200, "application/json", buildStm32UpdateStatusJson());
    requestPortalStopAfterResponse();
  });

  _pWebServer->on("/stm32/write", HTTP_POST, [this, requirePortalPurpose]() {
    sendCorsHeaders(_pWebServer);
    if (!isPortalTokenValid(_pWebServer->arg("token"))) {
      _pWebServer->send(403, "application/json", "{\"error\":\"forbidden\"}");
      return;
    }
    if (!requirePortalPurpose(OtaPortalPurpose::Stm32Update, "stm32_update")) {
      return;
    }

    if (_stm32UpdateManager.canWrite() && !stm32UpdateSessionMatchesVerifiedPackage()) {
      _stm32UpdateManager.abortPackageUpload("STM32 package does not match update session");
      _pWebServer->send(409, "application/json", buildStm32UpdateStatusJson());
      return;
    }

    if (!_stm32UpdateManager.startWrite(_pStm32Bootloader, &Serial1)) {
      _pWebServer->send(409, "application/json", buildStm32UpdateStatusJson());
      return;
    }

    sendNoStoreHeaders(_pWebServer);
    _pWebServer->send(202, "application/json", buildStm32UpdateStatusJson());
    requestStm32WriteStartAfterResponse();
  });

  _pWebServer->on("/stm32/sync-probe", HTTP_POST, [this, requirePortalPurpose]() {
    sendCorsHeaders(_pWebServer);
    if (!isPortalTokenValid(_pWebServer->arg("token"))) {
      _pWebServer->send(403, "application/json", "{\"error\":\"forbidden\"}");
      return;
    }
    if (!requirePortalPurpose(OtaPortalPurpose::Stm32Update, "stm32_update")) {
      return;
    }

    if (_stm32UpdateManager.canWrite() && !stm32UpdateSessionMatchesVerifiedPackage()) {
      _stm32UpdateManager.abortPackageUpload("STM32 package does not match update session");
      _pWebServer->send(409, "application/json", buildStm32UpdateStatusJson());
      return;
    }

    const bool ok = _stm32UpdateManager.probeBootloaderSync(_pStm32Bootloader, &Serial1);
    sendNoStoreHeaders(_pWebServer);
    _pWebServer->send(ok ? 200 : 409, "application/json", buildStm32UpdateStatusJson());
  });

  _pWebServer->on("/stm32/package", HTTP_POST,
    [this, requirePortalPurpose]() {
      restoreWatchdogAfterWebOta();
      sendCorsHeaders(_pWebServer);

      if (!isPortalTokenValid(_pWebServer->arg("token"))) {
        _pWebServer->send(403, "application/json", "{\"error\":\"forbidden\"}");
        return;
      }
      if (!requirePortalPurpose(OtaPortalPurpose::Stm32Update, "stm32_update")) {
        return;
      }

      if (_stm32UploadRejected) {
        const uint16_t status = _stm32UploadRejectStatus == 0
          ? 400
          : _stm32UploadRejectStatus;
        _pWebServer->send(status, "application/json", buildStm32UpdateStatusJson());
        return;
      }

      if (_stm32UpdateManager.getLastError()[0] != '\0') {
        _pWebServer->send(400, "application/json", buildStm32UpdateStatusJson());
        return;
      }

      sendNoStoreHeaders(_pWebServer);
      _pWebServer->send(200, "application/json", buildStm32UpdateStatusJson());
    },
    [this, requirePortalPurpose]() {
      HTTPUpload& upload = _pWebServer->upload();

      if (upload.status == UPLOAD_FILE_START) {
        _stm32UploadAccepted = false;
        _stm32UploadRejected = false;
        _stm32UploadRejectStatus = 0;
      } else if (_stm32UploadRejected) {
        return;
      }

      auto rejectUpload = [this](const char* error,
                                 uint16_t httpStatus,
                                 bool abortAcceptedUpload) {
        if (abortAcceptedUpload && _stm32UploadAccepted) {
          _stm32UpdateManager.abortPackageUpload(error);
        }
        _stm32UploadAccepted = false;
        _stm32UploadRejected = true;
        _stm32UploadRejectStatus = httpStatus;
        restoreWatchdogAfterWebOta();
      };

      if (!isPortalTokenValid(_pWebServer->arg("token"))) {
        rejectUpload("Invalid OTA token", 403, false);
        return;
      }
      if (getPortalPurpose() != OtaPortalPurpose::Stm32Update) {
        rejectUpload("Wrong portal purpose", 409, false);
        return;
      }

      if (upload.status == UPLOAD_FILE_START) {
        if (!isStm32PackageFilename(upload.filename)) {
          rejectUpload("STM32 package must be a .ulsa-stm32pkg file", 400, false);
          return;
        }

        detachWatchdogForWebOta();
        const String expectedSha256 = _pWebServer->header("X-ULSA-STM32-Package-SHA256");
        if (!_stm32UpdateManager.beginPackageUpload(
              _pWebServer->clientContentLength(),
              expectedSha256.c_str())) {
          rejectUpload(
            _stm32UpdateManager.isBusy()
              ? "STM32 update is busy"
              : "STM32 package upload could not start",
            _stm32UpdateManager.isBusy() ? 409 : 400,
            false);
          return;
        }
        clearLastError();
        resetWebOtaProgress();
        _totalBytes = _pWebServer->clientContentLength();
        _stm32UploadAccepted = true;
        if (instance->onStartCallback != nullptr) {
          instance->onStartCallback();
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!_stm32UploadAccepted) return;
        if (!_stm32UpdateManager.writePackageChunk(upload.buf, upload.currentSize)) {
          rejectUpload("STM32 package upload write failed", 400, true);
          return;
        }
        _uploadedBytes += upload.currentSize;
        const uint8_t progress = _totalBytes == 0
          ? 0
          : static_cast<uint8_t>(
              (_uploadedBytes * 100UL) / _totalBytes > 100UL
                ? 100UL
                : (_uploadedBytes * 100UL) / _totalBytes);
        _progress = progress;
        if (instance->onProgressCallback != nullptr) {
          instance->onProgressCallback(progress);
        }
        yield();
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!_stm32UploadAccepted) return;
        if (_stm32UpdateManager.finishPackageUpload(instance->onProgressCallback) &&
            !stm32UpdateSessionMatchesVerifiedPackage()) {
          rejectUpload("STM32 package does not match update session", 409, true);
        } else {
          _stm32UploadAccepted = false;
        }
        restoreWatchdogAfterWebOta();
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        rejectUpload("STM32 package upload aborted", 400, true);
      }
    }
  );
  
  // OTAアップデート処理
  _pWebServer->on("/doUpdate", HTTP_POST, 
    // アップロード完了後のレスポンス
    [this, requirePortalPurpose]() {
      restoreWatchdogAfterWebOta();
      sendCorsHeaders(_pWebServer);

      if (!isPortalTokenValid(_pWebServer->arg("token"))) {
        Update.abort();
        _state = OTA_STATE_PORTAL_ACTIVE;
        _pWebServer->send(403, "text/plain", "Forbidden");
        return;
      }
      if (isInitialSetupPortal() && !isInitialSessionClaimed()) {
        Update.abort();
        _state = OTA_STATE_PORTAL_ACTIVE;
        _pWebServer->send(409, "text/plain", "Initial setup session is not claimed");
        return;
      }
      if (!requirePortalPurpose(OtaPortalPurpose::Esp32Ota, "esp32_ota")) {
        Update.abort();
        _state = OTA_STATE_PORTAL_ACTIVE;
        return;
      }

      if (_webOtaRejected) {
        Update.abort();
        _state = OTA_STATE_PORTAL_ACTIVE;
        String errorMsg = getLastError()[0] ? String(getLastError()) : String("Upload rejected");
        _pWebServer->send(400, "text/plain", errorMsg);
        return;
      }

      bool hasError = Update.hasError();
      if (hasError) {
        String errorMsg = "Update failed: ";
        errorMsg += getLastError()[0] ? getLastError() : Update.errorString();
        _pWebServer->send(500, "text/plain", errorMsg);
        // M5.Log.printf("OTA Web: %s\n", errorMsg.c_str());
        
        if (instance->onErrorCallback) {
          instance->onErrorCallback(Update.errorString());
        }
        instance->_state = OTA_STATE_PORTAL_ACTIVE;
      } else {
        String html = applyStyle(OTA_SUCCESS_HTML);
        html.replace("%TOKEN%", String(getPortalToken()));
        _pWebServer->send(200, "text/html", html);
        // M5.Log.println("OTA Web: Update successful, rebooting...");
        
        if (instance->onEndCallback) {
          instance->onEndCallback();
        }
        
        delay(1000);
        ESP.restart();
      }
    },
    // ファイルアップロードハンドラ
    [this]() {
      HTTPUpload& upload = _pWebServer->upload();

      auto rejectUpload = [this](const char* error) {
        setLastError(error);
        _webOtaRejected = true;
        Update.abort();
        abortInitialImageValidation();
        restoreWatchdogAfterWebOta();
        _state = OTA_STATE_PORTAL_ACTIVE;
      };

      if (!isPortalTokenValid(_pWebServer->arg("token"))) {
        rejectUpload("Invalid OTA token");
        return;
      }
      if (isInitialSetupPortal() && !isInitialSessionClaimed()) {
        rejectUpload("Initial setup session is not claimed");
        return;
      }
      if (getPortalPurpose() != OtaPortalPurpose::Esp32Ota) {
        rejectUpload("Wrong portal purpose");
        return;
      }
      
      if (upload.status == UPLOAD_FILE_START) {
        // M5.Log.printf("OTA Web: Starting update, file: %s\n", upload.filename.c_str());
        clearLastError();
        resetWebOtaProgress();

        if (!isBinFirmwareFilename(upload.filename)) {
          rejectUpload("Firmware file must be a .bin file");
          return;
        }

        size_t maxSketchSpace = ESP.getFreeSketchSpace();
        maxSketchSpace = (maxSketchSpace > 0x1000) ? ((maxSketchSpace - 0x1000) & 0xFFFFF000) : 0;
        _totalBytes = _pWebServer->clientContentLength();
        if (maxSketchSpace == 0 || (_totalBytes > 0 && _totalBytes > maxSketchSpace)) {
          rejectUpload("Not enough OTA partition space");
          return;
        }
        if (!beginInitialImageValidation()) {
          rejectUpload(getLastError()[0] ? getLastError()
                                         : "Initial Demo validation failed");
          return;
        }
        
        // OTA中はウォッチドッグタイマーを無効化
        // ファームウェア書き込みは時間がかかるためタイムアウトを防止
        detachWatchdogForWebOta();
        // M5.Log.println("OTA Web: Watchdog disabled for update");
        
        instance->_state = OTA_STATE_WEB_OTA_UPDATING;
        instance->_progress = 0;
        instance->_uploadedBytes = 0;        // 累積サイズリセット
        instance->_lastProgressPercent = 0;  // 進捗ログリセット
        
        if (instance->onStartCallback) {
          instance->onStartCallback();
        }
        
        // OTAパーティションの利用可能サイズを指定して書き込み開始
        if (!Update.begin(maxSketchSpace, U_FLASH)) {
          // M5.Log.printf("OTA Web: Begin error - %s\n", Update.errorString());
          rejectUpload(Update.errorString());
        }
        
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_webOtaRejected) {
          return;
        }

        if (!feedInitialImageValidation(upload.buf, upload.currentSize)) {
          rejectUpload(getLastError()[0] ? getLastError()
                                         : "Initial Demo validation failed");
          return;
        }

        // ファームウェアデータ書き込み
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          // M5.Log.printf("OTA Web: Write error - %s\n", Update.errorString());
          rejectUpload(Update.errorString());
        } else {
          // 累積サイズを更新
          instance->_uploadedBytes += upload.currentSize;
          
          // 進捗計算（Content-Lengthからファイルサイズを取得）
          size_t totalSize = instance->_pWebServer->clientContentLength();
          if (totalSize > 0) {
            uint8_t pct = (uint8_t)((instance->_uploadedBytes * 100) / totalSize);
            instance->_progress = pct;
            
            // 10%ごとにログ出力（ログが多すぎるのを防止）
            if (pct / 10 > instance->_lastProgressPercent / 10) {
              // M5.Log.printf("OTA Web: Progress %d%% (%u / %u bytes)\n", 
              //               pct, instance->_uploadedBytes, totalSize);
              instance->_lastProgressPercent = pct;
            }
            
            if (instance->onProgressCallback) {
              instance->onProgressCallback(pct);
            }
          }
        }
        // yieldで他のタスクに制御を渡す
        yield();
        
      } else if (upload.status == UPLOAD_FILE_END) {
        if (_webOtaRejected) {
          Update.abort();
          restoreWatchdogAfterWebOta();
          instance->_state = OTA_STATE_PORTAL_ACTIVE;
          return;
        }

        if (!finishInitialImageValidation()) {
          rejectUpload(getLastError()[0] ? getLastError()
                                         : "Initial Demo validation failed");
          return;
        }

        // アップロード完了
        if (Update.end(true)) {
          // M5.Log.printf("OTA Web: Upload complete, size: %u bytes\n", instance->_uploadedBytes);
          instance->_progress = 100;
          if (instance->onProgressCallback) {
            instance->onProgressCallback(100);
          }
        } else {
          instance->setLastError(Update.errorString());
          M5.Log.printf("OTA Web: End error - %s\n", Update.errorString());
          instance->_state = OTA_STATE_PORTAL_ACTIVE;
        }
        
        // ウォッチドッグタイマーを再有効化（再起動前に）
        restoreWatchdogAfterWebOta();
        // M5.Log.println("OTA Web: Watchdog re-enabled");
        
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        // M5.Log.println("OTA Web: Upload aborted");
        instance->setLastError("Upload aborted");
        Update.abort();
        instance->_state = OTA_STATE_PORTAL_ACTIVE;
        
        // ウォッチドッグタイマーを再有効化
        restoreWatchdogAfterWebOta();
        // M5.Log.println("OTA Web: Watchdog re-enabled");
      }
    }
  );
  
  // 未知パスは手動Web OTAならアップロード画面、アプリ主導OTAならアプリ復帰画面にする。
  _pWebServer->onNotFound([sendPortalPage]() {
    sendPortalPage();
  });
}
