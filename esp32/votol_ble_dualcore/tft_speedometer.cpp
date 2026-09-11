#include "tft_speedometer.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "wifi_handler.h"

extern std::atomic<int> atomicSpeed;
extern std::atomic<int> atomicRPM;
extern std::atomic<int32_t> atomicVoltsRaw;
extern std::atomic<int32_t> atomicAmpereRaw;
extern std::atomic<int32_t> atomicPowerRaw;
extern std::atomic<float> atomicOdometerKm;
extern std::atomic<VehicleMode> atomicMode;
extern std::atomic<bool> atomicRegenActive;
extern int valSOC;
extern int valSOH;
extern int valCtrlTemp;
extern int valMotorTemp;
extern float valRemainingCapacity;
extern float valFullCapacity;
extern bool oriChargerDetected;
extern uint16_t valCells[23];
extern uint16_t valHighestCellVolt;
extern uint8_t valHighestCellNum;
extern uint16_t valLowestCellVolt;
extern uint8_t valLowestCellNum;
extern uint16_t valAvgCellVolt;
extern uint8_t valMaxTemp;
extern uint8_t valMaxTempCell;
extern uint8_t valMinTemp;
extern uint8_t valMinTempCell;
extern uint16_t valCycleCount;
extern std::atomic<bool> canDataReady;
extern Preferences preferences;

TFT_eSPI tft = TFT_eSPI();

namespace {
const uint16_t DISP_CS_PIN = 27;
const uint16_t DISP_DC_PIN = 26;
const uint16_t DISP_RST_PIN = 25;
const uint16_t DISP_MOSI_PIN = 23;
const uint16_t DISP_MISO_PIN = 19;
const uint16_t DISP_SCK_PIN = 18;

const uint16_t COLOR_BLACK = TFT_BLACK;
const uint16_t COLOR_WHITE = TFT_WHITE;
const uint16_t COLOR_RED = TFT_RED;
const uint16_t COLOR_GREEN = TFT_GREEN;
const uint16_t COLOR_BLUE = TFT_BLUE;
const uint16_t COLOR_CYAN = TFT_CYAN;
const uint16_t COLOR_YELLOW = TFT_YELLOW;
const uint16_t COLOR_ORANGE = 0xFD20;
const uint16_t COLOR_DARKGREY = 0x4208;
const uint16_t COLOR_GREY = 0x7BEF;

const uint8_t PAGE_BUTTON_PIN = 32;
const uint8_t TRIP_RESET_BUTTON_PIN = 33;
const uint32_t BUTTON_DEBOUNCE_MS = 20UL;

const uint32_t STARTUP_LOADING_MS = 1500UL;
const uint32_t STARTUP_ANIMATION_MS = 1800UL;

const int RPM_MAX = 1300;
const int DISPLAY_WIDTH = 480;
const int DISPLAY_HEIGHT = 320;
const int CENTER_X = 240;
const int CENTER_Y = 150;

enum StartupState {
  STARTUP_LOADING,
  STARTUP_ANIMATING,
  STARTUP_DONE
};

StartupState startupState = STARTUP_LOADING;
uint32_t startupStartedAt = 0;
uint32_t startupPhaseStartedAt = 0;
float startupSpeed = 0.0f;
float startupRPM = 0.0f;

float displaySpeed = 0.0f;
float displayRPM = 0.0f;
float displayVoltage = 0.0f;
float displayCurrent = 0.0f;
float displayPower = 0.0f;
float displaySOC = 0.0f;
float displayOdometerKm = 0.0f;
float displayTripKm = 0.0f;

bool tripButtonLatched = false;

// Page 1 = main/speed screen.
// Page 2 = BMS screen.
// The GPIO page button is active only while held to GND.

uint32_t lastFrameMs = 0;
uint32_t lastTripUpdateMs = 0;
uint32_t lastOdoSaveMs = 0;
bool mainPageInitialized = false;
uint32_t sportHoldStartMs = 0;
bool tripResetArmed = false;
bool tripResetConsumed = false;
bool headerStateInitialized = false;
VehicleMode lastHeaderMode = MODE_PARK;
bool lastReadyVisible = false;
int lastRenderedSpeed = -1;
VehicleMode lastCenterMode = MODE_PARK;
bool lastCenterParkState = false;
bool lastRegenVisible = false;
enum DisplayPage {
  PAGE_MAIN = 0,
  PAGE_BMS = 1
};

DisplayPage currentDisplayPage = PAGE_MAIN;
VehicleMode lastPageMode = MODE_PARK;

void centerText(const String& text, int x, int y, int textSize, uint16_t color) {
  tft.setTextSize(textSize);
  tft.setTextColor(color, COLOR_BLACK);
  int width = tft.textWidth(text);
  tft.setCursor(x - width / 2, y);
  tft.print(text);
}

void drawLoadingScreen(uint8_t progressPercent) {
  tft.fillScreen(COLOR_BLACK);

  centerText("LOADING", CENTER_X, 90, 4, COLOR_CYAN);
  centerText("INITIALIZING DASHBOARD", CENTER_X, 150, 2, COLOR_WHITE);

  const int barX = 25;
  const int barY = 200;
  const int barW = 430;
  const int barH = 20;

  tft.drawRect(barX, barY, barW, barH, COLOR_DARKGREY);

  int fillWidth = constrain(progressPercent, 0, 100);
  fillWidth = (fillWidth * (barW - 2)) / 100;

  if (fillWidth > 0) {
    tft.fillRect(barX + 1, barY + 1, fillWidth, barH - 2, COLOR_CYAN);
  }

  centerText(String(progressPercent) + "%", CENTER_X, 235, 2, COLOR_WHITE);
}

const char* getModeName() {
  VehicleMode mode = atomicMode.load(std::memory_order_acquire);

  switch (mode) {
    case MODE_PARK: return "PARK";
    case MODE_DRIVE: return "DRIVE";
    case MODE_SPORT: return "SPORT";
    case MODE_REVERSE: return "REVERSE";
    case MODE_BRAKE: return "BRAKE";
    case MODE_CHARGING: return "CHARGING";
    case MODE_STAND: return "STAND";
    default: return "UNKNOWN";
  }
}

uint16_t getRPMColor(float rpm) {
  if (rpm < 200) return COLOR_DARKGREY;
  if (rpm < 600) return COLOR_BLUE;
  if (rpm < 800) return COLOR_ORANGE;
  return COLOR_RED;
}

void drawRPMArc(float rpm) {
  const int radiusOuter = 105;
  const int radiusInner = 96;

  tft.fillRect(CENTER_X - radiusOuter - 12, CENTER_Y - radiusOuter - 12,
               (radiusOuter + 12) * 2, (radiusOuter + 12) * 2, COLOR_BLACK);

  for (int angle = -135; angle <= 135; angle++) {
    float rad = (angle - 90) * DEG_TO_RAD;
    int x1 = CENTER_X + cos(rad) * radiusInner;
    int y1 = CENTER_Y + sin(rad) * radiusInner;
    int x2 = CENTER_X + cos(rad) * radiusOuter;
    int y2 = CENTER_Y + sin(rad) * radiusOuter;
    tft.drawLine(x1, y1, x2, y2, COLOR_DARKGREY);
  }

  float percentage = rpm / (float)RPM_MAX;
  percentage = constrain(percentage, 0.0f, 1.0f);
  float activeAngle = -135.0f + percentage * 270.0f;

  for (float angle = -135; angle <= activeAngle; angle += 1.0f) {
    float rad = (angle - 90) * DEG_TO_RAD;
    int x1 = CENTER_X + cos(rad) * radiusInner;
    int y1 = CENTER_Y + sin(rad) * radiusInner;
    int x2 = CENTER_X + cos(rad) * radiusOuter;
    int y2 = CENTER_Y + sin(rad) * radiusOuter;
    float sectionRPM = ((angle + 135.0f) / 270.0f) * RPM_MAX;

    tft.drawLine(x1, y1, x2, y2, getRPMColor(sectionRPM));
  }

  tft.drawCircle(CENTER_X, CENTER_Y, radiusOuter + 1, COLOR_ORANGE);
  tft.drawCircle(CENTER_X, CENTER_Y, radiusOuter, COLOR_ORANGE);
}

void drawRPMScale(float rpm) {
  for (int i = 0; i <= 36; i++) {
    float tickRPM = i * 250.0f;
    float angle = -135.0f + ((float)i / 36.0f) * 270.0f;
    float rad = (angle - 90) * DEG_TO_RAD;

    bool active = tickRPM <= rpm;
    uint16_t color = active ? getRPMColor(tickRPM) : COLOR_DARKGREY;

    int rOuter = 100;
    int rInner = (i % 4 == 0) ? 87 : 92;

    int x1 = CENTER_X + cos(rad) * rInner;
    int y1 = CENTER_Y + sin(rad) * rInner;
    int x2 = CENTER_X + cos(rad) * rOuter;
    int y2 = CENTER_Y + sin(rad) * rOuter;

    tft.drawLine(x1, y1, x2, y2, color);
  }

  int labels[] = {0, 200, 400, 600, 800, 1000, 1200, 1300};
  int labelCount = sizeof(labels) / sizeof(labels[0]);

  for (int i = 0; i < labelCount; i++) {
    float numberRPM = labels[i];
    float angle = -135.0f + (numberRPM / RPM_MAX) * 270.0f;
    float rad = (angle - 90) * DEG_TO_RAD;

    const int radius = 76;
    int x = CENTER_X + cos(rad) * radius;
    int y = CENTER_Y + sin(rad) * radius;

    uint16_t color = numberRPM <= rpm ? getRPMColor(numberRPM) : COLOR_DARKGREY;
    centerText(String(numberRPM), x, y - 5, 1, color);
  }
}

void drawSpeedWithValue(float speedValue) {
  tft.fillRect(CENTER_X - 150, CENTER_Y - 80, 300, 160, COLOR_BLACK);

  int speedValueInt = round(speedValue);
  speedValueInt = constrain(speedValueInt, 0, 999);

  if (atomicMode.load(std::memory_order_acquire) == MODE_CHARGING) {
    if (displaySOC >= 100.0f) {
      centerText("FULL", CENTER_X, CENTER_Y - 12, 3, COLOR_CYAN);
    } else {
      bool blink = (millis() / 250) % 2 == 0;
      centerText("C", CENTER_X, CENTER_Y - 22, 4, blink ? COLOR_CYAN : COLOR_BLUE);
    }
    return;
  }

  centerText(String(speedValueInt), CENTER_X, CENTER_Y - 18, 5, COLOR_WHITE);
  centerText("km/h", CENTER_X, CENTER_Y + 34, 2, COLOR_CYAN);

  VehicleMode currentMode = atomicMode.load(std::memory_order_acquire);
  bool regenVisible = (atomicRegenActive.load(std::memory_order_acquire) ||
                       (currentMode == MODE_BRAKE && displayPower < -0.5f)) &&
                      currentMode != MODE_CHARGING;
  if (regenVisible) {
    centerText("REGEN", CENTER_X, CENTER_Y + 55, 2, COLOR_YELLOW);
  }
}

void drawHeader() {
  VehicleMode mode = atomicMode.load(std::memory_order_acquire);
  bool readyVisible = mode == MODE_DRIVE && displaySpeed <= 0.5f && displayRPM <= 100 && mode != MODE_CHARGING;

  if (!headerStateInitialized || mode != lastHeaderMode) {
    tft.fillRect(0, 0, DISPLAY_WIDTH, 42, COLOR_BLACK);

    tft.setTextSize(2);
    tft.setTextColor(COLOR_GREY, COLOR_BLACK);
    tft.setCursor(15, 10);
    tft.print("TAMIYA BALAP");
    tft.drawFastHLine(15, 34, DISPLAY_WIDTH - 15, COLOR_DARKGREY);

    uint16_t modeColor = COLOR_GREEN;

    if (mode == MODE_SPORT) modeColor = COLOR_ORANGE;
    else if (mode == MODE_REVERSE) modeColor = COLOR_BLUE;
    else if (mode == MODE_CHARGING) modeColor = COLOR_CYAN;
    else if (mode == MODE_BRAKE) modeColor = COLOR_RED;

    tft.setTextColor(modeColor, COLOR_BLACK);
    tft.setTextSize(2);
    const char* modeText = getModeName();
    int modeWidth = tft.textWidth(modeText);
    tft.setCursor(DISPLAY_WIDTH - 15 - modeWidth, 10);
    tft.print(modeText);

    lastHeaderMode = mode;
    headerStateInitialized = true;
  }

  if (readyVisible != lastReadyVisible) {
    tft.fillRect(DISPLAY_WIDTH - 90, 28, 90, 18, COLOR_BLACK);

    if (readyVisible) {
      tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
      tft.setTextSize(1);
      String ready = "READY";
      int readyWidth = tft.textWidth(ready);
      tft.setCursor(DISPLAY_WIDTH - 15 - readyWidth, 48);
      tft.print(ready);
    }

    lastReadyVisible = readyVisible;
  }
}

void drawBattery() {
  int x = 15;
  int y = 55;

  uint16_t borderColor = atomicMode.load(std::memory_order_acquire) == MODE_CHARGING ? COLOR_YELLOW : COLOR_CYAN;

  tft.drawRect(x, y, 40, 12, borderColor);
  tft.fillRect(x + 40, y + 3, 3, 5, borderColor);

  int soc = constrain((int)round(displaySOC), 0, 100);
  int fillWidth = map(soc, 0, 100, 0, 36);

  uint16_t fillColor = soc < 20 ? COLOR_RED : (soc < 50 ? COLOR_YELLOW : COLOR_GREEN);

  if (fillWidth > 0) {
    tft.fillRect(x + 2, y + 2, fillWidth, 8, fillColor);
  }

  tft.setTextSize(1);
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.setCursor(15, 43);
  tft.print("BATTERY");

  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(62, 56);
  tft.printf("%d%%", soc);

  if (atomicMode.load(std::memory_order_acquire) == MODE_CHARGING && soc < 100) {
    int boltX = x + 7 + (fillWidth * 22) / 36;
    tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
    tft.setTextSize(2);
    tft.setCursor(boltX, y - 1);
    tft.print("⚡");
  }
}

void drawFooter() {
  tft.setTextSize(1);
  tft.drawFastHLine(0, 268, DISPLAY_WIDTH, COLOR_DARKGREY);

  bool canOK = canDataReady.load(std::memory_order_acquire);
  bool chargingMode = atomicMode.load(std::memory_order_acquire) == MODE_CHARGING;
  bool regenVisible = atomicRegenActive.load(std::memory_order_acquire) ||
                      (atomicMode.load(std::memory_order_acquire) == MODE_BRAKE && displayPower < -0.5f);

float powerIn = 0.0f;
float powerOut = 0.0f;

if (chargingMode) {
    // ==========================================
    // CHARGING = DAYA MASUK KE BATTERY
    // ==========================================
    float chargingCurrent = fabsf(displayCurrent);

    powerIn = displayVoltage * chargingCurrent;

    // Tidak ada W OUT ketika charging
    powerOut = 0.0f;

} else if (displayPower > 0.5f) {
    // ==========================================
    // MOTOR CONSUMPTION = DAYA KELUAR
    // ==========================================
    powerOut = displayPower;

} else if (displayPower < -0.5f) {
    // ==========================================
    // REGEN = DAYA KEMBALI KE BATTERY
    // ==========================================
    powerIn = fabsf(displayPower);
}

  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.setCursor(15, 276);
  tft.print("TRIP:");
  tft.printf("%.1f", displayTripKm);
  tft.print("km");

  tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
  tft.fillRect(120, 276, 90, 16, COLOR_BLACK);
  tft.setCursor(120, 276);
  tft.print("W IN:");
  tft.print((long)powerIn);
  tft.print("W");

  tft.setTextColor(COLOR_ORANGE, COLOR_BLACK);
  tft.fillRect(220, 276, 100, 16, COLOR_BLACK);
  tft.setCursor(220, 276);
  tft.print("W OUT:");
  tft.print((long)powerOut);
  tft.print("W");

  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.setCursor(330, 276);
  tft.print("ODO:");
  tft.printf("%.0f", displayOdometerKm);
  tft.print("km");

  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setCursor(15, 300);
  tft.print("BATT:");
  tft.printf("%.1fV", displayVoltage);

  tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  tft.setCursor(120, 300);
  tft.print("BLDC:");
  tft.printf("%dC", valMotorTemp);

  tft.setTextColor(COLOR_RED, COLOR_BLACK);
  tft.setCursor(230, 300);
  tft.print("CTRL:");
  tft.printf("%dC", valCtrlTemp);

  if (chargingMode && oriChargerDetected) {
    tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft.setTextSize(1);
    tft.setCursor(15, 288);
    tft.print("ORIGINAL CHARGING");
  }

  tft.setTextColor(canOK ? COLOR_GREEN : COLOR_RED, COLOR_BLACK);
  tft.setCursor(DISPLAY_WIDTH - 120, 300);
  tft.print(canOK ? "CAN: OK" : "CAN: OFF");
}

void drawBMSHeader() {
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.setTextSize(2);
  tft.setCursor(15, 10);
  tft.print("BMS MONITORING & CELLS");
  tft.drawFastHLine(0, 34, DISPLAY_WIDTH, COLOR_DARKGREY);
}

void drawBMSParameters() {
  tft.setTextSize(1);
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);

  tft.setCursor(15, 47);
  tft.printf("Volt : %.1f V", displayVoltage);

  tft.setCursor(250, 47);
  tft.printf("Curr : %.1f A", displayCurrent);

  tft.setCursor(15, 67);
  tft.printf("Power: %.0f W", displayPower);

  tft.setCursor(250, 67);
  tft.printf("SOH : %d %%", valSOH > 0 ? valSOH : (int)displaySOC);

  tft.setCursor(15, 87);
  tft.printf("Min Cell: %.3f V (#%d)", valLowestCellVolt / 1000.0f, valLowestCellNum);

  tft.setCursor(250, 87);
  tft.printf("Max Cell: %.3f V (#%d)", valHighestCellVolt / 1000.0f, valHighestCellNum);

  tft.setCursor(15, 107);
  tft.printf("Delta : %.3f V", (valHighestCellVolt - valLowestCellVolt) / 1000.0f);

  tft.setCursor(250, 107);
  tft.printf("Avg Cell: %.3f V", valAvgCellVolt / 1000.0f);

  tft.drawFastHLine(0, 145, DISPLAY_WIDTH, COLOR_DARKGREY);
}

void drawCells() {
  tft.setTextSize(1);

  for (int i = 0; i < 23; i++) {
    int col = i % 6;
    int row = i / 6;
    int x = 15 + col * 75;
    int y = 155 + row * 17;

    uint16_t color = COLOR_WHITE;
    if (i + 1 == valLowestCellNum) color = COLOR_RED;
    if (i + 1 == valHighestCellNum) color = COLOR_CYAN;

    tft.setTextColor(color, COLOR_BLACK);
    tft.setCursor(x, y);
    tft.printf("C%02d:%.2f", i + 1, valCells[i] / 1000.0f);
  }
}

void drawBMSFooter() {
  tft.setTextSize(1);
  tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  tft.setCursor(15, 300);
  tft.printf("Cycle: %d | Temp Max: %d C", valCycleCount, valMaxTemp);
}

void drawBMSPage() {
  tft.fillScreen(COLOR_BLACK);
  drawBMSHeader();
  drawBMSParameters();
  drawCells();
  drawBMSFooter();
}

void drawPark() {
  bool blink = (millis() / 250) % 2 == 0;
  if (blink) {
    centerText("P", CENTER_X, CENTER_Y - 28, 8, COLOR_RED);
  }
}

void drawMainPageWithValues(float speedValue, float rpmValue) {
  if (!mainPageInitialized) {
    tft.fillScreen(COLOR_BLACK);
    mainPageInitialized = true;
  }

  drawHeader();
  drawBattery();

  VehicleMode mode = atomicMode.load(std::memory_order_acquire);
  int roundedSpeed = round(speedValue);
  bool regenVisible = (atomicRegenActive.load(std::memory_order_acquire) ||
                       (mode == MODE_BRAKE && displayPower < -0.5f)) && mode != MODE_CHARGING;
  bool centerNeedsRedraw = !mainPageInitialized || mode != lastCenterMode || roundedSpeed != lastRenderedSpeed ||
                           (mode == MODE_PARK) != lastCenterParkState || regenVisible != lastRegenVisible;

  if (centerNeedsRedraw) {
    if (mode == MODE_PARK) {
      tft.fillRect(CENTER_X - 150, CENTER_Y - 120, 300, 220, COLOR_BLACK);
      drawPark();
    } else {
      tft.fillRect(CENTER_X - 150, CENTER_Y - 120, 300, 220, COLOR_BLACK);
      drawSpeedWithValue(speedValue);
    }

    lastCenterMode = mode;
    lastCenterParkState = (mode == MODE_PARK);
    lastRenderedSpeed = roundedSpeed;
    lastRegenVisible = regenVisible;
  }

  drawFooter();
}

void drawMainPage() {
  drawMainPageWithValues(displaySpeed, displayRPM);
}

void readValues() {
  displaySpeed = (float)atomicSpeed.load(std::memory_order_acquire);
  displayRPM = (float)atomicRPM.load(std::memory_order_acquire);
  displayVoltage = atomicVoltsRaw.load(std::memory_order_acquire) / 10.0f;
  displayCurrent = atomicAmpereRaw.load(std::memory_order_acquire) / 10.0f;
  displayPower = atomicPowerRaw.load(std::memory_order_acquire);

  if (valFullCapacity > 0.1f) {
    displaySOC = constrain((valRemainingCapacity / valFullCapacity) * 100.0f, 0.0f, 100.0f);
  } else {
    displaySOC = (float)valSOC;
  }

  uint32_t now = millis();
  if (lastTripUpdateMs == 0) {
    lastTripUpdateMs = now;
  }

  float odometerFromCAN = atomicOdometerKm.load(std::memory_order_acquire);
  if (odometerFromCAN > 0.0f) {
    displayOdometerKm = odometerFromCAN;
  }

  if (now - lastTripUpdateMs >= 1000UL) {
    float elapsedHours = (now - lastTripUpdateMs) / 1000.0f / 3600.0f;
    float distanceKm = displaySpeed * elapsedHours;
    if (odometerFromCAN <= 0.0f) {
      displayOdometerKm += distanceKm;
    }
    displayTripKm += distanceKm;
    lastTripUpdateMs = now;
  }

  VehicleMode currentMode = atomicMode.load(std::memory_order_acquire);
  if (currentMode == MODE_SPORT && displaySpeed <= 0.5f && displayRPM <= 100) {
    if (!tripResetArmed) {
      tripResetArmed = true;
      sportHoldStartMs = now;
    } else if (now - sportHoldStartMs >= 5000UL && !tripResetConsumed) {
      displayTripKm = 0.0f;
      preferences.putFloat("trip", displayTripKm);
      tripResetConsumed = true;
    }
  } else {
    tripResetArmed = false;
    tripResetConsumed = false;
    sportHoldStartMs = 0;
  }

  if (now - lastOdoSaveMs >= 5000UL) {
    preferences.putFloat("odo", displayOdometerKm);
    preferences.putFloat("trip", displayTripKm);
    lastOdoSaveMs = now;
  }
}
}  // namespace

void initTftSpeedometer() {
  SPI.begin(DISP_SCK_PIN, DISP_MISO_PIN, DISP_MOSI_PIN, DISP_CS_PIN);
  tft.init();
  tft.setRotation(1);

  pinMode(PAGE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIP_RESET_BUTTON_PIN, INPUT_PULLUP);

  displayOdometerKm = preferences.getFloat("odo", 0.0f);
  displayTripKm = preferences.getFloat("trip", 0.0f);
  lastTripUpdateMs = millis();
  lastOdoSaveMs = millis();

  startupState = STARTUP_LOADING;
  startupStartedAt = millis();
  startupPhaseStartedAt = 0;
  startupSpeed = 0.0f;
  startupRPM = 0.0f;
  mainPageInitialized = false;

  drawLoadingScreen(0);
}

void updateTftSpeedometer() {
  uint32_t now = millis();

  static DisplayPage lastRenderedPage = PAGE_MAIN;
  static bool bmsPageDirty = true;
  static bool bmsSnapshotInitialized = false;
  static float lastBmsVoltage = 0.0f;
  static float lastBmsCurrent = 0.0f;
  static float lastBmsPower = 0.0f;
  static float lastBmsSoc = 0.0f;
  static uint16_t lastBmsCells[23] = {0};
  static uint16_t lastBmsHighestCellVolt = 0;
  static uint8_t lastBmsHighestCellNum = 0;
  static uint16_t lastBmsLowestCellVolt = 0;
  static uint8_t lastBmsLowestCellNum = 0;
  static uint16_t lastBmsAvgCellVolt = 0;
  static uint8_t lastBmsMaxTemp = 0;
  static uint16_t lastBmsCycleCount = 0;
  static uint8_t lastBmsSOH = 0;

  bool pageButtonNow = (digitalRead(PAGE_BUTTON_PIN) == LOW);
  bool tripButtonNow = (digitalRead(TRIP_RESET_BUTTON_PIN) == LOW);

  // Page 2 is active only while the page GPIO is held LOW to GND.
  currentDisplayPage = pageButtonNow ? PAGE_BMS : PAGE_MAIN;

  if (currentDisplayPage != lastRenderedPage) {
    tft.fillScreen(COLOR_BLACK);
    lastRenderedPage = currentDisplayPage;

    if (currentDisplayPage == PAGE_MAIN) {
      mainPageInitialized = false;
      headerStateInitialized = false;
      lastReadyVisible = false;
      lastCenterMode = MODE_PARK;
      lastCenterParkState = false;
      lastRenderedSpeed = -1;
      lastPageMode = MODE_PARK;
    }

    bmsPageDirty = true;
    bmsSnapshotInitialized = false;
  }

  // Reset trip once per press, when the button is held LOW.
  if (tripButtonNow && !tripButtonLatched) {
    displayTripKm = 0.0f;
    preferences.putFloat("trip", displayTripKm);
    tripButtonLatched = true;
  }

  if (!tripButtonNow) {
    tripButtonLatched = false;
  }

  if (startupState == STARTUP_LOADING) {
    uint32_t elapsed = now - startupStartedAt;
    uint8_t progress = (elapsed * 100) / STARTUP_LOADING_MS;
    progress = constrain(progress, 0, 100);

    drawLoadingScreen(progress);

    if (elapsed >= STARTUP_LOADING_MS) {
      startupState = STARTUP_ANIMATING;
      startupPhaseStartedAt = now;
      startupSpeed = 0.0f;
      startupRPM = 0.0f;
    }

    return;
  }

  if (startupState == STARTUP_ANIMATING) {
    uint32_t elapsed = now - startupPhaseStartedAt;
    float phase = constrain((float)elapsed / (float)STARTUP_ANIMATION_MS, 0.0f, 1.0f);

    if (phase < 0.5f) {
      float ramp = phase / 0.5f;
      startupSpeed = 156.0f * ramp;
      startupRPM = RPM_MAX * ramp;
    } else {
      float ramp = 1.0f - ((phase - 0.5f) / 0.5f);
      startupSpeed = 156.0f * ramp;
      startupRPM = RPM_MAX * ramp;
    }

    if (phase >= 1.0f) {
      startupState = STARTUP_DONE;
      startupSpeed = 0.0f;
      startupRPM = 0.0f;
    }

    drawMainPageWithValues(startupSpeed, startupRPM);
    return;
  }

  if (now - lastFrameMs < 100) {
    return;
  }

  lastFrameMs = now;
  readValues();

  if (currentDisplayPage == PAGE_MAIN) {
    drawMainPage();
  } else {
    bool dataChanged = !bmsSnapshotInitialized ||
                       fabsf(displayVoltage - lastBmsVoltage) > 0.05f ||
                       fabsf(displayCurrent - lastBmsCurrent) > 0.05f ||
                       fabsf(displayPower - lastBmsPower) > 0.5f ||
                       fabsf(displaySOC - lastBmsSoc) > 0.5f ||
                       valSOH != lastBmsSOH ||
                       valHighestCellVolt != lastBmsHighestCellVolt ||
                       valHighestCellNum != lastBmsHighestCellNum ||
                       valLowestCellVolt != lastBmsLowestCellVolt ||
                       valLowestCellNum != lastBmsLowestCellNum ||
                       valAvgCellVolt != lastBmsAvgCellVolt ||
                       valMaxTemp != lastBmsMaxTemp ||
                       valCycleCount != lastBmsCycleCount ||
                       memcmp(valCells, lastBmsCells, sizeof(valCells)) != 0;

    if (dataChanged || bmsPageDirty) {
      drawBMSPage();
      bmsPageDirty = false;
      bmsSnapshotInitialized = true;

      lastBmsVoltage = displayVoltage;
      lastBmsCurrent = displayCurrent;
      lastBmsPower = displayPower;
      lastBmsSoc = displaySOC;
      lastBmsSOH = valSOH;
      lastBmsHighestCellVolt = valHighestCellVolt;
      lastBmsHighestCellNum = valHighestCellNum;
      lastBmsLowestCellVolt = valLowestCellVolt;
      lastBmsLowestCellNum = valLowestCellNum;
      lastBmsAvgCellVolt = valAvgCellVolt;
      lastBmsMaxTemp = valMaxTemp;
      lastBmsCycleCount = valCycleCount;
      memcpy(lastBmsCells, valCells, sizeof(valCells));
    }
  }
}
