#pragma once

#include <Arduino.h>
#include <string.h>

enum class SystemState : uint8_t {
  Init,
  Normal,
  PulseOpen,
  Cooldown,
  Fault,
};

enum class FaultReason : uint8_t {
  None,
  SensorError,
  Overpressure,
  SupplyLow,
  RegulationTimeout,
};

// 個別センサ1点の読み取り結果
struct SensorSample {
  float value = 0.0f;
  bool valid = false;
};

// 1周期分のセンサ読み取りまとめ
struct SensorReadings {
  SensorSample primaryMpa;   // 1次側空気圧
  SensorSample secondaryMpa; // 2次側空気圧
  SensorSample fuelMpa;      // 燃料圧力(制御量)

  bool allValid() const {
    return primaryMpa.valid && secondaryMpa.valid && fuelMpa.valid;
  }
};

// SensorReadingsのうち無効なセンサ名をスペース区切りでbufに書き込む(例: "P1 P2")
inline void formatSensorErrorDetail(const SensorReadings& r, char* buf, size_t bufSize) {
  if (bufSize == 0) return;
  buf[0] = '\0';
  if (!r.primaryMpa.valid) strlcat(buf, buf[0] ? " P1" : "P1", bufSize);
  if (!r.secondaryMpa.valid) strlcat(buf, buf[0] ? " P2" : "P2", bufSize);
  if (!r.fuelMpa.valid) strlcat(buf, buf[0] ? " FUEL" : "FUEL", bufSize);
}

// バルブの現在状態(表示用)
struct ValveStatus {
  bool energized = false; // 現在通電(開弁)中か
};

// コントローラの現在状態(表示用)
struct ControllerStatus {
  SystemState state = SystemState::Init;
  FaultReason faultReason = FaultReason::None;
};

// システム状態の文字列ラベルを返す
inline const char* faultReasonLabel(FaultReason reason) {
  switch (reason) {
    case FaultReason::None: return "";
    case FaultReason::SensorError: return "SENSOR ERROR";
    case FaultReason::Overpressure: return "OVERPRESSURE";
    case FaultReason::SupplyLow: return "PRIMARY SUPPLY LOW";
    case FaultReason::RegulationTimeout: return "REGULATION TIMEOUT";
  }
  return "";
}

// システム状態の文字列ラベルを返す
inline const char* systemStateLabel(SystemState state) {
  switch (state) {
    case SystemState::Init: return "INIT";
    case SystemState::Normal: return "NORMAL";
    case SystemState::PulseOpen: return "PULSE OPEN";
    case SystemState::Cooldown: return "COOLDOWN";
    case SystemState::Fault: return "FAULT";
  }
  return "";
}
