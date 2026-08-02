#include <Arduino.h>

#include <LcdIli9341.h>
#include <image_fullscreen.h>

// ============================================================
// 第 09 课：RGB565 全屏图片显示
//
// 学习目标：
// 1. 理解图片在 MCU 中就是 RGB565 像素数组
// 2. 理解 PROGMEM / Flash 存放大图，避免占用宝贵 SRAM
// 3. 学会设置 LCD 窗口后连续写入 width*height 个像素
//
// 数据流：
//   PNG -> 离线居中裁切缩放 -> image_fullscreen.h
//        -> ESP32 Flash -> SPI -> ILI9341 GRAM -> 液晶像素
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println("09_SPI-LCD-image: start");

    lcdInit();

    // 1) 铺满整屏：240 x 320 RGB565
    lcdDrawRGB565Image(
        0,
        0,
        IMAGE_FULLSCREEN_WIDTH,
        IMAGE_FULLSCREEN_HEIGHT,
        IMAGE_FULLSCREEN
    );

    // 2) 叠一行提示文字，证明“图片 + 文字”可以同屏合成
    lcdFillRect(0, 0, LCD_WIDTH, 28, COLOR_BLACK);
    lcdDrawText(
        18,
        6,
        "ESP32 IMAGE OK",
        COLOR_YELLOW,
        COLOR_BLACK,
        2
    );

    Serial.println("09_SPI-LCD-image: fullscreen image drawn");
}

void loop()
{
    // 本课只演示静态图片显示，无需循环刷新。
}
