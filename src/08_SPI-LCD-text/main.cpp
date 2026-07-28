#include <Arduino.h>
#include <SPI.h>
#include <stdio.h>

#include "font5x7.h"

// ============================================================
// CHD-ESP32-S3-BOX V2.0 LCD 引脚
// ============================================================

constexpr int LCD_DC   = 4;
constexpr int LCD_CS   = 5;
constexpr int LCD_MOSI = 6;   // 原理图名称 LCD_SDA
constexpr int LCD_SCK  = 7;
constexpr int LCD_BL   = 47;  // LCD_CTRL，背光控制

constexpr uint16_t LCD_X_OFFSET = 0;
constexpr uint16_t LCD_Y_OFFSET = 0;

constexpr uint16_t LCD_WIDTH  = 240;
constexpr uint16_t LCD_HEIGHT = 320;

constexpr uint32_t LCD_SPI_FREQUENCY = 4000000;

SPISettings lcdSpiSettings(
    LCD_SPI_FREQUENCY,
    MSBFIRST,
    SPI_MODE0
);

constexpr uint16_t COLOR_BLACK   = 0x0000;
constexpr uint16_t COLOR_WHITE   = 0xFFFF;
constexpr uint16_t COLOR_RED     = 0xF800;
constexpr uint16_t COLOR_GREEN   = 0x07E0;
constexpr uint16_t COLOR_BLUE    = 0x001F;
constexpr uint16_t COLOR_YELLOW  = 0xFFE0;
constexpr uint16_t COLOR_CYAN    = 0x07FF;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;

constexpr int32_t FONT_WIDTH       = 5;
constexpr int32_t FONT_HEIGHT      = 7;
constexpr int32_t FONT_CELL_WIDTH  = 6;
constexpr int32_t FONT_CELL_HEIGHT = 8;

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

    lcdCommandMode();
    lcdWrite8Raw(0x2A);
    lcdDataMode();
    lcdWrite16Raw(x0);
    lcdWrite16Raw(x1);

    lcdCommandMode();
    lcdWrite8Raw(0x2B);
    lcdDataMode();
    lcdWrite16Raw(y0);
    lcdWrite16Raw(y1);

    lcdCommandMode();
    lcdWrite8Raw(0x2C);
    lcdDataMode();
}

void lcdDrawFastHLine(
    int32_t x,
    int32_t y,
    int32_t width,
    uint16_t color)
{
    if (width <= 0)
    {
        return;
    }

    if (y < 0 || y >= LCD_HEIGHT)
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

    if (x >= LCD_WIDTH ||
        y >= LCD_HEIGHT ||
        width <= 0 ||
        height <= 0)
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

void lcdDrawChar(
    int32_t x,
    int32_t y,
    char character,
    uint16_t foregroundColor,
    uint16_t backgroundColor,
    uint8_t scale = 1)
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

            lcdWrite16Raw(
                pixelOn ? foregroundColor : backgroundColor
            );
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
    uint8_t scale = 1)
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

void lcdClearTextLine(
    int32_t y,
    uint8_t scale,
    uint16_t backgroundColor)
{
    const int32_t lineHeight = FONT_CELL_HEIGHT * scale;

    lcdFillRect(
        0,
        y,
        LCD_WIDTH,
        lineHeight,
        backgroundColor
    );
}

void lcdDrawDebugLayout()
{
    lcdFillScreen(COLOR_BLACK);

    lcdDrawText(
        12,
        10,
        "LCD DEBUG",
        COLOR_YELLOW,
        COLOR_BLACK,
        3
    );

    lcdDrawFastHLine(
        10,
        40,
        220,
        COLOR_BLUE
    );

    lcdDrawText(
        10,
        55,
        "ILI9341 READY",
        COLOR_GREEN,
        COLOR_BLACK,
        2
    );
}

void lcdUpdateDebugInfo()
{
    static uint32_t refreshCounter = 0;

    char buffer[40];

    lcdClearTextLine(90, 2, COLOR_BLACK);
    snprintf(
        buffer,
        sizeof(buffer),
        "UPTIME: %lu s",
        static_cast<unsigned long>(millis() / 1000)
    );
    lcdDrawText(10, 90, buffer, COLOR_WHITE, COLOR_BLACK, 2);

    lcdClearTextLine(120, 2, COLOR_BLACK);
    snprintf(
        buffer,
        sizeof(buffer),
        "HEAP: %lu B",
        static_cast<unsigned long>(ESP.getFreeHeap())
    );
    lcdDrawText(10, 120, buffer, COLOR_CYAN, COLOR_BLACK, 2);

    lcdClearTextLine(150, 2, COLOR_BLACK);
    snprintf(
        buffer,
        sizeof(buffer),
        "COUNT: %lu",
        static_cast<unsigned long>(refreshCounter)
    );
    lcdDrawText(10, 150, buffer, COLOR_MAGENTA, COLOR_BLACK, 2);

    lcdClearTextLine(180, 2, COLOR_BLACK);
    snprintf(
        buffer,
        sizeof(buffer),
        "CPU: %u MHz",
        ESP.getCpuFreqMHz()
    );
    lcdDrawText(10, 180, buffer, COLOR_YELLOW, COLOR_BLACK, 2);

    lcdClearTextLine(220, 2, COLOR_BLACK);
    lcdDrawText(
        10,
        220,
        "STATUS: RUNNING",
        COLOR_GREEN,
        COLOR_BLACK,
        2
    );

    ++refreshCounter;
}

void lcdInit()
{
    Serial.begin(115200);

    pinMode(LCD_DC, OUTPUT);
    pinMode(LCD_CS, OUTPUT);
    pinMode(LCD_BL, OUTPUT);

    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_BL, LOW);

    // MISO 未使用，因此第二个参数传 -1
    SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);

    delay(100);
    Serial.println("08_SPI-LCD-text: init ILI9341");

    lcdBeginWrite();
    lcdCommandMode();
    lcdWrite8Raw(0x01);
    lcdEndWrite();
    delay(200);

    lcdBeginWrite();
    lcdCommandMode();
    lcdWrite8Raw(0x11);
    lcdEndWrite();
    delay(150);

    lcdBeginWrite();
    lcdCommandMode();
    lcdWrite8Raw(0x3A);
    lcdDataMode();
    lcdWrite8Raw(0x55);

    lcdCommandMode();
    lcdWrite8Raw(0x36);
    lcdDataMode();
    lcdWrite8Raw(0x00);

    lcdCommandMode();
    lcdWrite8Raw(0x29);
    lcdEndWrite();
    delay(50);

    lcdFillScreen(COLOR_BLACK);
    digitalWrite(LCD_BL, HIGH);
    delay(200);

    Serial.println("LCD initialized.");
}

void setup()
{
    lcdInit();
    lcdDrawDebugLayout();
    lcdUpdateDebugInfo();
}

void loop()
{
    static uint32_t previousUpdateTime = 0;

    const uint32_t currentTime = millis();

    if (currentTime - previousUpdateTime >= 500)
    {
        previousUpdateTime = currentTime;
        lcdUpdateDebugInfo();
    }
}
