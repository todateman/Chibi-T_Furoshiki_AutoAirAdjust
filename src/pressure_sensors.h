#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_MPX5700.h>
#include <Adafruit_ADS1X15.h>

#include "system_types.h"
#include "config.h"

class PressureSensors {
 public:
  bool begin();

  // SENSOR_READ_INTERVAL_MS 周期で呼び出し、最新の読み取り結果を返す
  SensorReadings read();

 private:
  DFRobot_MPX5700 primarySensor_{&Wire, PRIMARY_SENSOR_I2C_ADDR};
  DFRobot_MPX5700 secondarySensor_{&Wire, SECONDARY_SENSOR_I2C_ADDR};
  Adafruit_ADS1015 ads_;

  bool primaryOk_ = false;
  bool secondaryOk_ = false;
  bool adsOk_ = false;

  SensorSample readPrimary();
  SensorSample readSecondary();
  SensorSample readFuel();
};
