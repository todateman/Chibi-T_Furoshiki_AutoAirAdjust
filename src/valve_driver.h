#pragma once

#include <Arduino.h>
#include <esp_timer.h>

// ノーマリークローズ・ソレノイドバルブの非ブロッキングパルス駆動。
// フェイルセーフの最終防衛ライン: begin()直後・forceClose()は常にLOW(閉)固定。
//
// 閉弁はesp_timerのワンショットタイマー(マイクロ秒精度、専用タスクコンテキストで実行)で
// 行う。sensors.read()等のI2Cブロッキング処理がloop()を長時間(実測約200ms)止めても、
// タイマーはそれとは独立に動作するため、パルス幅どおりの精度で閉弁できる。
// (以前はloop()からのポーリング(update())のみで閉弁していたが、loop()自体が
//  センサー読み取りのブロッキングでほぼ常時ふさがっており、8msのパルス幅を指定しても
//  実際には次のセンサー読み取りサイクル完了まで(数百ms)開弁したままになる不具合があった)
// update()はタイマーが何らかの理由で機能しなかった場合の保険として引き続き呼び出すこと。
class ValveDriver {
 public:
  void begin();

  // 指定したパルス幅(widthMs)で開弁を開始する。呼び出し側(Controller)が自己適応させた幅を渡す。
  // 閉弁は内部でesp_timerのワンショットタイマーにより正確にスケジュールされる。
  void trigger(uint32_t now, uint32_t widthMs);

  // loop()毎回呼び出すこと。閉弁は通常esp_timerで行われるが、タイマーが機能しなかった
  // 場合の保険としてポーリングでも経過判定する(フェイルセーフの多重防御)。
  void update(uint32_t now);

  // 即座に閉弁し、パルス状態をクリアする(安全側フェイルセーフ)
  void forceClose();

  bool isPulsing() const { return pulsing_; }
  bool isEnergized() const { return pulsing_; }

 private:
  static void onTimerFire(void* arg);
  void closeNow();

  esp_timer_handle_t closeTimer_ = nullptr;
  volatile bool pulsing_ = false; // esp_timerタスクとloop()の両方から読み書きされるためvolatile
  uint32_t pulseStartMs_ = 0;
  uint32_t pulseWidthMs_ = 0;
};
