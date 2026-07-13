#pragma once

#include <Arduino.h>

// ノーマリークローズ・ソレノイドバルブの非ブロッキングパルス駆動。
// フェイルセーフの最終防衛ライン: begin()直後・forceClose()は常にLOW(閉)固定。
class ValveDriver {
 public:
  void begin();

  // 固定パルス幅(PULSE_WIDTH_MS)の開弁を開始する
  void trigger(uint32_t now);

  // loop()毎回呼び出すこと。パルス幅経過で自動的に閉弁する
  void update(uint32_t now);

  // 即座に閉弁し、パルス状態をクリアする(安全側フェイルセーフ)
  void forceClose();

  bool isPulsing() const { return pulsing_; }
  bool isEnergized() const { return pulsing_; }

 private:
  bool pulsing_ = false;
  uint32_t pulseStartMs_ = 0;
};
