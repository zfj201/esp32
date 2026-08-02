#include "Gt911.h"

#include <Wire.h>

// ============================================================
// 内部状态
// ============================================================

static uint8_t g_gt911Address = 0;
static volatile bool g_touchIrqPending = false;
static Gt911TouchFrame g_previousFrame = {};
static Gt911TouchEventCallback g_eventCallback = nullptr;

// ============================================================
// 基础 I2C 读写
// ============================================================

static uint16_t makeUint16(uint8_t highByte, uint8_t lowByte)
{
    return static_cast<uint16_t>((highByte << 8) | lowByte);
}

static bool writeRegister8(
    uint8_t deviceAddress,
    uint16_t registerAddress,
    uint8_t value)
{
    Wire.beginTransmission(deviceAddress);
    Wire.write(registerAddress >> 8);
    Wire.write(registerAddress & 0xFF);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool readRegisters16(
    uint8_t deviceAddress,
    uint16_t registerAddress,
    uint8_t* buffer,
    size_t length)
{
    Wire.beginTransmission(deviceAddress);
    Wire.write((registerAddress >> 8) & 0xFF);
    Wire.write(registerAddress & 0xFF);

    // false = 发完寄存器地址后发重复起始，不发 STOP。
    if (Wire.endTransmission(false) != 0)
    {
        return false;
    }

    const uint8_t requestResult = Wire.requestFrom(deviceAddress, length);
    if (requestResult != length)
    {
        while (Wire.available())
        {
            Wire.read();
        }
        return false;
    }

    for (size_t i = 0; i < length; ++i)
    {
        buffer[i] = Wire.read();
    }

    return true;
}

static bool clearTouchStatus()
{
    return writeRegister8(g_gt911Address, GT911_REG_TOUCH_STATUS, 0x00);
}

// ============================================================
// 地址探测与产品信息
// ============================================================

static uint8_t detectGt911Address()
{
    const uint8_t addressList[] = {
        GT911_ADDRESS_1,
        GT911_ADDRESS_2
    };

    for (uint8_t address : addressList)
    {
        uint8_t productId[4] = {};
        if (!readRegisters16(
                address,
                GT911_REG_PRODUCT_INFO,
                productId,
                4))
        {
            continue;
        }

        // 产品 ID 应为可打印 ASCII，例如 "911"。
        const bool looksValid =
            productId[0] >= 0x20 && productId[0] <= 0x7E &&
            productId[1] >= 0x20 && productId[1] <= 0x7E &&
            productId[2] >= 0x20 && productId[2] <= 0x7E;

        if (looksValid)
        {
            return address;
        }
    }

    return 0;
}

static void readProductInfo()
{
    uint8_t buffer[GT911_PRODUCT_INFO_LENGTH] = {};
    if (!readRegisters16(
            g_gt911Address,
            GT911_REG_PRODUCT_INFO,
            buffer,
            GT911_PRODUCT_INFO_LENGTH))
    {
        Serial.println("Gt911: failed to read product info");
        return;
    }

    char productId[5] = {
        static_cast<char>(buffer[0]),
        static_cast<char>(buffer[1]),
        static_cast<char>(buffer[2]),
        static_cast<char>(buffer[3]),
        '\0'
    };

    const uint16_t firmwareVersion = makeUint16(buffer[5], buffer[4]);
    const uint16_t xResolution = makeUint16(buffer[7], buffer[6]);
    const uint16_t yResolution = makeUint16(buffer[9], buffer[8]);
    const uint8_t vendorId = buffer[10];

    Serial.println("--------------------------------");
    Serial.print("Gt911 Product ID: ");
    Serial.println(productId);
    Serial.print("Firmware Version: ");
    Serial.println(firmwareVersion);
    Serial.print("X Resolution: ");
    Serial.println(xResolution);
    Serial.print("Y Resolution: ");
    Serial.println(yResolution);
    Serial.print("Vendor ID: ");
    Serial.println(vendorId);
    Serial.println("--------------------------------");
}

// ============================================================
// 中断配置
// ============================================================

static uint8_t readIntTriggerMode()
{
    uint8_t value = 0;
    if (!readRegisters16(
            g_gt911Address,
            GT911_REG_MODULE_SWITCH1,
            &value,
            1))
    {
        return 0xFF;
    }

    return value & GT911_INT_TRIGGER_MASK;
}

static int mapIntTriggerToArduinoMode(uint8_t triggerMode)
{
    switch (triggerMode)
    {
        case 0:
            return RISING;
        case 1:
            return FALLING;
        case 2:
            // 低电平有效：抓进入低电平的边沿，避免电平中断反复触发。
            return FALLING;
        case 3:
            return RISING;
        default:
            return -1;
    }
}

static void IRAM_ATTR onGt911Interrupt()
{
    g_touchIrqPending = true;
}

static bool setupTouchInterrupt()
{
    pinMode(GT911_INT_PIN, INPUT);

    const uint8_t triggerMode = readIntTriggerMode();
    if (triggerMode == 0xFF)
    {
        Serial.println("Gt911: failed to read INT trigger mode");
        return false;
    }

    const char* triggerName = "Unknown";
    switch (triggerMode)
    {
        case 0:
            triggerName = "Rising edge";
            break;
        case 1:
            triggerName = "Falling edge";
            break;
        case 2:
            triggerName = "Low level";
            break;
        case 3:
            triggerName = "High level";
            break;
    }

    Serial.printf(
        "Gt911 INT trigger (0x804D[1:0]): %u (%s)\n",
        triggerMode,
        triggerName
    );

    const int arduinoMode = mapIntTriggerToArduinoMode(triggerMode);
    if (arduinoMode < 0)
    {
        Serial.println("Gt911: unsupported INT trigger mode");
        return false;
    }

    // 挂中断前清残留状态，避免第一次触摸丢事件。
    if (!clearTouchStatus())
    {
        Serial.println("Gt911: failed to clear old touch status");
        return false;
    }

    g_touchIrqPending = false;
    attachInterrupt(
        digitalPinToInterrupt(GT911_INT_PIN),
        onGt911Interrupt,
        arduinoMode
    );

    Serial.printf("Gt911 interrupt attached on GPIO %d\n", GT911_INT_PIN);
    return true;
}

// ============================================================
// 读点与事件分发
// ============================================================

Gt911FrameReadResult gt911ReadTouchFrame(Gt911TouchFrame& frame)
{
    frame.count = 0;

    uint8_t status = 0;
    if (!readRegisters16(
            g_gt911Address,
            GT911_REG_TOUCH_STATUS,
            &status,
            1))
    {
        Serial.println("Gt911: failed to read touch status");
        return Gt911FrameReadResult::Error;
    }

    // bit7=0：没有新的有效数据
    if ((status & GT911_STATUS_READY_MASK) == 0)
    {
        return Gt911FrameReadResult::NoData;
    }

    uint8_t touchCount = status & GT911_TOUCH_COUNT_MASK;
    if (touchCount > GT911_MAX_TOUCH_POINTS)
    {
        Serial.print("Gt911: invalid touch count ");
        Serial.println(touchCount);
        clearTouchStatus();
        return Gt911FrameReadResult::Error;
    }

    frame.count = touchCount;

    // touchCount=0 是有效的“全部松开”帧。
    if (touchCount == 0)
    {
        if (!clearTouchStatus())
        {
            Serial.println("Gt911: failed to clear release status");
            return Gt911FrameReadResult::Error;
        }
        return Gt911FrameReadResult::Success;
    }

    const size_t dataLength =
        static_cast<size_t>(touchCount) * GT911_POINT_DATA_LENGTH;

    uint8_t rawData[GT911_MAX_TOUCH_POINTS * GT911_POINT_DATA_LENGTH] = {};
    if (!readRegisters16(
            g_gt911Address,
            GT911_REG_POINT_1,
            rawData,
            dataLength))
    {
        Serial.println("Gt911: failed to read point data");
        return Gt911FrameReadResult::Error;
    }

    for (size_t index = 0; index < touchCount; ++index)
    {
        const size_t offset = index * GT911_POINT_DATA_LENGTH;
        Gt911TouchPoint& point = frame.points[index];

        point.id = rawData[offset + 0];
        point.x = makeUint16(rawData[offset + 2], rawData[offset + 1]);
        point.y = makeUint16(rawData[offset + 4], rawData[offset + 3]);
        point.size = makeUint16(rawData[offset + 6], rawData[offset + 5]);
    }

    if (!clearTouchStatus())
    {
        Serial.println("Gt911: failed to clear touch status");
        return Gt911FrameReadResult::Error;
    }

    return Gt911FrameReadResult::Success;
}

static const Gt911TouchPoint* findPointById(
    const Gt911TouchFrame& frame,
    uint8_t id)
{
    for (size_t index = 0; index < frame.count; ++index)
    {
        if (frame.points[index].id == id)
        {
            return &frame.points[index];
        }
    }

    return nullptr;
}

static void processTouchEvents(const Gt911TouchFrame& currentFrame)
{
    // 当前帧：生成 Pressed / Moved
    for (size_t index = 0; index < currentFrame.count; ++index)
    {
        const Gt911TouchPoint& currentPoint = currentFrame.points[index];
        const Gt911TouchPoint* previousPoint =
            findPointById(g_previousFrame, currentPoint.id);

        if (previousPoint == nullptr)
        {
            if (g_eventCallback != nullptr)
            {
                g_eventCallback(Gt911TouchEventType::Pressed, currentPoint);
            }
            continue;
        }

        const bool moved =
            previousPoint->x != currentPoint.x ||
            previousPoint->y != currentPoint.y;

        if (moved && g_eventCallback != nullptr)
        {
            g_eventCallback(Gt911TouchEventType::Moved, currentPoint);
        }
    }

    // 上一帧有、当前帧没有：Released
    for (size_t index = 0; index < g_previousFrame.count; ++index)
    {
        const Gt911TouchPoint& previousPoint = g_previousFrame.points[index];
        const Gt911TouchPoint* currentPoint =
            findPointById(currentFrame, previousPoint.id);

        if (currentPoint == nullptr && g_eventCallback != nullptr)
        {
            g_eventCallback(Gt911TouchEventType::Released, previousPoint);
        }
    }

    g_previousFrame = currentFrame;
}

// ============================================================
// 对外 API
// ============================================================

bool gt911Begin(uint32_t i2cClockHz)
{
    if (!Wire.begin(I2C_SDA, I2C_SCL, i2cClockHz))
    {
        Serial.println("Gt911: I2C begin failed");
        return false;
    }

    Wire.setTimeOut(50);

    g_gt911Address = detectGt911Address();
    if (g_gt911Address == 0)
    {
        Serial.println("Gt911: address detect failed");
        return false;
    }

    Serial.printf("Gt911: address 0x%02X\n", g_gt911Address);
    readProductInfo();

    if (!setupTouchInterrupt())
    {
        Serial.println("Gt911: interrupt setup failed");
        return false;
    }

    g_previousFrame = {};
    return true;
}

void gt911SetEventCallback(Gt911TouchEventCallback callback)
{
    g_eventCallback = callback;
}

void gt911Service()
{
    if (!g_touchIrqPending)
    {
        return;
    }

    g_touchIrqPending = false;

    Gt911TouchFrame currentFrame = {};
    const Gt911FrameReadResult result = gt911ReadTouchFrame(currentFrame);

    if (result == Gt911FrameReadResult::Success)
    {
        processTouchEvents(currentFrame);
    }
}

void gt911MapToLcd(
    uint16_t touchX,
    uint16_t touchY,
    int32_t& lcdX,
    int32_t& lcdY)
{
    // 当前板子触摸分辨率与 LCD 同为 240x320，且轴向一致。
    // 这里仍写成“可扩展”的形式，方便以后改成比例映射：
    //   lcd = touch * LCD_SIZE / TOUCH_SIZE
    int32_t x = static_cast<int32_t>(touchX);
    int32_t y = static_cast<int32_t>(touchY);

    if (GT911_TOUCH_WIDTH != 240 || GT911_TOUCH_HEIGHT != 320)
    {
        x = static_cast<int32_t>(touchX) * 240 / GT911_TOUCH_WIDTH;
        y = static_cast<int32_t>(touchY) * 320 / GT911_TOUCH_HEIGHT;
    }

    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }
    if (x > 239)
    {
        x = 239;
    }
    if (y > 319)
    {
        y = 319;
    }

    lcdX = x;
    lcdY = y;
}

uint8_t gt911GetAddress()
{
    return g_gt911Address;
}
