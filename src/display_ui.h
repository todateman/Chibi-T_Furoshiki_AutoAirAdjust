#pragma once

#include <M5Unified.h>

#include "system_types.h"

class DisplayUI {
 public:
  void begin();

  // DISPLAY_UPDATE_INTERVAL_MS 周期で呼び出すこと
  void update(const SensorReadings& r, const ControllerStatus& status, bool valveEnergized);

 private:
  M5Canvas valuesSprite_{&M5.Display};
  M5Canvas warningSprite_{&M5.Display};

  void drawValues(const SensorReadings& r, SystemState state, bool valveEnergized);
  void drawWarning(const SensorReadings& r, SystemState state, FaultReason reason);

  // ラベル・大きな数値・ゲージバーを1ブロック分描画する(P1/P2/Fuel共通)
  void drawGaugeBlock(const char* label, const SensorSample& s, uint16_t color, int y,
                      float gaugeMin, float gaugeMax, float bandLoMpa, float bandHiMpa,
                      uint16_t bandColor);
};
