#include "valve_driver.h"
#include "config.h"

void ValveDriver::begin() {
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW); // フェイルセーフ: 起動時は必ず閉
  pulsing_ = false;

  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = &ValveDriver::onTimerFire;
  timerArgs.arg = this;
  timerArgs.name = "valve_close";
  esp_timer_create(&timerArgs, &closeTimer_);
}

// 指定幅のパルスを開始する。閉弁はesp_timerのワンショットタイマーで正確に行われる。
// (update()はタイマーが機能しなかった場合の保険として引き続き呼んでおくこと)
void ValveDriver::trigger(uint32_t now, uint32_t widthMs) {
  digitalWrite(SOLENOID_PIN, HIGH);
  pulsing_ = true;
  pulseStartMs_ = now;
  pulseWidthMs_ = widthMs;

  esp_timer_stop(closeTimer_); // 前回分が万一残っていれば念のためキャンセル
  esp_timer_start_once(closeTimer_, static_cast<uint64_t>(widthMs) * 1000ULL); // ms -> us
}

// esp_timerのコールバック(専用タスクコンテキストで実行される。digitalWrite()呼び出し可)
void ValveDriver::onTimerFire(void* arg) {
  static_cast<ValveDriver*>(arg)->closeNow();
}

void ValveDriver::closeNow() {
  digitalWrite(SOLENOID_PIN, LOW);
  pulsing_ = false;
}

// パルス中かどうかを返す。閉弁は通常esp_timerで行われるため、ここでの判定は
// タイマーが何らかの理由で発火しなかった場合の保険(フェイルセーフの多重防御)。
void ValveDriver::update(uint32_t now) {
  if (pulsing_ && (now - pulseStartMs_ >= pulseWidthMs_)) {
    esp_timer_stop(closeTimer_); // 保険側が先に閉じた場合、タイマー側の発火は不要なので止める
    closeNow();
  }
}

// 強制的に閉弁する。パルス中でも即座に閉弁する。
void ValveDriver::forceClose() {
  esp_timer_stop(closeTimer_);
  closeNow();
}
