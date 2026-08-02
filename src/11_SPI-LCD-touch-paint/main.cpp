#include <Arduino.h>

#include <Gt911.h>
#include <LcdIli9341.h>

// ============================================================
// 第 11 课：GT911 触摸画板
//
// 学习目标：
// 1. 用 lastX/lastY + drawLine 把离散触摸点连成连续笔迹
// 2. 区分“画布区域”和“工具栏区域”
// 3. 实现改颜色 / 清屏（只清画布，保留工具栏）
//
// 布局（240 x 320）：
//   [0 .. TOOLBAR_TOP)     画布
//   [TOOLBAR_TOP .. 319]   工具栏：4 个色块 + CLEAR
// ============================================================

constexpr int32_t TOOLBAR_HEIGHT = 48;
constexpr int32_t TOOLBAR_TOP = LCD_HEIGHT - TOOLBAR_HEIGHT;  // 272

constexpr int32_t COLOR_SLOT_COUNT = 4;
constexpr int32_t COLOR_SLOT_WIDTH = 40;
constexpr int32_t COLOR_SLOT_GAP = 8;
constexpr int32_t COLOR_SLOT_Y = TOOLBAR_TOP + 8;
constexpr int32_t COLOR_SLOT_HEIGHT = 32;
constexpr int32_t COLOR_SLOT_START_X = 8;

constexpr int32_t CLEAR_BUTTON_X = 188;
constexpr int32_t CLEAR_BUTTON_Y = TOOLBAR_TOP + 8;
constexpr int32_t CLEAR_BUTTON_W = 44;
constexpr int32_t CLEAR_BUTTON_H = 32;

static const uint16_t PEN_COLORS[COLOR_SLOT_COUNT] = {
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_YELLOW
};

static uint16_t g_penColor = COLOR_RED;
static int32_t g_selectedColorIndex = 0;

// 画线状态：只有在画布内持续按住时才连接上一点。
static bool g_drawing = false;
static int32_t g_lastX = 0;
static int32_t g_lastY = 0;

// ------------------------------------------------------------
// 区域判断
// ------------------------------------------------------------

static bool pointInCanvas(int32_t x, int32_t y)
{
    return x >= 0 &&
           x < LCD_WIDTH &&
           y >= 0 &&
           y < TOOLBAR_TOP;
}

static bool pointInRect(
    int32_t x,
    int32_t y,
    int32_t rx,
    int32_t ry,
    int32_t rw,
    int32_t rh)
{
    return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

static int32_t hitColorSlot(int32_t x, int32_t y)
{
    for (int32_t i = 0; i < COLOR_SLOT_COUNT; ++i)
    {
        const int32_t slotX =
            COLOR_SLOT_START_X + i * (COLOR_SLOT_WIDTH + COLOR_SLOT_GAP);

        if (pointInRect(
                x,
                y,
                slotX,
                COLOR_SLOT_Y,
                COLOR_SLOT_WIDTH,
                COLOR_SLOT_HEIGHT))
        {
            return i;
        }
    }

    return -1;
}

static bool hitClearButton(int32_t x, int32_t y)
{
    return pointInRect(
        x,
        y,
        CLEAR_BUTTON_X,
        CLEAR_BUTTON_Y,
        CLEAR_BUTTON_W,
        CLEAR_BUTTON_H
    );
}

// ------------------------------------------------------------
// UI 绘制
// ------------------------------------------------------------

static void drawToolbar()
{
    lcdFillRect(0, TOOLBAR_TOP, LCD_WIDTH, TOOLBAR_HEIGHT, COLOR_GRAY);
    lcdDrawFastHLine(0, TOOLBAR_TOP, LCD_WIDTH, COLOR_WHITE);

    for (int32_t i = 0; i < COLOR_SLOT_COUNT; ++i)
    {
        const int32_t slotX =
            COLOR_SLOT_START_X + i * (COLOR_SLOT_WIDTH + COLOR_SLOT_GAP);

        lcdFillRect(
            slotX,
            COLOR_SLOT_Y,
            COLOR_SLOT_WIDTH,
            COLOR_SLOT_HEIGHT,
            PEN_COLORS[i]
        );

        // 当前笔色加白色选中框
        if (i == g_selectedColorIndex)
        {
            lcdDrawRect(
                slotX - 2,
                COLOR_SLOT_Y - 2,
                COLOR_SLOT_WIDTH + 4,
                COLOR_SLOT_HEIGHT + 4,
                COLOR_WHITE
            );
        }
    }

    lcdFillRect(
        CLEAR_BUTTON_X,
        CLEAR_BUTTON_Y,
        CLEAR_BUTTON_W,
        CLEAR_BUTTON_H,
        COLOR_WHITE
    );

    lcdDrawText(
        CLEAR_BUTTON_X + 4,
        CLEAR_BUTTON_Y + 10,
        "CLR",
        COLOR_BLACK,
        COLOR_WHITE,
        1
    );
}

static void clearCanvas()
{
    lcdFillRect(0, 0, LCD_WIDTH, TOOLBAR_TOP, COLOR_BLACK);

    lcdDrawText(
        8,
        8,
        "TOUCH PAINT",
        COLOR_CYAN,
        COLOR_BLACK,
        2
    );

    lcdDrawText(
        8,
        32,
        "Draw above / tap color or CLR",
        COLOR_WHITE,
        COLOR_BLACK,
        1
    );
}

static void drawScreen()
{
    clearCanvas();
    drawToolbar();
}

// ------------------------------------------------------------
// 触摸事件处理（只使用第一触点语义：事件里自带该点）
// ------------------------------------------------------------

static void handleToolbarTap(int32_t x, int32_t y)
{
    const int32_t colorIndex = hitColorSlot(x, y);
    if (colorIndex >= 0)
    {
        g_selectedColorIndex = colorIndex;
        g_penColor = PEN_COLORS[colorIndex];
        drawToolbar();
        Serial.printf("Pen color index -> %ld\n", static_cast<long>(colorIndex));
        return;
    }

    if (hitClearButton(x, y))
    {
        // 只清画布，保留工具栏。
        clearCanvas();
        g_drawing = false;
        Serial.println("Canvas cleared");
    }
}

static void onTouchEvent(
    Gt911TouchEventType type,
    const Gt911TouchPoint& point)
{
    int32_t lcdX = 0;
    int32_t lcdY = 0;
    gt911MapToLcd(point.x, point.y, lcdX, lcdY);

    switch (type)
    {
        case Gt911TouchEventType::Pressed:
        {
            if (pointInCanvas(lcdX, lcdY))
            {
                // 落笔：记录起点，先打一个点，避免轻点看不见。
                g_drawing = true;
                g_lastX = lcdX;
                g_lastY = lcdY;
                lcdDrawPixel(lcdX, lcdY, g_penColor);
            }
            else
            {
                g_drawing = false;
                handleToolbarTap(lcdX, lcdY);
            }
            break;
        }

        case Gt911TouchEventType::Moved:
        {
            if (!g_drawing)
            {
                break;
            }

            // 移动到工具栏则结束当前笔划，避免线画进按钮区。
            if (!pointInCanvas(lcdX, lcdY))
            {
                g_drawing = false;
                break;
            }

            lcdDrawLine(
                g_lastX,
                g_lastY,
                lcdX,
                lcdY,
                g_penColor
            );

            g_lastX = lcdX;
            g_lastY = lcdY;
            break;
        }

        case Gt911TouchEventType::Released:
        {
            g_drawing = false;
            break;
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println("11_SPI-LCD-touch-paint: start");

    lcdInit();
    drawScreen();

    if (!gt911Begin())
    {
        lcdDrawText(
            8,
            80,
            "GT911 INIT FAIL",
            COLOR_RED,
            COLOR_BLACK,
            2
        );
        while (true)
        {
            delay(1000);
        }
    }

    gt911SetEventCallback(onTouchEvent);
    Serial.println("11_SPI-LCD-touch-paint: ready");
}

void loop()
{
    gt911Service();
}
