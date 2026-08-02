#pragma once

#include <Arduino.h>

// ============================================================
// CHD-ESP32-S3-BOX V2.0 / ILI9341 LCD 公共驱动
//
// 职责：
// 1. 完成 SPI + ILI9341 初始化
// 2. 提供像素 / 线 / 矩形 / 文字 / RGB565 图片绘制
//
// 颜色格式：
//   RGB565，一个像素 2 字节
//   发送顺序：高字节在前（MSB first）
// ============================================================

// ------------------------------------------------------------
// 引脚（原理图 CHD-ESP32-S3-BOX V2.0）
// ------------------------------------------------------------
constexpr int LCD_DC   = 4;
constexpr int LCD_CS   = 5;
constexpr int LCD_MOSI = 6;   // 原理图名称：LCD_SDA
constexpr int LCD_SCK  = 7;
constexpr int LCD_BL   = 47;  // 原理图名称：LCD_CTRL，背光控制

// V2.0 不直接使用 GPIO48 作为 LCD_RST。
// 本驱动通过命令 0x01 做软件复位。

// ------------------------------------------------------------
// 屏幕几何
// ------------------------------------------------------------
// 当前使用原生竖屏方向：宽 240，高 320。
constexpr uint16_t LCD_WIDTH  = 240;
constexpr uint16_t LCD_HEIGHT = 320;

// 若后续改成其他面板或偏移窗口，可在这里统一调整。
constexpr uint16_t LCD_X_OFFSET = 0;
constexpr uint16_t LCD_Y_OFFSET = 0;

// 初学阶段先用较低 SPI 时钟，保证稳定性。
// 屏幕稳定后可尝试提高到 20 MHz / 40 MHz。
constexpr uint32_t LCD_SPI_FREQUENCY = 40000000;

// ------------------------------------------------------------
// 常用 RGB565 颜色
// ------------------------------------------------------------
constexpr uint16_t COLOR_BLACK   = 0x0000;
constexpr uint16_t COLOR_WHITE   = 0xFFFF;
constexpr uint16_t COLOR_RED     = 0xF800;
constexpr uint16_t COLOR_GREEN   = 0x07E0;
constexpr uint16_t COLOR_BLUE    = 0x001F;
constexpr uint16_t COLOR_YELLOW  = 0xFFE0;
constexpr uint16_t COLOR_CYAN    = 0x07FF;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;
constexpr uint16_t COLOR_ORANGE  = 0xFD20;
constexpr uint16_t COLOR_GRAY    = 0x8410;

// 5x7 点阵文字的单元尺寸（含 1 像素间距）
constexpr int32_t FONT_WIDTH       = 5;
constexpr int32_t FONT_HEIGHT      = 7;
constexpr int32_t FONT_CELL_WIDTH  = 6;
constexpr int32_t FONT_CELL_HEIGHT = 8;

// 把 RGB888 转成 RGB565。
// 例：rgb565(255, 0, 0) -> 0xF800
constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return static_cast<uint16_t>(
        ((red   & 0xF8) << 8) |
        ((green & 0xFC) << 3) |
        (blue   >> 3)
    );
}

// ------------------------------------------------------------
// 初始化与底层写接口
// ------------------------------------------------------------

// 初始化 GPIO / SPI / ILI9341，并打开背光。
void lcdInit();

// 开始一次连续 SPI 事务（拉低 CS）。
void lcdBeginWrite();

// 结束一次连续 SPI 事务（拉高 CS）。
void lcdEndWrite();

// 设置 DC=命令模式 / 数据模式。
void lcdCommandMode();
void lcdDataMode();

// 在当前事务中写 8/16 bit 原始数据。
void lcdWrite8Raw(uint8_t data);
void lcdWrite16Raw(uint16_t data);

// 设置显存写入窗口，并进入 RAMWR（0x2C）数据模式。
// 调用前必须已经 lcdBeginWrite()。
void lcdSetAddressWindowRaw(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1
);

// ------------------------------------------------------------
// 基础绘图
// ------------------------------------------------------------

void lcdDrawPixel(int32_t x, int32_t y, uint16_t color);

void lcdDrawFastHLine(
    int32_t x,
    int32_t y,
    int32_t width,
    uint16_t color
);

void lcdDrawFastVLine(
    int32_t x,
    int32_t y,
    int32_t height,
    uint16_t color
);

void lcdDrawLine(
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint16_t color
);

void lcdDrawRect(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color
);

void lcdFillRect(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color
);

void lcdFillScreen(uint16_t color);

// ------------------------------------------------------------
// 文字
// ------------------------------------------------------------

void lcdDrawChar(
    int32_t x,
    int32_t y,
    char character,
    uint16_t foregroundColor,
    uint16_t backgroundColor,
    uint8_t scale = 1
);

void lcdDrawText(
    int32_t x,
    int32_t y,
    const char* text,
    uint16_t foregroundColor,
    uint16_t backgroundColor,
    uint8_t scale = 1
);

// ------------------------------------------------------------
// RGB565 图片
// ------------------------------------------------------------

// 从 Flash 中的 RGB565 数组绘制图片。
// data 按行优先排列：data[0] 是左上角像素。
// 使用 PROGMEM 时，本函数内部通过 pgm_read_word 读取。
void lcdDrawRGB565Image(
    int32_t x,
    int32_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t* data
);

// 把 RAM 中已经按“高字节、低字节”排列的 RGB565 数据连续写入 LCD。
// 主要供 LVGL 的 RGB565_SWAPPED 部分刷新缓冲区使用。
// 当前接口要求整个矩形位于屏幕内，不执行源数据裁剪。
void lcdDrawRGB565Bytes(
    int32_t x,
    int32_t y,
    uint16_t width,
    uint16_t height,
    const uint8_t* data
);
