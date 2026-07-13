#include <M5Unified.h>

#include "config.h"             // 各変数
#include "system_types.h"       // システム状態やセンサ読み取り結果の型定義
#include "pressure_sensors.h"
#include "controller.h"
#include "display_ui.h"

namespace {

PressureSensors sensors;
Controller controller;
DisplayUI displayUI;

uint32_t lastSensorReadMs = 0;
uint32_t lastDisplayUpdateMs = 0;

SensorReadings latestReadings;
SystemState lastLoggedState = SystemState::Init;
bool lastLoggedValve = false;

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(100);

  Serial.println("[BOOT] Chibi-T_Furoshiki_AutoAirAdjust starting...");

  bool sensorsOk = sensors.begin();
  Serial.printf("[BOOT] PressureSensors.begin() -> %s\n",
                sensorsOk ? "OK" : "one or more sensors FAILED (check wiring/I2C address)");

  controller.begin(millis());
  displayUI.begin();

  Serial.println("[BOOT] setup complete");
}

void loop() {
  M5.update();
  uint32_t now = millis();

  // 定期的にセンサーを読み取り、コントローラーを更新する
  if (now - lastSensorReadMs >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = now;

    latestReadings = sensors.read();            // センサ読み取り
    controller.update(now, latestReadings);     // コントローラー更新

    ControllerStatus status = controller.status();      // コントローラー状態取得
    bool valveEnergized = controller.valveEnergized();  // バルブ通電状態取得

    // 状態変化があればログ出力
    if (status.state != lastLoggedState) {
      Serial.printf("[STATE] %s -> %s (fuel=%.3fMPa p1=%.3fMPa p2=%.3fMPa)\n",
                     systemStateLabel(lastLoggedState), systemStateLabel(status.state),
                     latestReadings.fuelMpa.value, latestReadings.primaryMpa.value,
                     latestReadings.secondaryMpa.value);
      // フォルト状態に入った場合は理由もログ出力
      if (status.state == SystemState::Fault) {
        Serial.printf("[FAULT] ENTER reason=%s\n", faultReasonLabel(status.faultReason));
      }
      lastLoggedState = status.state;
    }

    // バルブ通電状態変化があればログ出力
    if (valveEnergized != lastLoggedValve) {
      Serial.printf("[VALVE] %s t=%lu\n", valveEnergized ? "OPEN" : "CLOSE",
                     static_cast<unsigned long>(now));
      lastLoggedValve = valveEnergized;
    }
  }

  // 定期的にディスプレイを更新する
  if (now - lastDisplayUpdateMs >= DISPLAY_UPDATE_INTERVAL_MS) {
    lastDisplayUpdateMs = now;
    displayUI.update(latestReadings, controller.status(), controller.valveEnergized());
  }
}
