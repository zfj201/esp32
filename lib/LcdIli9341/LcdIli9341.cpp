#include "LcdIli9341.h"

#include <SPI.h>

#include <font5x7.h>

// ============================================================
// SPI 事务参数
// ============================================================

static SPISettings lcdSpiSettings(
    LCD_SPI_FREQUENCY,
    MSBFIRST,
    SPI_MODE0
);

// ============================================================
// 底层 SPI / 模式切换
// ============================================================

void lcdBeginWrite()
{
    SPI.beginTransaction(lcdSpiSettings);
    digitalWrite(LCD_CS, LOW);
}

void lcdEndWrite()
{
    digitalWrite(LCD_CS, HIGH);
    SPI.endTransaction();
}

void lcdCommandMode()
{
    digitalWrite(LCD_DC, LOW);
}

void lcdDataMode()
{
    digitalWrite(LCD_DC, HIGH);
}

void lcdWrite8Raw(uint8_t data)
{
    SPI.transfer(data);
}

void lcdWrite16Raw(uint16_t data)
{
    // ILI9341 RGB565：先发高字节，再发低字节。
    SPI.transfer(static_cast<uint8_t>(data >> 8));
    SPI.transfer(static_cast<uint8_t>(data & 0xFF));
}

void lcdSetAddressWindowRaw(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1)
{
    x0 += LCD_X_OFFSET;
    x1 += LCD_X_OFFSET;
    y0 += LCD_Y_OFFSET;
    y1 += LCD_Y_OFFSET;

    // 0x2A：列地址（X）
    lcdCommandMode();
    lcdWrite8Raw(0x2A);
    lcdDataMode();
    lcdWrite16Raw(x0);
    lcdWrite16Raw(x1);

    // 0x2B：行地址（Y）
    lcdCommandMode();
    lcdWrite8Raw(0x2B);
    lcdDataMode();
    lcdWrite16Raw(y0);
    lcdWrite16Raw(y1);

    // 0x2C：开始写显存；退出时保持数据模式，方便连续写像素。
    lcdCommandMode();
    lcdWrite8Raw(0x2C);
    lcdDataMode();
}

// ============================================================
// 初始化
// ============================================================

void lcdInit()
{
    pinMode(LCD_DC, OUTPUT);
    pinMode(LCD_CS, OUTPUT);
    pinMode(LCD_BL, OUTPUT);

    // 空闲时 CS 保持高电平，避免误选中。
    digitalWrite(LCD_CS, HIGH);

    // 初始化阶段先关背光，减少白屏闪烁。
    digitalWrite(LCD_BL, LOW);

    // LCD 没有 MISO，因此第二个参数传 -1。
    SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);
    delay(100);

    Serial.println("LcdIli9341: initializing...");

    // 软件复位
    lcdBeginWrite();
    lcdCommandMode();
    lcdWrite8Raw(0x01);
    lcdEndWrite();
    delay(200);

    // 退出休眠
    lcdBeginWrite();
    lcdCommandMode();
    lcdWrite8Raw(0x11);
    lcdEndWrite();
    delay(150);

    lcdBeginWrite();

    // 0x3A：像素格式 = RGB565（0x55）
    lcdCommandMode();
    lcdWrite8Raw(0x3A);
    lcdDataMode();
    lcdWrite8Raw(0x55);

    // 0x36：内存访问控制
    // 0x00 = 原生 240x320，RGB 顺序
    lcdCommandMode();
    lcdWrite8Raw(0x36);
    lcdDataMode();
    lcdWrite8Raw(0x00);

    // 开启显示
    lcdCommandMode();
    lcdWrite8Raw(0x29);
    lcdEndWrite();
    delay(50);

    lcdFillScreen(COLOR_BLACK);
    digitalWrite(LCD_BL, HIGH);
    delay(50);

    Serial.println("LcdIli9341: ready.");
}

// ============================================================
// 基础绘图
// ============================================================

void lcdDrawPixel(int32_t x, int32_t y, uint16_t color)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
    {
        return;
    }

    lcdBeginWrite();
    lcdSetAddressWindowRaw(
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y),
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y)
    );
    lcdWrite16Raw(color);
    lcdEndWrite();
}

// 已处于 lcdBeginWrite() 事务中时使用的内部打点。
static void lcdDrawPixelRaw(int32_t x, int32_t y, uint16_t color)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
    {
        return;
    }

    lcdSetAddressWindowRaw(
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y),
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y)
    );
    lcdWrite16Raw(color);
}

void lcdDrawFastHLine(
    int32_t x,
    int32_t y,
    int32_t width,
    uint16_t color)
{
    if (width <= 0 || y < 0 || y >= LCD_HEIGHT)
    {
        return;
    }

    if (x < 0)
    {
        width += x;
        x = 0;
    }

    if (x >= LCD_WIDTH || width <= 0)
    {
        return;
    }

    if (x + width > LCD_WIDTH)
    {
        width = LCD_WIDTH - x;
    }

    lcdBeginWrite();
    lcdSetAddressWindowRaw(
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y),
        static_cast<uint16_t>(x + width - 1),
        static_cast<uint16_t>(y)
    );

    for (int32_t i = 0; i < width; ++i)
    {
        lcdWrite16Raw(color);
    }

    lcdEndWrite();
}

void lcdDrawFastVLine(
    int32_t x,
    int32_t y,
    int32_t height,
    uint16_t color)
{
    if (height <= 0 || x < 0 || x >= LCD_WIDTH)
    {
        return;
    }

    if (y < 0)
    {
        height += y;
        y = 0;
    }

    if (y >= LCD_HEIGHT || height <= 0)
    {
        return;
    }

    if (y + height > LCD_HEIGHT)
    {
        height = LCD_HEIGHT - y;
    }

    lcdBeginWrite();
    lcdSetAddressWindowRaw(
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y),
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y + height - 1)
    );

    for (int32_t i = 0; i < height; ++i)
    {
        lcdWrite16Raw(color);
    }

    lcdEndWrite();
}

void lcdDrawLine(
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint16_t color)
{
    // 水平 / 垂直走快速路径，减少 SPI 开销。
    if (y0 == y1)
    {
        const int32_t startX = min(x0, x1);
        const int32_t width = abs(x1 - x0) + 1;
        lcdDrawFastHLine(startX, y0, width, color);
        return;
    }

    if (x0 == x1)
    {
        const int32_t startY = min(y0, y1);
        const int32_t height = abs(y1 - y0) + 1;
        lcdDrawFastVLine(x0, startY, height, color);
        return;
    }

    // Bresenham 直线算法
    int32_t dx = abs(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -abs(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;

    lcdBeginWrite();

    while (true)
    {
        lcdDrawPixelRaw(x0, y0, color);

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        const int32_t error2 = error * 2;

        if (error2 >= dy)
        {
            error += dy;
            x0 += sx;
        }

        if (error2 <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }

    lcdEndWrite();
}

void lcdDrawRect(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    lcdDrawFastHLine(x, y, width, color);

    if (height > 1)
    {
        lcdDrawFastHLine(x, y + height - 1, width, color);
    }

    lcdDrawFastVLine(x, y, height, color);

    if (width > 1)
    {
        lcdDrawFastVLine(x + width - 1, y, height, color);
    }
}

void lcdFillRect(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (x < 0)
    {
        width += x;
        x = 0;
    }

    if (y < 0)
    {
        height += y;
        y = 0;
    }

    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || width <= 0 || height <= 0)
    {
        return;
    }

    if (x + width > LCD_WIDTH)
    {
        width = LCD_WIDTH - x;
    }

    if (y + height > LCD_HEIGHT)
    {
        height = LCD_HEIGHT - y;
    }

    uint32_t pixelCount =
        static_cast<uint32_t>(width) *
        static_cast<uint32_t>(height);

    lcdBeginWrite();
    lcdSetAddressWindowRaw(
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y),
        static_cast<uint16_t>(x + width - 1),
        static_cast<uint16_t>(y + height - 1)
    );

    while (pixelCount > 0)
    {
        lcdWrite16Raw(color);
        --pixelCount;
    }

    lcdEndWrite();
}

void lcdFillScreen(uint16_t color)
{
    lcdFillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

// ============================================================
// 文字
// ============================================================

void lcdDrawChar(
    int32_t x,
    int32_t y,
    char character,
    uint16_t foregroundColor,
    uint16_t backgroundColor,
    uint8_t scale)
{
    if (scale == 0)
    {
        scale = 1;
    }

    uint8_t code = static_cast<uint8_t>(character);
    if (code < 32 || code > 126)
    {
        code = '?';
    }

    const int32_t cellWidth = FONT_CELL_WIDTH * scale;
    const int32_t cellHeight = FONT_CELL_HEIGHT * scale;

    if (x < 0 ||
        y < 0 ||
        x + cellWidth > LCD_WIDTH ||
        y + cellHeight > LCD_HEIGHT)
    {
        return;
    }

    const uint32_t fontStartIndex =
        static_cast<uint32_t>(code - 32) * FONT_WIDTH;

    lcdBeginWrite();
    lcdSetAddressWindowRaw(
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y),
        static_cast<uint16_t>(x + cellWidth - 1),
        static_cast<uint16_t>(y + cellHeight - 1)
    );

    for (int32_t outputY = 0; outputY < cellHeight; ++outputY)
    {
        const int32_t fontY = outputY / scale;

        for (int32_t outputX = 0; outputX < cellWidth; ++outputX)
        {
            const int32_t fontX = outputX / scale;
            bool pixelOn = false;

            if (fontX < FONT_WIDTH && fontY < FONT_HEIGHT)
            {
                const uint8_t columnData =
                    FONT5X7[fontStartIndex + fontX];
                pixelOn = ((columnData >> fontY) & 0x01) != 0;
            }

            lcdWrite16Raw(pixelOn ? foregroundColor : backgroundColor);
        }
    }

    lcdEndWrite();
}

void lcdDrawText(
    int32_t x,
    int32_t y,
    const char* text,
    uint16_t foregroundColor,
    uint16_t backgroundColor,
    uint8_t scale)
{
    if (text == nullptr)
    {
        return;
    }

    if (scale == 0)
    {
        scale = 1;
    }

    const int32_t characterWidth = FONT_CELL_WIDTH * scale;
    const int32_t characterHeight = FONT_CELL_HEIGHT * scale;

    int32_t cursorX = x;
    int32_t cursorY = y;

    while (*text != '\0')
    {
        const char character = *text;
        ++text;

        if (character == '\r')
        {
            continue;
        }

        if (character == '\n')
        {
            cursorX = x;
            cursorY += characterHeight;
            continue;
        }

        if (cursorX + characterWidth > LCD_WIDTH)
        {
            cursorX = x;
            cursorY += characterHeight;
        }

        if (cursorY + characterHeight > LCD_HEIGHT)
        {
            break;
        }

        lcdDrawChar(
            cursorX,
            cursorY,
            character,
            foregroundColor,
            backgroundColor,
            scale
        );

        cursorX += characterWidth;
    }
}

// ============================================================
// RGB565 图片
//
// 核心思路：
// 1. 设置目标窗口
// 2. 按行优先连续写 width * height 个像素
// 3. 数据在 Flash（PROGMEM）中，用 pgm_read_word 读取
// ============================================================

void lcdDrawRGB565Image(
    int32_t x,
    int32_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t* data)
{
    if (data == nullptr || width == 0 || height == 0)
    {
        return;
    }

    // 完全在屏幕外则直接返回。
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
    {
        return;
    }

    if (x + static_cast<int32_t>(width) <= 0 ||
        y + static_cast<int32_t>(height) <= 0)
    {
        return;
    }

    // 源图裁剪参数：若目标位置有负偏移，跳过源图左侧 / 上方像素。
    int32_t srcOffsetX = 0;
    int32_t srcOffsetY = 0;
    int32_t drawX = x;
    int32_t drawY = y;
    int32_t drawWidth = static_cast<int32_t>(width);
    int32_t drawHeight = static_cast<int32_t>(height);

    if (drawX < 0)
    {
        srcOffsetX = -drawX;
        drawWidth += drawX;
        drawX = 0;
    }

    if (drawY < 0)
    {
        srcOffsetY = -drawY;
        drawHeight += drawY;
        drawY = 0;
    }

    if (drawX + drawWidth > LCD_WIDTH)
    {
        drawWidth = LCD_WIDTH - drawX;
    }

    if (drawY + drawHeight > LCD_HEIGHT)
    {
        drawHeight = LCD_HEIGHT - drawY;
    }

    if (drawWidth <= 0 || drawHeight <= 0)
    {
        return;
    }

    lcdBeginWrite();
    lcdSetAddressWindowRaw(
        static_cast<uint16_t>(drawX),
        static_cast<uint16_t>(drawY),
        static_cast<uint16_t>(drawX + drawWidth - 1),
        static_cast<uint16_t>(drawY + drawHeight - 1)
    );

    // 逐行发送，便于处理左右裁剪后的源图跨距。
    for (int32_t row = 0; row < drawHeight; ++row)
    {
        const uint32_t srcRowStart =
            (static_cast<uint32_t>(srcOffsetY + row) * width) +
            static_cast<uint32_t>(srcOffsetX);

        for (int32_t col = 0; col < drawWidth; ++col)
        {
            const uint16_t color =
                pgm_read_word(&data[srcRowStart + col]);
            lcdWrite16Raw(color);
        }
    }

    lcdEndWrite();
}
