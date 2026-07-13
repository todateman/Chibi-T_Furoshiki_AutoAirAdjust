#pragma once

#include <Arduino.h>

#include "system_types.h"
#include "valve_driver.h"

// 状態機械: Init -> Normal <-> PulseOpen -> Cooldown -> Normal
// 任意の状態からFaultへ即遷移し、バルブを強制的に閉じる。
class Controller {
 public:
  void begin(uint32_t now);

  // SENSOR_READ_INTERVAL_MS 周期で呼び出すこと
  void update(uint32_t now, const SensorReadings& r);

  ControllerStatus status() const { return {state_, faultReason_}; }
  bool valveEnergized() const { return valve_.isEnergized(); }

 private:
  ValveDriver valve_;

  SystemState state_ = SystemState::Init;
  FaultReason faultReason_ = FaultReason::None;

  uint32_t pulseEpisodeStartMs_ = 0; // 目標帯復帰を試み続けている区間の開始時刻(0=非計測中)
  uint32_t cooldownStartMs_ = 0;

  // フォルト判定のデバウンス
  FaultReason pendingTripReason_ = FaultReason::None;
  uint8_t tripStreak_ = 0;
  uint8_t clearStreak_ = 0;

  FaultReason evaluateSafety(const SensorReadings& r) const;
  void enterFault(FaultReason reason);
};
