/**
 * @file ota_manager.h
 * @brief WiFi OTAアップデートモジュール
 * @date 2025-12-03
 * 
 * 一時SoftAPとtoken付きHTTPによるfirmware OTAを提供する。
 * LAN/STA ArduinoOTAは製品surfaceに含めない。
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include "ota/esp32_ota_image_validator.h"
#include "ota/portal_stop_deadline.h"
#include "stm32_update/stm32_update_manager.h"

class STM32Bootloader;
class UlsaEvoI2cClient;

// ============================================
// OTA設定
// ============================================

// WiFiポータル設定
#define PORTAL_SSID_PREFIX    "ULSA-EVO-OTA-"
#define RECOVERY_ESP32_SSID_PREFIX "ULSA-EVO-ESP32-REC-"
#define INITIAL_SETUP_SSID    "ULSA-EVO-INITIAL"
#define INITIAL_SETUP_PASSWORD "ulsa-evo-initial"
#define PORTAL_PASSWORD       ""            // オープンアクセス
#define PORTAL_IP             192,168,4,1
#define PORTAL_TIMEOUT        300           // ポータルタイムアウト（秒）
#define DNS_PORT              53
#define PORTAL_PASSWORD_LENGTH 12
#define PORTAL_PASSWORD_BUFFER_LENGTH 63
#define PORTAL_TOKEN_LENGTH   32
#define PORTAL_SESSION_SUFFIX_LENGTH 3
#define PORTAL_RESPONSE_GRACE_MS 250
#define STM32_WRITE_RESPONSE_GRACE_MS 250
#define PORTAL_ERROR_LENGTH   96
#define INITIAL_SESSION_NONCE_HEX_LENGTH 64
#define INITIAL_SESSION_SHA256_HEX_LENGTH 64
#define INITIAL_SESSION_COMMIT_LENGTH 40
#define INITIAL_SESSION_VERSION_LENGTH 11
#define STM32_UPDATE_SESSION_TARGET_MAX_LEN 32
#define STM32_UPDATE_SESSION_RELEASE_TAG_MAX_LEN 64

/**
 * @brief OTA状態
 */
enum OtaState {
  OTA_STATE_DISABLED = 0,
  OTA_STATE_WIFI_DISCONNECTED = 1,
  OTA_STATE_RESERVED_STA_CONNECTING = 2,
  OTA_STATE_PORTAL_ACTIVE = 3,
  OTA_STATE_RESERVED_LAN_READY = 4,
  OTA_STATE_RESERVED_LAN_UPDATING = 5,
  OTA_STATE_WEB_OTA_UPDATING = 6,
  OTA_STATE_ERROR = 7,
};

// BLE OTA Statusはこの値をraw byteで公開するため、廃止したLAN状態も予約してABIを維持する。
static_assert(OTA_STATE_DISABLED == 0, "OtaState BLE ABI changed");
static_assert(OTA_STATE_WIFI_DISCONNECTED == 1, "OtaState BLE ABI changed");
static_assert(OTA_STATE_RESERVED_STA_CONNECTING == 2, "OtaState BLE ABI changed");
static_assert(OTA_STATE_PORTAL_ACTIVE == 3, "OtaState BLE ABI changed");
static_assert(OTA_STATE_RESERVED_LAN_READY == 4, "OtaState BLE ABI changed");
static_assert(OTA_STATE_RESERVED_LAN_UPDATING == 5, "OtaState BLE ABI changed");
static_assert(OTA_STATE_WEB_OTA_UPDATING == 6, "OtaState BLE ABI changed");
static_assert(OTA_STATE_ERROR == 7, "OtaState BLE ABI changed");

enum class OtaPortalPurpose : uint8_t {
  Esp32Ota = 0,
  Stm32Update = 1,
};

/**
 * @brief OTAマネージャークラス
 * 
 * SoftAP Web OTAとSTM32 update portalを管理
 */
class OtaManager {
public:
  OtaManager();
  
  /**
   * @brief 初期化（WiFi接続なし）
   * @param nodeId ユーザー設定Node label（SoftAP SSID表示に使用）
   * @param stm32Bootloader STM32 BOOT0/NRST control object
   * @param i2cClient STM32 FW version readback client
   */
  void begin(uint8_t nodeId = 0,
             STM32Bootloader* stm32Bootloader = nullptr,
             UlsaEvoI2cClient* i2cClient = nullptr);
  
  /**
   * @brief OTA処理（毎ループ呼び出し）
   */
  void update();
  
  /**
   * @brief 現在の状態を取得
   * @return OtaState
   */
  OtaState getState() const;
  
  /**
   * @brief 状態を文字列で取得
   * @return 状態文字列
   */
  const char* getStateString() const;
  
  /**
   * @brief アップデート中か
   * @return true: アップデート中
   */
  bool isUpdating() const;
  
  /**
   * @brief OTA表示用Node labelを取得
   * @return 現在のinformational label
   */
  uint8_t getNodeId() const { return _nodeId; }
  
  /**
   * @brief アップデート進捗を取得（0-100%）
   * @return 進捗率
   */
  uint8_t getProgress() const;

  /**
   * @brief Web OTA受信済みバイト数を取得
   * @return 受信済みbytes
   */
  size_t getUploadedBytes() const;

  /**
   * @brief Web OTAアップロード全体サイズを取得
   * @return Content-Length bytes。不明時0
   */
  size_t getTotalBytes() const;

  /**
   * @brief ポータル停止までの残り秒数を取得
   * @return 残り秒数。非アクティブ時0
   */
  uint32_t getPortalRemainingSeconds() const;

  /**
   * @brief 最後のWeb OTAエラーを取得
   * @return エラー文字列。エラーなしなら空文字
   */
  const char* getLastError() const;

  /**
   * @brief 現在のポータルトークンを取得
   * @return token文字列
   */
  const char* getPortalToken() const;

  /**
   * @brief 現在のポータルSSIDを取得
   * @return SSID文字列
   */
  const char* getPortalSsid() const;

  /**
   * @brief 現在のポータルパスワードを取得
   * @return password文字列。オープンAP時は空文字
   */
  const char* getPortalPassword() const;

  /**
   * @brief ポータルIPアドレス文字列を取得
   * @return SoftAP IP文字列
   */
  const char* getPortalIpString() const;

  /**
   * @brief ポータルトークンを検証
   * @param token HTTP query token
   * @return true: token一致
   */
  bool isPortalTokenValid(const String& token) const;

  /**
   * @brief アプリ主導更新用にSSID/password/tokenを先に生成
   * @param purpose 共通SoftAP上で扱う更新目的
   * @return true: 準備成功
   */
  bool preparePortalSession(OtaPortalPurpose purpose = OtaPortalPurpose::Esp32Ota);

  /**
   * @brief boot-held physical recovery用のopen SoftAP sessionを準備
   */
  bool prepareRecoveryPortalSession(OtaPortalPurpose purpose);

  /**
   * @brief Initial profileからDemoを導入する固定SoftAP sessionを準備
   */
  bool prepareInitialDemoSession();

  /**
   * @brief Initial setup sessionを一つのapp nonceとDemo artifactへ束縛
   */
  bool claimInitialDemoSession(const char* clientNonce,
                               const char* artifactSha256,
                               const char* version,
                               uint32_t revision,
                               const char* commit);

  /** @brief Initial setup session response JSON */
  String buildInitialDemoSessionJson() const;

  /**
   * @brief STM32 update用にtarget/releaseを束縛したSoftAP sessionを作成
   *
   * Node label引数は旧wire/status ABIへのecho用であり、認可条件ではない。
   */
  bool prepareStm32UpdateSession(bool hasNodeLabel,
                                 uint8_t nodeLabel,
                                 const char* target,
                                 const char* releaseTag);

  /**
   * @brief 共通SoftAP上で扱う更新目的
   */
  OtaPortalPurpose getPortalPurpose() const { return _portalPurpose; }

  /**
   * @brief 共通SoftAP上で扱う更新目的を文字列で取得
   */
  const char* getPortalPurposeString() const;

  /**
   * @brief STM32 update sessionの対象target
   */
  const char* getStm32UpdateSessionTarget() const { return _stm32UpdateSessionTarget; }

  /**
   * @brief STM32 update sessionのreleaseTag
   */
  const char* getStm32UpdateSessionReleaseTag() const { return _stm32UpdateSessionReleaseTag; }

  /**
   * @brief STM32 update sessionにtarget/releaseTagが束縛されているか
   */
  bool isStm32UpdateSessionBound() const { return _stm32UpdateSessionBound; }

  /**
   * @brief STM32 update scratch/write managerの現在phase
   */
  stm32_update::UpdatePhase getStm32UpdatePhase() const {
    return _stm32UpdateManager.getPhase();
  }

  /** @brief Whether STM32 scratch state is canonical for Initial factory boot. */
  bool isStm32UpdateFactoryReady() const {
    return _stm32UpdateManager.isAvailable() &&
           _stm32UpdateManager.getPhase() == stm32_update::UpdatePhase::Idle;
  }

  /**
   * @brief OTA表示用Node labelを更新し、SoftAP SSIDへ反映
   * @param nodeId 新しいuser label
   * @return true: 更新成功または変更なし
   */
  bool updateNodeId(uint8_t nodeId);

  /**
   * @brief STM32の公開Node labelを読み、OTA/SoftAPの名称へbest effort同期する。
   * @return true: labelを正常に反映できた。falseでもOTA可否は変えない。
   */
  bool refreshNodeIdentityFromI2c();

  /**
   * @brief Keep the informational SSID label stable while a session exists.
   */
  bool isNodeLabelFrozen() const;

  /**
   * @brief 起動前のアプリ主導OTAセッションを破棄
   */
  void cancelPortalSession();

  /**
   * @brief WiFi設定ポータルを開始
   * キャプティブポータルを起動してWiFi設定を受付
   * @return true: SoftAP/DNS/WebServer 起動成功
   */
  bool startPortal();

  /**
   * @brief WiFi設定ポータルを停止
   */
  void stopPortal();

  /**
   * @brief アプリ主導OTAとして起動したポータルか
   * @return true: BLEで資格情報を共有してから起動したポータル
   */
  bool isAppDrivenPortal() const;

  /**
   * @brief boot-held Recoveryとして準備・起動したportalか
   */
  bool isRecoveryPortal() const { return _recoveryPortal; }
  bool isInitialSetupPortal() const { return _initialSetupPortal; }
  bool isInitialSessionClaimed() const { return _initialSessionClaimed; }
  const char* getInitialExpectedSha256() const { return _initialExpectedSha256; }
  const char* getInitialExpectedVersion() const { return _initialExpectedVersion; }
  uint32_t getInitialExpectedRevision() const { return _initialExpectedRevision; }
  const char* getInitialExpectedCommit() const { return _initialExpectedCommit; }

  /**
   * @brief ポータルがアクティブか
   * @return true: ポータル起動中
   */
  bool isPortalActive() const;

  /**
   * @brief SoftAPへclientがassociation済みか
   * app-driven portalのLEDを、端末のWi-Fi切替完了後にだけportal色へ
   * 進めるための表示用状態。認証やHTTP到達性の代用には使用しない。
   */
  bool isPortalClientConnected() const;

  /**
   * @brief アプリ主導STM32書込みtaskがSTM32 bootloader UARTを占有中か
   * @return true: main loop should not run the transparent bootloader bridge
   */
  bool isStm32BootloaderSessionActive() const;

  /**
   * @brief /stm32/status用JSONを生成
   */
  String buildStm32UpdateStatusJson() const;

  /**
   * @brief コールバック: アップデート開始時
   */
  void (*onStartCallback)();
  
  /**
   * @brief コールバック: アップデート終了時
   */
  void (*onEndCallback)();
  
  /**
   * @brief コールバック: 進捗更新時
   */
  void (*onProgressCallback)(uint8_t progress);
  
  /**
   * @brief コールバック: エラー時
   */
  void (*onErrorCallback)(const char* error);

private:
  OtaState _state;
  uint8_t _nodeId;
  char _portalSsid[32];
  char _portalPassword[PORTAL_PASSWORD_BUFFER_LENGTH + 1];
  char _portalIpString[16];
  uint8_t _progress;
  bool _initialized;
  bool _portalActive;
  uint32_t _portalStartTime;
  size_t _uploadedBytes;        // OTA用: アップロード済みバイト数
  size_t _totalBytes;           // OTA用: HTTP Content-Length
  uint8_t _lastProgressPercent; // OTA用: 最後にログ出力した進捗率
  char _portalToken[PORTAL_TOKEN_LENGTH + 1];
  char _lastPortalSessionSuffix[PORTAL_SESSION_SUFFIX_LENGTH + 1];
  PortalStopDeadline _portalStopDeadline;
  PortalStopDeadline _stm32WriteStartDeadline;
  char _lastError[PORTAL_ERROR_LENGTH];
  bool _webOtaRejected;
  bool _stm32UploadAccepted;
  bool _stm32UploadRejected;
  uint16_t _stm32UploadRejectStatus;
  bool _initialImageShaActive;
  mbedtls_sha256_context _initialImageShaContext;
  Esp32OtaImageValidator _initialImageValidator;
  bool _appDrivenPortal;
  bool _recoveryPortal;
  bool _initialSetupPortal;
  bool _initialSessionClaimed;
  char _initialClientNonce[INITIAL_SESSION_NONCE_HEX_LENGTH + 1];
  char _initialExpectedSha256[INITIAL_SESSION_SHA256_HEX_LENGTH + 1];
  char _initialExpectedVersion[INITIAL_SESSION_VERSION_LENGTH + 1];
  uint32_t _initialExpectedRevision;
  char _initialExpectedCommit[INITIAL_SESSION_COMMIT_LENGTH + 1];
  OtaPortalPurpose _portalPurpose;
  bool _stm32UpdateSessionBound;
  bool _stm32UpdateSessionHasExpectedNodeId;
  uint8_t _stm32UpdateSessionExpectedNodeId;
  char _stm32UpdateSessionTarget[STM32_UPDATE_SESSION_TARGET_MAX_LEN + 1];
  char _stm32UpdateSessionReleaseTag[STM32_UPDATE_SESSION_RELEASE_TAG_MAX_LEN + 1];
  bool _watchdogDetachedForWebOta;
  STM32Bootloader* _pStm32Bootloader;
  UlsaEvoI2cClient* _pI2cClient;
  
  // ポータル用サーバー
  WebServer* _pWebServer;
  DNSServer* _pDnsServer;
  
  /**
   * @brief ポータルページハンドラ設定
   */
  void setupPortalHandlers();

  /**
   * @brief SoftAP SSIDを現在のsession suffixから再生成
   */
  void updatePortalIdentity();

  /**
   * @brief Web OTA進捗をリセット
   */
  void resetWebOtaProgress();

  bool beginInitialImageValidation();
  bool feedInitialImageValidation(const uint8_t* data, size_t length);
  bool finishInitialImageValidation();
  void abortInitialImageValidation();

  /**
   * @brief ポータルトークンを生成
   */
  void generatePortalToken();

  /**
   * @brief アプリ主導OTA用のWPA2パスワードを生成
   */
  void generatePortalPassword();

  /** @brief HTTP response送信後にgraceを置いてportal停止を予約 */
  void requestPortalStopAfterResponse();
  /** @brief HTTP 202 response送信後にSTM32 writer開始を予約 */
  void requestStm32WriteStartAfterResponse();

  /**
   * @brief ポータルトークンを破棄
   */
  void clearPortalSession();

  /**
   * @brief STM32 update session bindingを破棄
   */
  void clearStm32UpdateSession();

  /**
   * @brief STM32 update session bindingと検証済みpackageが一致するか
   */
  bool stm32UpdateSessionMatchesVerifiedPackage() const;

  /**
   * @brief 最後のWeb OTAエラーを設定
   */
  void setLastError(const char* error);

  /**
   * @brief 最後のWeb OTAエラーをクリア
   */
  void clearLastError();

  /**
   * @brief Web OTA中だけwatchdog監視を外す
   */
  void detachWatchdogForWebOta();

  /**
   * @brief Web OTA終了後にwatchdog監視を戻す
   */
  void restoreWatchdogAfterWebOta();

  /**
   * @brief /status用JSONを生成
   */
  String buildPortalStatusJson() const;

  /**
   * @brief STM32 update scratch/package manager
   */
  stm32_update::Stm32UpdateManager _stm32UpdateManager;
};
#endif // OTA_MANAGER_H
