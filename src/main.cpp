#include <M5Unified.h>

#include "config.h"             // 各変数
#include "system_types.h"       // システム状態やセンサ読み取り結果の型定義
#include "pressure_sensors.h"
#include "controller.h"
#include "display_ui.h"
#include "ble_service.h"
#include "secondary_target_store.h"
#include "ota_service.h"

namespace {

PressureSensors sensors;
Controller controller;
DisplayUI displayUI;
BleService bleService;
SecondaryTargetStore secondaryTargetStore;

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

  // OTAモード判定: M5.update()を呼ばないとBtn*の状態が更新されないため、
  // 他モジュールの初期化より前に必ず1回呼ぶ。起動時にBボタンが押されていれば、
  // 通常運転(センサ・コントローラ・BLE等)を一切開始せずWi-Fi AP経由のOTA更新モードへ遷移する。
  // OtaService::run()はブロッキングで、更新成功時はESP.restart()するため戻らない
  // (失敗時もAP/サーバーを維持したまま再アップロード待機を続け、setup()には戻らない)。
  M5.update();
  if (M5.BtnB.isPressed()) {
    Serial.println("[BOOT] BtnB held at startup -> entering OTA update mode (normal boot skipped)");
    OtaService otaService;
    otaService.run();
  }

  Serial.println("[BOOT] Chibi-T_Furoshiki_AutoAirAdjust starting...");

  bool sensorsOk = sensors.begin();
  Serial.printf("[BOOT] PressureSensors.begin() -> %s\n",
                sensorsOk ? "OK" : "one or more sensors FAILED (check wiring/I2C address)");

  controller.begin(millis());
  displayUI.begin();
  bleService.begin();
  secondaryTargetStore.begin();
  Serial.printf("[BOOT] P2 target = %.2fMPa (loaded from NVS or default)\n", secondaryTargetStore.target());

  Serial.println("[BOOT] setup complete");
}

void loop() {
  M5.update();
  uint32_t now = millis();

  // バルブのオフ判定(パルス幅経過判定)は、周期処理の間引きに巻き込まれると
  // 最小パルス幅(PULSE_WIDTH_MIN_MS)より閉弁が遅れて過供給を招くため、毎ループ無条件で実行する
  controller.updateValve(now);

  // 目標2次側空気圧ボタン操作(取りこぼし防止のため間引き処理の外、毎ループ判定する)
  // Aボタン: -0.01MPa, Cボタン: +0.01MPa, Bボタン長押し: NVSへ保存
  if (M5.BtnA.wasClicked()) {
    secondaryTargetStore.adjust(-SECONDARY_TARGET_STEP_MPA);
    Serial.printf("[SETTINGS] P2 target -> %.2fMPa\n", secondaryTargetStore.target());
  }
  if (M5.BtnC.wasClicked()) {
    secondaryTargetStore.adjust(SECONDARY_TARGET_STEP_MPA);
    Serial.printf("[SETTINGS] P2 target -> %.2fMPa\n", secondaryTargetStore.target());
  }
  if (M5.BtnB.wasHold()) {
    bool saved = secondaryTargetStore.save();
    Serial.printf("[SETTINGS] P2 target %.2fMPa %s\n", secondaryTargetStore.target(),
                  saved ? "SAVED to NVS" : "(no change to save)");
  }

  // 定期的にセンサーを読み取り、コントローラーを更新する
  if (now - lastSensorReadMs >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = now;

    latestReadings = sensors.read();                                       // センサ読み取り
    // sensors.read()は内部で約200msブロッキングするため、ループ先頭で取得した`now`を
    // そのままcontroller.update()に渡すと、valve_.trigger()に刻まれるタイムスタンプが
    // 実時刻より約200ms過去になり、次ループのupdateValve()判定で「パルス幅経過」と
    // 誤判定して実際の通電時間がcurrentPulseWidthMs_と乖離する(オーバーシュートの直接原因)。
    // ブロッキング読み取り直後に時刻を再取得し、以降の状態遷移・パルストリガーの基準に使う。
    uint32_t nowAfterRead = millis();
    controller.update(nowAfterRead, latestReadings, secondaryTargetStore.info());  // コントローラー更新

    ControllerStatus status = controller.status();      // コントローラー状態取得
    bool valveEnergized = controller.valveEnergized();  // バルブ通電状態取得

    // 状態変化があればログ出力
    if (status.state != lastLoggedState) {
      Serial.printf("[STATE] %s -> %s (p2=%.3fMPa p1=%.3fMPa fuel=%.3fMPa)\n",
                     systemStateLabel(lastLoggedState), systemStateLabel(status.state),
                     latestReadings.secondaryMpa.value, latestReadings.primaryMpa.value,
                     latestReadings.fuelMpa.value);
      // フォルト状態に入った場合は理由もログ出力
      if (status.state == SystemState::Fault) {
        if (status.faultReason == FaultReason::SensorError) {
          char detail[16];
          formatSensorErrorDetail(latestReadings, detail, sizeof(detail));
          Serial.printf("[FAULT] ENTER reason=%s (%s)\n", faultReasonLabel(status.faultReason), detail);
        } else {
          Serial.printf("[FAULT] ENTER reason=%s\n", faultReasonLabel(status.faultReason));
        }
      }
      lastLoggedState = status.state;
    }

    // バルブ通電状態変化があればログ出力
    if (valveEnergized != lastLoggedValve) {
      Serial.printf("[VALVE] %s t=%lu width=%.0fms\n", valveEnergized ? "OPEN" : "CLOSE",
                     static_cast<unsigned long>(nowAfterRead), controller.currentPulseWidthMs());
      lastLoggedValve = valveEnergized;
    }
  }

  // 定期的にディスプレイを更新する
  if (now - lastDisplayUpdateMs >= DISPLAY_UPDATE_INTERVAL_MS) {
    lastDisplayUpdateMs = now;
    displayUI.update(latestReadings, controller.status(), controller.valveEnergized(),
                      secondaryTargetStore.info(), bleService.isConnected());
    bleService.update(latestReadings);    // デバッグ用のUSB Serial出力も兼ねる
  }
}
