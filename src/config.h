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
constexpr uint8_t MEAN_SAMPLE_SIZE_MPX5700   = 30;   // DFRobotライブラリ内蔵移動平均サンプル数
// 要実機調整: 大きいほどノイズは減り微小な圧力上昇を検知しやすくなるが、実際の変化への追従はやや緩やかになる(トレードオフ)
// ライブラリの初期値は 10 で、MPX5700APのノイズレベルでは十分な精度が得られないため、30に増やす
// ADS1015は連続変換モードでバックグラウンド駆動し、read()呼び出し(SENSOR_READ_INTERVAL_MS周期)ごとに
// 最新変換結果を1個だけ非ブロッキング取得してリングバッファに積む(移動平均バッファ深さ)。
// そのため実効窓幅は 約 ADS1015_OVERSAMPLE_COUNT × SENSOR_READ_INTERVAL_MS(既定値では約200ms)に伸びる。
// 旧実装(readADC_SingleEndedを4回連続ブロッキング呼び出し、数ms未満で完結)より窓が広がるため、
// FUEL_ADC_MAX_SAMPLE_SPREAD_COUNTS(フローティング検出の許容ばらつき)は実機再チューニングが必要な場合がある。
constexpr uint8_t ADS1015_OVERSAMPLE_COUNT   = 4;    // 移動平均バッファ深さ(サンプル数)
constexpr adsGain_t FUEL_ADS_GAIN            = GAIN_ONE; // ±4.096V, 12bit
// 実機計測(2026-08-11): バルブ切替直後にspreadが最大177まで跳ね上がり、約2秒(約10サンプル)かけて
// 減衰するノイズ相関を確認(診断ログ[FUEL_ADC] spread=... 参照)。旧閾値20ではこの正常な
// バルブ動作起因の揺れを毎回センサ未接続と誤検知していたため、観測ピーク(177)に余裕を持たせて
// 250へ引き上げる。要実機再確認: 燃圧センサーを物理的に切断した状態でSensorError(FUEL)が
// 引き続き正しく検出されることを確認してから確定すること(誤検知緩和のため感度を落としすぎていないか)。
constexpr int16_t FUEL_ADC_MAX_SAMPLE_SPREAD_COUNTS = 250; // フローティング(センサ未接続)検出用の許容ばらつき(LSB)

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
// 目標2次側空気圧はボタン(A: -0.01MPa, C: +0.01MPa)で実行時に調整可能(SecondaryTargetStore参照)。
// 以下はNVS未保存時の初期値および調整範囲の定数。
constexpr float SECONDARY_TARGET_DEFAULT_MPA = 0.35f; // NVSに保存値が無い場合の初期目標2次側空気圧
constexpr float SECONDARY_TOLERANCE_MPA      = 0.01f; // 目標2次側空気圧の許容誤差
constexpr float SECONDARY_TARGET_MIN_MPA     = 0.10f; // ボタン調整の下限
constexpr float SECONDARY_TARGET_MAX_MPA     = 0.45f; // ボタン調整の上限(過圧しきい値0.50MPaに対する安全マージン)
constexpr float SECONDARY_TARGET_STEP_MPA    = 0.01f; // Aボタン/Cボタン1回あたりの増減量

constexpr float PRIMARY_FILL_REFERENCE_MPA = 0.6f; // 参考表示用(充填時想定値)

// ============================================================
// パルス駆動パラメータ
// ============================================================
// 自己適応パルス幅制御: 最小幅から開始し、パルス結果(下限未達=伸長/上限超過=短縮)に応じて次回幅を調整する
// 実機計測(2026-08-11, RIGOL DHO804/VISA-MCP, docs/oscilloscope_dho804_visa_mcp.md参照):
//   デッドタイム(SOLENOID_PIN駆動開始→2次側センサ電圧の有意な立ち上がり) 実測 約5.4ms
//   整定時間(駆動停止→2次側センサ電圧が最終値の±10%以内に収束)       実測 約21ms
constexpr uint32_t PULSE_WIDTH_MIN_MS = 8;   // 下限パルス幅(実測デッドタイム約5.4ms + 微小送気マージンを反映。旧暫定値5ms)
constexpr uint32_t PULSE_WIDTH_MAX_MS = 50;  // 上限パルス幅(安全キャップ、旧PULSE_WIDTH_MS)
constexpr float PULSE_WIDTH_GROW_FACTOR   = 1.5f; // 目標帯下限未達時の伸長倍率
constexpr float PULSE_WIDTH_SHRINK_FACTOR = 0.5f; // 目標帯上限超過(過供給)時の短縮倍率
constexpr uint32_t PULSE_COOLDOWN_MS = 50; // パルス間の最小休止時間(実測整定時間 約21ms << 50ms で十分なマージンを確保)
constexpr uint32_t MAX_REGULATION_EPISODE_MS = 5000; // 連続パルスの上限(多重防御)

// ============================================================
// 安全保護しきい値 (ヒステリシス付き, 単位: MPa)
// ============================================================
constexpr float PRIMARY_SUPPLY_LOW_TRIP_MPA  = 0.40f; // これを下回ったら供給不能
constexpr float PRIMARY_SUPPLY_LOW_CLEAR_MPA = 0.43f; // これを上回ったら供給可能

constexpr float OVERPRESSURE_TRIP_MPA  = 0.50f; // 燃圧・2次側の過圧しきい値(多重防御。制御量ではなく安全判定にのみ使用)
constexpr float OVERPRESSURE_CLEAR_MPA = 0.45f; // これを下回ったら過圧解除

// 注意: 「約Xms」はSENSOR_READ_INTERVAL_MS(50ms)周期で1サンプル進む前提の値だが、
// 実際にはsensors.read()内のMPX5700読み取りが約200msブロッキングするため、
// controller.update()(=1サンプル)は実質50msではなく約200ms周期でしか進まない。
// そのため実時間はこのコメントの約4倍(TRIP≈600ms、CLEAR≈2000ms)になる。
// 判定ロジック自体は連続サンプル数ベースなので安全性には影響しないが、
// sensors.read()のブロッキングを別途解消した場合はこのコメントも再導出が必要。
constexpr uint8_t FAULT_TRIP_DEBOUNCE_SAMPLES  = 3;  // 約150ms(理論値。実際は約600ms、上記注意参照)
constexpr uint8_t FAULT_CLEAR_DEBOUNCE_SAMPLES = 10; // 約500ms(理論値。実際は約2000ms、上記注意参照)

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

// ============================================================
// OTA (Wi-Fi AP経由ブラウザアップロード) 関連
// ============================================================
// 起動時にBボタンを押した状態で電源を入れると、通常運転(センサ・コントローラ・BLE)を
// 一切開始せずWi-Fi APを起動し、ブラウザ経由でファームウェア(.bin)を書き込めるモードへ遷移する
// (src/ota_service.h/.cpp参照)。ESP32 Arduino core標準のWiFi.h/WebServer.h/Update.hのみを使用し、
// 外部OTAライブラリ(lib_deps)は追加しない。
constexpr const char* OTA_AP_SSID     = "ChibiT-OTA";  // Wi-Fi AP SSID
constexpr const char* OTA_AP_PASSWORD = "chibit-ota";  // Wi-Fi APパスワード(WPA2は8文字以上必須。運用時は変更を推奨)
constexpr uint16_t    OTA_HTTP_PORT   = 80;             // アップロード用WebServerのポート
