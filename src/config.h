#pragma once

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

// ============================================================
// ピン割り当て
// ============================================================
//
// !!! 重要 !!!
// Ver1.00のKiCad回路図上ではソレノイド駆動信号 SOL_TRG は M-BUS 25番ピン
// (ネットラベル "GPIO34") に配線されているが、ESP32のGPIO34は
// ハードウェア的に入力専用ピンであり digitalWrite() で駆動できない。
// そのためVer1.01 では M5Core2 のGPIO19（M5Stack Basic では GPIO13）に変更した。
// 旧回路図のまま配線してしまった場合は、ジャンパ線でGPIO19/13に接続し直す必要がある。

#if defined(ARDUINO_M5STACK_Core2)
constexpr uint8_t SOLENOID_PIN = 19; // M5Core2 GPIO19 (J4-2) ソレノイド駆動信号
#elif defined(ARDUINO_M5Stack_Core)
constexpr uint8_t SOLENOID_PIN = 13; // M5Stack Basic GPIO13 (J4-2) ソレノイド駆動信号
#else
#error "Unsupported board: SOLENOID_PIN is not defined for this target"
#endif

// ============================================================
// I2C アドレス
// ============================================================
constexpr uint8_t PRIMARY_SENSOR_I2C_ADDR   = 0x16;  // 1次側 MPX5700AP (J5)
constexpr uint8_t SECONDARY_SENSOR_I2C_ADDR = 0x17;  // 2次側 MPX5700AP (J6)
constexpr uint8_t ADS1015_I2C_ADDR          = 0x48;  // 燃圧センサ用ADC (固定)
constexpr uint8_t ADS1015_FUEL_CHANNEL      = 0;     // AIN0

// ============================================================
// タイミング
// ============================================================
constexpr uint32_t SENSOR_READ_INTERVAL_MS  = 50;   // 20Hz
constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 100; // 10Hz

// ============================================================
// センサ関連定数
// ============================================================
constexpr uint8_t MEAN_SAMPLE_SIZE_MPX5700   = 10;   // DFRobotライブラリ内蔵移動平均サンプル数
constexpr uint8_t ADS1015_OVERSAMPLE_COUNT   = 4;    // ソフトウェア平均回数
constexpr adsGain_t FUEL_ADS_GAIN            = GAIN_ONE; // ±4.096V, 12bit

// 燃圧センサ: 0.5-4.5V ⇔ 0-1.0MPa の線形変換
constexpr float FUEL_SENSOR_V_AT_0MPA   = 0.5f;
constexpr float FUEL_SENSOR_V_AT_FULL   = 4.5f;
constexpr float FUEL_SENSOR_MPA_AT_FULL = 1.0f;
// 基板上に分圧抵抗が存在する場合のみ1.0未満に変更する(要ハード確認)
constexpr float FUEL_SENSOR_DIVIDER_RATIO = 1.0f;

// センサ有効レンジ(異常検知用、定格に余裕を持たせた範囲)
constexpr float SENSOR_RANGE_PRIMARY_KPA_MIN   = 10.0f;
constexpr float SENSOR_RANGE_PRIMARY_KPA_MAX   = 720.0f;
constexpr float SENSOR_RANGE_SECONDARY_KPA_MIN = 10.0f;
constexpr float SENSOR_RANGE_SECONDARY_KPA_MAX = 720.0f;
constexpr float SENSOR_RANGE_FUEL_MPA_MIN      = -0.02f;
constexpr float SENSOR_RANGE_FUEL_MPA_MAX      = 1.05f;

// ============================================================
// 制御目標・しきい値 (単位: MPa)
// ============================================================
constexpr float FUEL_TARGET_MPA    = 0.35f;
constexpr float FUEL_TOLERANCE_MPA = 0.01f;
constexpr float FUEL_LOWER_MPA     = FUEL_TARGET_MPA - FUEL_TOLERANCE_MPA; // 0.34
constexpr float FUEL_UPPER_MPA     = FUEL_TARGET_MPA + FUEL_TOLERANCE_MPA; // 0.36

constexpr float PRIMARY_FILL_REFERENCE_MPA = 0.6f; // 参考表示用(充填時想定値)

// ============================================================
// パルス駆動パラメータ
// ============================================================
constexpr uint32_t PULSE_WIDTH_MS    = 50;  // 1回の開弁時間
constexpr uint32_t PULSE_COOLDOWN_MS = 300; // パルス間の最小休止時間
constexpr uint32_t MAX_REGULATION_EPISODE_MS = 5000; // 連続パルスの上限(多重防御)

// ============================================================
// 安全保護しきい値 (ヒステリシス付き, 単位: MPa)
// ============================================================
constexpr float PRIMARY_SUPPLY_LOW_TRIP_MPA  = 0.40f; // これを下回ったら供給不能
constexpr float PRIMARY_SUPPLY_LOW_CLEAR_MPA = 0.43f;

constexpr float OVERPRESSURE_TRIP_MPA  = 0.50f; // 燃圧・2次側の過圧しきい値
constexpr float OVERPRESSURE_CLEAR_MPA = 0.45f;

constexpr uint8_t FAULT_TRIP_DEBOUNCE_SAMPLES  = 3;  // 約150ms
constexpr uint8_t FAULT_CLEAR_DEBOUNCE_SAMPLES = 10; // 約500ms
