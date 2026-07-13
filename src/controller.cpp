#include "controller.h"
#include "config.h"

void Controller::begin(uint32_t now) {
  (void)now;
  valve_.begin();
  state_ = SystemState::Init;
  faultReason_ = FaultReason::None;
}

FaultReason Controller::evaluateSafety(const SensorReadings& r) const {
  if (!r.allValid()) return FaultReason::SensorError;

  // ヒステリシス: 既にトリップ中ならクリア側の緩い閾値で判定を継続する
  bool overpressure = (faultReason_ == FaultReason::Overpressure)
      ? (r.fuelMpa.value > OVERPRESSURE_CLEAR_MPA || r.secondaryMpa.value > OVERPRESSURE_CLEAR_MPA)
      : (r.fuelMpa.value > OVERPRESSURE_TRIP_MPA || r.secondaryMpa.value > OVERPRESSURE_TRIP_MPA);
  if (overpressure) return FaultReason::Overpressure;

  bool supplyLow = (faultReason_ == FaultReason::SupplyLow)
      ? (r.primaryMpa.value < PRIMARY_SUPPLY_LOW_CLEAR_MPA)
      : (r.primaryMpa.value < PRIMARY_SUPPLY_LOW_TRIP_MPA);
  if (supplyLow) return FaultReason::SupplyLow;

  return FaultReason::None;
}

void Controller::enterFault(FaultReason reason) {
  state_ = SystemState::Fault;
  faultReason_ = reason;
  valve_.forceClose();
  tripStreak_ = 0;
  clearStreak_ = 0;
}

void Controller::update(uint32_t now, const SensorReadings& r) {
  valve_.update(now); // パルス幅終了判定は毎回(呼び出し周期非依存)

  FaultReason detected = evaluateSafety(r);

  if (detected != FaultReason::None) {
    clearStreak_ = 0;
    tripStreak_ = (detected == pendingTripReason_) ? tripStreak_ + 1 : 1;
    pendingTripReason_ = detected;
  } else {
    tripStreak_ = 0;
    pendingTripReason_ = FaultReason::None;
    clearStreak_++;
  }

  if (state_ != SystemState::Fault && detected != FaultReason::None &&
      tripStreak_ >= FAULT_TRIP_DEBOUNCE_SAMPLES) {
    enterFault(detected);
  }

  if (state_ == SystemState::Fault) {
    valve_.forceClose();
    if (clearStreak_ >= FAULT_CLEAR_DEBOUNCE_SAMPLES) {
      state_ = SystemState::Normal;
      faultReason_ = FaultReason::None;
      pulseEpisodeStartMs_ = 0;
    }
    return;
  }

  switch (state_) {
    case SystemState::Init:
      if (r.allValid()) {
        state_ = SystemState::Normal;
      }
      break;

    case SystemState::Normal:
      if (r.fuelMpa.value < FUEL_LOWER_MPA) {
        if (pulseEpisodeStartMs_ == 0) pulseEpisodeStartMs_ = now;
        valve_.trigger(now);
        state_ = SystemState::PulseOpen;
      } else {
        pulseEpisodeStartMs_ = 0; // 目標帯内 or 上限超過なら連続区間の計測をリセット
      }
      break;

    case SystemState::PulseOpen:
      if (!valve_.isPulsing()) {
        state_ = SystemState::Cooldown;
        cooldownStartMs_ = now;
      }
      break;

    case SystemState::Cooldown:
      if (now - cooldownStartMs_ >= PULSE_COOLDOWN_MS) {
        state_ = SystemState::Normal; // 次周期で再判定
      }
      break;

    case SystemState::Fault:
      break; // 上で処理済み、ここには到達しない
  }

  if (pulseEpisodeStartMs_ != 0 &&
      now - pulseEpisodeStartMs_ > MAX_REGULATION_EPISODE_MS) {
    enterFault(FaultReason::RegulationTimeout);
  }
}
