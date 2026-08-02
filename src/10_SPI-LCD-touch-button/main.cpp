#include <Arduino.h>

#include <Gt911.h>
#include <LcdIli9341.h>

// ============================================================
// 第 10 课：GT911 触摸按钮
//
// 学习目标：
// 1. 把触摸坐标映射到 LCD 坐标
// 2. 用矩形区域做最简单的 UI hit-test
// 3. 点击后改变按钮颜色，形成“可交互界面”
//
// 界面：
//   黑底 + 中央 START 按钮
//   每次按下（Pressed）在一组颜色间循环切换按钮填充色
// ============================================================

// ------------------------------------------------------------
// START 按钮几何（屏幕中央）
// ------------------------------------------------------------
constexpr int32_t BUTTON_WIDTH  = 160;
constexpr int32_t BUTTON_HEIGHT = 64;
constexpr int32_t BUTTON_X =
    (LCD_WIDTH - BUTTON_WIDTH) / 2;      // 40
constexpr int32_t BUTTON_Y =
    (LCD_HEIGHT - BUTTON_HEIGHT) / 2;    // 128

// 按钮可切换的颜色表
static const uint16_t BUTTON_COLORS[] = {
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_MAGENTA,
    COLOR_CYAN
};

constexpr size_t BUTTON_COLOR_COUNT =
    sizeof(BUTTON_COLORS) / sizeof(BUTTON_COLORS[0]);

static size_t g_buttonColorIndex = 0;

// ------------------------------------------------------------
// UI 绘制
// ------------------------------------------------------------

static bool pointInButton(int32_t x, int32_t y)
{
    return x >= BUTTON_X &&
           x < (BUTTON_X + BUTTON_WIDTH) &&
           y >= BUTTON_Y &&
           y < (BUTTON_Y + BUTTON_HEIGHT);
}

static void drawStartButton()
{
    const uint16_t fillColor = BUTTON_COLORS[g_buttonColorIndex];

    // 深色外框，让按钮边界更清楚
    lcdFillRect(
        BUTTON_X - 4,
        BUTTON_Y - 4,
        BUTTON_WIDTH + 8,
        BUTTON_HEIGHT + 8,
        COLOR_WHITE
    );

    lcdFillRect(
        BUTTON_X,
        BUTTON_Y,
        BUTTON_WIDTH,
        BUTTON_HEIGHT,
        fillColor
    );

    // 黄/青较亮，文字用黑；其余用白，保证对比度。
    const uint16_t textColor =
        (fillColor == COLOR_YELLOW || fillColor == COLOR_CYAN)
            ? COLOR_BLACK
            : COLOR_WHITE;

    lcdDrawText(
        BUTTON_X + 34,
        BUTTON_Y + 20,
        "START",
        textColor,
        fillColor,
        3
    );
}

static void drawScreen()
{
    lcdFillScreen(COLOR_BLACK);

    lcdDrawText(
        28,
        36,
        "TOUCH BUTTON",
        COLOR_CYAN,
        COLOR_BLACK,
        2
    );

    lcdDrawText(
        16,
        70,
        "Tap START to recolor",
        COLOR_WHITE,
        COLOR_BLACK,
        1
    );

    drawStartButton();

    lcdDrawText(
        10,
        290,
        "Lesson 10 / GT911 UI",
        COLOR_GRAY,
        COLOR_BLACK,
        1
    );
}

// ------------------------------------------------------------
// 触摸事件：只关心第一触点的 Pressed
// ------------------------------------------------------------

static void onTouchEvent(
    Gt911TouchEventType type,
    const Gt911TouchPoint& point)
{
    // 本课只要“点一下”的语义，忽略 Moved / Released。
    if (type != Gt911TouchEventType::Pressed)
    {
        return;
    }

    int32_t lcdX = 0;
    int32_t lcdY = 0;
    gt911MapToLcd(point.x, point.y, lcdX, lcdY);

    Serial.printf(
        "Pressed raw=(%u,%u) lcd=(%ld,%ld)\n",
        point.x,
        point.y,
        static_cast<long>(lcdX),
        static_cast<long>(lcdY)
    );

    if (!pointInButton(lcdX, lcdY))
    {
        return;
    }

    // 命中按钮：循环切换颜色并重绘按钮。
    g_buttonColorIndex = (g_buttonColorIndex + 1) % BUTTON_COLOR_COUNT;
    drawStartButton();

    Serial.printf(
        "Button color index -> %u\n",
        static_cast<unsigned>(g_buttonColorIndex)
    );
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println("10_SPI-LCD-touch-button: start");

    lcdInit();
    drawScreen();

    if (!gt911Begin())
    {
        lcdDrawText(
            10,
            250,
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
    Serial.println("10_SPI-LCD-touch-button: ready");
}

void loop()
{
    // 中断只置位标志；真正的 I2C 读点在主循环完成。
    gt911Service();
}
