#include "pressure_sensors.h"

namespace {

bool i2cPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool inRange(float v, float lo, float hi) {
  return !isnan(v) && !isinf(v) && v >= lo && v <= hi;
}

}  // namespace

bool PressureSensors::begin() {
  // 拡張基板(PCB)側のI2C配線ピンに合わせてWireを明示的に初期化する。
  // (ボードデフォルトのSDA/SCLに依存すると、M5Core2ではPort A(32/33)、
  //  M5Stack BasicではPort A(21/22)になり、実際の基板配線と一致しないため)
  //
  // M5.begin() は M5.Ex_I2C(Port A) の begin()(ドライバインストール)までは行わないため、
  // ここで呼ばないと Wire.beginTransmission()/endTransmission() が txBuffer=NULL のまま
  // 常に失敗し、i2cPing() && sensor.begin() の短絡評価で sensor.begin() 自体が
  // 一度も呼ばれず、Wire が永久に未初期化になる(=センサが常に未検出になる)。
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  primaryOk_ = i2cPing(PRIMARY_SENSOR_I2C_ADDR) && primarySensor_.begin();
  if (primaryOk_) {
    primarySensor_.setMeanSampleSize(MEAN_SAMPLE_SIZE_MPX5700);
  }

  secondaryOk_ = i2cPing(SECONDARY_SENSOR_I2C_ADDR) && secondarySensor_.begin();
  if (secondaryOk_) {
    secondarySensor_.setMeanSampleSize(MEAN_SAMPLE_SIZE_MPX5700);
  }

  adsOk_ = i2cPing(ADS1015_I2C_ADDR) && ads_.begin(ADS1015_I2C_ADDR, &Wire);
  if (adsOk_) {
    ads_.setGain(FUEL_ADS_GAIN);
    // シングルショットのブロッキング読み取り(readADC_SingleEnded)はメインループを数msジッタさせるため、
    // 連続変換モードを1度だけ開始し、以降はgetLastConversionResults()で最新値を非ブロッキング取得する
    ads_.startADCReading(MUX_BY_CHANNEL[ADS1015_FUEL_CHANNEL], /*continuous=*/true);
  }

  return primaryOk_ && secondaryOk_ && adsOk_;
}

// 1次側空気圧センサの読み取り
SensorSample PressureSensors::readPrimary() {
  SensorSample s;
  if (!primaryOk_ || !i2cPing(PRIMARY_SENSOR_I2C_ADDR)) return s;

  // MPX5700AP は kPa 単位で返すので、MPa に変換する
  // getPressureValue_kpa()はセンサ基板側で既に0.01kPa単位(kPa×100の固定小数点)を保持しており、
  // ここでの変換は丸めを行わずfloatのままMPaへスケール変換するだけなので、そのネイティブ分解能をそのまま維持する
  float kpa = primarySensor_.getPressureValue_kpa(0);
  if (!inRange(kpa, SENSOR_RANGE_PRIMARY_KPA_MIN, SENSOR_RANGE_PRIMARY_KPA_MAX)) return s;

  s.value = kpa / 1000.0f - 101.325f / 1000.0f;  // 大気圧補正(101.325kPa=0.101325MPa)
  s.valid = true;
  return s;
}

// 2次側空気圧センサの読み取り
SensorSample PressureSensors::readSecondary() {
  SensorSample s;
  if (!secondaryOk_ || !i2cPing(SECONDARY_SENSOR_I2C_ADDR)) return s;

  // MPX5700AP は kPa 単位で返すので、MPa に変換する
  // (readPrimary()と同様、0.01kPa単位のネイティブ分解能を丸めずに維持する)
  float kpa = secondarySensor_.getPressureValue_kpa(0);
  if (!inRange(kpa, SENSOR_RANGE_SECONDARY_KPA_MIN, SENSOR_RANGE_SECONDARY_KPA_MAX)) return s;

  s.value = kpa / 1000.0f - 101.325f / 1000.0f;  // 大気圧補正(101.325kPa=0.101325MPa)
  s.valid = true;
  return s;
}

// 燃圧センサの読み取り
SensorSample PressureSensors::readFuel() {
  SensorSample s;
  if (!adsOk_ || !i2cPing(ADS1015_I2C_ADDR)) return s;

  // 連続変換モードでバックグラウンド変換中の最新結果を1個だけ非ブロッキングで取得し、
  // リングバッファに積む(オーバーサンプルはread()呼び出し=SENSOR_READ_INTERVAL_MSごとに1サンプルずつ進む)
  fuelSampleBuffer_[fuelSampleIndex_] = ads_.getLastConversionResults();
  fuelSampleIndex_ = (fuelSampleIndex_ + 1) % ADS1015_OVERSAMPLE_COUNT;
  if (fuelSampleFilled_ < ADS1015_OVERSAMPLE_COUNT) fuelSampleFilled_++;
  if (fuelSampleFilled_ < ADS1015_OVERSAMPLE_COUNT) return s; // 起動直後、バッファが埋まるまでは無効値

  int32_t sum = 0;
  for (uint8_t i = 0; i < ADS1015_OVERSAMPLE_COUNT; i++) {
    sum += fuelSampleBuffer_[i];
  }
  // 平均値を計算し、電圧に変換する
  int16_t rawAvg = static_cast<int16_t>(sum / ADS1015_OVERSAMPLE_COUNT);
  float vAtPin = ads_.computeVolts(rawAvg);
  float vSensor = vAtPin / FUEL_SENSOR_DIVIDER_RATIO;
  float mpa = (vSensor - FUEL_SENSOR_V_AT_0MPA) *
              (FUEL_SENSOR_MPA_AT_FULL / (FUEL_SENSOR_V_AT_FULL - FUEL_SENSOR_V_AT_0MPA));
  // AIN0はPCB改版でプルダウン抵抗(R10, 100kΩ)を追加済みのため、センサ未接続時は0V付近に
  // 安定して引き下げられる(0V換算で約-0.125MPa)。よってフローティング検出用の特別な
  // ロジック(旧: ADCサンプル間スプレッド判定)は不要で、他の異常値と同じレンジ判定だけで
  // 断線を確実に検出できる。
  if (!inRange(mpa, SENSOR_RANGE_FUEL_MPA_MIN, SENSOR_RANGE_FUEL_MPA_MAX)) return s;  // 異常値(断線含む)は無効として返す

  s.value = mpa;
  s.valid = true;
  return s;
}

// 1周期分のセンサ読み取りまとめ
SensorReadings PressureSensors::read() {
  SensorReadings r;
  r.primaryMpa = readPrimary();
  r.secondaryMpa = readSecondary();
  r.fuelMpa = readFuel();
  return r;
}
