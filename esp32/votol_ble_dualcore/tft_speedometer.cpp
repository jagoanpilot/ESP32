#include "tft_speedometer.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <cstring>
#include <atomic>

#include "wifi_handler.h"


// =====================================================
// EXTERNAL VEHICLE DATA
// =====================================================

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
extern uint8_t  valHighestCellNum;

extern uint16_t valLowestCellVolt;
extern uint8_t  valLowestCellNum;

extern uint16_t valAvgCellVolt;

extern uint8_t valMaxTemp;
extern uint8_t valMaxTempCell;

extern uint8_t valMinTemp;
extern uint8_t valMinTempCell;

extern uint16_t valCycleCount;

extern Preferences preferences;


// =====================================================
// TFT
// =====================================================

TFT_eSPI tft = TFT_eSPI();

// =====================================================
// CENTER SPRITE
// =====================================================
// Seluruh area lingkaran dirender di RAM terlebih dahulu,
// lalu dikirim sekaligus ke TFT. Ini mencegah flicker/tearing.
TFT_eSprite centerSprite = TFT_eSprite(&tft);
bool centerSpriteReady = false;


// =====================================================
// PRIVATE
// =====================================================

namespace {


// =====================================================
// PIN
// =====================================================

constexpr uint8_t DISP_CS_PIN   = 27;
constexpr uint8_t DISP_DC_PIN   = 26;
constexpr uint8_t DISP_RST_PIN  = 25;
constexpr uint8_t DISP_MOSI_PIN = 23;
constexpr uint8_t DISP_MISO_PIN = 19;
constexpr uint8_t DISP_SCK_PIN  = 18;

constexpr uint8_t PAGE_BUTTON_PIN       = 32;
constexpr uint8_t TRIP_RESET_BUTTON_PIN = 33;


// =====================================================
// DISPLAY
// =====================================================

constexpr int DISPLAY_WIDTH  = 480;
constexpr int DISPLAY_HEIGHT = 320;

constexpr int CENTER_X = 240;
constexpr int CENTER_Y = 145;


// =====================================================
// STANDARD RGB565 COLORS
// =====================================================

constexpr uint16_t COLOR_BLACK   = 0x0000;
constexpr uint16_t COLOR_WHITE   = 0xFFFF;

constexpr uint16_t COLOR_RED     = 0xF800;
constexpr uint16_t COLOR_GREEN   = 0x07E0;
constexpr uint16_t COLOR_BLUE    = 0x001F;
constexpr uint16_t COLOR_YELLOW  = 0xFFE0;
constexpr uint16_t COLOR_CYAN    = 0x07FF;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;
constexpr uint16_t COLOR_ORANGE  = 0xFC00;
constexpr uint16_t COLOR_GRAY    = 0x8410;


// =====================================================
// DISPLAY PALETTE
// =====================================================

constexpr uint16_t COLOR_BG      = COLOR_BLACK;
constexpr uint16_t COLOR_TEXT    = COLOR_WHITE;
constexpr uint16_t COLOR_MUTED   = COLOR_GRAY;

constexpr uint16_t COLOR_GOOD    = COLOR_GREEN;
constexpr uint16_t COLOR_ACCENT  = COLOR_CYAN;
constexpr uint16_t COLOR_REGEN   = COLOR_MAGENTA;

constexpr uint16_t COLOR_PANEL   = 0x2104;
constexpr uint16_t COLOR_LINE    = 0x4208;

constexpr uint16_t COLOR_RING_DARK = 0x4208;


// =====================================================
// TIMING
// =====================================================

constexpr uint32_t MAIN_UPDATE_MS = 40UL;
constexpr uint32_t BMS_UPDATE_MS  = 100UL;

constexpr uint32_t STARTUP_LOADING_MS   = 1500UL;
constexpr uint32_t STARTUP_ANIMATION_MS = 1800UL;


// =====================================================
// SPEED
// =====================================================

constexpr int MAX_SPEED = 130;
constexpr int RPM_MAX   = 1300;


// =====================================================
// STARTUP
// =====================================================

enum StartupState
{
    STARTUP_LOADING,
    STARTUP_ANIMATING,
    STARTUP_DONE
};

StartupState startupState = STARTUP_LOADING;

uint32_t startupStartedAt      = 0;
uint32_t startupPhaseStartedAt = 0;

float startupSpeed = 0.0f;
float startupRPM   = 0.0f;

uint8_t lastLoadingProgress = 255;
int lastStartupSpeed = -999;


// =====================================================
// DISPLAY VALUES
// =====================================================

float displaySpeed      = 0.0f;
float displayRPM        = 0.0f;

float displayVoltage    = 0.0f;
float displayCurrent    = 0.0f;
float displayPower      = 0.0f;

float displaySOC        = 0.0f;

float displayOdometerKm = 0.0f;
float displayTripKm     = 0.0f;

// =====================================================
// CENTER SMOOTH ANIMATION
// =====================================================
float centerAnimatedSpeed = 0.0f;
bool centerAnimationActive = false;


// =====================================================
// TIMERS
// =====================================================

uint32_t lastFrameMs      = 0;
uint32_t lastTripUpdateMs = 0;
uint32_t lastOdoSaveMs    = 0;
uint32_t lastBmsDrawMs    = 0;


// =====================================================
// BUTTON
// =====================================================

bool tripButtonLatched = false;

bool tripResetArmed    = false;
bool tripResetConsumed = false;

uint32_t sportHoldStartMs = 0;


// =====================================================
// PAGE
// =====================================================

enum DisplayPage
{
    PAGE_MAIN = 0,
    PAGE_BMS  = 1
};

DisplayPage currentDisplayPage = PAGE_MAIN;
DisplayPage lastRenderedPage   = PAGE_MAIN;

bool forceFullRefresh = true;


// =====================================================
// MAIN CACHE
// =====================================================

struct MainDisplayCache
{
    bool initialized = false;

    int speed       = -999;
    int rpm         = -999;

    int voltage10   = -999;
    int current10   = -999;

    int soc         = -999;
    int est         = -999;
    int efficiency  = -999;

    int trip10      = -999;
    int odo         = -999;

    int batTemp     = -999;
    int ctrlTemp    = -999;
    int motorTemp   = -999;

    int power10     = -99999;

    VehicleMode mode = MODE_PARK;

    bool charging        = false;
    bool chargerDetected = false;
    bool ready           = false;
    bool regen           = false;
};

MainDisplayCache mainCache;


// =====================================================
// BMS CACHE
// =====================================================

bool bmsSnapshotInitialized = false;

float lastBmsVoltage = 0.0f;
float lastBmsCurrent = 0.0f;
float lastBmsPower   = 0.0f;
float lastBmsSoc     = 0.0f;

uint16_t lastBmsCells[23] = {0};

uint16_t lastBmsHighestCellVolt = 0;
uint8_t  lastBmsHighestCellNum  = 0;

uint16_t lastBmsLowestCellVolt = 0;
uint8_t  lastBmsLowestCellNum  = 0;

uint16_t lastBmsAvgCellVolt = 0;

uint8_t  lastBmsMaxTemp    = 0;
uint16_t lastBmsCycleCount = 0;
uint8_t  lastBmsSOH        = 0;


// =====================================================
// FORWARD DECLARATIONS
// =====================================================

int calculateEstimatedRange();
int calculateEfficiency();

void drawMainStatic();

void drawCenterDynamic(
    int speed,
    VehicleMode mode,
    bool regen
);

void drawBatteryDynamic(
    int soc,
    bool charging
);

void drawModeDynamic(
    VehicleMode mode
);

void drawPackValue();
void drawCurrentValue();
void drawEfficiencyValue();

void drawRangeValue();
void drawRPMValue();
void drawTripValue();

void drawBatteryTemperature();
void drawControllerTemperature();
void drawMotorTemperature();
void drawOdometerValue();

void drawFooterDynamic();

void drawBMSPage();


// =====================================================
// CENTER TEXT
// =====================================================

void centerText(
    const char* text,
    int x,
    int y,
    int textSize,
    uint16_t color,
    uint16_t bg = COLOR_BG
)
{
    tft.setTextSize(textSize);
    tft.setTextColor(color, bg);

    int width = tft.textWidth(text);

    tft.setCursor(
        x - width / 2,
        y
    );

    tft.print(text);
}


// =====================================================
// CLEAR SCREEN
// =====================================================

void clearScreen()
{
    tft.fillScreen(COLOR_BG);
}


// =====================================================
// RESET PAGE CACHE
// =====================================================

void resetDisplayCaches()
{
    mainCache = MainDisplayCache();

    centerAnimatedSpeed = 0.0f;
    centerAnimationActive = false;

    bmsSnapshotInitialized = false;

    lastBmsVoltage = 0.0f;
    lastBmsCurrent = 0.0f;
    lastBmsPower = 0.0f;
    lastBmsSoc = 0.0f;

    memset(
        lastBmsCells,
        0,
        sizeof(lastBmsCells)
    );

    lastBmsHighestCellVolt = 0;
    lastBmsHighestCellNum  = 0;

    lastBmsLowestCellVolt = 0;
    lastBmsLowestCellNum  = 0;

    lastBmsAvgCellVolt = 0;

    lastBmsMaxTemp = 0;
    lastBmsCycleCount = 0;
    lastBmsSOH = 0;
}


// =====================================================
// FULL PAGE REFRESH
// =====================================================

void refreshPage()
{
    // =================================================
    // 1. CLEAR 100% SCREEN
    // =================================================

    tft.fillScreen(COLOR_BG);


    // =================================================
    // 2. RESET CACHE
    // =================================================

    resetDisplayCaches();


    // =================================================
    // 3. FORCE NEXT DRAW
    // =================================================

    forceFullRefresh = true;
}


// =====================================================
// LOADING SCREEN
// =====================================================

void drawLoadingScreen(uint8_t progress)
{
    clearScreen();

    centerText(
        "FOX R",
        CENTER_X,
        70,
        5,
        COLOR_ACCENT
    );

    centerText(
        "MODERN EV DASHBOARD",
        CENTER_X,
        135,
        2,
        COLOR_TEXT
    );

    const int barX = 25;
    const int barY = 200;
    const int barW = 430;
    const int barH = 20;

    tft.drawRoundRect(
        barX,
        barY,
        barW,
        barH,
        5,
        COLOR_LINE
    );

    int fillWidth =
        map(
            constrain(progress, 0, 100),
            0,
            100,
            0,
            barW - 4
        );

    if (fillWidth > 0)
    {
        tft.fillRoundRect(
            barX + 2,
            barY + 2,
            fillWidth,
            barH - 4,
            3,
            COLOR_ACCENT
        );
    }

    char buffer[8];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d%%",
        progress
    );

    centerText(
        buffer,
        CENTER_X,
        235,
        2,
        COLOR_TEXT
    );
}


// =====================================================
// MODE NAME
// =====================================================

const char* getModeName(VehicleMode mode)
{
    switch (mode)
    {
        case MODE_PARK:
            return "P";

        case MODE_DRIVE:
            return "D";

        case MODE_SPORT:
            return "S";

        case MODE_REVERSE:
            return "R";

        case MODE_BRAKE:
            return "B";

        case MODE_CHARGING:
            return "C";

        case MODE_STAND:
            return "N";

        default:
            return "?";
    }
}


// =====================================================
// MODE COLOR
// =====================================================

uint16_t getModeColor(VehicleMode mode)
{
    switch (mode)
    {
        case MODE_PARK:
            return COLOR_RED;

        case MODE_DRIVE:
            return COLOR_GOOD;

        case MODE_SPORT:
            return COLOR_ORANGE;

        case MODE_REVERSE:
            return COLOR_ACCENT;

        case MODE_BRAKE:
            return COLOR_REGEN;

        case MODE_CHARGING:
            return COLOR_ACCENT;

        case MODE_STAND:
            return COLOR_MUTED;

        default:
            return COLOR_TEXT;
    }
}


// =====================================================
// BATTERY HEADER
// =====================================================

void drawBatteryDynamic(
    int soc,
    bool charging
)
{
    tft.fillRect(
        8,
        6,
        120,
        30,
        COLOR_BG
    );

    soc = constrain(
        soc,
        0,
        100
    );

    const int x = 13;
    const int y = 15;

    uint16_t border =
        charging
            ? COLOR_ACCENT
            : COLOR_MUTED;


    // Battery body
    tft.drawRoundRect(
        x,
        y,
        44,
        16,
        4,
        border
    );


    // Terminal
    tft.fillRect(
        x + 45,
        y + 5,
        4,
        6,
        border
    );


    int fillWidth =
        map(
            soc,
            0,
            100,
            0,
            36
        );


    uint16_t fillColor;

    if (soc < 20)
        fillColor = COLOR_RED;
    else if (soc < 50)
        fillColor = COLOR_YELLOW;
    else
        fillColor = COLOR_GOOD;


    // Interior
    tft.fillRect(
        x + 2,
        y + 2,
        40,
        12,
        COLOR_BG
    );


    if (fillWidth > 0)
    {
        tft.fillRoundRect(
            x + 3,
            y + 3,
            fillWidth,
            10,
            2,
            fillColor
        );
    }


    // SOC
    char buffer[12];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d%%",
        soc
    );

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_TEXT,
        COLOR_BG
    );

    tft.setCursor(
        63,
        18
    );

    tft.print(buffer);


    // CHG
    if (charging)
    {
        tft.setTextColor(
            COLOR_ACCENT,
            COLOR_BG
        );

        tft.setCursor(
            93,
            18
        );

        tft.print("C");
    }
}


// =====================================================
// STATIC HEADER
// =====================================================

void drawStaticHeader()
{
    tft.fillRect(
        0,
        0,
        DISPLAY_WIDTH,
        45,
        COLOR_BG
    );

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_MUTED,
        COLOR_BG
    );

    const char* title =
        "FOX R | EV DASHBOARD";

    int width =
        tft.textWidth(title);

    tft.setCursor(
        CENTER_X - width / 2,
        19
    );

    tft.print(title);


    tft.drawFastHLine(
        12,
        43,
        DISPLAY_WIDTH - 24,
        COLOR_LINE
    );
}


// =====================================================
// MODE
// =====================================================

void drawModeDynamic(VehicleMode mode)
{
    tft.fillRect(
        425,
        7,
        48,
        34,
        COLOR_BG
    );

    const char* text =
        getModeName(mode);

    uint16_t color =
        getModeColor(mode);

    tft.setTextSize(3);

    tft.setTextColor(
        color,
        COLOR_BG
    );

    int width =
        tft.textWidth(text);

    tft.setCursor(
        470 - width,
        10
    );

    tft.print(text);
}


// =====================================================
// METRIC CARD
// =====================================================

void drawMetricCard(
    int x,
    int y,
    int w,
    int h,
    const char* label,
    bool rightLabel = false
)
{
    tft.fillRoundRect(
        x,
        y,
        w,
        h,
        6,
        COLOR_PANEL
    );

    tft.drawRoundRect(
        x,
        y,
        w,
        h,
        6,
        COLOR_LINE
    );

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_MUTED,
        COLOR_PANEL
    );

    int tw =
        tft.textWidth(label);

    if (rightLabel)
    {
        tft.setCursor(
            x + w - tw - 7,
            y + 6
        );
    }
    else
    {
        tft.setCursor(
            x + 7,
            y + 6
        );
    }

    tft.print(label);
}


// =====================================================
// STATIC SIDE METRICS
// =====================================================
//
// Semua card kiri dan kanan:
// X sama
// WIDTH sama
// HEIGHT sama
// Y sama
//
// =====================================================

void drawStaticSideMetrics()
{
    constexpr int LEFT_X  = 10;
    constexpr int RIGHT_X = 345;

    constexpr int CARD_W = 125;
    constexpr int CARD_H = 44;

    constexpr int Y1 = 53;
    constexpr int Y2 = 103;
    constexpr int Y3 = 153;


    // =================================================
    // LEFT
    // =================================================

    drawMetricCard(
        LEFT_X,
        Y1,
        CARD_W,
        CARD_H,
        "PACK"
    );

    drawMetricCard(
        LEFT_X,
        Y2,
        CARD_W,
        CARD_H,
        "CURRENT"
    );

    drawMetricCard(
        LEFT_X,
        Y3,
        CARD_W,
        CARD_H,
        "EFFICIENCY"
    );


    // =================================================
    // RIGHT
    // =================================================

    drawMetricCard(
        RIGHT_X,
        Y1,
        CARD_W,
        CARD_H,
        "RANGE",
        true
    );

    drawMetricCard(
        RIGHT_X,
        Y2,
        CARD_W,
        CARD_H,
        "RPM",
        true
    );

    drawMetricCard(
        RIGHT_X,
        Y3,
        CARD_W,
        CARD_H,
        "TRIP",
        true
    );
}


// =====================================================
// METRIC VALUE
// =====================================================

void drawMetricValue(
    int x,
    int y,
    int width,
    const char* text,
    uint16_t color,
    bool rightAlign = false
)
{
    tft.fillRect(
        x + 3,
        y,
        width - 6,
        22,
        COLOR_PANEL
    );

    tft.setTextSize(2);

    tft.setTextColor(
        color,
        COLOR_PANEL
    );

    int tw =
        tft.textWidth(text);

    if (rightAlign)
    {
        tft.setCursor(
            x + width - tw - 7,
            y + 2
        );
    }
    else
    {
        tft.setCursor(
            x + 7,
            y + 2
        );
    }

    tft.print(text);
}


// =====================================================
// PACK
// =====================================================

void drawPackValue()
{
    char buffer[24];

    snprintf(
        buffer,
        sizeof(buffer),
        "%.1f V",
        displayVoltage
    );

    drawMetricValue(
        10,
        73,
        125,
        buffer,
        COLOR_TEXT
    );
}


// =====================================================
// CURRENT
// =====================================================

void drawCurrentValue()
{
    char buffer[24];

    if (displayCurrent >= 0.0f)
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "+%.1f A",
            displayCurrent
        );
    }
    else
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "%.1f A",
            displayCurrent
        );
    }

    drawMetricValue(
        10,
        123,
        125,
        buffer,
        displayCurrent < 0.0f
            ? COLOR_REGEN
            : COLOR_ACCENT
    );
}


// =====================================================
// EFFICIENCY
// =====================================================

void drawEfficiencyValue()
{
    char buffer[24];

    int efficiency =
        calculateEfficiency();

    if (efficiency > 0)
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "%d Wh/km",
            efficiency
        );
    }
    else
    {
        snprintf(
            buffer,
            sizeof(buffer),
            "-- Wh/km"
        );
    }

    drawMetricValue(
        10,
        173,
        125,
        buffer,
        COLOR_GOOD
    );
}


// =====================================================
// RANGE
// =====================================================

void drawRangeValue()
{
    char buffer[24];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d km",
        calculateEstimatedRange()
    );

    drawMetricValue(
        345,
        73,
        125,
        buffer,
        COLOR_GOOD,
        true
    );
}


// =====================================================
// RPM
// =====================================================

void drawRPMValue()
{
    char buffer[24];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d",
        (int)round(displayRPM)
    );

    drawMetricValue(
        345,
        123,
        125,
        buffer,
        COLOR_ORANGE,
        true
    );
}


// =====================================================
// TRIP
// =====================================================

void drawTripValue()
{
    char buffer[24];

    snprintf(
        buffer,
        sizeof(buffer),
        "%.1f km",
        displayTripKm
    );

    drawMetricValue(
        345,
        173,
        125,
        buffer,
        COLOR_ACCENT,
        true
    );
}


// =====================================================
// RING BASE
// =====================================================

void drawRingBase()
{
    const int outer = 88;
    const int inner = 73;

    tft.drawCircle(
        CENTER_X,
        CENTER_Y,
        outer + 2,
        COLOR_LINE
    );

    tft.drawCircle(
        CENTER_X,
        CENTER_Y,
        outer,
        COLOR_RING_DARK
    );

    for (
        int angle = -135;
        angle <= 135;
        angle += 2
    )
    {
        float rad =
            (angle - 90) * DEG_TO_RAD;

        int x1 =
            CENTER_X +
            cos(rad) * inner;

        int y1 =
            CENTER_Y +
            sin(rad) * inner;

        int x2 =
            CENTER_X +
            cos(rad) * outer;

        int y2 =
            CENTER_Y +
            sin(rad) * outer;

        tft.drawLine(
            x1,
            y1,
            x2,
            y2,
            COLOR_RING_DARK
        );
    }
}


// =====================================================
// ACTIVE RING
// =====================================================

void drawActiveSpeedRing(
    int speed,
    bool regen
)
{
    float percent =
        constrain(
            speed / (float)MAX_SPEED,
            0.0f,
            1.0f
        );

    float activeAngle =
        -135.0f +
        percent * 270.0f;

    const int outer = 88;
    const int inner = 73;

    for (
        float angle = -135.0f;
        angle <= activeAngle;
        angle += 2.0f
    )
    {
        float rad =
            (angle - 90) * DEG_TO_RAD;

        int x1 =
            CENTER_X +
            cos(rad) * inner;

        int y1 =
            CENTER_Y +
            sin(rad) * inner;

        int x2 =
            CENTER_X +
            cos(rad) * outer;

        int y2 =
            CENTER_Y +
            sin(rad) * outer;

        float ratio =
            (angle + 135.0f) / 270.0f;

        uint16_t color;

        if (regen)
        {
            color = COLOR_REGEN;
        }
        else if (ratio < 0.55f)
        {
            color = COLOR_ACCENT;
        }
        else
        {
            color = COLOR_GOOD;
        }

        tft.drawLine(
            x1,
            y1,
            x2,
            y2,
            color
        );
    }
}


// =====================================================
// CENTER
// =====================================================

void drawCenterSpriteText(
    const char* text,
    int x,
    int y,
    int textSize,
    uint16_t color,
    uint16_t bg = COLOR_BG
)
{
    centerSprite.setTextSize(textSize);
    centerSprite.setTextColor(color, bg);

    int width =
        centerSprite.textWidth(text);

    centerSprite.setCursor(
        x - width / 2,
        y
    );

    centerSprite.print(text);
}


// =====================================================
// CENTER SPRITE RING BASE
// =====================================================

void drawCenterSpriteRingBase()
{
    const int cx = 96;
    const int cy = 96;
    const int outer = 88;
    const int inner = 73;

    centerSprite.drawCircle(
        cx,
        cy,
        outer + 2,
        COLOR_LINE
    );

    centerSprite.drawCircle(
        cx,
        cy,
        outer,
        COLOR_RING_DARK
    );

    for (
        int angle = -135;
        angle <= 135;
        angle += 2
    )
    {
        float rad =
            (angle - 90) * DEG_TO_RAD;

        int x1 =
            cx +
            cos(rad) * inner;

        int y1 =
            cy +
            sin(rad) * inner;

        int x2 =
            cx +
            cos(rad) * outer;

        int y2 =
            cy +
            sin(rad) * outer;

        centerSprite.drawLine(
            x1,
            y1,
            x2,
            y2,
            COLOR_RING_DARK
        );
    }
}


// =====================================================
// CENTER SPRITE ACTIVE RING
// =====================================================

void drawCenterSpriteActiveRing(
    float speed,
    bool regen
)
{
    float percent =
        constrain(
            speed / (float)MAX_SPEED,
            0.0f,
            1.0f
        );

    float activeAngle =
        -135.0f +
        percent * 270.0f;

    const int cx = 96;
    const int cy = 96;
    const int outer = 88;
    const int inner = 73;

    for (
        float angle = -135.0f;
        angle <= activeAngle;
        angle += 2.0f
    )
    {
        float rad =
            (angle - 90) * DEG_TO_RAD;

        int x1 =
            cx +
            cos(rad) * inner;

        int y1 =
            cy +
            sin(rad) * inner;

        int x2 =
            cx +
            cos(rad) * outer;

        int y2 =
            cy +
            sin(rad) * outer;

        float ratio =
            (angle + 135.0f) / 270.0f;

        uint16_t color;

        if (regen)
        {
            color = COLOR_REGEN;
        }
        else if (ratio < 0.55f)
        {
            color = COLOR_ACCENT;
        }
        else
        {
            color = COLOR_GOOD;
        }

        centerSprite.drawLine(
            x1,
            y1,
            x2,
            y2,
            color
        );
    }
}


// =====================================================
// CENTER
// =====================================================
//
// Semua isi circle dibuat di sprite 192x192 lalu
// pushSprite() sekali. Tidak ada lagi render langsung
// ke TFT yang menyebabkan P berkedip.
//
// Speed juga dianimasikan bertahap menuju nilai target.
// =====================================================

void drawCenterDynamic(
    int speed,
    VehicleMode mode,
    bool regen
)
{
    if (!centerSpriteReady)
        return;

    float targetSpeed =
        constrain(
            (float)speed,
            0.0f,
            (float)MAX_SPEED
        );

    if (
        mode == MODE_PARK ||
        mode == MODE_CHARGING
    )
    {
        centerAnimatedSpeed = targetSpeed;
        centerAnimationActive = false;
    }
    else
    {
        float delta =
            targetSpeed -
            centerAnimatedSpeed;

        float distance =
            fabsf(delta);

        if (distance <= 0.25f)
        {
            centerAnimatedSpeed = targetSpeed;
            centerAnimationActive = false;
        }
        else
        {
            float step =
                constrain(
                    distance * 0.22f,
                    0.8f,
                    8.0f
                );

            if (delta > 0.0f)
                centerAnimatedSpeed += step;
            else
                centerAnimatedSpeed -= step;

            if (
                fabsf(
                    targetSpeed -
                    centerAnimatedSpeed
                ) <= 0.8f
            )
            {
                centerAnimatedSpeed =
                    targetSpeed;

                centerAnimationActive =
                    false;
            }
            else
            {
                centerAnimationActive =
                    true;
            }
        }
    }

    centerSprite.fillSprite(COLOR_BG);

    drawCenterSpriteRingBase();

    drawCenterSpriteActiveRing(
        centerAnimatedSpeed,
        regen
    );

    centerSprite.fillCircle(
        96,
        96,
        70,
        COLOR_BG
    );

    centerSprite.drawCircle(
        96,
        96,
        70,
        COLOR_LINE
    );

    // =================================================
    // PARK
    // =================================================
    // P selalu digambar dan tidak pernah blink.
    // =================================================

    if (mode == MODE_PARK)
    {
        drawCenterSpriteText(
            "P",
            96,
            61,
            6,
            COLOR_RED
        );

        drawCenterSpriteText(
            "PARK",
            96,
            128,
            1,
            COLOR_MUTED
        );
    }

    // =================================================
    // CHARGING
    // =================================================
    // Center:
    // C
    // Charging
    // SOC
    // =================================================

    else if (mode == MODE_CHARGING)
    {
        drawCenterSpriteText(
            "C",
            96,
            54,
            6,
            COLOR_ACCENT
        );

        drawCenterSpriteText(
            "Charging",
            96,
            118,
            2,
            COLOR_ACCENT
        );

        char socBuffer[12];

        snprintf(
            socBuffer,
            sizeof(socBuffer),
            "%d%%",
            (int)round(displaySOC)
        );

        drawCenterSpriteText(
            socBuffer,
            96,
            142,
            1,
            COLOR_GOOD
        );
    }

    // =================================================
    // NORMAL SPEED
    // =================================================

    else
    {
        char speedBuffer[8];

        snprintf(
            speedBuffer,
            sizeof(speedBuffer),
            "%d",
            (int)round(centerAnimatedSpeed)
        );

        drawCenterSpriteText(
            speedBuffer,
            96,
            62,
            5,
            COLOR_TEXT
        );

        drawCenterSpriteText(
            "km/h",
            96,
            112,
            1,
            COLOR_MUTED
        );

        char powerBuffer[24];

        float powerKW =
            fabs(displayPower) / 1000.0f;

        if (
            regen ||
            displayPower < -5.0f
        )
        {
            snprintf(
                powerBuffer,
                sizeof(powerBuffer),
                "%.2f kW REGEN",
                powerKW
            );

            drawCenterSpriteText(
                powerBuffer,
                96,
                128,
                1,
                COLOR_REGEN
            );
        }
        else
        {
            snprintf(
                powerBuffer,
                sizeof(powerBuffer),
                "%.2f kW DRIVE",
                powerKW
            );

            drawCenterSpriteText(
                powerBuffer,
                96,
                128,
                1,
                COLOR_ACCENT
            );
        }
    }

    // =================================================
    // PUSH SEKALIGUS
    // =================================================

    centerSprite.pushSprite(
        CENTER_X - 96,
        CENTER_Y - 96
    );
}


// =====================================================
// BOTTOM TILE
// =====================================================

void drawBottomTile(
    int x,
    int width,
    const char* label
)
{
    // =================================================
    // TURUN 25 PX
    // Sebelumnya Y = 235
    // Sekarang Y = 260
    // =================================================

    constexpr int y = 260;
    constexpr int h = 45;

    tft.fillRoundRect(
        x,
        y,
        width,
        h,
        7,
        COLOR_PANEL
    );

    tft.drawRoundRect(
        x,
        y,
        width,
        h,
        7,
        COLOR_LINE
    );

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_MUTED,
        COLOR_PANEL
    );

    tft.setCursor(
        x + 8,
        y + 7
    );

    tft.print(label);
}


// =====================================================
// STATIC BOTTOM
// =====================================================

void drawStaticBottom()
{
    drawBottomTile(
        12,
        108,
        "BAT TEMP"
    );

    drawBottomTile(
        126,
        108,
        "CTRL TEMP"
    );

    drawBottomTile(
        240,
        108,
        "MOTOR TEMP"
    );

    drawBottomTile(
        354,
        114,
        "ODO"
    );
}


// =====================================================
// BOTTOM VALUE
// =====================================================

void drawBottomValue(
    int x,
    int width,
    const char* value,
    uint16_t color
)
{
    constexpr int y = 260;

    tft.fillRect(
        x + 4,
        y + 22,
        width - 8,
        18,
        COLOR_PANEL
    );

    tft.setTextSize(1);

    tft.setTextColor(
        color,
        COLOR_PANEL
    );

    int textWidth =
        tft.textWidth(value);

    tft.setCursor(
        x + (width - textWidth) / 2,
        y + 27
    );

    tft.print(value);
}


// =====================================================
// BATTERY TEMP
// =====================================================

void drawBatteryTemperature()
{
    char buffer[16];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d C",
        valMaxTemp
    );

    drawBottomValue(
        12,
        108,
        buffer,
        COLOR_GOOD
    );
}


// =====================================================
// CONTROLLER TEMP
// =====================================================

void drawControllerTemperature()
{
    char buffer[16];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d C",
        valCtrlTemp
    );

    drawBottomValue(
        126,
        108,
        buffer,
        COLOR_ORANGE
    );
}


// =====================================================
// MOTOR TEMP
// =====================================================

void drawMotorTemperature()
{
    char buffer[16];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d C",
        valMotorTemp
    );

    drawBottomValue(
        240,
        108,
        buffer,
        COLOR_REGEN
    );
}


// =====================================================
// ODOMETER
// =====================================================

void drawOdometerValue()
{
    char buffer[20];

    snprintf(
        buffer,
        sizeof(buffer),
        "%d km",
        (int)round(displayOdometerKm)
    );

    drawBottomValue(
        354,
        114,
        buffer,
        COLOR_ACCENT
    );
}


// =====================================================
// FOOTER STATIC
// =====================================================
//
// Bottom card sekarang sampai sekitar Y=305.
// Footer dipindahkan ke area 307+ agar tidak overlap.
//
// =====================================================

void drawStaticFooter()
{
    tft.drawFastHLine(
        12,
        307,
        DISPLAY_WIDTH - 24,
        COLOR_LINE
    );
}


// =====================================================
// FOOTER DYNAMIC
// =====================================================

void drawFooterDynamic()
{
    tft.fillRect(
        12,
        309,
        456,
        10,
        COLOR_BG
    );

    VehicleMode mode =
        atomicMode.load(
            std::memory_order_acquire
        );

    if (
        mode == MODE_CHARGING &&
        oriChargerDetected
    )
    {
        const char* text =
            "ORIGINAL CHARGER";

        tft.setTextSize(1);

        tft.setTextColor(
            COLOR_GOOD,
            COLOR_BG
        );

        int width =
            tft.textWidth(text);

        tft.setCursor(
            CENTER_X - width / 2,
            310
        );

        tft.print(text);
    }
}


// =====================================================
// MAIN STATIC
// =====================================================

void drawMainStatic()
{
    // =================================================
    // FULL CLEAN
    // =================================================

    tft.fillScreen(COLOR_BG);


    // =================================================
    // HEADER
    // =================================================

    drawStaticHeader();


    // =================================================
    // LEFT / RIGHT
    // =================================================

    drawStaticSideMetrics();


    // =================================================
    // BOTTOM
    // =================================================

    drawStaticBottom();


    // =================================================
    // FOOTER
    // =================================================

    drawStaticFooter();
}


// =====================================================
// MAIN INITIALIZATION
// =====================================================

void initializeMainPage()
{
    drawMainStatic();

    mainCache.initialized = false;
}


// =====================================================
// RANGE
// =====================================================

int calculateEstimatedRange()
{
    float est =
        displaySOC * 1.3f;

    return constrain(
        (int)round(est),
        0,
        130
    );
}


// =====================================================
// EFFICIENCY
// =====================================================

int calculateEfficiency()
{
    if (displaySpeed <= 5.0f)
        return 0;

    if (displayPower <= 0.0f)
        return 0;

    float efficiency =
        displayPower /
        displaySpeed;

    return constrain(
        (int)round(efficiency),
        0,
        999
    );
}


// =====================================================
// MAIN DYNAMIC
// =====================================================

void updateMainDynamic()
{
    VehicleMode mode =
        atomicMode.load(
            std::memory_order_acquire
        );

    bool charging =
        mode == MODE_CHARGING;

    bool regen =
        atomicRegenActive.load(
            std::memory_order_acquire
        );

    if (displayPower < -5.0f)
        regen = true;


    int speed =
        constrain(
            (int)round(displaySpeed),
            0,
            999
        );

    int rpm =
        (int)round(displayRPM);

    int voltage10 =
        (int)round(
            displayVoltage * 10.0f
        );

    int current10 =
        (int)round(
            displayCurrent * 10.0f
        );

    int soc =
        constrain(
            (int)round(displaySOC),
            0,
            100
        );

    int est =
        calculateEstimatedRange();

    int efficiency =
        calculateEfficiency();

    int trip10 =
        (int)round(
            displayTripKm * 10.0f
        );

    int odo =
        (int)round(
            displayOdometerKm
        );

    int batTemp =
        valMaxTemp;

    int ctrlTemp =
        valCtrlTemp;

    int motorTemp =
        valMotorTemp;

    int power10 =
        (int)round(
            displayPower / 10.0f
        );

    bool ready =
        mode == MODE_DRIVE &&
        displaySpeed <= 0.5f &&
        displayRPM <= 100;

    // =================================================
    // FIRST DRAW
    // =================================================

    if (!mainCache.initialized)
    {
        // Full static layout
        drawMainStatic();

        // Header
        drawBatteryDynamic(
            soc,
            charging
        );

        drawModeDynamic(
            mode
        );


        // Center
        drawCenterDynamic(
            speed,
            mode,
            regen
        );


        // Left
        drawPackValue();
        drawCurrentValue();
        drawEfficiencyValue();


        // Right
        drawRangeValue();
        drawRPMValue();
        drawTripValue();


        // Bottom
        drawBatteryTemperature();
        drawControllerTemperature();
        drawMotorTemperature();
        drawOdometerValue();


        // Footer
        drawFooterDynamic();


        // Cache
        mainCache.speed       = speed;
        mainCache.rpm         = rpm;

        mainCache.voltage10   = voltage10;
        mainCache.current10   = current10;

        mainCache.soc         = soc;
        mainCache.est         = est;
        mainCache.efficiency  = efficiency;

        mainCache.trip10      = trip10;
        mainCache.odo         = odo;

        mainCache.batTemp     = batTemp;
        mainCache.ctrlTemp    = ctrlTemp;
        mainCache.motorTemp   = motorTemp;

        mainCache.power10     = power10;

        mainCache.mode        = mode;
        mainCache.charging    = charging;

        mainCache.chargerDetected =
            oriChargerDetected;

        mainCache.ready      = ready;
        mainCache.regen      = regen;

        mainCache.initialized = true;

        return;
    }


    // =================================================
    // SAVE OLD STATE FOR CHANGE DETECTION
    // =================================================

    bool modeChanged =
        mode != mainCache.mode;

    bool chargingChanged =
        charging != mainCache.charging;

    bool regenChanged =
        regen != mainCache.regen;

    bool powerChanged =
        power10 != mainCache.power10;


    // =================================================
    // BATTERY
    // =================================================

    if (
        soc != mainCache.soc ||
        chargingChanged
    )
    {
        drawBatteryDynamic(
            soc,
            charging
        );

        mainCache.soc =
            soc;

        mainCache.charging =
            charging;
    }


    // =================================================
    // MODE
    // =================================================

    if (modeChanged)
    {
        drawModeDynamic(
            mode
        );

        mainCache.mode =
            mode;
    }


    // =================================================
    // CENTER
    // =================================================
    //
    // FIX:
    // Jangan membandingkan mode setelah cache
    // sudah diubah.
    //
    // =================================================

    bool centerChanged =
        speed != mainCache.speed ||
        modeChanged ||
        chargingChanged ||
        regenChanged ||
        powerChanged ||
        centerAnimationActive;

    if (centerChanged)
    {
        drawCenterDynamic(
            speed,
            mode,
            regen
        );

        mainCache.speed =
            speed;

        mainCache.power10 =
            power10;

        mainCache.regen =
            regen;
    }


    // =================================================
    // RPM CACHE
    // =================================================

    if (rpm != mainCache.rpm)
    {
        drawRPMValue();

        mainCache.rpm =
            rpm;
    }


    // =================================================
    // PACK
    // =================================================

    if (
        voltage10 != mainCache.voltage10
    )
    {
        drawPackValue();

        mainCache.voltage10 =
            voltage10;
    }


    // =================================================
    // CURRENT
    // =================================================

    if (
        current10 != mainCache.current10
    )
    {
        drawCurrentValue();

        mainCache.current10 =
            current10;
    }


    // =================================================
    // EFFICIENCY
    // =================================================

    if (
        efficiency != mainCache.efficiency
    )
    {
        drawEfficiencyValue();

        mainCache.efficiency =
            efficiency;
    }


    // =================================================
    // RANGE
    // =================================================

    if (
        est != mainCache.est
    )
    {
        drawRangeValue();

        mainCache.est =
            est;
    }


    // =================================================
    // TRIP
    // =================================================

    if (
        trip10 != mainCache.trip10
    )
    {
        drawTripValue();

        mainCache.trip10 =
            trip10;
    }


    // =================================================
    // BATTERY TEMP
    // =================================================

    if (
        batTemp != mainCache.batTemp
    )
    {
        drawBatteryTemperature();

        mainCache.batTemp =
            batTemp;
    }


    // =================================================
    // CONTROLLER TEMP
    // =================================================

    if (
        ctrlTemp != mainCache.ctrlTemp
    )
    {
        drawControllerTemperature();

        mainCache.ctrlTemp =
            ctrlTemp;
    }


    // =================================================
    // MOTOR TEMP
    // =================================================

    if (
        motorTemp != mainCache.motorTemp
    )
    {
        drawMotorTemperature();

        mainCache.motorTemp =
            motorTemp;
    }


    // =================================================
    // ODO
    // =================================================

    if (
        odo != mainCache.odo
    )
    {
        drawOdometerValue();

        mainCache.odo =
            odo;
    }


    // =================================================
    // FOOTER
    // =================================================

    if (
        oriChargerDetected !=
        mainCache.chargerDetected ||
        chargingChanged
    )
    {
        drawFooterDynamic();

        mainCache.chargerDetected =
            oriChargerDetected;
    }


    // =================================================
    // READY CACHE
    // =================================================

    mainCache.ready =
        ready;
}


// =====================================================
// BMS HEADER
// =====================================================

void drawBMSHeader()
{
    tft.setTextColor(
        COLOR_ACCENT,
        COLOR_BG
    );

    tft.setTextSize(2);

    tft.setCursor(
        15,
        10
    );

    tft.print(
        "BMS MONITORING & CELLS"
    );

    tft.drawFastHLine(
        0,
        34,
        DISPLAY_WIDTH,
        COLOR_LINE
    );
}


// =====================================================
// BMS PARAMETERS
// =====================================================

void drawBMSParameters()
{
    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_TEXT,
        COLOR_BG
    );

    tft.setCursor(
        15,
        47
    );

    tft.printf(
        "Volt : %.1f V",
        displayVoltage
    );

    tft.setCursor(
        250,
        47
    );

    tft.printf(
        "Curr : %.1f A",
        displayCurrent
    );

    tft.setCursor(
        15,
        67
    );

    tft.printf(
        "Power: %.0f W",
        displayPower
    );

    tft.setCursor(
        250,
        67
    );

    tft.printf(
        "SOH : %d %%",
        valSOH > 0
            ? valSOH
            : (int)displaySOC
    );

    tft.setCursor(
        15,
        87
    );

    tft.printf(
        "Min Cell: %.3f V (#%d)",
        valLowestCellVolt / 1000.0f,
        valLowestCellNum
    );

    tft.setCursor(
        250,
        87
    );

    tft.printf(
        "Max Cell: %.3f V (#%d)",
        valHighestCellVolt / 1000.0f,
        valHighestCellNum
    );

    tft.setCursor(
        15,
        107
    );

    tft.printf(
        "Delta : %.3f V",
        (
            valHighestCellVolt -
            valLowestCellVolt
        ) / 1000.0f
    );

    tft.setCursor(
        250,
        107
    );

    tft.printf(
        "Avg Cell: %.3f V",
        valAvgCellVolt / 1000.0f
    );

    tft.drawFastHLine(
        0,
        145,
        DISPLAY_WIDTH,
        COLOR_LINE
    );
}


// =====================================================
// BMS CELLS
// =====================================================

void drawCells()
{
    tft.setTextSize(1);

    for (int i = 0; i < 23; i++)
    {
        int col =
            i % 6;

        int row =
            i / 6;

        int x =
            15 +
            col * 75;

        int y =
            155 +
            row * 17;


        uint16_t color =
            COLOR_TEXT;


        if (
            i + 1 ==
            valLowestCellNum
        )
        {
            color =
                COLOR_RED;
        }


        if (
            i + 1 ==
            valHighestCellNum
        )
        {
            color =
                COLOR_ACCENT;
        }


        tft.setTextColor(
            color,
            COLOR_BG
        );

        tft.setCursor(
            x,
            y
        );

        tft.printf(
            "C%02d:%.2f",
            i + 1,
            valCells[i] / 1000.0f
        );
    }
}


// =====================================================
// BMS FOOTER
// =====================================================

void drawBMSFooter()
{
    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_MUTED,
        COLOR_BG
    );

    tft.setCursor(
        15,
        300
    );

    tft.printf(
        "Cycle: %d | Temp Max: %d C",
        valCycleCount,
        valMaxTemp
    );
}


// =====================================================
// BMS PAGE
// =====================================================

void drawBMSPage()
{
    // =================================================
    // WAJIB FULL CLEAR
    // =================================================

    tft.fillScreen(
        COLOR_BG
    );


    // =================================================
    // DRAW FULL BMS
    // =================================================

    drawBMSHeader();

    drawBMSParameters();

    drawCells();

    drawBMSFooter();
}


// =====================================================
// READ VALUES
// =====================================================

void readValues()
{
    displaySpeed =
        (float)atomicSpeed.load(
            std::memory_order_acquire
        );

    displayRPM =
        (float)atomicRPM.load(
            std::memory_order_acquire
        );

    displayVoltage =
        atomicVoltsRaw.load(
            std::memory_order_acquire
        ) / 10.0f;

    displayCurrent =
        atomicAmpereRaw.load(
            std::memory_order_acquire
        ) / 10.0f;

    displayPower =
        atomicPowerRaw.load(
            std::memory_order_acquire
        );


    // =================================================
    // SOC
    // =================================================

    if (
        valFullCapacity > 0.1f
    )
    {
        displaySOC =
            constrain(
                (
                    valRemainingCapacity /
                    valFullCapacity
                ) * 100.0f,
                0.0f,
                100.0f
            );
    }
    else
    {
        displaySOC =
            constrain(
                (float)valSOC,
                0.0f,
                100.0f
            );
    }


    uint32_t now =
        millis();


    // =================================================
    // TRIP TIMER
    // =================================================

    if (
        lastTripUpdateMs == 0
    )
    {
        lastTripUpdateMs =
            now;
    }


    // =================================================
    // ODOMETER
    // =================================================

    float odometerFromCAN =
        atomicOdometerKm.load(
            std::memory_order_acquire
        );

    if (
        odometerFromCAN > 0.0f
    )
    {
        displayOdometerKm =
            odometerFromCAN;
    }


    // =================================================
    // TRIP
    // =================================================

    if (
        now -
        lastTripUpdateMs >=
        1000UL
    )
    {
        float elapsedHours =
            (
                now -
                lastTripUpdateMs
            ) /
            1000.0f /
            3600.0f;


        float distanceKm =
            displaySpeed *
            elapsedHours;


        if (
            odometerFromCAN <= 0.0f
        )
        {
            displayOdometerKm +=
                distanceKm;
        }


        displayTripKm +=
            distanceKm;


        lastTripUpdateMs =
            now;
    }


    // =================================================
    // SPORT 5 SECOND RESET
    // =================================================

    VehicleMode currentMode =
        atomicMode.load(
            std::memory_order_acquire
        );


    if (
        currentMode == MODE_SPORT &&
        displaySpeed <= 0.5f &&
        displayRPM <= 100
    )
    {
        if (!tripResetArmed)
        {
            tripResetArmed =
                true;

            sportHoldStartMs =
                now;
        }
        else if (
            now -
            sportHoldStartMs >=
            5000UL &&
            !tripResetConsumed
        )
        {
            displayTripKm =
                0.0f;

            preferences.putFloat(
                "trip",
                displayTripKm
            );

            tripResetConsumed =
                true;
        }
    }
    else
    {
        tripResetArmed =
            false;

        tripResetConsumed =
            false;

        sportHoldStartMs =
            0;
    }


    // =================================================
    // SAVE
    // =================================================

    if (
        now -
        lastOdoSaveMs >=
        5000UL
    )
    {
        preferences.putFloat(
            "odo",
            displayOdometerKm
        );

        preferences.putFloat(
            "trip",
            displayTripKm
        );

        lastOdoSaveMs =
            now;
    }
}

} // namespace


// =====================================================
// INIT
// =====================================================

void initTftSpeedometer()
{
    SPI.begin(
        DISP_SCK_PIN,
        DISP_MISO_PIN,
        DISP_MOSI_PIN,
        DISP_CS_PIN
    );


    tft.init();

    tft.setRotation(1);

    tft.setTextWrap(false);

    // =================================================
    // CREATE CENTER SPRITE
    // =================================================
    // 192x192 @ 16-bit = 73,728 bytes.
    // Seluruh center dirender di RAM lalu dipush sekaligus.
    centerSprite.setColorDepth(16);
    centerSpriteReady =
        centerSprite.createSprite(192, 192) != nullptr;

    if (centerSpriteReady)
    {
        centerSprite.fillSprite(COLOR_BG);
    }


    // =================================================
    // BUTTON
    // =================================================

    pinMode(
        PAGE_BUTTON_PIN,
        INPUT_PULLUP
    );

    pinMode(
        TRIP_RESET_BUTTON_PIN,
        INPUT_PULLUP
    );


    // =================================================
    // LOAD STORAGE
    // =================================================

    displayOdometerKm =
        preferences.getFloat(
            "odo",
            0.0f
        );

    displayTripKm =
        preferences.getFloat(
            "trip",
            0.0f
        );


    uint32_t now =
        millis();

    lastTripUpdateMs =
        now;

    lastOdoSaveMs =
        now;

    lastFrameMs =
        0;

    lastBmsDrawMs =
        0;


    // =================================================
    // STARTUP
    // =================================================

    startupState =
        STARTUP_LOADING;

    startupStartedAt =
        now;

    startupPhaseStartedAt =
        0;

    startupSpeed =
        0.0f;

    startupRPM =
        0.0f;

    centerAnimatedSpeed = 0.0f;
    centerAnimationActive = false;


    lastLoadingProgress =
        255;

    lastStartupSpeed =
        -999;


    // =================================================
    // RESET PAGE
    // =================================================

    currentDisplayPage =
        PAGE_MAIN;

    lastRenderedPage =
        PAGE_MAIN;

    forceFullRefresh =
        true;


    resetDisplayCaches();


    // =================================================
    // INITIAL CANVAS
    // =================================================

    tft.fillScreen(
        COLOR_BG
    );

    drawLoadingScreen(0);
}


// =====================================================
// UPDATE
// =====================================================

void updateTftSpeedometer()
{
    uint32_t now =
        millis();


    // =================================================
    // BUTTON
    // =================================================

    bool pageButtonNow =
        digitalRead(
            PAGE_BUTTON_PIN
        ) == LOW;


    bool tripButtonNow =
        digitalRead(
            TRIP_RESET_BUTTON_PIN
        ) == LOW;


    // =================================================
    // DETERMINE PAGE
    // =================================================

    currentDisplayPage =
        pageButtonNow
            ? PAGE_BMS
            : PAGE_MAIN;


    // =================================================
    // PAGE CHANGED
    // =================================================

    if (
        currentDisplayPage !=
        lastRenderedPage
    )
    {
        // =================================================
        // FULL REFRESH
        // =================================================

        refreshPage();


        // =================================================
        // SET NEW PAGE
        // =================================================

        lastRenderedPage =
            currentDisplayPage;


        // =================================================
        // BMS
        // =================================================

        if (
            currentDisplayPage ==
            PAGE_BMS
        )
        {
            bmsSnapshotInitialized =
                false;
        }


        // =================================================
        // MAIN
        // =================================================

        else
        {
            mainCache.initialized =
                false;
        }
    }


    // =================================================
    // TRIP RESET BUTTON
    // =================================================

    if (
        tripButtonNow &&
        !tripButtonLatched
    )
    {
        displayTripKm =
            0.0f;

        preferences.putFloat(
            "trip",
            displayTripKm
        );

        tripButtonLatched =
            true;
    }


    if (!tripButtonNow)
    {
        tripButtonLatched =
            false;
    }


    // =================================================
    // STARTUP LOADING
    // =================================================

    if (
        startupState ==
        STARTUP_LOADING
    )
    {
        uint32_t elapsed =
            now -
            startupStartedAt;


        uint8_t progress =
            (
                elapsed *
                100UL
            ) /
            STARTUP_LOADING_MS;


        progress =
            constrain(
                progress,
                0,
                100
            );


        if (
            progress !=
            lastLoadingProgress
        )
        {
            drawLoadingScreen(
                progress
            );

            lastLoadingProgress =
                progress;
        }


        if (
            elapsed >=
            STARTUP_LOADING_MS
        )
        {
            startupState =
                STARTUP_ANIMATING;

            startupPhaseStartedAt =
                now;

            startupSpeed =
                0.0f;

            startupRPM =
                0.0f;

            lastStartupSpeed =
                -999;

            mainCache.initialized =
                false;
        }

        return;
    }


    // =================================================
    // STARTUP ANIMATION
    // =================================================

    if (
        startupState ==
        STARTUP_ANIMATING
    )
    {
        uint32_t elapsed =
            now -
            startupPhaseStartedAt;


        float phase =
            constrain(
                (float)elapsed /
                (float)STARTUP_ANIMATION_MS,
                0.0f,
                1.0f
            );


        if (phase < 0.5f)
        {
            float ramp =
                phase /
                0.5f;

            startupSpeed =
                MAX_SPEED *
                ramp;

            startupRPM =
                RPM_MAX *
                ramp;
        }
        else
        {
            float ramp =
                1.0f -
                (
                    (phase - 0.5f) /
                    0.5f
                );

            startupSpeed =
                MAX_SPEED *
                ramp;

            startupRPM =
                RPM_MAX *
                ramp;
        }


        // =================================================
        // STARTUP MAIN
        // =================================================

        if (
            currentDisplayPage ==
            PAGE_MAIN
        )
        {
            int animationSpeed =
                constrain(
                    (int)round(startupSpeed),
                    0,
                    MAX_SPEED
                );


            if (
                animationSpeed !=
                lastStartupSpeed
            )
            {
                if (
                    !mainCache.initialized
                )
                {
                    drawMainStatic();

                    mainCache.initialized =
                        true;
                }


                drawCenterDynamic(
                    animationSpeed,
                    MODE_DRIVE,
                    false
                );


                lastStartupSpeed =
                    animationSpeed;
            }
        }


        // =================================================
        // END STARTUP
        // =================================================

        if (
            phase >= 1.0f
        )
        {
            startupState =
                STARTUP_DONE;

            startupSpeed =
                0.0f;

            startupRPM =
                0.0f;

            centerAnimatedSpeed =
                0.0f;

            centerAnimationActive =
                false;

            mainCache.initialized =
                false;

            forceFullRefresh =
                true;
        }

        return;
    }


    // =================================================
    // FRAME LIMIT
    // =================================================

    if (
        now -
        lastFrameMs <
        MAIN_UPDATE_MS
    )
    {
        return;
    }

    lastFrameMs =
        now;


    // =================================================
    // READ VEHICLE DATA
    // =================================================

    readValues();


    // =================================================
    // MAIN PAGE
    // =================================================

    if (
        currentDisplayPage ==
        PAGE_MAIN
    )
    {
        if (forceFullRefresh)
        {
            tft.fillScreen(
                COLOR_BG
            );

            mainCache.initialized =
                false;

            forceFullRefresh =
                false;
        }

        updateMainDynamic();

        return;
    }


    // =================================================
    // BMS PAGE
    // =================================================

    if (
        now -
        lastBmsDrawMs <
        BMS_UPDATE_MS
    )
    {
        return;
    }

    lastBmsDrawMs =
        now;


    // =================================================
    // BMS CHANGE DETECTION
    // =================================================

    bool dataChanged =
        !bmsSnapshotInitialized ||


        fabsf(
            displayVoltage -
            lastBmsVoltage
        ) > 0.05f ||


        fabsf(
            displayCurrent -
            lastBmsCurrent
        ) > 0.05f ||


        fabsf(
            displayPower -
            lastBmsPower
        ) > 0.5f ||


        fabsf(
            displaySOC -
            lastBmsSoc
        ) > 0.5f ||


        valSOH !=
        lastBmsSOH ||


        valHighestCellVolt !=
        lastBmsHighestCellVolt ||


        valHighestCellNum !=
        lastBmsHighestCellNum ||


        valLowestCellVolt !=
        lastBmsLowestCellVolt ||


        valLowestCellNum !=
        lastBmsLowestCellNum ||


        valAvgCellVolt !=
        lastBmsAvgCellVolt ||


        valMaxTemp !=
        lastBmsMaxTemp ||


        valCycleCount !=
        lastBmsCycleCount ||


        memcmp(
            valCells,
            lastBmsCells,
            sizeof(valCells)
        ) != 0;


    // =================================================
    // FULL BMS DRAW
    // =================================================

    if (
        dataChanged ||
        forceFullRefresh
    )
    {
        drawBMSPage();


        forceFullRefresh =
            false;


        bmsSnapshotInitialized =
            true;


        // =================================================
        // SAVE BMS CACHE
        // =================================================

        lastBmsVoltage =
            displayVoltage;

        lastBmsCurrent =
            displayCurrent;

        lastBmsPower =
            displayPower;

        lastBmsSoc =
            displaySOC;

        lastBmsSOH =
            valSOH;


        lastBmsHighestCellVolt =
            valHighestCellVolt;

        lastBmsHighestCellNum =
            valHighestCellNum;


        lastBmsLowestCellVolt =
            valLowestCellVolt;

        lastBmsLowestCellNum =
            valLowestCellNum;


        lastBmsAvgCellVolt =
            valAvgCellVolt;


        lastBmsMaxTemp =
            valMaxTemp;

        lastBmsCycleCount =
            valCycleCount;


        memcpy(
            lastBmsCells,
            valCells,
            sizeof(valCells)
        );
    }
}