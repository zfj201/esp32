#include <Arduino.h>
#include <SPI.h>

// ============================================================
// CHD-ESP32-S3-BOX V2.0 LCD 引脚
// ============================================================

constexpr int LCD_DC   = 4;
constexpr int LCD_CS   = 5;
constexpr int LCD_MOSI = 6;   // 原理图名称 LCD_SDA
constexpr int LCD_SCK  = 7;
constexpr int LCD_BL   = 47;  // LCD_CTRL，背光控制

// V2.0 不直接使用 GPIO48 作为 LCD_RST。
// 本程序通过 0x01 软件复位 LCD。

// ============================================================
// 屏幕参数
// ============================================================

constexpr uint16_t LCD_WIDTH  = 240;
constexpr uint16_t LCD_HEIGHT = 320;

// 初学先使用 20 MHz。
// 屏幕稳定后可以修改为 40'000'000。
constexpr uint32_t LCD_SPI_FREQUENCY = 4000000;

SPISettings lcdSpiSettings(
    LCD_SPI_FREQUENCY,
    MSBFIRST,
    SPI_MODE0
);

// ============================================================
// 常用颜色：RGB565
// ============================================================

constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_RED   = 0xF800;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_BLUE  = 0x001F;

// ============================================================
// 底层 SPI 发送函数
// ============================================================

void lcdWriteCommand(uint8_t command)
{
    SPI.beginTransaction(lcdSpiSettings);

    digitalWrite(LCD_CS, LOW);

    // DC=0：下面发送的是命令
    digitalWrite(LCD_DC, LOW);
    SPI.transfer(command);

    digitalWrite(LCD_CS, HIGH);

    SPI.endTransaction();
}

void lcdWriteCommandData(
    uint8_t command,
    const uint8_t* data,
    size_t dataLength)
{
    SPI.beginTransaction(lcdSpiSettings);

    digitalWrite(LCD_CS, LOW);

    // 第一个字节是命令
    digitalWrite(LCD_DC, LOW);
    SPI.transfer(command);

    // 后面的字节是该命令的参数
    if (data != nullptr && dataLength > 0) {
        digitalWrite(LCD_DC, HIGH);

        for (size_t i = 0; i < dataLength; ++i) {
            SPI.transfer(data[i]);
        }
    }

    digitalWrite(LCD_CS, HIGH);

    SPI.endTransaction();
}

// ============================================================
// ILI9341 初始化
// ============================================================

void lcdMinimalInitialize()
{
    // 软件复位
    lcdWriteCommand(0x01);
    delay(200);

    // 退出休眠
    lcdWriteCommand(0x11);
    delay(150);

    // RGB565，一个像素16位
    static const uint8_t pixelFormat[] = {
        0x55
    };

    lcdWriteCommandData(
        0x3A,
        pixelFormat,
        sizeof(pixelFormat)
    );

    // 原生240×320方向，RGB颜色顺序
    static const uint8_t memoryAccessControl[] = {
        0x00
    };

    lcdWriteCommandData(
        0x36,
        memoryAccessControl,
        sizeof(memoryAccessControl)
    );

    // 开启显示
    lcdWriteCommand(0x29);
    delay(50);
}

void lcdFillScreen(uint16_t color)
{
    SPI.beginTransaction(lcdSpiSettings);
    digitalWrite(LCD_CS, LOW);

    // ========================================================
    // 设置列地址 X
    // ========================================================

    const uint16_t x0 = 0;
    const uint16_t x1 = LCD_WIDTH - 1;

    digitalWrite(LCD_DC, LOW);
    SPI.transfer(0x2A);

    digitalWrite(LCD_DC, HIGH);
    SPI.transfer(static_cast<uint8_t>(x0 >> 8));
    SPI.transfer(static_cast<uint8_t>(x0 & 0xFF));
    SPI.transfer(static_cast<uint8_t>(x1 >> 8));
    SPI.transfer(static_cast<uint8_t>(x1 & 0xFF));

    // ========================================================
    // 设置行地址 Y
    // ========================================================

    const uint16_t y0 = 0;
    const uint16_t y1 = LCD_HEIGHT - 1;

    digitalWrite(LCD_DC, LOW);
    SPI.transfer(0x2B);

    digitalWrite(LCD_DC, HIGH);
    SPI.transfer(static_cast<uint8_t>(y0 >> 8));
    SPI.transfer(static_cast<uint8_t>(y0 & 0xFF));
    SPI.transfer(static_cast<uint8_t>(y1 >> 8));
    SPI.transfer(static_cast<uint8_t>(y1 & 0xFF));

    // ========================================================
    // 开始写显存
    // ========================================================

    digitalWrite(LCD_DC, LOW);
    SPI.transfer(0x2C);

    digitalWrite(LCD_DC, HIGH);

    const uint8_t colorHigh =
        static_cast<uint8_t>(color >> 8);

    const uint8_t colorLow =
        static_cast<uint8_t>(color & 0xFF);

    const uint32_t pixelCount =
        static_cast<uint32_t>(LCD_WIDTH) *
        static_cast<uint32_t>(LCD_HEIGHT);

    for (uint32_t i = 0; i < pixelCount; ++i) {
        SPI.transfer(colorHigh);
        SPI.transfer(colorLow);
    }

    digitalWrite(LCD_CS, HIGH);
    SPI.endTransaction();
}

void setup()
{
    Serial.begin(115200);

    pinMode(LCD_DC, OUTPUT);
    pinMode(LCD_CS, OUTPUT);
    pinMode(LCD_BL, OUTPUT);

    // 未通信时，CS 保持高电平
    digitalWrite(LCD_CS, HIGH);

    // 初始化期间先关闭背光，减少白屏闪烁
    digitalWrite(LCD_BL, LOW);

    /*
     * SPI.begin(
     *     SCK,
     *     MISO,
     *     MOSI,
     *     SS
     * );
     *
     * LCD 没有使用 MISO，因此传入 -1。
     */
    SPI.begin(
        LCD_SCK,
        -1,
        LCD_MOSI,
        LCD_CS
    );

    delay(100);

    Serial.println("Initializing ILI9341...");

    lcdMinimalInitialize();

    // 先写黑屏，再打开背光
    lcdFillScreen(COLOR_BLACK);

    // 若背光电路是高电平有效，这里会点亮
    digitalWrite(LCD_BL, HIGH);

    delay(500);

    Serial.println("LCD initialized.");
}

void loop()
{
    lcdFillScreen(COLOR_RED);
    delay(1000);

    lcdFillScreen(COLOR_GREEN);
    delay(1000);

    lcdFillScreen(COLOR_BLUE);
    delay(1000);

    lcdFillScreen(COLOR_WHITE);
    delay(1000);

    lcdFillScreen(COLOR_BLACK);
    delay(1000);
}