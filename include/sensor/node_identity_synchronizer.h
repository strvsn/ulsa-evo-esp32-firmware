#ifndef NODE_IDENTITY_SYNCHRONIZER_H
#define NODE_IDENTITY_SYNCHRONIZER_H

#include <Arduino.h>

class OtaManager;
class UlsaEvoI2cClient;

/**
 * STM32の公開NODE_IDをuser-facing labelとしてESP32表示へ同期する。
 * 計測値の有効性やDATA_SEQとは独立して動作する。
 * label取得失敗、0、重複、変更はOTA authorizationの成否へ影響しない。
 */
class NodeIdentitySynchronizer {
public:
  explicit NodeIdentitySynchronizer(uint32_t intervalMs = 1000);

  bool synchronize(UlsaEvoI2cClient& i2cClient,
                   OtaManager& otaManager,
                   bool force = false);

  uint8_t getNodeId() const;

private:
  uint32_t _lastAttemptMs;
  uint32_t _intervalMs;
  uint8_t _nodeId;
};

#endif  // NODE_IDENTITY_SYNCHRONIZER_H
