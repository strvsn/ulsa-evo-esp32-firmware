/**
 * @file node_identity_synchronizer.cpp
 * @brief Synchronize the public STM32 user label into ESP32 displays.
 */

#include "sensor/node_identity_synchronizer.h"

#include "ota/ota_manager.h"
#include "sensor/ulsa_evo_i2c_client.h"

NodeIdentitySynchronizer::NodeIdentitySynchronizer(uint32_t intervalMs)
  : _lastAttemptMs(0)
  , _intervalMs(intervalMs)
  , _nodeId(0) {
}

bool NodeIdentitySynchronizer::synchronize(UlsaEvoI2cClient& i2cClient,
                                            OtaManager& otaManager,
                                            bool force) {
  const uint32_t now = millis();
  if (!force && (uint32_t)(now - _lastAttemptMs) < _intervalMs) {
    return false;
  }
  _lastAttemptMs = now;

  // Freeze the already-disclosed SSID label for connection reliability only.
  // Authorization and HTTP access do not depend on this user label.
  if (otaManager.isNodeLabelFrozen()) {
    return false;
  }

  uint8_t nodeId = 0;
  if (!i2cClient.readNodeId(nodeId)) {
    return false;
  }

  if (!otaManager.updateNodeId(nodeId)) {
    return false;
  }

  _nodeId = nodeId;
  return true;
}

uint8_t NodeIdentitySynchronizer::getNodeId() const {
  return _nodeId;
}
