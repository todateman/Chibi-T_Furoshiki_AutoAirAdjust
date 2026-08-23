#pragma once

#include <Arduino.h>

#include "system_types.h"
#include "valve_driver.h"
#include "secondary_target_store.h"

// 状態機械: Init -> Normal <-> PulseOpen -> Cooldown -> Normal
// 任意の状態からFaultへ即遷移し、バルブを強制的に閉じる。
class Controller {
 public:
  void begin(uint32_t now);

  // 毎ループ(周期処理の間引きの外側)で呼び出すこと。
  // バルブのオフ判定(パルス幅経過判定)はSENSOR_READ_INTERVAL_MS周期に依存させると
  // 最小パルス幅(PULSE_WIDTH_MIN_MS)より遅れて閉弁し過供給を招くため、update()から分離している。
  void updateValve(uint32_t now) { valve_.update(now); }

  // SENSOR_READ_INTERVAL_MS 周期で呼び出すこと
  // secondaryTargetはSecondaryTargetStoreから取得した目標帯情報(下限/上限、ボタンで実行時変更される)
  void update(uint32_t now, const SensorReadings& r, const SecondaryTargetInfo& secondaryTarget);

  ControllerStatus status() const { return {state_, faultReason_}; }
  bool valveEnergized() const { return valve_.isEnergized(); }
  float currentPulseWidthMs() const { return currentPulseWidthMs_; } // 実機チューニング用(Serialログ出力等)

  // 直近完了パルスのΔP(圧力上昇幅)。小口径バルブ交換・バッファチャンバー等のハード対策の
  // 効果を実機で定量比較するための実機チューニング用(Serialログ出力等)
  float lastPulseWidthMs() const { return lastPulseWidthMs_; }
  float lastPulseDeltaMpa() const { return lastPulseDeltaMpa_; }
  float lastPulseBeforeMpa() const { return lastPulseBeforeMpa_; }
  float lastPulseAfterMpa() const { return lastPulseAfterMpa_; }
  bool  lastPulseResultValid() const { return lastPulseResultValid_; }

 private:
  ValveDriver valve_;

  SystemState state_ = SystemState::Init;
  FaultReason faultReason_ = FaultReason::None;

  uint32_t pulseEpisodeStartMs_ = 0; // 目標帯復帰を試み続けている区間の開始時刻(0=非計測中)
  uint32_t cooldownStartMs_ = 0;
  float currentPulseWidthMs_ = 0.0f; // 自己適応パルス幅(begin()/enterFault()でPULSE_WIDTH_MIN_MSにリセット)

  float pulseStartSecondaryMpa_ = 0.0f; // パルス開始直前の2次側圧力(ΔPログ用の一時保持)
  bool  pulseStartValid_ = false;       // pulseStartSecondaryMpa_が今回のパルスに対して有効か(フォルト遷移等でのガード)

  float lastPulseWidthMs_ = 0.0f;   // 直近完了パルスで実際に使った幅(ms)。currentPulseWidthMs_は次回用に更新済みのため別保持
  float lastPulseBeforeMpa_ = 0.0f; // 直近パルス開始直前の2次側圧力
  float lastPulseAfterMpa_ = 0.0f;  // 直近パルスCooldown終了時(次回幅の自己適応判定直前)の2次側圧力
  float lastPulseDeltaMpa_ = 0.0f;  // 上記2値の差分(1パルスあたりの圧力上昇幅)
  bool  lastPulseResultValid_ = false; // 上記が一度でも記録済みか(起動直後・初回パルス完了前はfalse)

  // フォルト判定のデバウンス
  FaultReason pendingTripReason_ = FaultReason::None;
  uint8_t tripStreak_ = 0;
  uint8_t clearStreak_ = 0;

  FaultReason evaluateSafety(const SensorReadings& r) const;
  void enterFault(FaultReason reason);
  void adjustPulseWidth(const SensorReadings& r, const SecondaryTargetInfo& secondaryTarget);
};
