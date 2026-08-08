#include "valve_driver.h"
#include "config.h"

void ValveDriver::begin() {
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW); // フェイルセーフ: 起動時は必ず閉
  pulsing_ = false;
}

// 指定幅のパルスを開始する。パルス中は update() を呼んで閉弁処理を行う必要がある。
void ValveDriver::trigger(uint32_t now, uint32_t widthMs) {
  digitalWrite(SOLENOID_PIN, HIGH);
  pulsing_ = true;
  pulseStartMs_ = now;
  pulseWidthMs_ = widthMs;
}

// パルス中かどうかを返す。パルス中であれば、update() を呼んで閉弁処理を行う必要がある。
void ValveDriver::update(uint32_t now) {
  if (pulsing_ && (now - pulseStartMs_ >= pulseWidthMs_)) {
    digitalWrite(SOLENOID_PIN, LOW);
    pulsing_ = false;
  }
}

// 強制的に閉弁する。パルス中でも即座に閉弁する。
void ValveDriver::forceClose() {
  digitalWrite(SOLENOID_PIN, LOW);
  pulsing_ = false;
}
