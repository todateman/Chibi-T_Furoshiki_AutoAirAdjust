#include "display_ui.h"
#include "config.h"

namespace {
constexpr int kBannerH = 30;
constexpr int kValuesH = 160;
constexpr int kWarningH = 45;
}  // namespace

void DisplayUI::begin() {
  bannerSprite_.createSprite(320, kBannerH);
  valuesSprite_.createSprite(320, kValuesH);
  warningSprite_.createSprite(320, kWarningH);

  M5.Display.fillScreen(TFT_BLACK);
}

// システム状態の文字列ラベルを返す
void DisplayUI::drawBanner(const SensorReadings& r, SystemState state, FaultReason reason) {
  uint16_t bg;
  switch (state) {
    case SystemState::Fault: bg = TFT_RED; break;
    case SystemState::PulseOpen: bg = TFT_CYAN; break;
    case SystemState::Init: bg = TFT_DARKGREY; break;
    default: bg = TFT_GREEN; break;  // Normal, Cooldown
  }
  bannerSprite_.fillSprite(bg);
  bannerSprite_.setTextSize(2);
  bannerSprite_.setTextColor(TFT_BLACK, bg);
  bannerSprite_.setCursor(8, 6);
  if (state == SystemState::Fault) {
    if (reason == FaultReason::SensorError) {
      char detail[16];
      formatSensorErrorDetail(r, detail, sizeof(detail));
      bannerSprite_.printf("FAULT: %s (%s)", faultReasonLabel(reason), detail);
    } else {
      bannerSprite_.printf("FAULT: %s", faultReasonLabel(reason));
    }
  } else {
    bannerSprite_.printf("%s", systemStateLabel(state));
  }
}

// センサ値の表示
void DisplayUI::drawValues(const SensorReadings& r, SystemState state, bool valveEnergized) {
  valuesSprite_.fillSprite(TFT_BLACK);

  valuesSprite_.setTextSize(1);
  valuesSprite_.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  valuesSprite_.setCursor(8, 4);
  if (r.primaryMpa.valid) {
    valuesSprite_.printf("1次側   %5.2f MPa", r.primaryMpa.value);
  } else {
    valuesSprite_.printf("1次側   ---- (センサ異常)");
  }

  valuesSprite_.setCursor(8, 22);
  if (r.secondaryMpa.valid) {
    valuesSprite_.printf("2次側   %5.2f MPa", r.secondaryMpa.value);
  } else {
    valuesSprite_.printf("2次側   ---- (センサ異常)");
  }

  uint16_t fuelColor;
  if (!r.fuelMpa.valid) {
    fuelColor = TFT_RED;
  } else if (r.fuelMpa.value < FUEL_LOWER_MPA) {
    fuelColor = TFT_CYAN;
  } else if (r.fuelMpa.value > FUEL_UPPER_MPA) {
    fuelColor = TFT_YELLOW;
  } else {
    fuelColor = TFT_GREEN;
  }

  valuesSprite_.setTextSize(3);
  valuesSprite_.setTextColor(fuelColor, TFT_BLACK);
  valuesSprite_.setCursor(8, 44);
  if (r.fuelMpa.valid) {
    valuesSprite_.printf("Fuel %4.2f", r.fuelMpa.value);
  } else {
    valuesSprite_.printf("Fuel ----");
  }

  // ゲージバー (0-0.5MPaレンジ、目標帯を濃緑で表示)
  const int gaugeX = 8, gaugeY = 84, gaugeW = 300, gaugeH = 20;
  const float gaugeMin = 0.0f, gaugeMax = 0.5f;
  valuesSprite_.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, TFT_WHITE);

  auto toX = [&](float mpa) {
    float clamped = constrain(mpa, gaugeMin, gaugeMax);
    return gaugeX + static_cast<int>((clamped - gaugeMin) / (gaugeMax - gaugeMin) * gaugeW);
  };

  int bandLo = toX(FUEL_LOWER_MPA);
  int bandHi = toX(FUEL_UPPER_MPA);
  valuesSprite_.fillRect(bandLo, gaugeY + 1, max(1, bandHi - bandLo), gaugeH - 2,
                          valuesSprite_.color565(0, 90, 0));

  if (r.fuelMpa.valid) {
    int needleX = toX(r.fuelMpa.value);
    valuesSprite_.fillRect(needleX - 2, gaugeY - 4, 4, gaugeH + 8, fuelColor);
  }

  valuesSprite_.setTextSize(2);
  valuesSprite_.setCursor(8, 120);
  if (valveEnergized) {
    valuesSprite_.setTextColor(TFT_CYAN, TFT_BLACK);
    valuesSprite_.print("VALVE: OPEN");
  } else {
    valuesSprite_.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    valuesSprite_.print("VALVE: CLOSED");
  }
}

// 警告表示
void DisplayUI::drawWarning(const SensorReadings& r, SystemState state, FaultReason reason) {
  if (state == SystemState::Fault) {
    warningSprite_.fillSprite(TFT_RED);
    warningSprite_.setTextSize(2);
    warningSprite_.setTextColor(TFT_WHITE, TFT_RED);
    warningSprite_.setCursor(8, 12);
    if (reason == FaultReason::SensorError) {
      char detail[16];
      formatSensorErrorDetail(r, detail, sizeof(detail));
      warningSprite_.printf("WARNING: %s (%s)", faultReasonLabel(reason), detail);
    } else {
      warningSprite_.printf("WARNING: %s", faultReasonLabel(reason));
    }
  } else {
    warningSprite_.fillSprite(TFT_BLACK);
  }
}

// 画面更新
void DisplayUI::update(const SensorReadings& r, const ControllerStatus& status, bool valveEnergized) {
  drawBanner(r, status.state, status.faultReason);
  drawValues(r, status.state, valveEnergized);
  drawWarning(r, status.state, status.faultReason);

  bannerSprite_.pushSprite(0, 0);
  valuesSprite_.pushSprite(0, kBannerH + 5);
  warningSprite_.pushSprite(0, 240 - kWarningH);
}
