#include "controller.h"
#include "config.h"

void Controller::begin(uint32_t now) {
  (void)now;
  valve_.begin();
  state_ = SystemState::Init;
  faultReason_ = FaultReason::None;
}

// 目標帯下限を下回った場合にソレノイドをパルス駆動するが、
// そのパルス駆動が一定時間以上続いた場合は制御不能と判断してフォルトに遷移する。
// 目標帯下限を下回したままパルス駆動が続く場合は、燃圧センサの故障やソレノイドの故障、配管のリークなどが疑われる。
FaultReason Controller::evaluateSafety(const SensorReadings& r) const {
  // センサのいずれかが無効値の場合は、即座にフォルト判定する
  if (!r.allValid()) return FaultReason::SensorError;

  // 過圧検知は燃圧センサと2次側空気圧センサの両方で判定する
  bool overpressure = (faultReason_ == FaultReason::Overpressure)
      ? (r.fuelMpa.value > OVERPRESSURE_CLEAR_MPA || r.secondaryMpa.value > OVERPRESSURE_CLEAR_MPA)
      : (r.fuelMpa.value > OVERPRESSURE_TRIP_MPA || r.secondaryMpa.value > OVERPRESSURE_TRIP_MPA);
  if (overpressure) return FaultReason::Overpressure;

  // 供給圧低下検知は1次側空気圧センサのみで判定する
  bool supplyLow = (faultReason_ == FaultReason::SupplyLow)
      ? (r.primaryMpa.value < PRIMARY_SUPPLY_LOW_CLEAR_MPA)
      : (r.primaryMpa.value < PRIMARY_SUPPLY_LOW_TRIP_MPA);
  if (supplyLow) return FaultReason::SupplyLow;

  return FaultReason::None;
}

// フォルト状態に遷移する際の共通処理
void Controller::enterFault(FaultReason reason) {
  state_ = SystemState::Fault;
  faultReason_ = reason;
  valve_.forceClose();
  tripStreak_ = 0;
  clearStreak_ = 0;
}

// 制御ループの更新処理
void Controller::update(uint32_t now, const SensorReadings& r) {
  valve_.update(now); // パルス幅終了判定は毎回(呼び出し周期非依存)

  FaultReason detected = evaluateSafety(r);

  // フォルト判定のデバウンス処理
  // 連続して FAULT_TRIP_DEBOUNCE_SAMPLES 回以上同じフォルト理由が検出された場合にフォルト遷移する
  if (detected != FaultReason::None) {
    clearStreak_ = 0;
    tripStreak_ = (detected == pendingTripReason_) ? tripStreak_ + 1 : 1;
    pendingTripReason_ = detected;
  } else {
    tripStreak_ = 0;
    pendingTripReason_ = FaultReason::None;
    clearStreak_++;
  }

  // フォルト状態に遷移する条件を満たした場合はフォルト状態に遷移する
  if (state_ != SystemState::Fault && detected != FaultReason::None &&
      tripStreak_ >= FAULT_TRIP_DEBOUNCE_SAMPLES) {
    enterFault(detected);
  }

  // フォルト状態の解除条件を満たした場合はフォルト状態を解除する
  if (state_ == SystemState::Fault) {
    valve_.forceClose();
    if (clearStreak_ >= FAULT_CLEAR_DEBOUNCE_SAMPLES) {
      state_ = SystemState::Normal;
      faultReason_ = FaultReason::None;
      pulseEpisodeStartMs_ = 0;
    }
    return;
  }

  // 正常状態の制御ロジック
  // 目標帯下限を下回った場合にソレノイドをパルス駆動するが、一定時間以上続いた場合は制御不能と判断してフォルトに遷移する
  switch (state_) {
    // 初期化状態では、センサが全て有効値になるまで待機する
    case SystemState::Init:
      if (r.allValid()) {
        state_ = SystemState::Normal;
      }
      break;
    // 正常状態では、燃圧が目標帯下限を下回った場合にソレノイドをパルス駆動する
    case SystemState::Normal:
      if (r.fuelMpa.value < FUEL_LOWER_MPA) {
        if (pulseEpisodeStartMs_ == 0) pulseEpisodeStartMs_ = now;
        valve_.trigger(now);
        state_ = SystemState::PulseOpen;
      } else {
        pulseEpisodeStartMs_ = 0; // 目標帯内 or 上限超過なら連続区間の計測をリセット
      }
      break;
    // パルス駆動中は、パルス幅が終了するまで待機する
    case SystemState::PulseOpen:
      if (!valve_.isPulsing()) {
        state_ = SystemState::Cooldown;
        cooldownStartMs_ = now;
      }
      break;
    // パルス駆動終了後は、パルス間の休止時間が経過するまで待機する
    case SystemState::Cooldown:
      if (now - cooldownStartMs_ >= PULSE_COOLDOWN_MS) {
        state_ = SystemState::Normal; // 次周期で再判定
      }
      break;
    // フォルト状態では、上記の安全評価でフォルトが解除されるまで待機する
    case SystemState::Fault:
      break; // 上で処理済み、ここには到達しない
  }

  // 連続パルス駆動が一定時間を超えた場合は制御不能と判断してフォルトに遷移する
  if (pulseEpisodeStartMs_ != 0 &&
      now - pulseEpisodeStartMs_ > MAX_REGULATION_EPISODE_MS) {
    enterFault(FaultReason::RegulationTimeout);
  }
}
