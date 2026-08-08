#pragma once

#include <Arduino.h>

// ノーマリークローズ・ソレノイドバルブの非ブロッキングパルス駆動。
// フェイルセーフの最終防衛ライン: begin()直後・forceClose()は常にLOW(閉)固定。
class ValveDriver {
 public:
  void begin();

  // 指定したパルス幅(widthMs)で開弁を開始する。呼び出し側(Controller)が自己適応させた幅を渡す。
  void trigger(uint32_t now, uint32_t widthMs);

  // loop()毎回呼び出すこと。パルス幅経過で自動的に閉弁する
  void update(uint32_t now);

  // 即座に閉弁し、パルス状態をクリアする(安全側フェイルセーフ)
  void forceClose();

  bool isPulsing() const { return pulsing_; }
  bool isEnergized() const { return pulsing_; }

 private:
  bool pulsing_ = false;
  uint32_t pulseStartMs_ = 0;
  uint32_t pulseWidthMs_ = 0;
};
