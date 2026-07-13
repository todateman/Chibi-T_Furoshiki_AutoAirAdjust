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
  }

  return primaryOk_ && secondaryOk_ && adsOk_;
}

SensorSample PressureSensors::readPrimary() {
  SensorSample s;
  if (!primaryOk_ || !i2cPing(PRIMARY_SENSOR_I2C_ADDR)) return s;

  float kpa = primarySensor_.getPressureValue_kpa(0);
  if (!inRange(kpa, SENSOR_RANGE_PRIMARY_KPA_MIN, SENSOR_RANGE_PRIMARY_KPA_MAX)) return s;

  s.value = kpa / 1000.0f;
  s.valid = true;
  return s;
}

SensorSample PressureSensors::readSecondary() {
  SensorSample s;
  if (!secondaryOk_ || !i2cPing(SECONDARY_SENSOR_I2C_ADDR)) return s;

  float kpa = secondarySensor_.getPressureValue_kpa(0);
  if (!inRange(kpa, SENSOR_RANGE_SECONDARY_KPA_MIN, SENSOR_RANGE_SECONDARY_KPA_MAX)) return s;

  s.value = kpa / 1000.0f;
  s.valid = true;
  return s;
}

SensorSample PressureSensors::readFuel() {
  SensorSample s;
  if (!adsOk_ || !i2cPing(ADS1015_I2C_ADDR)) return s;

  int32_t sum = 0;
  for (uint8_t i = 0; i < ADS1015_OVERSAMPLE_COUNT; i++) {
    sum += ads_.readADC_SingleEnded(ADS1015_FUEL_CHANNEL);
  }
  int16_t rawAvg = static_cast<int16_t>(sum / ADS1015_OVERSAMPLE_COUNT);
  float vAtPin = ads_.computeVolts(rawAvg);
  float vSensor = vAtPin / FUEL_SENSOR_DIVIDER_RATIO;

  float mpa = (vSensor - FUEL_SENSOR_V_AT_0MPA) *
              (FUEL_SENSOR_MPA_AT_FULL / (FUEL_SENSOR_V_AT_FULL - FUEL_SENSOR_V_AT_0MPA));
  if (!inRange(mpa, SENSOR_RANGE_FUEL_MPA_MIN, SENSOR_RANGE_FUEL_MPA_MAX)) return s;

  s.value = mpa;
  s.valid = true;
  return s;
}

SensorReadings PressureSensors::read() {
  SensorReadings r;
  r.primaryMpa = readPrimary();
  r.secondaryMpa = readSecondary();
  r.fuelMpa = readFuel();
  return r;
}
