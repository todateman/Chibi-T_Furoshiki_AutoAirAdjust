#pragma once

#include <M5Unified.h>

#include "system_types.h"

class DisplayUI {
 public:
  void begin();

  // DISPLAY_UPDATE_INTERVAL_MS 周期で呼び出すこと
  void update(const SensorReadings& r, const ControllerStatus& status, bool valveEnergized);

 private:
  M5Canvas bannerSprite_{&M5.Display};
  M5Canvas valuesSprite_{&M5.Display};
  M5Canvas warningSprite_{&M5.Display};

  void drawBanner(SystemState state, FaultReason reason);
  void drawValues(const SensorReadings& r, SystemState state, bool valveEnergized);
  void drawWarning(SystemState state, FaultReason reason);
};
