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

// 起動時のOTAホールド確認中のみ表示する簡易UI。ota_service.cppの描画スタイル
// (M5.Displayへ直接描画、TFT_BLACK背景、setTextSize(3)見出し+setTextSize(1.5)詳細、
//  barX/barY/barW/barHの進捗バー座標)に合わせる。タッチ検出直後に一度だけ呼び、
// 以後はdrawOtaHoldProgress()で進捗バーのみ更新する。OTAモードに確定した場合は
// この直後にOtaService::run()内のdrawIdleScreen()が画面全体を上書きするため、
// 凝った画面引き継ぎ処理は不要(fillScreenで単純に上書きされる)。
void drawOtaHoldPrompt() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(8, 8);
  M5.Display.println("Hold to enter OTA...");
  M5.Display.setTextSize(1.5);
  M5.Display.setCursor(8, 40);
  M5.Display.println("Keep touching BtnB area.");
  M5.Display.println("Release to boot normally.");
}

// heldMs: 現在の連続押下継続時間(ms), thresholdMs: 確定に必要な時間(OTA_HOLD_CONFIRM_MS)
// ota_service.cpp::drawProgress()と同じバー座標・配色を使い、OTAモード突入後の
// アップロード進捗表示との視覚的な連続性を持たせる。
void drawOtaHoldProgress(uint32_t heldMs, uint32_t thresholdMs) {
  static uint8_t lastDrawnPercent = 255;  // 再描画間引き用(OtaService::drawProgress()と同じ手法)
  uint32_t clamped = min(heldMs, thresholdMs);
  uint8_t percent = static_cast<uint8_t>((100ULL * clamped) / thresholdMs);
  if (percent == lastDrawnPercent) return;
  lastDrawnPercent = percent;

  const int barX = 8, barY = 160, barW = 304, barH = 20;
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.fillRect(0, 140, 320, 40, TFT_BLACK);
  M5.Display.setCursor(barX, 140);
  M5.Display.printf("Hold... %3u%%", percent);

  M5.Display.drawRect(barX, barY, barW, barH, TFT_WHITE);
  int fillW = (barW - 2) * percent / 100;
  M5.Display.fillRect(barX + 1, barY + 1, max(0, fillW), barH - 2, TFT_CYAN);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // M5UnifiedのBtnA/BtnB/BtnCは既定でタッチボタン判定領域の高さが0のため、これを呼ばないと
  // 画面のほぼどこを押しても反応しない(config.h::TOUCH_BUTTON_ZONE_HEIGHT_PX参照)。
  // 以降のBtn*.isPressed()等が意味を持つように、他の判定より前に必ず設定する。
  M5.setTouchButtonHeight(TOUCH_BUTTON_ZONE_HEIGHT_PX);

  Serial.begin(115200);
  delay(100);

  // OTAモード判定: M5.update()を呼ばないとBtn*の状態が更新されないため、
  // 他モジュールの初期化より前に必ず呼ぶ。BtnBはM5Core2の物理ボタンではなく静電容量式
  // タッチパネルの仮想ゾーンで、実機検証の結果、以下がわかった:
  //   - 「電源投入前から指を触れたままにする」操作方式は機能しない。タッチIC(FT6336系)は
  //     電源投入直後に無接触状態を自己校正するため、その瞬間に指が触れているとその状態自体を
  //     「無接触」の基準点として学習してしまい、指を離して再度触れるまで検出できなくなる
  //     (コードの問題ではなくタッチIC側の既知の挙動で、ポーリング時間を延ばしても解決しない)。
  //   - 「電源投入後にBボタンへ触れてホールドする」操作方式なら確実に検出できる(実機確認済み)。
  // そのため運用手順は「電源投入 → その後BボタンをOTA_HOLD_CONFIRM_MS以上ホールド」に統一し、
  // 電源投入直後の反応時間を確保するためOTA_ENTRY_DETECT_MSの間入口判定をポーリングする。
  // 一度でも押下を検出できたらOTA_HOLD_CONFIRM_MS以上の継続押下(ホールド)を確認するフェーズへ
  // 移行し、押下が一度も検出されなければ通常起動へ進む(この待ち時間だけ通常起動も一律遅延するが、
  // タッチ検出の確実性を優先する)。
  //
  // NOTE: BtnBのwasHold()/isHolding()/setHoldThresh()はloop()内の「長押しでNVS保存」機能
  // (BtnB共有の内部しきい値_msecHold、既定500ms)と競合するためここでは使わず、
  // pressedFor()に明示的な閾値を渡してBtnBの内部状態には触れない設計にする。
  //
  // OtaService::run()はブロッキングで、更新成功時はESP.restart()するため戻らない
  // (失敗時もAP/サーバーを維持したまま再アップロード待機を続け、setup()には戻らない)。
  bool touchDetected = false;
  uint32_t entryStartMs = millis();
  do {
    M5.update();
    if (M5.BtnB.isPressed()) {
      touchDetected = true;
      break;
    }
    delay(OTA_HOLD_POLL_INTERVAL_MS);
  } while (millis() - entryStartMs < OTA_ENTRY_DETECT_MS);

  if (touchDetected) {
    Serial.println("[BOOT] BtnB press detected -> confirming hold before entering OTA mode...");
    drawOtaHoldPrompt();

    bool otaConfirmed = false;
    for (;;) {
      delay(OTA_HOLD_POLL_INTERVAL_MS);
      M5.update();

      if (M5.BtnB.wasReleased()) {
        Serial.println("[BOOT] BtnB released before hold threshold -> normal boot");
        break;
      }

      uint32_t heldMs = millis() - M5.BtnB.lastChange();
      drawOtaHoldProgress(heldMs, OTA_HOLD_CONFIRM_MS);

      if (M5.BtnB.pressedFor(OTA_HOLD_CONFIRM_MS)) {
        otaConfirmed = true;
        break;
      }
    }

    if (otaConfirmed) {
      Serial.println("[BOOT] BtnB held >= OTA_HOLD_CONFIRM_MS -> entering OTA update mode (normal boot skipped)");
      OtaService otaService;
      otaService.run();
    }
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
      // Cooldown -> Normal遷移時、直近パルスのΔP(圧力上昇幅)をログ出力
      // (小口径バルブ交換・バッファチャンバー等のハード対策の効果を実機で定量比較するため)
      if (lastLoggedState == SystemState::Cooldown && status.state == SystemState::Normal &&
          controller.lastPulseResultValid()) {
        Serial.printf("[PULSE] width=%.0fms delta=%+.3fMPa (before=%.3f after=%.3f)\n",
                      controller.lastPulseWidthMs(), controller.lastPulseDeltaMpa(),
                      controller.lastPulseBeforeMpa(), controller.lastPulseAfterMpa());
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
