#include "secondary_target_store.h"
#include "config.h"

namespace {
constexpr const char* kNvsNamespace = "chibit";
constexpr const char* kNvsKeySecondaryTarget = "secTgt";
}  // namespace

void SecondaryTargetStore::begin() {
  prefs_.begin(kNvsNamespace, false);

  float loaded = prefs_.getFloat(kNvsKeySecondaryTarget, NAN);
  if (isnan(loaded) || loaded < SECONDARY_TARGET_MIN_MPA || loaded > SECONDARY_TARGET_MAX_MPA) {
    loaded = SECONDARY_TARGET_DEFAULT_MPA;
  }

  target_ = loaded;
  lastSavedTarget_ = loaded; // 起動直後は「未保存の変更なし」として扱う
}

void SecondaryTargetStore::adjust(float deltaMpa) {
  float next = constrain(target_ + deltaMpa, SECONDARY_TARGET_MIN_MPA, SECONDARY_TARGET_MAX_MPA);
  // 加減算の繰り返しによる浮動小数の累積誤差を防ぐため、0.01MPa単位に丸める
  target_ = roundf(next * 100.0f) / 100.0f;
}

bool SecondaryTargetStore::save() {
  if (!dirty()) return false;
  prefs_.putFloat(kNvsKeySecondaryTarget, target_);
  lastSavedTarget_ = target_;
  return true;
}

float SecondaryTargetStore::lower() const { return target_ - SECONDARY_TOLERANCE_MPA; }
float SecondaryTargetStore::upper() const { return target_ + SECONDARY_TOLERANCE_MPA; }
bool SecondaryTargetStore::dirty() const { return fabsf(target_ - lastSavedTarget_) > 1e-4f; }
