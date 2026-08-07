#pragma once

#include <Arduino.h>
#include <Preferences.h>

// 目標燃圧の現在値と、それに基づく制御用の下限/上限、未保存変更の有無をまとめた値
struct FuelTargetInfo {
  float target; // 現在の目標燃圧 (MPa)
  float lower;  // 目標帯下限 (target - FUEL_TOLERANCE_MPA)
  float upper;  // 目標帯上限 (target + FUEL_TOLERANCE_MPA)
  bool dirty;   // NVSに未保存の変更があるか
};

// 目標燃圧の実行時保持・ボタン調整・NVS(ESP32 Preferences)への永続化を担当する。
// - adjust() でボタン操作による±0.01MPa調整を反映(即座に制御へ反映される)
// - save() でBボタン長押し時にNVSへ確定保存(電源断後も復元される)
class FuelTargetStore {
 public:
  // NVSから保存値を読み込む。未保存または範囲外ならFUEL_TARGET_DEFAULT_MPAを使う
  void begin();

  // 目標燃圧をdeltaMpa分だけ増減し、[FUEL_TARGET_MIN_MPA, FUEL_TARGET_MAX_MPA]でclampする
  void adjust(float deltaMpa);

  // 未保存の変更があればNVSへ書き込む。書き込みを行った場合のみtrueを返す
  bool save();

  float target() const { return target_; }
  float lower() const; // target() - FUEL_TOLERANCE_MPA
  float upper() const; // target() + FUEL_TOLERANCE_MPA
  bool dirty() const;  // NVSに未保存の変更があるか

  FuelTargetInfo info() const { return {target_, lower(), upper(), dirty()}; }

 private:
  Preferences prefs_;
  float target_ = 0.0f;
  float lastSavedTarget_ = 0.0f;
};
