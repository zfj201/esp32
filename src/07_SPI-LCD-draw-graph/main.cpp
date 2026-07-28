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

// 当前竖屏模式不需要偏移 
constexpr uint16_t LCD_X_OFFSET = 0; 
constexpr uint16_t LCD_Y_OFFSET = 0;
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

constexpr uint16_t COLOR_BLACK   = 0x0000;
constexpr uint16_t COLOR_WHITE   = 0xFFFF;
constexpr uint16_t COLOR_RED     = 0xF800;
constexpr uint16_t COLOR_GREEN   = 0x07E0;
constexpr uint16_t COLOR_BLUE    = 0x001F;
constexpr uint16_t COLOR_YELLOW  = 0xFFE0;
constexpr uint16_t COLOR_CYAN    = 0x07FF;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;

constexpr uint16_t rgb565(
    uint8_t red,
    uint8_t green,
    uint8_t blue)
{
    return static_cast<uint16_t>(
        ((red   & 0xF8) << 8) |
        ((green & 0xFC) << 3) |
        (blue   >> 3)
    );
}

void lcdBeginWrite() {
    SPI.beginTransaction(lcdSpiSettings);
    digitalWrite(LCD_CS, LOW);
}

void lcdEndWrite() {
    digitalWrite(LCD_CS, HIGH);
    SPI.endTransaction();
}
void lcdCommandMode() {
    digitalWrite(LCD_DC, LOW);
}
void lcdDataMode() {
    digitalWrite(LCD_DC, HIGH);
}
void lcdWrite8Raw(uint8_t data) {
    SPI.transfer(data);
}
void lcdWrite16Raw(uint16_t data) {
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

    // 0x2A：设置列地址，也就是 X 范围
    lcdCommandMode();
    lcdWrite8Raw(0x2A);

    lcdDataMode();
    lcdWrite16Raw(x0);
    lcdWrite16Raw(x1);

    // 0x2B：设置行地址，也就是 Y 范围
    lcdCommandMode();
    lcdWrite8Raw(0x2B);

    lcdDataMode();
    lcdWrite16Raw(y0);
    lcdWrite16Raw(y1);

    // 0x2C：开始向显存写入颜色
    lcdCommandMode();
    lcdWrite8Raw(0x2C);

    // 函数退出时保持数据模式
    lcdDataMode();
}
void lcdDrawPixel(
    int32_t x,
    int32_t y,
    uint16_t color)
{
    // 超出屏幕的坐标不绘制
    if (x < 0 || x >= LCD_WIDTH ||
        y < 0 || y >= LCD_HEIGHT)
    {
        return;
    }

    lcdBeginWrite();

    // 一个点就是一个 1×1 地址窗口
    lcdSetAddressWindowRaw(
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y),
        static_cast<uint16_t>(x),
        static_cast<uint16_t>(y)
    );

    // 写入一个像素颜色
    lcdWrite16Raw(color);

    lcdEndWrite();
}

// 绘制水平线
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

    // 左边越界时裁剪
    if (x < 0)
    {
        width += x;
        x = 0;
    }

    // 整条线都在屏幕右边
    if (x >= LCD_WIDTH || width <= 0)
    {
        return;
    }

    // 右边越界时裁剪
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

// 绘制垂直线
void lcdDrawFastVLine(
    int32_t x,
    int32_t y,
    int32_t height,
    uint16_t color)
{
    if (height <= 0)
    {
        return;
    }

    if (x < 0 || x >= LCD_WIDTH)
    {
        return;
    }

    // 上方越界时裁剪
    if (y < 0)
    {
        height += y;
        y = 0;
    }

    // 整条线都在屏幕下方
    if (y >= LCD_HEIGHT || height <= 0)
    {
        return;
    }

    // 下方越界时裁剪
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
inline void lcdDrawPixelRaw(
    int32_t x,
    int32_t y,
    uint16_t color)
{
    if (x < 0 || x >= LCD_WIDTH ||
        y < 0 || y >= LCD_HEIGHT)
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
// 绘制任意直线
void lcdDrawLine(
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint16_t color)
{
    // 水平线走快速路径
    if (y0 == y1)
    {
        int32_t startX = min(x0, x1);
        int32_t width = abs(x1 - x0) + 1;

        lcdDrawFastHLine(
            startX,
            y0,
            width,
            color
        );

        return;
    }

    // 垂直线走快速路径
    if (x0 == x1)
    {
        int32_t startY = min(y0, y1);
        int32_t height = abs(y1 - y0) + 1;

        lcdDrawFastVLine(
            x0,
            startY,
            height,
            color
        );

        return;
    }

    int32_t dx = abs(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;

    int32_t dy = -abs(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;

    int32_t error = dx + dy;

    lcdBeginWrite();

    while (true)
    {
        lcdDrawPixelRaw(x0, y0, color);

        // 已经到达终点
        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        int32_t error2 = error * 2;

        // 判断这一步是否应该移动 X
        if (error2 >= dy)
        {
            error += dy;
            x0 += sx;
        }

        // 判断这一步是否应该移动 Y
        if (error2 <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }

    lcdEndWrite();
}
// 绘制空心矩形
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

    // 顶边
    lcdDrawFastHLine(
        x,
        y,
        width,
        color
    );

    // 底边
    if (height > 1)
    {
        lcdDrawFastHLine(
            x,
            y + height - 1,
            width,
            color
        );
    }

    // 左边
    lcdDrawFastVLine(
        x,
        y,
        height,
        color
    );

    // 右边
    if (width > 1)
    {
        lcdDrawFastVLine(
            x + width - 1,
            y,
            height,
            color
        );
    }
}

// 绘制实心矩形
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

    // 左边越界裁剪
    if (x < 0)
    {
        width += x;
        x = 0;
    }

    // 上边越界裁剪
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

    // 右边越界裁剪
    if (x + width > LCD_WIDTH)
    {
        width = LCD_WIDTH - x;
    }

    // 下边越界裁剪
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
    lcdFillRect(
        0,
        0,
        LCD_WIDTH,
        LCD_HEIGHT,
        color
    );
}

// 整体绘制函数
void drawLesson6Demo()
{
    // 1. 黑色背景
    lcdFillScreen(COLOR_BLACK);

    // 2. 最外层白色边框
    // 同时测试四条屏幕边界
    lcdDrawRect(
        0,
        0,
        LCD_WIDTH,
        LCD_HEIGHT,
        COLOR_WHITE
    );

    // 3. 一排单像素点
    for (int32_t x = 10; x < 230; x += 10)
    {
        lcdDrawPixel(
            x,
            20,
            COLOR_YELLOW
        );
    }

    // 4. 水平线
    lcdDrawFastHLine(
        20,
        50,
        200,
        COLOR_RED
    );

    // 5. 垂直线
    lcdDrawFastVLine(
        120,
        35,
        100,
        COLOR_GREEN
    );

    // 6. 两条斜线
    lcdDrawLine(
        20,
        80,
        219,
        150,
        COLOR_CYAN
    );

    lcdDrawLine(
        219,
        80,
        20,
        150,
        COLOR_MAGENTA
    );

    // 7. 空心矩形
    lcdDrawRect(
        20,
        170,
        80,
        50,
        COLOR_BLUE
    );

    // 8. 实心矩形
    lcdFillRect(
        130,
        170,
        80,
        50,
        COLOR_YELLOW
    );

    // 9. 底部三个 RGB 色块
    lcdFillRect(
        25,
        245,
        50,
        50,
        COLOR_RED
    );

    lcdFillRect(
        95,
        245,
        50,
        50,
        COLOR_GREEN
    );

    lcdFillRect(
        165,
        245,
        50,
        50,
        COLOR_BLUE
    );
}
void lcdInit() {
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
    lcdCommandMode();
    lcdWrite8Raw(0x3A);
    lcdDataMode();
    // RGB565，一个像素16位
    lcdWrite8Raw(0x55);
    lcdCommandMode();
    lcdWrite8Raw(0x36);
    lcdDataMode();
    // 原生240×320方向，RGB颜色顺序
    lcdWrite8Raw(0x00);

    // 开启显示
    lcdCommandMode();
    lcdWrite8Raw(0x29);
    lcdDataMode();
    lcdEndWrite();
    delay(50);

    // 先写黑屏，再打开背光
    lcdFillScreen(COLOR_BLACK);

    // 若背光电路是高电平有效，这里会点亮
    digitalWrite(LCD_BL, HIGH);

    delay(500);

    Serial.println("LCD initialized.");
}
void setup() {
    lcdInit(); 
    drawLesson6Demo();
}

void loop() {

}