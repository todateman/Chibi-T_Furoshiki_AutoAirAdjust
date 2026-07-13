#include "valve_driver.h"
#include "config.h"

void ValveDriver::begin() {
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW); // フェイルセーフ: 起動時は必ず閉
  pulsing_ = false;
}

void ValveDriver::trigger(uint32_t now) {
  digitalWrite(SOLENOID_PIN, HIGH);
  pulsing_ = true;
  pulseStartMs_ = now;
}

void ValveDriver::update(uint32_t now) {
  if (pulsing_ && (now - pulseStartMs_ >= PULSE_WIDTH_MS)) {
    digitalWrite(SOLENOID_PIN, LOW);
    pulsing_ = false;
  }
}

void ValveDriver::forceClose() {
  digitalWrite(SOLENOID_PIN, LOW);
  pulsing_ = false;
}
