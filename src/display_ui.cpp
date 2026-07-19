#include "display_ui.h"
#include "config.h"

namespace {
constexpr int kValuesH = 195;
constexpr int kWarningH = 45;

// P1/P2ゲージの表示レンジ(センサのフルスケール0.7MPaに余裕を持たせた範囲)
constexpr float PRESSURE_GAUGE_MIN_MPA = 0.0f;
constexpr float PRESSURE_GAUGE_MAX_MPA = 0.8f;
}  // namespace

void DisplayUI::begin() {
  // M5Canvasはデフォルトで内部DRAMからバッファを確保するため(PSRAM未使用)、
  // 320x195px(16bit)だと空きDRAMを圧迫し確保に失敗する場合がある。
  // 8bit色深度にしてバッファサイズを半減させ、確保失敗(→描画されず画面が真っ黒になる)を防ぐ。
  valuesSprite_.setColorDepth(8);
  valuesSprite_.createSprite(320, kValuesH);
  warningSprite_.setColorDepth(8);
  warningSprite_.createSprite(320, kWarningH);

  M5.Display.fillScreen(TFT_BLACK);
}

// ラベル・大きな数値・ゲージバーを1ブロック分描画する(P1/P2/Fuel共通)
void DisplayUI::drawGaugeBlock(const char* label, const SensorSample& s, uint16_t color, int y,
                                float gaugeMin, float gaugeMax, float bandLoMpa, float bandHiMpa,
                                uint16_t bandColor) {
  valuesSprite_.setTextSize(3);
  valuesSprite_.setTextColor(color, TFT_BLACK);
  valuesSprite_.setCursor(8, y);
  if (s.valid) {
    valuesSprite_.printf("%-5s%4.2f", label, s.value);
  } else {
    valuesSprite_.printf("%-5s----", label);
  }

  const int gaugeX = 8, gaugeW = 300, gaugeH = 16;
  const int gaugeY = y + 28;
  valuesSprite_.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, TFT_WHITE);

  auto toX = [&](float mpa) {
    float clamped = constrain(mpa, gaugeMin, gaugeMax);
    return gaugeX + static_cast<int>((clamped - gaugeMin) / (gaugeMax - gaugeMin) * gaugeW);
  };

  int bandLo = toX(bandLoMpa);
  int bandHi = toX(bandHiMpa);
  valuesSprite_.fillRect(bandLo, gaugeY + 1, max(1, bandHi - bandLo), gaugeH - 2, bandColor);

  if (s.valid) {
    int needleX = toX(s.value);
    valuesSprite_.fillRect(needleX - 2, gaugeY - 3, 4, gaugeH + 6, color);
  }
}

// センサ値の表示
void DisplayUI::drawValues(const SensorReadings& r, SystemState state, bool valveEnergized) {
  valuesSprite_.fillSprite(TFT_BLACK);

  // P1(1次側): 供給圧低下しきい値を下回ったら警告色(CYAN)、無効値ならRED
  uint16_t p1Color;
  if (!r.primaryMpa.valid) {
    p1Color = TFT_RED;
  } else if (r.primaryMpa.value < PRIMARY_SUPPLY_LOW_TRIP_MPA) {
    p1Color = TFT_CYAN;
  } else {
    p1Color = TFT_GREEN;
  }
  drawGaugeBlock("P1", r.primaryMpa, p1Color, 4, PRESSURE_GAUGE_MIN_MPA, PRESSURE_GAUGE_MAX_MPA,
                 PRESSURE_GAUGE_MIN_MPA, PRIMARY_SUPPLY_LOW_TRIP_MPA,
                 valuesSprite_.color565(0, 60, 90));

  // P2(2次側): 過圧しきい値を上回ったら警告色(YELLOW)、無効値ならRED
  uint16_t p2Color;
  if (!r.secondaryMpa.valid) {
    p2Color = TFT_RED;
  } else if (r.secondaryMpa.value > OVERPRESSURE_TRIP_MPA) {
    p2Color = TFT_YELLOW;
  } else {
    p2Color = TFT_GREEN;
  }
  drawGaugeBlock("P2", r.secondaryMpa, p2Color, 54, PRESSURE_GAUGE_MIN_MPA, PRESSURE_GAUGE_MAX_MPA,
                 OVERPRESSURE_TRIP_MPA, PRESSURE_GAUGE_MAX_MPA, valuesSprite_.color565(90, 70, 0));

  // Fuel(燃圧): 目標帯を下回ればCYAN、上回ればYELLOW、無効値ならRED
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
  drawGaugeBlock("Fuel", r.fuelMpa, fuelColor, 104, 0.0f, 0.5f, FUEL_LOWER_MPA, FUEL_UPPER_MPA,
                 valuesSprite_.color565(0, 90, 0));

  valuesSprite_.setTextSize(3);
  valuesSprite_.setCursor(8, 160);
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
  drawValues(r, status.state, valveEnergized);
  drawWarning(r, status.state, status.faultReason);

  valuesSprite_.pushSprite(0, 0);
  warningSprite_.pushSprite(0, 240 - kWarningH);
}
