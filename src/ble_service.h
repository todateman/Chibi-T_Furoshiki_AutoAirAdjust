#pragma once

#include <BLEServer.h>

#include "system_types.h"

// M5NanoC6(BLE Central, M5NanoC6_BLE_Centralリポジトリ)に対して
// P1/P2/燃圧をNotifyで送信するBLE Peripheral。
class BleService : public BLEServerCallbacks {
 public:
  void begin();

  // DISPLAY_UPDATE_INTERVAL_MS 周期で呼び出すこと(表示更新と同期)
  void update(const SensorReadings& r);

 private:
  // BLEServerCallbacks
  void onConnect(BLEServer* server) override;
  void onDisconnect(BLEServer* server) override;

  BLEServer* server_ = nullptr;
  BLECharacteristic* notifyChar_ = nullptr;

  // センサ無効時は直近の有効値を送り続けるためのキャッシュ
  float lastPrimaryMpa_ = 0.0f;
  float lastSecondaryMpa_ = 0.0f;
  float lastFuelMpa_ = 0.0f;
};
