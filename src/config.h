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

// Wire.begin()の引数なし(ボードのデフォルトSDA/SCL)には依存せず、
// 拡張基板(PCB)側の実配線に合わせて明示的にピンを指定する。
#if defined(ARDUINO_M5STACK_Core2)
constexpr int I2C_SDA_PIN = 32; // M5Core2 Port A(Grove) SDA
constexpr int I2C_SCL_PIN = 33; // M5Core2 Port A(Grove) SCL
#elif defined(ARDUINO_M5Stack_Core)
constexpr int I2C_SDA_PIN = 2;  // M5Stack Basic SDA (PCB改版でM-BUS配線を変更)
constexpr int I2C_SCL_PIN = 5;  // M5Stack Basic SCL (PCB改版でM-BUS配線を変更)
#else
#error "Unsupported board: I2C pins are not defined for this target"
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
constexpr int16_t FUEL_ADC_MAX_SAMPLE_SPREAD_COUNTS = 20; // 要実機調整: フローティング(センサ未接続)検出用の許容ばらつき(LSB)。暫定値

// 燃圧センサ: 0.5-4.5V ⇔ 0-1.0MPa の線形変換
constexpr float FUEL_SENSOR_V_AT_0MPA   = 0.5f; // 0MPa時の電圧
constexpr float FUEL_SENSOR_V_AT_FULL   = 4.5f; // 1.0MPa時の電圧
constexpr float FUEL_SENSOR_MPA_AT_FULL = 1.0f; // 1.0MPa時の圧力
// 基板上に分圧抵抗が存在する場合のみ1.0未満に変更する(要ハード確認)
// 恒久的な未接続検出にはAIN0への物理的なプルダウン/プルアップ抵抗追加が望ましい(要基板改版)
constexpr float FUEL_SENSOR_DIVIDER_RATIO = 1.0f;

// センサ有効レンジ(異常検知用、定格に余裕を持たせた範囲)
constexpr float SENSOR_RANGE_PRIMARY_KPA_MIN   = 10.0f;     // 1次側センサの有効下限値
constexpr float SENSOR_RANGE_PRIMARY_KPA_MAX   = 720.0f;    // 1次側センサの有効上限値
constexpr float SENSOR_RANGE_SECONDARY_KPA_MIN = 10.0f;     // 2次側センサの有効下限値
constexpr float SENSOR_RANGE_SECONDARY_KPA_MAX = 720.0f;    // 2次側センサの有効上限値
constexpr float SENSOR_RANGE_FUEL_MPA_MIN      = -0.02f;    // 燃圧センサの有効下限値
constexpr float SENSOR_RANGE_FUEL_MPA_MAX      = 1.05f;     // 燃圧センサの有効上限値

// ============================================================
// 制御目標・しきい値 (単位: MPa)
// ============================================================
// 目標燃圧はボタン(A: -0.01MPa, C: +0.01MPa)で実行時に調整可能(FuelTargetStore参照)。
// 以下はNVS未保存時の初期値および調整範囲の定数。
constexpr float FUEL_TARGET_DEFAULT_MPA = 0.35f; // NVSに保存値が無い場合の初期目標燃圧
constexpr float FUEL_TOLERANCE_MPA      = 0.01f; // 目標燃圧の許容誤差
constexpr float FUEL_TARGET_MIN_MPA     = 0.10f; // ボタン調整の下限
constexpr float FUEL_TARGET_MAX_MPA     = 0.45f; // ボタン調整の上限(過圧しきい値0.50MPaに対する安全マージン)
constexpr float FUEL_TARGET_STEP_MPA    = 0.01f; // Aボタン/Cボタン1回あたりの増減量

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
constexpr float PRIMARY_SUPPLY_LOW_CLEAR_MPA = 0.43f; // これを上回ったら供給可能

constexpr float OVERPRESSURE_TRIP_MPA  = 0.50f; // 燃圧・2次側の過圧しきい値
constexpr float OVERPRESSURE_CLEAR_MPA = 0.45f; // これを下回ったら過圧解除

constexpr uint8_t FAULT_TRIP_DEBOUNCE_SAMPLES  = 3;  // 約150ms
constexpr uint8_t FAULT_CLEAR_DEBOUNCE_SAMPLES = 10; // 約500ms

// ============================================================
// BLE (Peripheral) 関連
// ============================================================
constexpr const char* BLE_DEVICE_NAME = "ChibiT-AutoAirAdjust";
// Service UUID: 対向機M5NanoC6(BLE Central, M5NanoC6_BLE_Centralリポジトリ)の実装に合わせる(変更不可)
constexpr const char* BLE_SERVICE_UUID = "7c44181A-c1a4-4635-a119-b490ed272552";
// 接続維持用ダミーCharacteristic(READ+WRITE)。M5NanoC6側は接続シーケンスでこのCharacteristicの
// 存在とWrite属性の有無を確認するのみで、実データの送受信には使わない
constexpr const char* BLE_DUMMY_CHAR_UUID = "c9f878f1-c311-4452-ae5e-e813b4fe057d";
// センサ値Notify用Characteristic(実データ送信専用)
constexpr const char* BLE_NOTIFY_CHAR_UUID = "1d25ec49-e19c-4bb6-8c36-5dc8d8aaaebe";
