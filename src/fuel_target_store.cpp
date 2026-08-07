#include "fuel_target_store.h"
#include "config.h"

namespace {
constexpr const char* kNvsNamespace = "chibit";
constexpr const char* kNvsKeyFuelTarget = "fuelTgt";
}  // namespace

void FuelTargetStore::begin() {
  prefs_.begin(kNvsNamespace, false);

  float loaded = prefs_.getFloat(kNvsKeyFuelTarget, NAN);
  if (isnan(loaded) || loaded < FUEL_TARGET_MIN_MPA || loaded > FUEL_TARGET_MAX_MPA) {
    loaded = FUEL_TARGET_DEFAULT_MPA;
  }

  target_ = loaded;
  lastSavedTarget_ = loaded; // 起動直後は「未保存の変更なし」として扱う
}

void FuelTargetStore::adjust(float deltaMpa) {
  float next = constrain(target_ + deltaMpa, FUEL_TARGET_MIN_MPA, FUEL_TARGET_MAX_MPA);
  // 加減算の繰り返しによる浮動小数の累積誤差を防ぐため、0.01MPa単位に丸める
  target_ = roundf(next * 100.0f) / 100.0f;
}

bool FuelTargetStore::save() {
  if (!dirty()) return false;
  prefs_.putFloat(kNvsKeyFuelTarget, target_);
  lastSavedTarget_ = target_;
  return true;
}

float FuelTargetStore::lower() const { return target_ - FUEL_TOLERANCE_MPA; }
float FuelTargetStore::upper() const { return target_ + FUEL_TOLERANCE_MPA; }
bool FuelTargetStore::dirty() const { return fabsf(target_ - lastSavedTarget_) > 1e-4f; }
