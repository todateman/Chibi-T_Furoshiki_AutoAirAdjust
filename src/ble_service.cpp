#include "ble_service.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEUtils.h>

#include "config.h"

void BleService::begin() {
  BLEDevice::init(BLE_DEVICE_NAME);
  server_ = BLEDevice::createServer();
  server_->setCallbacks(this);

  BLEService* service = server_->createService(BLE_SERVICE_UUID);

  // M5NanoC6側の接続シーケンスで存在確認としてread()されるだけのダミーCharacteristic。
  // Write属性がないとM5NanoC6側のcanWrite()判定に失敗し切断されるため必須。実データは扱わない
  BLECharacteristic* dummyChar = service->createCharacteristic(
      BLE_DUMMY_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  dummyChar->setValue("");

  // センサ値送信用(実データ)。CCCD(0x2902)はBluedroidスタックでは自動生成されないため明示追加
  notifyChar_ = service->createCharacteristic(BLE_NOTIFY_CHAR_UUID,
                                               BLECharacteristic::PROPERTY_NOTIFY);
  notifyChar_->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] advertising started");
}

void BleService::onConnect(BLEServer* /*server*/) {
  Serial.println("[BLE] client connected");
}

void BleService::onDisconnect(BLEServer* /*server*/) {
  Serial.println("[BLE] client disconnected, restarting advertising");
  BLEDevice::startAdvertising();  // 再Advertisingしないと再接続できなくなるため必須
}

void BleService::update(const SensorReadings& r) {
  // 無効値は直近の有効値で穴埋めする(センサ一時異常時も監視データを送り続ける)
  if (r.primaryMpa.valid) lastPrimaryMpa_ = r.primaryMpa.value;
  if (r.secondaryMpa.valid) lastSecondaryMpa_ = r.secondaryMpa.value;
  if (r.fuelMpa.valid) lastFuelMpa_ = r.fuelMpa.value;

  bool connected = server_ != nullptr && server_->getConnectedCount() > 0;

  // デバッグ用: BLE接続の有無によらず、送信(予定)データをUSB Serialにも出力する
  // MPa 5桁(=0.01kPa相当)まで表示し、センサのネイティブ分解能でのノイズ低減効果を目視確認できるようにする
  // (BLE Notify側のペイロード書式(下記buf)は対向機の実装に合わせるため変更しない)
  Serial.printf("[BLE TX] PRI=%.5f SEC=%.5f FUEL=%.5f (connected=%s)\n", lastPrimaryMpa_,
                lastSecondaryMpa_, lastFuelMpa_, connected ? "yes" : "no");

  if (!connected) return;

  char buf[80];
  snprintf(buf, sizeof(buf), "PRI:%.2f\nSEC:%.2f\nFUEL:%.2f\n", lastPrimaryMpa_,
            lastSecondaryMpa_, lastFuelMpa_);

  notifyChar_->setValue(reinterpret_cast<uint8_t*>(buf), strlen(buf));
  notifyChar_->notify();
}
